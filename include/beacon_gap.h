// Keeps a station's status beacon away from its own position beacon.
//
// Both timers restart together at boot and after every config save, so with the default
// intervals (1800 s each) the status and the position went out back to back every time.
// On RF that loses packets: a digipeater that repeats the first one is transmitting, and so
// deaf, while the second one is on the air.
#pragma once
#include <stdint.h>

#define BEACON_GAP_MS 60000ULL // minimum time between a status and a position of the same station

// A status is due at 'now'. Returns 'now' if it may go out, or the later time to retry.
//   lastPos: when the last position of this station was sent, 0 = never
//   nextPos: when its next position is due, 0 = unknown (smart beacon, position off)
// A position overdue by more than 'gap' (no GPS fix, for example) does not hold the status.
static inline uint64_t statusSendAt(uint64_t now, uint64_t lastPos, uint64_t nextPos, uint64_t gap = BEACON_GAP_MS)
{
    if (nextPos != 0 && nextPos <= now + gap && nextPos + gap > now)
        return nextPos + gap; // a position goes out now or soon: send the status after it
    if (lastPos != 0 && now >= lastPos && now - lastPos < gap)
        return lastPos + gap; // a position went out moments ago
    return now;
}
