/*
 Internet -> RF gating rules (iGate good practice). A packet from APRS-IS goes to RF only when:
 1. a message's addressee was heard on RF in the last 30 minutes (it is somewhere this station reaches);
 2. its sender was NOT heard on RF in that time (then the message already went out on RF locally);
 3. the addressee is NOT connected straight to APRS-IS (a phone app gets it from there);
 5. it is sent as a clean third-party frame: }SRC>DEST,TCPIP,MYCALL*:info (no qAC/qAR APRS-IS path);
    packets marked NOGATE, RFONLY or TCPXX, and packets that are already third-party ('}'), are never gated;
 6. at most "Internet -> RF per minute" packets per minute, so a burst from the Internet can't fill the channel.
 Messages to this station are never gated (they are for us). Other packet types (when chosen in "Internet -> RF
 types") only go through rules 5 and 6.
 Log only: the same decisions are taken and logged, nothing is transmitted (test before turning it on).
 (Rule 4, sending the sender's position after a message, is not done.)
*/
#include "main.h"
#include "inet2rf.h"

extern Configuration config;

#define HEARD_N 40                    // stations remembered (RF and direct APRS-IS)
#define HEARD_MS (30UL * 60UL * 1000) // "recently" = last 30 minutes
#define LOG_N 16

typedef struct
{
    char call[10];
    uint32_t rfMs;   // millis() when heard on RF, 0 = never
    uint32_t inetMs; // millis() when seen connected straight to APRS-IS (TCPIP*), 0 = never
} heardEntry;

static heardEntry heard[HEARD_N];
static portMUX_TYPE heardMux = portMUX_INITIALIZER_UNLOCKED;
static char logTxt[LOG_N][128];
static uint8_t logHead = 0, logCount = 0;
static uint32_t sentMs[16]; // times of the last sends, for the per-minute limit

static bool recent(uint32_t ms, uint32_t now)
{
    return ms != 0 && (uint32_t)(now - ms) < HEARD_MS;
}

// Copy a callsign (up to 9 chars, stops at a separator), upper case
static void callCopy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; src[i] && i < n - 1 && i < 9 && src[i] != ' ' && src[i] != '>' && src[i] != ',' && src[i] != ':' && src[i] != '*'; i++)
        dst[i] = toupper((unsigned char)src[i]);
    dst[i] = 0;
}

static void note(const char *call, bool rf)
{
    char c[10];
    callCopy(c, call, sizeof(c));
    if (!c[0])
        return;
    uint32_t now = millis() | 1;
    portENTER_CRITICAL(&heardMux);
    int slot = -1, oldest = 0;
    uint32_t oldestAge = 0;
    for (int i = 0; i < HEARD_N; i++)
    {
        if (strcmp(heard[i].call, c) == 0)
        {
            slot = i;
            break;
        }
        uint32_t last = heard[i].rfMs > heard[i].inetMs ? heard[i].rfMs : heard[i].inetMs;
        uint32_t age = heard[i].call[0] ? (uint32_t)(now - last) : UINT32_MAX;
        if (age >= oldestAge)
        {
            oldestAge = age;
            oldest = i;
        }
    }
    if (slot < 0)
    {
        slot = oldest;
        memset(&heard[slot], 0, sizeof(heardEntry));
        strcpy(heard[slot].call, c);
    }
    if (rf)
        heard[slot].rfMs = now;
    else
        heard[slot].inetMs = now;
    portEXIT_CRITICAL(&heardMux);
}

static void lookup(const char *call, bool &rf, bool &inet)
{
    uint32_t now = millis();
    rf = inet = false;
    portENTER_CRITICAL(&heardMux);
    for (int i = 0; i < HEARD_N; i++)
        if (strcmp(heard[i].call, call) == 0)
        {
            rf = recent(heard[i].rfMs, now);
            inet = recent(heard[i].inetMs, now);
            break;
        }
    portEXIT_CRITICAL(&heardMux);
}

static const char *logInfo = ""; // info field of the packet being decided, shown in the log

static void logAdd(bool sent, bool test, const char *src, const char *what, const char *why)
{
    char *e = logTxt[logHead];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(e, sizeof(logTxt[0]), "%02d:%02d:%02d %s %s %s: %s [%.30s]", tm.tm_hour, tm.tm_min, tm.tm_sec,
             sent ? (test ? "WOULD SEND" : "SENT") : "skip", src, what, why, logInfo);
    logHead = (logHead + 1) % LOG_N;
    if (logCount < LOG_N)
        logCount++;
}

