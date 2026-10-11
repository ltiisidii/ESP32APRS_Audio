/*
 APRS objects of this station (APRS 1.01, chapter 11: Object reports).

 Packet:  CALL-SSID>APE32A,WIDE2-1:;NAME_____*DDHHMMzDDMM.mmN/DDDMM.mmW$comment
          ';' object, name padded to 9 characters, '*' live / '_' killed, UTC time stamp (DHM zulu)

 How they are sent (the usual practice for fixed objects):
 - New or edited object: sent now, then 30 s, 1, 2, 4, 8, 16 min after ("decay"), then every interval
   (10..120 min, 30 by default). Everyone hears it soon without flooding the channel.
 - After a boot: once in its slot, then every interval (it was already on the maps).
 - Objects never go out together: each one has its own slot, some seconds apart.
 - Turned off or expired: sent as killed ('_') 3 times (now, 1 min, 2 min) so maps and radios drop it.
 - Nothing is sent without a real UTC clock (no fake time stamps) or without a callsign.
 - Path at most WIDE2-2: local information should not travel further.
*/
#include <Arduino.h>
#include <time.h>
#include <math.h>
#include "objects.h"
#include "main.h"
#include "millis64.h"

extern Configuration config;

static const char *const OBJ_PATH[] = {"", "WIDE1-1", "WIDE2-1", "WIDE2-2"};
#define OBJ_FIRST_MS 60000ULL  // first slot after a boot: WiFi, APRS-IS and the clock come up first
#define OBJ_SLOT_MS 20000ULL   // objects are sent this far apart
#define OBJ_DECAY_FIRST_S 30   // decay after a change: 30 s, then doubling up to the interval
#define OBJ_KILL_SENDS 3       // killed object: sent 3 times, 1 and 2 minutes apart
#define OBJ_CLOCK_OK 1600000000

typedef struct
{
    uint64_t next;     // millis64() of the next send, 0 = nothing scheduled
    uint32_t decay;    // seconds of the next decay step, 0 = decay over (every interval)
    uint32_t sig;      // fingerprint of the settings being sent
    uint8_t killLeft;  // killed sends still to do
    bool wasOn;        // enabled at the previous tick
    uint32_t lastSent; // UTC time of the last send
    uint32_t sent;     // packets sent since boot
} objRun;

static objRun run[OBJ_COUNT];
static volatile bool saveReq = false;
static bool started = false;

static bool clockOk(time_t &now)
{
    now = time(NULL);
    return now > OBJ_CLOCK_OK;
}

static bool callOk()
{
    return strlen(config.aprs_mycall) >= 3 && strcasecmp(config.aprs_mycall, "NOCALL") != 0;
}

// Object names: 3..9 printable characters, not only spaces; '|' and '~' are reserved in APRS
static bool nameOk(const char *n)
{
    size_t len = strlen(n), used = 0;
    if (len < 3 || len > 9)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)n[i];
        if (c < 0x20 || c > 0x7e || c == '|' || c == '~')
            return false;
        if (c != ' ')
            used++;
    }
    return used > 0;
}

static uint32_t fnv(const void *p, size_t n, uint32_t h)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

// What goes on the air: a change restarts the decay
static uint32_t sigOf(const aprsObject &o)
{
    uint32_t h = 2166136261u;
    h = fnv(o.name, strlen(o.name), h);
    h = fnv(&o.lat, sizeof(o.lat), h);
    h = fnv(&o.lon, sizeof(o.lon), h);
    h = fnv(o.symbol, strlen(o.symbol), h);
    h = fnv(o.comment, strlen(o.comment), h);
    h = fnv(&o.path, sizeof(o.path), h);
    h = fnv(&o.rf, sizeof(o.rf), h);
    h = fnv(&o.inet, sizeof(o.inet), h);
    return h;
}

bool objectsInfo(int i, bool killed, char *out, size_t len)
{
    if (i < 0 || i >= OBJ_COUNT)
        return false;
    const aprsObject &o = config.obj[i];
    time_t now;
    if (!clockOk(now) || !nameOk(o.name) || !(o.lat >= -90 && o.lat <= 90) || !(o.lon >= -180 && o.lon <= 180))
        return false;
    char name[10];
    memset(name, ' ', 9);
    memcpy(name, o.name, strlen(o.name));
    name[9] = 0;
    char cmt[44];
    size_t c = 0;
    for (const char *p = o.comment; *p && c < 43; p++)
        if ((unsigned char)*p >= 0x20 && (unsigned char)*p <= 0x7e && *p != '|' && *p != '~')
            cmt[c++] = *p;
    cmt[c] = 0;
    struct tm t;
    gmtime_r(&now, &t);
    // degrees + hundredths of a minute, rounded once: 59.996' must not print as 60.00'
    long la = lround(fabs((double)o.lat) * 6000.0), lo = lround(fabs((double)o.lon) * 6000.0);
    char table = o.symbol[0] ? o.symbol[0] : '/', code = o.symbol[1] ? o.symbol[1] : '/';
    int r = snprintf(out, len, ";%s%c%02d%02d%02dz%02ld%02ld.%02ld%c%c%03ld%02ld.%02ld%c%c%s", name, killed ? '_' : '*',
                     t.tm_mday, t.tm_hour, t.tm_min, la / 6000, (la % 6000) / 100, la % 100, o.lat < 0 ? 'S' : 'N', table,
                     lo / 6000, (lo % 6000) / 100, lo % 100, o.lon < 0 ? 'W' : 'E', code, cmt);
    return r > 0 && (size_t)r < len;
}

