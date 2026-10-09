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

// Start the supervisor task (call once from setup() after the tasks are created)
void supervisorStart(void);

#endif
