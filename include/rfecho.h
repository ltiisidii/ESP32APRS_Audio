// Own RF echo: a digipeater repeated a packet of this station and we heard it.
//
// aprs.fi keeps only the first copy of a packet. When the station also sends its beacon to
// APRS-IS that copy always wins, so the map can no longer tell whether the RF transmission
// reached anyone. The digipeater's repeat can: it is proof, heard here, that the packet made
// it out on RF. Needs a path (WIDE1-1, WIDE2-1) and a digipeater in range that repeats it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "AX25.h"

// "CALL" or "CALL-SSID", the way the firmware writes callsigns
static inline void rfEchoCallStr(const AX25Call *c, char *out, size_t sz)
{
    snprintf(out, sz, c->ssid ? "%s-%d" : "%s", c->call, c->ssid);
}

// A path alias, not a station: WIDEn-N, TRACEn-N, RELAY
static inline bool rfEchoGeneric(const char *call)
{
    return strncmp(call, "WIDE", 4) == 0 || strncmp(call, "TRACE", 5) == 0 || strcmp(call, "RELAY") == 0;
}

// Is 'm' an echo of our own packet, and who repeated it?
//   own: our callsigns ("CALL-SSID"), empty strings are skipped
// Returns -1: not an echo (not ours, or nobody repeated it yet)
//          1: repeated, 'by' = the last station that marked itself in the path (the one we heard)
//          0: repeated by a digipeater that left no call (plain WIDEn-N), 'by' = "?"
static inline int rfEchoBy(const AX25Msg *m, const char *const *own, int ownN, char *by, size_t bySz)
{
    char src[12];
    rfEchoCallStr(&m->src, src, sizeof(src));
    bool mine = false;
    for (int i = 0; i < ownN; i++)
        if (own[i] && own[i][0] && strcmp(own[i], src) == 0)
            mine = true;
    if (!mine)
        return -1;
    int last = -1;    // last repeated hop that is a station
    bool any = false; // any hop repeated at all
    for (int i = 0; i < m->rpt_count && i < AX25_MAX_RPT; i++)
    {
        if (!(m->rpt_flags & (1 << i))) // H bit: this hop already repeated it ("*" in TNC2)
            continue;
        any = true;
        char c[12];
        rfEchoCallStr(&m->rpt_list[i], c, sizeof(c));
        bool self = false;
        for (int k = 0; k < ownN; k++)
            if (own[k] && own[k][0] && strcmp(own[k], c) == 0)
                self = true;
        if (!self && !rfEchoGeneric(m->rpt_list[i].call))
            last = i;
    }
    if (!any)
        return -1; // our own call heard but not repeated: not proof of anything
    if (last < 0)
    {
        snprintf(by, bySz, "?");
        return 0;
    }
    rfEchoCallStr(&m->rpt_list[last], by, bySz);
    return 1;
}
