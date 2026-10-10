/*
 Web UI (static app in web/, embedded gzip by tools/embed_web.py) and its JSON API.

 GET  /, /app.css, /app.js  embedded files (gzip, ETag, 304 when unchanged)
 GET  /api/info             live status (dashboard)
 GET  /api/config           whole configuration, secrets replaced by CFG_SECRET_MASK
 GET  /api/meta             fixed lists for the forms (module types, CTCSS, modems)
 POST /api/config           JSON object with the keys to change; saves, applies what can be applied
                            live (RF module, modem, WiFi power, beacon timers) and reports if a
                            restart is needed (WiFi mode/networks, Bluetooth)
 POST /api/reboot           restart in ~1 s
 GET  /api/about            board, chip, WiFi and PPPoS details
 GET  /api/sensors          live sensor readings (sample, average)
 GET  /api/messages         APRS message list
 GET  /api/lastheard        last heard list
 GET  /api/monitor?after=N  TNC2 frames received after sequence N (TNC2 page)
 GET  /api/gnss?after=N     GPS fix and NMEA sentences after sequence N (GPS page)
 POST /api/messages/send    {"to", "text"}: send an APRS message
 POST /api/time             {"epoch": UTC seconds}: set the clock by hand
 POST /api/factory          defaults saved, then restart
 POST /api/reload           read /default.cfg again (drop unsaved runtime changes)
 GET  /api/files            LittleFS listing; /api/files/get?name=, /delete?name=, /format, /upload (multipart)
 Everything requires the web login (config.http_username / http_password).
*/
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "webservice.h"
#include "webapi.h"
#include "web_assets.h"
#include "millis64.h"
#include "webfeed.h"
#include "sensor.h"
#include "wireguard_vpn.h"
#include "esp_wifi.h"
#include <AFSK.h>
#include <ESPCPUTemp.h>
#ifdef PPPOS
#include <PPP.h>
extern pppType pppStatus;
#endif
#ifdef MQTT
#include <PubSubClient.h>
extern PubSubClient clientMQTT;
#endif

#define API_MAX_BODY 16384

#ifndef OTA_SERVER_URL
#define OTA_SERVER_URL "" // online OTA off unless set at build time (same default as webservice.cpp)
#endif

extern bool VBat_Flag;
extern bool initInterval;






static bool authOk(AsyncWebServerRequest *request)
{
    if (request->authenticate(config.http_username, config.http_password))
        return true;
    request->requestAuthentication();
    return false;
}

static void sendAsset(AsyncWebServerRequest *request, const WebAsset &a)
{
    if (!authOk(request))
        return;
    if (request->hasHeader("If-None-Match") && request->header("If-None-Match") == a.etag)
    {
        request->send(304);
        return;
    }
    AsyncWebServerResponse *res = request->beginResponse_P(200, a.mime, a.data, a.len);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache"); // revalidate with the ETag, so a new firmware shows at once
    res->addHeader("ETag", a.etag);
    request->send(res);
}

static void sendJson(AsyncWebServerRequest *request, JsonDocument &doc, int code = 200)
{
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    res->setCode(code);
    res->addHeader("Cache-Control", "no-store");
    serializeJson(doc, *res);
    request->send(res);
}