void inet2rfLogRead(JsonArray out)
{
    for (int i = 0; i < logCount; i++)
        out.add(logTxt[(logHead + LOG_N - logCount + i) % LOG_N]);
}

void inet2rfHeardRf(const char *call)
{
    note(call, true);
}

static bool rateOk(void)
{
    uint8_t max = config.inet2rf_rate ? config.inet2rf_rate : 4;
    if (max > 16)
        max = 16;
    uint32_t now = millis();
    int n = 0;
    for (int i = 0; i < 16; i++)
        if (sentMs[i] && (uint32_t)(now - sentMs[i]) < 60000)
            n++;
    return n < max;
}

static void rateNote(void)
{
    static uint8_t next = 0;
    sentMs[next] = millis() | 1;
    next = (next + 1) % 16;
}

bool inet2rfLine(const char *line, uint16_t type, char *tnc2, size_t tnc2Size)
{
    const char *gt = strchr(line, '>');
    const char *colon = strchr(line, ':');
    if (!gt || !colon || colon < gt || line[0] == '#')
        return false;
    // header: SRC>DEST,PATH...   info: after the first ':'
    size_t hlen = colon - line;
    char header[120];
    if (hlen >= sizeof(header))
        return false;
    memcpy(header, line, hlen);
    header[hlen] = 0;
    char info[260]; // info field, without the CR/LF of the APRS-IS line
    size_t ilen = strcspn(colon + 1, "\r\n");
    if (ilen >= sizeof(info))
        ilen = sizeof(info) - 1;
    memcpy(info, colon + 1, ilen);
    info[ilen] = 0;

    char src[10];
    callCopy(src, line, sizeof(src));
    if (strstr(header, ",TCPIP*") != NULL)
        note(src, false); // sent by a client connected straight to APRS-IS

    if (!config.inet2rf && !config.inet2rf_log)
        return false;
    if (!(type & config.inet2rfFilter))
        return false;
    bool test = !config.inet2rf; // log only

    char dest[10];
    callCopy(dest, gt + 1, sizeof(dest));
    char what[24];
    snprintf(what, sizeof(what), "%.20s", info[0] == ':' ? "message" : "packet");

    logInfo = info;
    if (info[0] == '}')
    {
        // already a third-party packet (an iGate passed it on): wrapping it again is not allowed
        logAdd(false, test, src, what, "already third-party");
        return false;
    }
    if (strstr(header, "NOGATE") || strstr(header, "RFONLY") || strstr(header, "TCPXX"))
    {
        logAdd(false, test, src, what, "marked not to gate");
        return false;
    }

    char mycall[12];
    if (config.aprs_ssid == 0)
        snprintf(mycall, sizeof(mycall), "%s", config.aprs_mycall);
    else
        snprintf(mycall, sizeof(mycall), "%s-%d", config.aprs_mycall, config.aprs_ssid);

    if (info[0] == ':' && strlen(info) >= 11 && info[10] == ':')
    {
        char to[10];
        callCopy(to, info + 1, sizeof(to));
        snprintf(what, sizeof(what), "msg to %s", to);
        bool toRf, toInet, fromRf, fromInet;
        lookup(to, toRf, toInet);
        lookup(src, fromRf, fromInet);
        if (strcasecmp(to, mycall) == 0)
        {
            logAdd(false, test, src, what, "for this station");
            return false;
        }
        if (!toRf)
        {
            logAdd(false, test, src, what, "addressee not heard on RF (30 min)");
            return false;
        }
        if (fromRf)
        {
            logAdd(false, test, src, what, "sender is on RF here");
            return false;
        }
        if (toInet)
        {
            logAdd(false, test, src, what, "addressee is on APRS-IS");
            return false;
        }
    }

    if (!rateOk())
    {
        logAdd(false, test, src, what, "per-minute limit");
        return false;
    }
    // }SRC>DEST,TCPIP,MYCALL*:info  (outer header: MYCALL>APE32A,RFONLY so it is not gated back)
    int n = snprintf(tnc2, tnc2Size, "%s>APE32A,RFONLY:}%s>%s,TCPIP,%s*:%s", mycall, src, dest, mycall, info);
    if (n <= 0 || (size_t)n >= tnc2Size || strlen(info) > 200)
    {
        logAdd(false, test, src, what, "too long for RF");
        return false;
    }
    rateNote();
    logAdd(true, test, src, what, test ? "log only, not transmitted" : "transmitted");
    return !test;
}
