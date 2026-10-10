#include "digirepeater.h"
#include "main.h"

RTC_DATA_ATTR digiTLMType digiLog;
RTC_DATA_ATTR uint8_t digiCount = 0;

extern Configuration config;

// ---- duplicate suppression (see digirepeater.h)
struct DigiDup
{
    uint32_t hash;
    uint32_t time;
    bool used;
};
static DigiDup digiDup[DIGI_DUP_TABLE_SIZE];
static uint8_t digiDupIdx = 0;

// FNV-1a over source, destination and information field (not the path: the same packet heard
// again through another digipeater has a different path)
static uint32_t digiHash(const AX25Msg &p)
{
    uint32_t h = 2166136261u;
    auto mix = [&h](uint8_t b) { h = (h ^ b) * 16777619u; };
    for (size_t i = 0; i < sizeof(p.src.call) && p.src.call[i]; i++)
        mix((uint8_t)p.src.call[i]);
    mix(p.src.ssid);
    mix('>');
    for (size_t i = 0; i < sizeof(p.dst.call) && p.dst.call[i]; i++)
        mix((uint8_t)p.dst.call[i]);
    mix(p.dst.ssid);
    mix(':');
    for (size_t i = 0; i < p.len && i < sizeof(p.info); i++)
        mix(p.info[i]);
    return h;
}

bool digiIsDuplicate(const AX25Msg &Packet, uint32_t nowMs)
{
    uint32_t h = digiHash(Packet);
    for (int i = 0; i < DIGI_DUP_TABLE_SIZE; i++)
    {
        if (digiDup[i].used && digiDup[i].hash == h && (uint32_t)(nowMs - digiDup[i].time) < DIGI_DUP_WINDOW_MS)
            return true;
    }
    return false;
}

void digiRemember(const AX25Msg &Packet, uint32_t nowMs)
{
    digiDup[digiDupIdx].hash = digiHash(Packet);
    digiDup[digiDupIdx].time = nowMs;
    digiDup[digiDupIdx].used = true;
    digiDupIdx = (digiDupIdx + 1) % DIGI_DUP_TABLE_SIZE;
}

// Insert our call at path position idx (marked as repeated). False if the path is full.
static bool digiInsertOwnCall(AX25Msg &P, int idx)
{
    if (P.rpt_count >= AX25_MAX_RPT || idx < 0 || idx > P.rpt_count)
        return false;
    for (int k = P.rpt_count; k > idx; k--)
        P.rpt_list[k] = P.rpt_list[k - 1];
    uint8_t low = P.rpt_flags & (uint8_t)((1u << idx) - 1);
    uint8_t high = (uint8_t)((P.rpt_flags >> idx) << (idx + 1));
    P.rpt_flags = low | high | (uint8_t)(1u << idx);
    memset(P.rpt_list[idx].call, 0, sizeof(P.rpt_list[idx].call));
    strlcpy(P.rpt_list[idx].call, config.digi_mycall, sizeof(P.rpt_list[idx].call));
    P.rpt_list[idx].ssid = config.digi_ssid;
    P.rpt_count++;
    return true;
}