static void apiInfo(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    StandByTick = millis() + (config.pwr_stanby_delay * 1000); // someone is watching: keep the power save standby away (as the old dashboard did)
    JsonDocument doc;
    doc["version"] = String(VERSION) + VERSION_BUILD;
    doc["host"] = config.host_name;
    doc["chip"] = ESP.getChipModel();
    doc["cpuMhz"] = ESP.getCpuFreqMHz();
    ESPCPUTemp tempSensor;
    if (tempSensor.begin())
        doc["temp"] = serialized(String(tempSensor.getTemp(), 1));
    if (VBat_Flag)
        doc["vbat"] = serialized(String(VBat, 2));
    doc["uptime"] = (uint32_t)(millis64() / 1000);
    doc["time"] = (uint32_t)time(NULL);
    doc["tz"] = config.timeZone;
    doc["heap"] = ESP.getFreeHeap();
    doc["heapMin"] = ESP.getMinFreeHeap();
    doc["heapSize"] = ESP.getHeapSize();
#ifdef BOARD_HAS_PSRAM
    doc["psram"] = ESP.getFreePsram();
    doc["psramSize"] = ESP.getPsramSize();
#endif
    doc["callsign"] = config.aprs_mycall;
    doc["ssid"] = config.aprs_ssid;
    doc["fsUsed"] = LITTLEFS.usedBytes();
    doc["fsTotal"] = LITTLEFS.totalBytes();

    JsonObject w = doc["wifi"].to<JsonObject>();
    static const char *const WIFI_MODE_NAME[] = {"OFF", "AP", "STA", "AP+STA"};
    w["mode"] = config.wifi_mode < 4 ? WIFI_MODE_NAME[config.wifi_mode] : "?";
    bool sta = WiFi.status() == WL_CONNECTED;
    w["sta"] = sta;
    if (sta)
    {
        w["ssid"] = WiFi.SSID();
        w["ip"] = WiFi.localIP().toString();
        w["rssi"] = WiFi.RSSI();
    }
    w["apIp"] = WiFi.softAPIP().toString();
    w["apClients"] = WiFi.softAPgetStationNum();

    JsonObject n = doc["net"].to<JsonObject>();
    n["aprsis"] = (bool)aprsClient.connected();
    n["aprsHost"] = config.aprs_host;
    n["aprsPort"] = config.aprs_port;
    n["vpn"] = wireguard_active();
#ifdef PPPOS
    n["ppp"] = PPP.connected();
#endif
#ifdef MQTT
    n["mqtt"] = clientMQTT.connected();
#endif

    JsonObject m = doc["modes"].to<JsonObject>();
    m["igate"] = config.igate_en;
    m["digi"] = config.digi_en;
    m["tracker"] = config.trk_en;
    m["wx"] = config.wx_en;

    JsonObject r = doc["radio"].to<JsonObject>();
    r["rf"] = config.rf_en;
    if (config.rf_en)
    {
        r["txFreq"] = serialized(String(config.freq_tx, 4));
        r["rxFreq"] = serialized(String(config.freq_rx, 4));
        r["power"] = config.rf_power ? "HIGH" : "LOW";
        if (RF_VERSION.length())
            r["version"] = RF_VERSION; // answer of the module to AT+VERSION at boot: proves the UART link works
    }
    r["modem"] = config.modem_type < 4 ? MODEM_TYPE[config.modem_type] : "?";
    r["fx25"] = config.fx25_mode < 3 ? FX25_MODE[config.fx25_mode] : "?";

#ifdef BLUETOOTH
    JsonObject b = doc["bt"].to<JsonObject>();
    b["master"] = config.bt_master;
    b["name"] = config.bt_name;
    b["mode"] = config.bt_mode == 1 ? "TNC2" : config.bt_mode == 2 ? "KISS" : "NONE";
#endif

    JsonObject g = doc["gps"].to<JsonObject>();
    g["en"] = config.gnss_enable;
    if (config.gnss_enable && gps.location.isValid())
    {
        g["lat"] = serialized(String(gps.location.lat(), 5));
        g["lng"] = serialized(String(gps.location.lng(), 5));
        g["alt"] = serialized(String(gps.altitude.meters(), 1));
        g["sat"] = gps.satellites.value();
    }

    JsonObject s = doc["stats"].to<JsonObject>();
    s["rx"] = status.rxCount;
    s["pkt"] = status.allCount;
    s["tx"] = status.txCount;
    s["digi"] = status.digiCount;
    s["rf2inet"] = status.rf2inet;
    s["inet2rf"] = status.inet2rf;
    s["drop"] = status.dropCount;
    s["dup"] = status.dupCount;
    s["error"] = status.errorCount;
    sendJson(request, doc);
}

static void apiConfigGet(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    configToJson(config, doc);
    configMaskSecrets(doc);
    sendJson(request, doc);
}

