/*
 Task supervisor: restarts the station when a critical task or the RX audio stops,
 and confirms a freshly OTA-updated firmware only after it has run healthy for a while
 (otherwise the bootloader rolls back to the previous firmware).
*/
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdint.h>

enum SupervisedTask
{
    SV_TASK_APRS = 0,  // taskAPRS: TX queue, beacons, igate/digi processing
    SV_TASK_APRS_POLL, // taskAPRSPoll: AFSK demodulator
    SV_TASK_NETWORK,   // taskNetwork: WiFi, APRS-IS, NTP
    SV_TASK_COUNT
};

// Call from the supervised task at its start and on every loop iteration
void supervisorFeed(SupervisedTask id);

// True when a task last fed at 'last' (millis) is older than limitMs at time 'now'.
// The age is signed: a feed stored after 'now' was read (the task runs on the other core, and
// feeds store millis() | 1, up to 1 ms ahead) is "fresh", not 49 days old. Comparing unsigned
// made (now - last) wrap to ~4294967 s and restarted the station every few seconds.
static inline bool svStalled(uint32_t now, uint32_t last, uint32_t limitMs)
{
    int32_t age = (int32_t)(now - last);
    return age > (int32_t)limitMs;
}

// Start the supervisor task (call once from setup() after the tasks are created)
void supervisorStart(void);
// Why the supervisor restarted the previous run ("" if it did not); for the web status
const char *supervisorLastRestart(void);

#endif
