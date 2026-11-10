// Own RF echo: count the packets this station puts on RF and the ones a digipeater repeated
// back (heard here). The Diagnostics page shows "repeated by LU8EB-2 4 min ago" or a warning
// when the last transmissions got no repeat at all. Rules in rfecho.h.
#include "main.h"
#include "millis64.h"
#include "rfecho.h"
#include "rfecho_api.h"
#include <math.h>

extern Configuration config;

#define ECHO_LOG_N 8

static portMUX_TYPE echoMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t ownTx = 0;       // own packets sent on RF since start
static uint32_t ownTxEchoed = 0; // of them, how many got at least one repeat
static uint32_t ownTxAtEcho = 0; // ownTx when the last echo arrived (TX since = ownTx - this)
static uint32_t echoes = 0;      // repeats heard (one TX repeated by 2 digis = 2)
static uint64_t lastEchoMs = 0;
static char lastBy[12] = "";
static float lastDb = 0;
static char logTxt[ECHO_LOG_N][96];
static uint8_t logHead = 0, logCount = 0;

// Our callsigns, the way the firmware writes them ("CALL" or "CALL-SSID")
static int ownCalls(char out[3][12])
{
    int n = 0;
    struct
    {
        bool en;
        const char *call;
        uint8_t ssid;
    } id[3] = {{config.igate_en, config.aprs_mycall, config.aprs_ssid},
               {config.digi_en, config.digi_mycall, config.digi_ssid},
               {config.trk_en, config.trk_mycall, config.trk_ssid}};
    for (int i = 0; i < 3; i++)
        if (id[i].en && id[i].call[0])
            snprintf(out[n++], 12, id[i].ssid ? "%s-%d" : "%s", id[i].call, id[i].ssid);
    return n;
}

void rfEchoOwnTx(const char *tnc2)
{
    char own[3][12];
    int n = ownCalls(own);
    const char *gt = strchr(tnc2, '>');
    if (!gt)
        return;
    size_t len = gt - tnc2;
    for (int i = 0; i < n; i++)
        if (strlen(own[i]) == len && strncmp(own[i], tnc2, len) == 0)
        {
            portENTER_CRITICAL(&echoMux);
            ownTx++;
            portEXIT_CRITICAL(&echoMux);
            return;
        }
}

void rfEchoRx(const AX25Msg *m, uint16_t mV)
{
    char own[3][12];
    int n = ownCalls(own);
    const char *ownP[3] = {own[0], own[1], own[2]};
    char by[12];
    if (rfEchoBy(m, ownP, n, by, sizeof(by)) < 0)
        return;
    float db = mV > 0 ? 20.0f * log10f(mV / 1000.0f) : -99.0f;
    char line[96];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(line, sizeof(line), "%02d:%02d:%02d repeated by %s, %.1f dBV [%.30s]", tm.tm_hour, tm.tm_min,
             tm.tm_sec, by, db, (const char *)m->info);
    portENTER_CRITICAL(&echoMux);
    echoes++;
    if (ownTxAtEcho != ownTx) // first repeat of this transmission
    {
        ownTxEchoed++;
        ownTxAtEcho = ownTx;
    }
    lastEchoMs = millis64();
    strlcpy(lastBy, by, sizeof(lastBy));
    lastDb = db;
    strlcpy(logTxt[logHead], line, sizeof(logTxt[0]));
    logHead = (logHead + 1) % ECHO_LOG_N;
    if (logCount < ECHO_LOG_N)
        logCount++;
    portEXIT_CRITICAL(&echoMux);
}

void rfEchoInfo(JsonObject out)
{
    portENTER_CRITICAL(&echoMux);
    uint32_t tx = ownTx, txE = ownTxEchoed, since = ownTx - ownTxAtEcho, ec = echoes;
    uint64_t last = lastEchoMs;
    char by[12];
    strlcpy(by, lastBy, sizeof(by));
    float db = lastDb;
    char lines[ECHO_LOG_N][96];
    uint8_t cnt = logCount, head = logHead;
    memcpy(lines, logTxt, sizeof(lines));
    portEXIT_CRITICAL(&echoMux);

    out["tx"] = tx;           // own packets sent on RF
    out["txEchoed"] = txE;    // of them, repeated by a digipeater (heard here)
    out["txSinceEcho"] = since;
    out["echoes"] = ec;
    if (last)
    {
        out["ago"] = (uint32_t)((millis64() - last) / 1000);
        out["by"] = by;
        out["db"] = roundf(db * 10) / 10;
    }
    JsonArray log = out["log"].to<JsonArray>();
    for (int i = 0; i < cnt; i++)
        log.add(lines[(head + ECHO_LOG_N - cnt + i) % ECHO_LOG_N]);
}