// The body arrives in chunks; collect it in _tempObject (freed by the library with the request)
static void apiBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    if (total > API_MAX_BODY)
        return;
    if (index == 0)
    {
        free(request->_tempObject);
        request->_tempObject = calloc(1, total + 1);
    }
    char *buf = (char *)request->_tempObject;
    if (buf && index + len <= total)
        memcpy(buf + index, data, len);
}

// What a configuration change needs, so the API applies it the same way the old pages did
struct ApplyPlan
{
    bool rfModule; // RF module settings: re-program the module (old Radio page)
    bool modem;    // AFSK/TNC settings: re-init the modem (old TNC form)
    bool txPower;  // WiFi TX power: set at once (old WiFi page)
    bool aprsIs;   // APRS-IS login data: drop the connection so it logs in again (old iGate page)
    bool clock;    // time zone or NTP server: re-run configTime (old System page)
    bool sensors;  // sensor setup: sensorInit(true) (old Sensor page)
    bool restart;  // WiFi mode/AP/networks, Bluetooth and the VPN are only read at boot
};

static ApplyPlan planChanges(const Configuration &o, const Configuration &n)
{
    ApplyPlan p = {};
    p.rfModule = o.rf_en != n.rf_en || o.rf_type != n.rf_type || o.freq_tx != n.freq_tx || o.freq_rx != n.freq_rx ||
                 o.offset_tx != n.offset_tx || o.offset_rx != n.offset_rx || o.tone_tx != n.tone_tx ||
                 o.tone_rx != n.tone_rx || o.band != n.band || o.rf_power != n.rf_power || o.volume != n.volume ||
                 o.sql_level != n.sql_level;
    p.modem = o.modem_type != n.modem_type || o.audio_lpf != n.audio_lpf || o.tx_timeslot != n.tx_timeslot ||
              o.preamble != n.preamble || o.fx25_mode != n.fx25_mode;
    p.txPower = o.wifi_power != n.wifi_power;
    p.clock = o.timeZone != n.timeZone || strcmp(o.ntp_host, n.ntp_host);
    p.sensors = memcmp(o.sensor, n.sensor, sizeof(o.sensor)) != 0;
    p.aprsIs = o.igate_en != n.igate_en || o.aprs_ssid != n.aprs_ssid || o.aprs_port != n.aprs_port ||
               strcmp(o.aprs_mycall, n.aprs_mycall) || strcmp(o.aprs_host, n.aprs_host) || strcmp(o.aprs_filter, n.aprs_filter);
    p.restart = o.wifi_mode != n.wifi_mode || o.wifi_ap_ch != n.wifi_ap_ch ||
                strcmp(o.wifi_ap_ssid, n.wifi_ap_ssid) || strcmp(o.wifi_ap_pass, n.wifi_ap_pass) ||
                memcmp(o.wifi_sta, n.wifi_sta, sizeof(o.wifi_sta)) ||
                memcmp(&o.bt_slave, &n.bt_slave, (const char *)&o.bt_power - (const char *)&o.bt_slave + sizeof(o.bt_power)) ||
                o.vpn != n.vpn || o.wg_port != n.wg_port || strcmp(o.wg_peer_address, n.wg_peer_address) ||
                strcmp(o.wg_local_address, n.wg_local_address) || strcmp(o.wg_netmask_address, n.wg_netmask_address) ||
                strcmp(o.wg_gw_address, n.wg_gw_address) || strcmp(o.wg_public_key, n.wg_public_key) ||
                strcmp(o.wg_private_key, n.wg_private_key);
    return p;
}

// Runs after the HTTP answer has left: re-programming the RF module talks to it over UART for a while
static void applyTask(void *arg)
{
    ApplyPlan *p = (ApplyPlan *)arg;
    vTaskDelay(pdMS_TO_TICKS(300));
    if (p->rfModule)
        RF_MODULE(false);
    if (p->modem)
        afskSetModem(config.modem_type, config.audio_lpf, config.tx_timeslot, config.preamble * 100, config.fx25_mode);
    if (p->sensors)
        sensorInit(true);
    delete p;
    vTaskDelete(NULL);
}

