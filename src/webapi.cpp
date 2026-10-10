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
 Everything requires the web login (config.http_username / http_password).
*/
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "webservice.h"
#include "webapi.h"
#include "web_assets.h"
#include "millis64.h"
#include "wireguard_vpn.h"
#include <AFSK.h>
#include <ESPCPUTemp.h>
#ifdef PPPOS
#include <PPP.h>
#endif
#ifdef MQTT
#include <PubSubClient.h>
extern PubSubClient clientMQTT;
#endif

#define API_MAX_BODY 16384

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
    bool restart;  // WiFi mode/AP/networks and Bluetooth are only read at boot
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
    p.aprsIs = o.igate_en != n.igate_en || o.aprs_ssid != n.aprs_ssid || o.aprs_port != n.aprs_port ||
               strcmp(o.aprs_mycall, n.aprs_mycall) || strcmp(o.aprs_host, n.aprs_host) || strcmp(o.aprs_filter, n.aprs_filter);
    p.restart = o.wifi_mode != n.wifi_mode || o.wifi_ap_ch != n.wifi_ap_ch ||
                strcmp(o.wifi_ap_ssid, n.wifi_ap_ssid) || strcmp(o.wifi_ap_pass, n.wifi_ap_pass) ||
                memcmp(o.wifi_sta, n.wifi_sta, sizeof(o.wifi_sta)) ||
                memcmp(&o.bt_slave, &n.bt_slave, (const char *)&o.bt_power - (const char *)&o.bt_slave + sizeof(o.bt_power));
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
        if (plan.rfModule || plan.modem)
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
    sendJson(request, doc);
}

static void rebootTask(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // let the HTTP answer leave first
    ESP.restart();
}

static void apiReboot(AsyncWebServerRequest *request)
{
    if (!authOk(request))
        return;
    request->send(200, "application/json", "{\"ok\":true}");
    xTaskCreate(rebootTask, "reboot", 2048, NULL, 1, NULL);
}

void webApiRegister(AsyncWebServer &server)
{
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++)
    {
        const WebAsset *a = &WEB_ASSETS[i];
        server.on(a->path, HTTP_GET, [a](AsyncWebServerRequest *request)
                  { sendAsset(request, *a); });
    }
    server.on("/api/info", HTTP_GET, apiInfo);
    server.on("/api/meta", HTTP_GET, apiMeta);
    server.on("/api/config", HTTP_GET, apiConfigGet);
    server.on("/api/config", HTTP_POST, apiConfigPost, NULL, apiBody);
    server.on("/api/reboot", HTTP_POST, apiReboot);
}
