// Stand-ins for the ESP32-only parts of AFSK.cpp that the portable code calls,
// plus the simulated clock and logging switch used by shim/Arduino.h.
#include <Arduino.h>

// ---- simulated time and logging (see shim/Arduino.h)
int64_t hostTimeUs = 1000000;
int hostLog = 0;
void hostAdvanceMs(uint32_t ms) { hostTimeUs += (int64_t)ms * 1000; }

// ---- AFSK.cpp stand-ins (hardware: DAC/ADC timers, PTT GPIO, LEDs)
volatile bool hw_afsk_dac_isr = false;
volatile bool pttOff = false;
bool hostPtt = false;
bool hostDacRunning = false;
void setPtt(bool state) { hostPtt = state; }
void DAC_TimerEnable(bool sts) { hostDacRunning = sts; }
void AFSK_TimerEnable(bool) {}
void AFSK_FlushFifo(void) {}
void LED_Status2(uint8_t, uint8_t, uint8_t) {}