static void apiConfigPost(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    const char *body = (const char *)request->_tempObject;
    if (!body)
    {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"empty or too large\"}");
        return;
    }
    JsonDocument patch;
    if (deserializeJson(patch, body))
    {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }
    Configuration *before = (Configuration *)malloc(sizeof(Configuration));
    if (!before)
    {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"out of memory\"}");
        return;
    }
    memcpy(before, &config, sizeof(Configuration));
#ifdef ST7735_LED_K_Pin
    int beforeBrightness = config.disp_brightness;
#endif
    int n = configApplyPatch(config, patch);
    if (n == -2)
    {
        free(before);
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"out of memory\"}");
        return;
    }
    if (n < 0)
    {
        free(before);
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"expected an object\"}");
        return;
    }
    for (char *call : {config.aprs_mycall, config.digi_mycall, config.trk_mycall})
        for (char *c = call; *c; c++)
            *c = toupper((unsigned char)*c); // callsigns are upper case (as the old pages stored them)
    if (config.ppp_enable && config.ppp_serial == 0) // the cellular modem owns its UART (old Modules page rule)
        config.uart0_enable = false;
    else if (config.ppp_enable && config.ppp_serial == 1)
        config.uart1_enable = false;
    ApplyPlan plan = planChanges(*before, config);
    bool changed = memcmp(before, &config, sizeof(Configuration)) != 0;
    free(before);
    bool saved = !changed || saveConfiguration("/default.cfg", config);
    if (changed)
    {
        initInterval = true; // restart the beacon timers with the new settings (old iGate/Digi/Tracker pages)
        if (plan.txPower)
            WiFi.setTxPower((wifi_power_t)config.wifi_power);
        if (plan.aprsIs)
            aprsIsStop();
        if (plan.clock)
            configTime(3600 * config.timeZone, 0, config.ntp_host);
#ifdef ST7735_LED_K_Pin
        if (beforeBrightness != config.disp_brightness)
            ledcWrite(0, (uint32_t)config.disp_brightness);
#endif
        if (plan.rfModule || plan.modem || plan.sensors)
        {
            ApplyPlan *p = new ApplyPlan(plan);
            if (xTaskCreate(applyTask, "cfgApply", 4096, p, 1, NULL) != pdPASS)
                delete p;
        }
    }
    JsonDocument doc;
    doc["ok"] = saved;
    doc["changed"] = changed;
    doc["restart"] = plan.restart;
    sendJson(request, doc, saved ? 200 : 500);
}

