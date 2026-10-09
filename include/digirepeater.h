#ifndef DIGIREPEATER_H
#define DIGIREPEATER_H

#include <AX25.h>

int digiProcess(AX25Msg &Packet);

// Duplicate suppression: a packet (same source, destination and information field) that this
// digipeater already repeated is not repeated again for DIGI_DUP_WINDOW_MS, even if it is heard
// back with another path (e.g. already repeated by a neighbour digipeater).
#define DIGI_DUP_WINDOW_MS 30000
#define DIGI_DUP_TABLE_SIZE 32
bool digiIsDuplicate(const AX25Msg &Packet, uint32_t nowMs);
void digiRemember(const AX25Msg &Packet, uint32_t nowMs);

// WIDEn-N with hops left: insert our call (traceable, "New-N paradigm"), e.g.
// WIDE2-2 -> LU1DIG-1*,WIDE2-1. 0 = only decrement (WIDE2-2 -> WIDE2-1), as before.
#ifndef DIGI_TRACE_WIDEN
#define DIGI_TRACE_WIDEN 1
#endif

#endif