static void sendObject(int i, bool killed)
{
    const aprsObject &o = config.obj[i];
    uint8_t ch = (o.rf ? RF_CHANNEL : 0) | (o.inet ? INET_CHANNEL : 0);
    char info[100], pkt[180];
    if (!ch || !objectsInfo(i, killed, info, sizeof(info)))
        return;
    char call[16];
    if (config.aprs_ssid)
        snprintf(call, sizeof(call), "%s-%d", config.aprs_mycall, config.aprs_ssid);
    else
        snprintf(call, sizeof(call), "%s", config.aprs_mycall);
    const char *path = OBJ_PATH[o.path < 4 ? o.path : 2];
    snprintf(pkt, sizeof(pkt), "%s>APE32A%s%s:%s", call, *path ? "," : "", path, info);
    pkgTxPush(pkt, strlen(pkt), 0, ch);
    run[i].lastSent = (uint32_t)time(NULL);
    run[i].sent++;
    log_i("OBJECT %d: %s", i + 1, pkt);
}

void objectsTick(void)
{
    uint64_t ms = millis64();
    if (!started)
    {
        started = true;
        for (int i = 0; i < OBJ_COUNT; i++)
        {
            run[i].wasOn = config.obj[i].enable;
            run[i].sig = sigOf(config.obj[i]);
            run[i].decay = 0; // already on the maps before the reboot: no decay, just its slot and the interval
            run[i].next = config.obj[i].enable ? ms + OBJ_FIRST_MS + i * OBJ_SLOT_MS : 0;
        }
    }
    time_t now;
    bool clk = clockOk(now), call = callOk();
    for (int i = 0; i < OBJ_COUNT; i++)
    {
        aprsObject &o = config.obj[i];
        objRun &r = run[i];
        if (r.wasOn && !o.enable)
        { // turned off (by the user or the expiry): kill what is on the air
            r.wasOn = false;
            r.killLeft = OBJ_KILL_SENDS;
            r.next = ms + i * 2000;
        }
        else if (!r.wasOn && o.enable)
        { // turned on: decay from now
            r.wasOn = true;
            r.killLeft = 0;
            r.sig = sigOf(o);
            r.decay = OBJ_DECAY_FIRST_S;
            r.next = ms + i * 2000;
        }
        else if (o.enable && sigOf(o) != r.sig)
        { // edited: the new version goes out with a fresh decay
            r.sig = sigOf(o);
            r.decay = OBJ_DECAY_FIRST_S;
            r.next = ms + i * 2000;
        }
        if (o.enable && o.expire > 0 && clk)
        {
            if (o.start == 0)
            {
                o.start = (uint32_t)now;
                saveReq = true;
            }
            else if ((uint32_t)now - o.start >= (uint32_t)o.expire * 3600u)
            {
                o.enable = false; // the next tick kills it
                saveReq = true;
                log_i("OBJECT %d expired", i + 1);
                continue;
            }
        }
        if (r.next == 0 || ms < r.next)
            continue;
        if (!clk || !call)
        {
            r.next = ms + 30000; // waiting for the clock (no fake time stamps) or for a callsign
            continue;
        }
        if (r.killLeft > 0)
        {
            sendObject(i, true);
            r.killLeft--;
            r.next = r.killLeft ? ms + (uint64_t)(OBJ_KILL_SENDS - r.killLeft) * 60000ULL : 0;
            continue;
        }
        if (!o.enable)
        {
            r.next = 0;
            continue;
        }
        sendObject(i, false);
        uint32_t inv = (uint32_t)constrain(o.interval, 10, 120) * 60u;
        uint32_t d = inv;
        if (r.decay)
        {
            d = r.decay < inv ? r.decay : inv;
            r.decay = r.decay * 2 < inv ? r.decay * 2 : 0;
        }
        r.next = ms + (uint64_t)d * 1000ULL;
    }
}

void objectsConfigChanged(const Configuration &before, Configuration &now)
{
    for (int i = 0; i < OBJ_COUNT; i++)
    {
        aprsObject &o = now.obj[i];
        o.interval = constrain(o.interval, 10, 120);
        if (o.path > 3)
            o.path = 2;
        if (o.expire > 8760)
            o.expire = 8760; // one year
        if ((o.enable && !before.obj[i].enable) || o.expire != before.obj[i].expire)
            o.start = 0; // the expiry counts from now
        if (o.enable && !nameOk(o.name))
            o.enable = false; // never on the air with an invalid name
    }
}

void objectsStatus(JsonArray out)
{
    time_t now;
    bool clk = clockOk(now), call = callOk();
    uint64_t ms = millis64();
    for (int i = 0; i < OBJ_COUNT; i++)
    {
        const aprsObject &o = config.obj[i];
        const objRun &r = run[i];
        JsonObject s = out.add<JsonObject>();
        const char *state = "off";
        if (r.killLeft)
            state = "killing";
        else if (o.enable)
            state = !nameOk(o.name) ? "invalid name" : !call ? "no callsign" : !clk ? "waiting for the clock" : !(o.rf || o.inet) ? "no output" : "on the air";
        s["state"] = state;
        s["sent"] = r.sent;
        s["last"] = r.lastSent;
        s["next"] = r.next > ms ? (uint32_t)((r.next - ms) / 1000) : 0;
        s["expires"] = o.enable && o.expire && o.start ? o.start + (uint32_t)o.expire * 3600u : 0;
        char info[100];
        if (objectsInfo(i, r.killLeft > 0, info, sizeof(info)))
            s["packet"] = info;
    }
}

bool objectsTakeSaveRequest(void)
{
    if (!saveReq)
        return false;
    saveReq = false;
    return true;
}