// Fixed lists the forms need, taken from the firmware tables so the browser never holds a stale copy
static void apiMeta(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    JsonArray a = doc["rfTypes"].to<JsonArray>();
    for (const auto &t : RF_TYPE)
        a.add(t);
    a = doc["ctcss"].to<JsonArray>();
    for (float f : ctcss)
        a.add(serialized(String(f, 1)));
    a = doc["modems"].to<JsonArray>();
#ifdef CONFIG_IDF_TARGET_ESP32S3
    const int modems = 4; // 9600 G3RUH needs the S3
#else
    const int modems = 3;
#endif
    for (int i = 0; i < modems; i++)
        a.add(MODEM_TYPE[i]);
    a = doc["fx25"].to<JsonArray>();
    for (const auto &t : FX25_MODE)
        a.add(t);
    a = doc["paths"].to<JsonArray>(); // index = config value; 13..16 are the user defined paths
    for (int i = 0; i < PATH_LEN; i++)
    {
        if (i >= 13 && i <= 16 && config.path[i - 13][0])
            a.add(String(PATH_NAME[i]) + " (" + config.path[i - 13] + ")");
        else
            a.add(PATH_NAME[i]);
    }
    a = doc["wifiPwr"].to<JsonArray>(); // [config value, dBm]
    for (const auto &w : wifiPwr)
    {
        JsonArray e = a.add<JsonArray>();
        e.add((int)w[0]);
        e.add(w[1]);
    }
    a = doc["micE"].to<JsonArray>();
    for (const auto &t : MIC_E_MSG)
        a.add(t);
    a = doc["tz"].to<JsonArray>(); // [offset hours, name]
    for (const auto &z : tzList)
    {
        JsonArray e = a.add<JsonArray>();
        e.add(z.tz);
        e.add(z.name);
    }
    a = doc["pwrModes"].to<JsonArray>();
    for (const auto &t : PWR_MODE)
        a.add(t);
    doc["otaServer"] = OTA_SERVER_URL;
    a = doc["wxSensors"].to<JsonArray>();
    for (const auto &t : WX_SENSOR)
        a.add(t);
    a = doc["sensorTypes"].to<JsonArray>();
    for (const auto &t : SENSOR_NAME)
        a.add(t);
    a = doc["sensorPorts"].to<JsonArray>();
    for (const auto &t : SENSOR_PORT)
        a.add(t);
    a = doc["tlmSystem"].to<JsonArray>();
    for (const auto &t : SYSTEM_NAME)
        a.add(t);
    a = doc["tlmBits"].to<JsonArray>();
    for (const auto &t : SYSTEM_BITS_NAME)
        a.add(t);
    a = doc["baudrates"].to<JsonArray>();
    for (unsigned long b : baudrate)
        a.add(b);
    a = doc["gnssPorts"].to<JsonArray>();
    for (const auto &t : GNSS_PORT)
        a.add(t);
    a = doc["tncPorts"].to<JsonArray>();
    for (const auto &t : TNC_PORT)
        a.add(t);
    a = doc["tncModes"].to<JsonArray>();
    for (const auto &t : TNC_MODE)
        a.add(t);
    a = doc["adcAtten"].to<JsonArray>();
    for (const auto &t : ADC_ATTEN)
        a.add(t);
    doc["gpioMax"] = (int)GPIO_NUM_MAX - 1;
    doc["sensorCount"] = SENSOR_NUMBER;
    JsonObject f = doc["features"].to<JsonObject>();
#ifdef BLUETOOTH
    f["bt"] = true;
#if !defined(CONFIG_IDF_TARGET_ESP32)
    f["btUuid"] = true; // the old page showed the UUIDs on every chip but the classic ESP32
#endif
#endif
#ifdef MQTT
    f["mqtt"] = true;
#endif
#ifdef PPPOS
    f["ppp"] = true;
#endif
#if defined OLED || defined ST7735_160x80 || defined GUI_LCD
    f["display"] = true;
#endif
#ifdef LOG_FILE
    f["logFile"] = true;
#endif
    sendJson(request, doc);
}

static void rebootTask(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // let the HTTP answer leave first
    TLM_SEQ = 0;                     // the old REBOOT button started the telemetry sequences again
    IGATE_TLM_SEQ = 0;
    DIGI_TLM_SEQ = 0;
    ESP.restart();
}

static void apiReboot(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    request->send(200, "application/json", "{\"ok\":true}");
    xTaskCreate(rebootTask, "reboot", 2048, NULL, 1, NULL);
}

// Live sensor readings for the Sensors page (the old page showed them once, at load)
static void apiSensors(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    JsonArray a = doc.to<JsonArray>();
    for (int i = 0; i < SENSOR_NUMBER; i++)
    {
        JsonObject o = a.add<JsonObject>();
        o["sample"] = serialized(String(sen[i].sample, 2));
        o["average"] = serialized(String(sen[i].average, 2));
    }
    sendJson(request, doc);
}

// ---- Live data, pulled by the pages (see webfeed.h for why nothing is pushed) ----
static uint32_t afterParam(AsyncWebServerRequest *request)
{
    return request->hasParam("after") ? (uint32_t)request->getParam("after")->value().toInt() : 0;
}

// Last heard list (same JSON the old /eventHeard stream carried)
static void apiLastHeard(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    res->addHeader("Cache-Control", "no-store");
    res->print(lastHeardJson());
    request->send(res);
}

// TNC2 monitor: frames newer than ?after=N
static void apiMonitor(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    doc["seq"] = webFeedMonitorRead(afterParam(request), doc["items"].to<JsonArray>());
    sendJson(request, doc);
}