int digiProcess(AX25Msg &Packet)
{
    int idx, j;
    uint8_t ctmp;
    // if(!DIGI) return;
    // if(rx_data) return;
    // if(digi_timeout<aprs_delay) return;
    // digi_timeout = 65530;
    // aprs_delay = 65535;

    j = 0;
    if (Packet.len < 5)
    {
        digiLog.ErPkts++;
        return 0; // NO DST
    }

    if (!strncmp(&Packet.src.call[0], "NOCALL", 6))
    {
        digiLog.DropRx++;
        return 0;
    }
    if (!strncmp(&Packet.src.call[0], "MYCALL", 6))
    {
        digiLog.DropRx++;
        return 0;
    }

    // Destination SSID Trace
    if (Packet.dst.ssid > 0)
    {
        uint8_t ctmp = Packet.dst.ssid & 0x1E; // Check DSSID

        if (ctmp > 15)
            ctmp = 0;
        if (ctmp < 8)
        { // Edit PATH Change to TRACEn-N
            if (ctmp > 0)
                ctmp--;
            Packet.dst.ssid = ctmp;
            if (Packet.rpt_count > 0)
            {
                for (idx = 0; idx < Packet.rpt_count; idx++)
                {
                    if (!strcmp(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0])) // Is path same callsign
                    {
                        if (Packet.rpt_list[idx].ssid == config.digi_ssid) // IS path same SSID
                        {
                            if (Packet.rpt_flags & (1 << idx))
                            {
                                digiLog.DropRx++;
                                return 0; // bypass flag *
                            }
                            Packet.rpt_flags |= (1 << idx);
                            return 1;
                        }
                    }
                    if (Packet.rpt_flags & (1 << idx))
                        continue;
                    if (Packet.rpt_count >= AX25_MAX_RPT)
                        return 0; // path full: inserting our call wrote past rpt_list[]
                    for (j = idx; j < Packet.rpt_count; j++)
                    {
                        if (Packet.rpt_flags & (1 << j))
                            break;
                    }
                    if (j >= Packet.rpt_count)
                        j = Packet.rpt_count - 1;
                    // Move current part to next part
                    for (; j >= idx; j--)
                    {
                        int n = j + 1;
                        strcpy(&Packet.rpt_list[n].call[0], &Packet.rpt_list[j].call[0]);
                        Packet.rpt_list[n].ssid = Packet.rpt_list[j].ssid;
                        if (Packet.rpt_flags & (1 << j))
                            Packet.rpt_flags |= (1 << n);
                        else
                            Packet.rpt_flags &= ~(1 << n);
                    }

                    // Add new part
                    Packet.rpt_count += 1;
                    strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                    Packet.rpt_list[idx].ssid = config.digi_ssid;
                    Packet.rpt_flags |= (1 << idx);
                    return 2;
                    // j = 1;
                    // break;
                }
            }
            else
            {
                idx = 0;
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                Packet.rpt_count += 1;
                return 2;
            }
        }
        else
        {
            digiLog.DropRx++;
            return 0; // NO PATH
        }
    }

    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if (!strncmp(&Packet.rpt_list[idx].call[0], "qA", 2))
        {
            digiLog.DropRx++;
            return 0;
        }
    }

    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if (!strncmp(&Packet.rpt_list[idx].call[0], "TCP", 3))
        {
            digiLog.DropRx++;
            return 0;
        }
    }

    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if (Packet.rpt_flags & (1 << idx))
        {
            if (idx == (Packet.rpt_count - 1))
                digiCount++;
            continue; // bypass flag *
        }
        if (!strncmp(&Packet.rpt_list[idx].call[0], "WIDE", 4))
        {
            // Check WIDEn-N
            if (Packet.rpt_list[idx].ssid > 0)
            {
                if (Packet.rpt_flags & (1 << idx))
                    continue; // bypass flag *
                ctmp = Packet.rpt_list[idx].ssid & 0x1F;
                if (ctmp > 0)
                    ctmp--;
                if (ctmp > 15)
                    ctmp = 0;
                if (ctmp == 0)
                {
                    strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                    Packet.rpt_list[idx].ssid = config.digi_ssid;
                    Packet.rpt_flags |= (1 << idx);
                    j = 2;
                    break;
                }
                else
                {
                    Packet.rpt_list[idx].ssid = ctmp;
                    Packet.rpt_flags &= ~(1 << idx);
#if DIGI_TRACE_WIDEN
                    digiInsertOwnCall(Packet, idx); // path full: only decrement, as before
#endif
                    j = 2;
                    break;
                }
            }
            else
            {
                j = 2;
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                break;
            }
        }
        else if (!strncmp(&Packet.rpt_list[idx].call[0], "TRACE", 5))
        {
            if (Packet.rpt_flags & (1 << idx))
                continue; // bypass flag *
            ctmp = Packet.rpt_list[idx].ssid & 0x1F;
            if (ctmp > 0)
                ctmp--;
            if (ctmp > 15)
                ctmp = 0;
            if (ctmp == 0)
            {
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                j = 2;
                break;
            }
            else
            {
                if (Packet.rpt_count >= AX25_MAX_RPT)
                    return 0; // path full: inserting our call wrote past rpt_list[]
                for (j = idx; j < Packet.rpt_count; j++)
                {
                    if (Packet.rpt_flags & (1 << j))
                        break;
                }
                if (j >= Packet.rpt_count)
                    j = Packet.rpt_count - 1;
                // Move current part to next part
                for (; j >= idx; j--)
                {
                    int n = j + 1;
                    strcpy(&Packet.rpt_list[n].call[0], &Packet.rpt_list[j].call[0]);
                    Packet.rpt_list[n].ssid = Packet.rpt_list[j].ssid;
                    if (Packet.rpt_flags & (1 << j))
                        Packet.rpt_flags |= (1 << n);
                    else
                        Packet.rpt_flags &= ~(1 << n);
                }
                // Reduce N part of TRACEn-N
                Packet.rpt_list[idx + 1].ssid = ctmp;

                // Add new part
                Packet.rpt_count += 1;
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                j = 2;
                break;
            }
        }
        else if (!strncmp(&Packet.rpt_list[idx].call[0], "RFONLY", 6))
        {
            j = 2;
            // strcpy(&Packet.rpt_list[idx].call[0], &config.aprs_mycall[0]);
            // Packet.rpt_list[idx].ssid = config.aprs_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (!strncmp(&Packet.rpt_list[idx].call[0], "RELAY", 5))
        {
            j = 2;
            strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
            Packet.rpt_list[idx].ssid = config.digi_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (!strncmp(&Packet.rpt_list[idx].call[0], "GATE", 4))
        {
            j = 2;
            strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
            Packet.rpt_list[idx].ssid = config.digi_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (!strncmp(&Packet.rpt_list[idx].call[0], "ECHO", 4))
        {
            j = 2;
            strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
            Packet.rpt_list[idx].ssid = config.digi_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (!strcmp(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0])) // Is path same callsign
        {
            ctmp = Packet.rpt_list[idx].ssid & 0x1F;
            if (ctmp == config.digi_ssid) // IS path same SSID
            {
                if (Packet.rpt_flags & (1 << idx))
                {
                    digiLog.DropRx++;
                    break; // bypass flag *
                }
                Packet.rpt_flags |= (1 << idx);
                j = 1;
                break;
            }
            else
            {
                j = 0;
                break;
            }
        }
        else
        {
            j = 0;
            break;
        }
    }
    return j;
}