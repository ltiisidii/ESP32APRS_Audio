#include "webfeed.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "millis64.h"
#include <esp_timer.h>

#define MON_ITEMS 24   // TNC2 frames kept for the monitor page
#define MON_TEXT 260   // a TNC2 frame is at most ~256 characters
#define NMEA_ITEMS 32  // NMEA sentences kept for the GPS page
#define NMEA_TEXT 84   // an NMEA sentence is at most 82 characters
#define IDLE_MS 60000  // free the buffers when no page has polled for this long

struct MonItem
{
    uint32_t seq;
    uint32_t time;
    uint16_t mV;
    char text[MON_TEXT];
};

struct NmeaItem
{
    uint32_t seq;
    char text[NMEA_TEXT];
};

struct Feed
{
    void *items;           // MonItem[] or NmeaItem[], allocated on the first read
    uint32_t seq;          // sequence number of the newest entry
    uint32_t lastReadMs;   // millis() of the last page poll
};

static SemaphoreHandle_t lock;
static Feed mon, nmea;

static bool take()
{
    if (!lock)
    {
        static portMUX_TYPE once = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&once);
        if (!lock)
            lock = xSemaphoreCreateMutex();
        portEXIT_CRITICAL(&once);
    }
    return lock && xSemaphoreTake(lock, pdMS_TO_TICKS(50)) == pdTRUE; // never wait long: drop the entry instead
}

static void give() { xSemaphoreGive(lock); }

// Called with the lock held: free the buffer of a feed nobody polls any more
static bool active(Feed &f)
{
    if (f.items && (uint32_t)(millis() - f.lastReadMs) > IDLE_MS)
    {
        free(f.items);
        f.items = nullptr;
    }
    return f.items != nullptr;
}

static void copyText(char *dst, size_t size, const char *src, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < size; i++)
    {
        unsigned char c = src[i];
        if (c == '\r' || c == '\n')
            continue;
        dst[n++] = (char)c;
    }
    dst[n] = 0;
}

void webFeedMonitor(const char *tnc2, size_t len, uint16_t mVrms)
{
    if (!take())
        return;
    if (active(mon))
    {
        MonItem *it = &((MonItem *)mon.items)[++mon.seq % MON_ITEMS];
        it->seq = mon.seq;
        it->time = (uint32_t)time(nullptr);
        it->mV = mVrms;
        copyText(it->text, sizeof(it->text), tnc2, len);
    }
    give();
}

void webFeedNmea(const char *line, size_t len)
{
    if (!take())
        return;
    if (active(nmea))
    {
        NmeaItem *it = &((NmeaItem *)nmea.items)[++nmea.seq % NMEA_ITEMS];
        it->seq = nmea.seq;
        copyText(it->text, sizeof(it->text), line, len);
    }
    give();
}

// Every 10 s while a buffer exists: free the ones no page has polled for a minute, even when no new
// data arrives (no radio or GPS traffic means the producers never run the check themselves)
static esp_timer_handle_t idleTimer;

static void idleCheck(void *)
{
    if (!take())
        return;
    active(mon);
    active(nmea);
    bool any = mon.items || nmea.items;
    give();
    if (!any)
        esp_timer_stop(idleTimer);
}

// Called with the lock held: start the feed if needed and note the poll
static bool open(Feed &f, size_t bytes)
{
    f.lastReadMs = millis();
    if (!f.items)
    {
        f.items = calloc(1, bytes); // zeroed: seq 0 marks an empty slot
        if (!idleTimer)
        {
            esp_timer_create_args_t a = {};
            a.callback = idleCheck;
            a.name = "webfeed";
            esp_timer_create(&a, &idleTimer);
        }
        if (idleTimer && !esp_timer_is_active(idleTimer))
            esp_timer_start_periodic(idleTimer, 10 * 1000000ULL);
    }
    return f.items != nullptr;
}

uint32_t webFeedMonitorRead(uint32_t after, JsonArray out)
{
    if (!take())
        return after;
    uint32_t newest = mon.seq;
    if (open(mon, sizeof(MonItem) * MON_ITEMS))
    {
        // oldest first; when the page fell behind by more than the buffer, it gets what is left
        for (uint32_t s = (newest > MON_ITEMS ? newest - MON_ITEMS + 1 : 1); s <= newest; s++)
        {
            const MonItem &it = ((MonItem *)mon.items)[s % MON_ITEMS];
            if (it.seq != s || s <= after)
                continue;
            JsonObject o = out.add<JsonObject>();
            o["n"] = it.seq;
            o["t"] = it.time;
            o["mv"] = it.mV;
            o["raw"] = String(it.text); // String: copied into the document while the lock is held
        }
    }
    give();
    return newest;
}

uint32_t webFeedNmeaRead(uint32_t after, JsonArray out)
{
    if (!take())
        return after;
    uint32_t newest = nmea.seq;
    if (open(nmea, sizeof(NmeaItem) * NMEA_ITEMS))
    {
        for (uint32_t s = (newest > NMEA_ITEMS ? newest - NMEA_ITEMS + 1 : 1); s <= newest; s++)
        {
            const NmeaItem &it = ((NmeaItem *)nmea.items)[s % NMEA_ITEMS];
            if (it.seq != s || s <= after)
                continue;
            out.add(String(it.text));
        }
    }
    give();
    return newest;
}