// GPS page: current fix and NMEA sentences newer than ?after=N
static void apiGnss(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    doc["seq"] = webFeedNmeaRead(afterParam(request), doc["lines"].to<JsonArray>());
    doc["en"] = config.gnss_enable;
    doc["valid"] = gps.location.isValid();
    doc["lat"] = serialized(String(gps.location.lat(), 5));
    doc["lng"] = serialized(String(gps.location.lng(), 5));
    doc["alt"] = serialized(String(gps.altitude.meters(), 2));
    doc["spd"] = serialized(String(gps.speed.kmph(), 2));
    doc["csd"] = serialized(String(gps.course.deg(), 1));
    doc["hdop"] = serialized(String(gps.hdop.hdop(), 2));
    doc["sat"] = gps.satellites.value();
    doc["time"] = gps.time.value();
    sendJson(request, doc);
}

// ---- APRS messages (old MSG tab). The page polls this list. ----
static void apiMessages(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    res->addHeader("Cache-Control", "no-store");
    res->print(event_chatMessage(true));
    request->send(res);
}

// {"to": "CALL-SSID", "text": "..."}; checked against the APRS message format before it goes on air
static void apiMessageSend(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    const char *body = (const char *)request->_tempObject;
    JsonDocument d;
    if (!body || deserializeJson(d, body))
    {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }
    String to = d["to"] | "";
    String text = d["text"] | "";
    to.trim();
    to.toUpperCase();
    bool toOk = to.length() >= 3 && to.length() <= 9;
    for (size_t i = 0; toOk && i < to.length(); i++)
        toOk = isalnum((unsigned char)to[i]) || to[i] == '-';
    bool textOk = text.length() >= 1 && text.length() <= 67; // APRS message text limit
    for (size_t i = 0; textOk && i < text.length(); i++)
    {
        unsigned char c = text[i];
        textOk = c >= 0x20 && c < 0x7f && c != '|' && c != '~' && c != '{'; // reserved in APRS messages
    }
    if (!toOk || !textOk)
    {
        request->send(400, "application/json", toOk ? "{\"ok\":false,\"error\":\"text: 1-67 printable characters, no | ~ {\"}"
                                                      : "{\"ok\":false,\"error\":\"bad callsign\"}");
        return;
    }
    sendAPRSMessage(to, text, config.msg_encrypt);
    request->send(200, "application/json", "{\"ok\":true}");
}

// Board name shown on About (same list as the old page)
static const char *boardName()
{
#if defined(TTGO_TWR)
    return "LilyGo T-TWR Plus";
#elif defined(CONFIG_IDF_TARGET_ESP32)
#if defined(SH1106)
    return "ESP32-WROOM + SH1106 OLED";
#elif defined(SSD1306)
    return "ESP32-WROOM + SSD1306 OLED";
#elif defined(NO_OTA)
    return "ESP32-WROOM no OTA, ESP32 DoIt DevKit";
#else
    return "ESP32-WROOM, ESP32 DoIt DevKit";
#endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#if defined(SH1106)
    return "ESP32-C3 + SH1106 OLED";
#elif defined(SSD1306)
    return "ESP32-C3 + SSD1306 OLED";
#elif defined(NO_OTA)
    return "ESP32-C3 no OTA, ESP32-C3 DIY";
#else
    return "ESP32-C3, ESP32-C3 DIY";
#endif
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#if defined(SH1106)
    return "ESP32-C6 + SH1106 OLED";
#elif defined(SSD1306)
    return "ESP32-C6 + SSD1306 OLED";
#elif defined(NO_OTA)
    return "ESP32-C6 no OTA, ESP32-C6 DIY";
#else
    return "ESP32-C6, ESP32-C6 DIY";
#endif
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(ESP32S3_N16R8)
    return "ESP32-S3 16 MB, ESP32-S3-DevKit";
#elif defined(SH1106)
    return "ESP32-S3 8 MB + SH1106 OLED";
#elif defined(SSD1306)
    return "ESP32-S3 8 MB + SSD1306 OLED";
#else
    return "ESP32-S3 Super mini, no OTA";
#endif
#else
    return "Unknown, ESP32 DIY";
#endif
}

