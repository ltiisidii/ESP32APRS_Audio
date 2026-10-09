/*
 Task supervisor, see supervisor.h
*/
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include "supervisor.h"

#define SV_CHECK_PERIOD_MS 1000
#define SV_ADC_STALL_MS 30000      // RX audio sampling stopped
#define SV_OTA_CONFIRM_MS 120000   // healthy run time before confirming a new OTA firmware
#define SV_RESET_MAGIC 0x53555056  // "SUPV"
#define SV_REPORT_MS 600000        // health report period in the log

extern volatile uint32_t adcIsrCount; // AFSK.cpp, counts ADC sampling interrupts
bool getTransmit();                   // AFSK.cpp, true while PTT/TX is active
bool AFSK_SamplingExpected(void);     // AFSK.cpp, false while TX or squelch-gated ADC is off
extern volatile uint32_t fifoOverflowCount; // AFSK.cpp, ADC samples dropped (FIFO full)

extern TaskHandle_t taskNetworkHandle, taskAPRSHandle, taskAPRSPollHandle;
extern TaskHandle_t taskSerialHandle, taskGPSHandle, taskSensorHandle;

// Max time without a feed before restarting. Covers the longest legitimate blocking call
// of each task (taskNetwork: WiFi reconnect, ping, APRS-IS/MQTT connect, PPP).
static const uint32_t svLimitMs[SV_TASK_COUNT] = {60000, 30000, 300000};
static const char *const svName[SV_TASK_COUNT] = {"taskAPRS", "taskAPRSPoll", "taskNetwork"};

static volatile uint32_t svLastFeed[SV_TASK_COUNT];
static TaskHandle_t svHandle[SV_TASK_COUNT];

// Survives esp_restart(), so the reason can be logged after the reboot
RTC_NOINIT_ATTR static uint32_t svResetMagic;
RTC_NOINIT_ATTR static char svResetReason[48];

// Arduino marks a new OTA image valid at boot unless this returns true.
// We confirm it ourselves after SV_OTA_CONFIRM_MS of healthy operation.
extern "C" bool verifyRollbackLater()
{
    return true;
}

void supervisorFeed(SupervisedTask id)
{
    if (id >= SV_TASK_COUNT)
        return;
    if (svHandle[id] == NULL)
        svHandle[id] = xTaskGetCurrentTaskHandle();
    svLastFeed[id] = millis() | 1; // 0 = never fed
}

static void supervisorRestart(const char *reason)
{
    log_e("SUPERVISOR: %s, restarting", reason);
    strlcpy(svResetReason, reason, sizeof(svResetReason));
    svResetMagic = SV_RESET_MAGIC;
    delay(100); // let the log out
    esp_restart();
}

static void reportStack(const char *name, TaskHandle_t h)
{
    if (h != NULL)
        log_i("  stack free %-13s %5u bytes", name, (unsigned)uxTaskGetStackHighWaterMark(h));
}

// Periodic health line for long-run testing: heap, fragmentation, RX audio and stack margins
static void supervisorReport(void)
{
    log_i("HEALTH up %lus heap %u min %u maxblk %u adc %u drop %u",
          (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)adcIsrCount,
          (unsigned)fifoOverflowCount);
    reportStack("taskAPRS", taskAPRSHandle);
    reportStack("taskAPRSPoll", taskAPRSPollHandle);
    reportStack("taskNetwork", taskNetworkHandle);
    reportStack("taskGPS", taskGPSHandle);
    reportStack("taskSerial", taskSerialHandle);
    reportStack("taskSensor", taskSensorHandle);
    reportStack("taskSupervisor", xTaskGetCurrentTaskHandle());
}

static void otaConfirmIfPending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK)
        return; // no OTA data (e.g. flashed by USB, or NO_OTA partition table)
    if (state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            log_i("SUPERVISOR: new firmware confirmed, rollback cancelled");
        else
            log_e("SUPERVISOR: can't confirm new firmware");
    }
}

static void taskSupervisor(void *pvParameters)
{
    bool wdtOk = (esp_task_wdt_add(NULL) == ESP_OK); // if this task hangs, the TWDT reboots
    if (!wdtOk)
        log_e("SUPERVISOR: task watchdog not available");

    uint32_t lastAdcCount = adcIsrCount;
    uint32_t lastAdcChange = millis();
    bool adcRunning = false; // only check the ADC once it has started at all
    bool otaDone = false;
    uint32_t startMs = millis();
    uint32_t lastReport = millis();

    for (;;)
    {
        vTaskDelay(SV_CHECK_PERIOD_MS / portTICK_PERIOD_MS);
        if (wdtOk)
            esp_task_wdt_reset();
        uint32_t now = millis();

        for (int i = 0; i < SV_TASK_COUNT; i++)
        {
            uint32_t last = svLastFeed[i];
            if (last == 0 || svHandle[i] == NULL)
                continue; // task not running (not created / not started yet)
            if (eTaskGetState(svHandle[i]) == eSuspended)
            {
                svLastFeed[i] = now | 1; // deliberately suspended (e.g. WiFi OFF from the menu)
                continue;
            }
            if ((uint32_t)(now - last) > svLimitMs[i])
            {
                char reason[48];
                snprintf(reason, sizeof(reason), "%s stalled %us", svName[i], (unsigned)((now - last) / 1000));
                supervisorRestart(reason);
            }
        }

        uint32_t adc = adcIsrCount;
        if (getTransmit() || !AFSK_SamplingExpected())
        {
            lastAdcChange = now; // sampling legitimately paused (TX, or squelch closed on ESP32)
        }
        else if (adc != lastAdcCount)
        {
            lastAdcCount = adc;
            lastAdcChange = now;
            adcRunning = true;
        }
        else if (adcRunning && (uint32_t)(now - lastAdcChange) > SV_ADC_STALL_MS)
        {
            supervisorRestart("RX ADC sampling stopped");
        }

        if ((uint32_t)(now - lastReport) >= SV_REPORT_MS)
        {
            lastReport = now;
            supervisorReport();
        }

        if (!otaDone && (uint32_t)(now - startMs) > SV_OTA_CONFIRM_MS)
        {
            otaConfirmIfPending();
            otaDone = true;
        }
    }
}

void supervisorStart(void)
{
    if (svResetMagic == SV_RESET_MAGIC)
    {
        svResetReason[sizeof(svResetReason) - 1] = 0;
        log_e("SUPERVISOR: last restart was forced: %s", svResetReason);
    }
    svResetMagic = 0;

    // High priority so a busy lower-priority task can't starve it; it sleeps almost all the time
    xTaskCreatePinnedToCore(taskSupervisor, "taskSupervisor", 3072, NULL, 12, NULL, 0);
}
