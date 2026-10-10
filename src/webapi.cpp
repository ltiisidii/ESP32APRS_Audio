/*
 Web UI (static app in web/, embedded gzip by tools/embed_web.py) and its JSON API.

 GET  /, /app.css, /app.js  embedded files (gzip, ETag, 304 when unchanged)
 GET  /api/info             live status (dashboard)
 GET  /api/config           whole configuration, secrets replaced by CFG_SECRET_MASK
 POST /api/config           JSON object with the keys to change; saves and asks for a restart
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
    int n = configApplyPatch(config, patch);
    if (n < 0)
    {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"expected an object\"}");
        return;
    }
    bool saved = n == 0 || saveConfiguration("/default.cfg", config);
    JsonDocument doc;
    doc["ok"] = saved;
    doc["applied"] = n;
    doc["restart"] = n > 0;
    sendJson(request, doc, saved ? 200 : 500);
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
    server.on("/api/config", HTTP_GET, apiConfigGet);
    server.on("/api/config", HTTP_POST, apiConfigPost, NULL, apiBody);
    server.on("/api/reboot", HTTP_POST, apiReboot);
}