// Everything the old About page showed (system, WiFi, PPPoS)
static void apiAbout(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    doc["board"] = boardName();
    doc["version"] = String(VERSION) + VERSION_BUILD;
    doc["rfModule"] = config.rf_type < 9 ? RF_TYPE[config.rf_type] : "?";
    doc["chip"] = ESP.getChipModel();
    doc["revision"] = ESP.getChipRevision();
    uint64_t chipid = ESP.getEfuseMac();
    char cid[20];
    snprintf(cid, sizeof(cid), "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    doc["chipId"] = cid;
    doc["flash"] = ESP.getFlashChipSize();
    doc["psram"] = ESP.getFreePsram();
    doc["psramSize"] = ESP.getPsramSize();
    doc["fsUsed"] = LITTLEFS.usedBytes();
    doc["fsTotal"] = LITTLEFS.totalBytes();
    JsonObject w = doc["wifi"].to<JsonObject>();
    static const char *const MODE[] = {"OFF", "AP", "STA", "AP+STA"};
    w["mode"] = config.wifi_mode < 4 ? MODE[config.wifi_mode] : "?";
    uint8_t proto = 0;
    esp_wifi_get_protocol(WIFI_IF_STA, &proto);
    String p = "802.11";
    if (proto & WIFI_PROTOCOL_11B)
        p += "b";
    if (proto & WIFI_PROTOCOL_11G)
        p += "g";
    if (proto & WIFI_PROTOCOL_11N)
        p += "n";
    if (proto & WIFI_PROTOCOL_LR)
        p += " LR";
    w["protocol"] = p;
    w["mac"] = WiFi.macAddress();
    w["channel"] = WiFi.channel();
    w["txPower"] = WiFi.getTxPower() / 4.0; // the driver counts in 0.25 dBm
    w["ssid"] = WiFi.SSID();
    w["ip"] = WiFi.localIP().toString();
    w["gateway"] = WiFi.gatewayIP().toString();
    w["dns"] = WiFi.dnsIP().toString();
#ifdef PPPOS
    JsonObject pp = doc["ppp"].to<JsonObject>();
    pp["manufacturer"] = pppStatus.manufacturer;
    pp["model"] = pppStatus.model;
    pp["imei"] = pppStatus.imei;
    pp["imsi"] = pppStatus.imsi;
    pp["operator"] = pppStatus.oper;
    pp["rssi"] = pppStatus.rssi;
    pp["ip"] = IPAddress(pppStatus.ip).toString();
    pp["gateway"] = IPAddress(pppStatus.gateway).toString();
#endif
    sendJson(request, doc);
}

// Set the clock by hand: {"epoch": seconds since 1970, UTC} (old System page "Time Update")
static void apiTime(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    const char *body = (const char *)request->_tempObject;
    JsonDocument d;
    uint32_t epoch = 0;
    if (body && !deserializeJson(d, body))
        epoch = d["epoch"] | 0;
    if (epoch < 1600000000UL) // before 2020: certainly a mistake
    {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad time\"}");
        return;
    }
    timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    request->send(200, "application/json", "{\"ok\":true}");
}

// Factory reset: defaults saved to flash, then restart. The old button only reset RAM and never answered.
static void apiFactory(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    defaultConfig();
    bool saved = saveConfiguration("/default.cfg", config);
    request->send(saved ? 200 : 500, "application/json", saved ? "{\"ok\":true}" : "{\"ok\":false}");
    if (saved)
        xTaskCreate(rebootTask, "reboot", 2048, NULL, 1, NULL);
}

// Throw away unsaved runtime changes: read /default.cfg again (old "Load Default" button)
static void apiReload(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    bool ok = loadConfiguration("/default.cfg", config);
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---- Files on LittleFS (old File tab). Plain names in the root only: no paths, no "..". ----
static bool fileNameOk(const String &n)
{
    if (n.length() == 0 || n.length() > 31 || n == "." || n == "..")
        return false;
    for (size_t i = 0; i < n.length(); i++)
    {
        char c = n[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-')
            return false;
    }
    return true;
}

static void apiFiles(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    JsonDocument doc;
    doc["total"] = LITTLEFS.totalBytes();
    doc["used"] = LITTLEFS.usedBytes();
    JsonArray a = doc["files"].to<JsonArray>();
    File root = LITTLEFS.open("/");
    if (root && root.isDirectory())
    {
        for (File f = root.openNextFile(); f; f = root.openNextFile())
        {
            JsonObject o = a.add<JsonObject>();
            o["name"] = f.name();
            o["size"] = f.size();
            o["dir"] = f.isDirectory();
        }
    }
    sendJson(request, doc);
}

static void apiFileGet(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    String n = request->hasParam("name") ? request->getParam("name")->value() : "";
    if (!fileNameOk(n) || !LITTLEFS.exists("/" + n))
    {
        request->send(404, "application/json", "{\"ok\":false,\"error\":\"no such file\"}");
        return;
    }
    request->send(LITTLEFS, "/" + n, "application/octet-stream", true);
}

static void apiFileDelete(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    String n = request->hasParam("name") ? request->getParam("name")->value() : "";
    bool ok = fileNameOk(n) && LITTLEFS.remove("/" + n);
    request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void apiFormat(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    bool ok = LITTLEFS.format();
    if (ok)
        saveConfiguration("/default.cfg", config); // keep the running settings: they would be lost at the next boot
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Upload: the data chunks arrive before the final handler; only write with a valid login and name
static void apiUploadData(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (index == 0)
    {
        if (!request->authenticate(config.http_username, config.http_password) || !fileNameOk(filename))
            return; // the final handler answers
        request->_tempFile = LITTLEFS.open("/" + filename, "w");
    }
    if (!request->_tempFile)
        return;
    if (LITTLEFS.totalBytes() - LITTLEFS.usedBytes() < len + 4096)
    {
        request->_tempFile.close(); // keep a margin: a full flash would also stop the config from saving
        LITTLEFS.remove("/" + filename);
        return;
    }
    request->_tempFile.write(data, len);
    if (final)
        request->_tempFile.close();
}

static void apiUploadDone(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    const AsyncWebParameter *p = request->hasParam("file", true, true) ? request->getParam("file", true, true) : nullptr;
    bool ok = p && fileNameOk(p->value()) && LITTLEFS.exists("/" + p->value());
    request->send(ok ? 200 : 400, "application/json",
                  ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"bad name, no space or not saved\"}");
}


void webApiRegister(AsyncWebServer &server)
{
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++)
    {
        const WebAsset *a = &WEB_ASSETS[i];
        server.on(a->path, HTTP_GET, [a](AsyncWebServerRequest *request)
                  { sendAsset(request, *a); });
    }
    server.on("/api/files/get", HTTP_GET, apiFileGet);
    server.on("/api/files/delete", HTTP_POST, apiFileDelete);
    server.on("/api/files/format", HTTP_POST, apiFormat);
    server.on("/api/files/upload", HTTP_POST, apiUploadDone, apiUploadData);
    server.on("/api/files", HTTP_GET, apiFiles);
    server.on("/api/messages/send", HTTP_POST, apiMessageSend, NULL, apiBody);
    server.on("/api/messages", HTTP_GET, apiMessages);
    server.on("/api/lastheard", HTTP_GET, apiLastHeard);
    server.on("/api/monitor", HTTP_GET, apiMonitor);
    server.on("/api/gnss", HTTP_GET, apiGnss);
    server.on("/api/about", HTTP_GET, apiAbout);
    server.on("/api/sensors", HTTP_GET, apiSensors);
    server.on("/api/time", HTTP_POST, apiTime, NULL, apiBody);
    server.on("/api/factory", HTTP_POST, apiFactory);
    server.on("/api/reload", HTTP_POST, apiReload);
    server.on("/api/info", HTTP_GET, apiInfo);
    server.on("/api/meta", HTTP_GET, apiMeta);
    server.on("/api/config", HTTP_GET, apiConfigGet);
    server.on("/api/config", HTTP_POST, apiConfigPost, NULL, apiBody);
    server.on("/api/reboot", HTTP_POST, apiReboot);
}
