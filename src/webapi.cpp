/*
 Web UI (static app in web/, embedded gzip by tools/embed_web.py) and its JSON API.

 GET  /, /app.css, /app.js  embedded files (gzip, ETag, 304 when unchanged)
 GET  /api/info             live status
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

#define API_MAX_BODY 16384






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
    JsonDocument doc;
    doc["version"] = String(VERSION) + VERSION_BUILD;
    doc["chip"] = ESP.getChipModel();
    doc["uptime"] = (uint32_t)(millis64() / 1000);
    doc["heap"] = ESP.getFreeHeap();
    doc["heapMin"] = ESP.getMinFreeHeap();
    doc["heapSize"] = ESP.getHeapSize();
    doc["callsign"] = config.aprs_mycall;
    doc["ssid"] = config.aprs_ssid;
    doc["fsUsed"] = LITTLEFS.usedBytes();
    doc["fsTotal"] = LITTLEFS.totalBytes();
    JsonObject w = doc["wifi"].to<JsonObject>();
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
    doc["igate"] = aprsClient.connected();
    JsonObject s = doc["stats"].to<JsonObject>();
    s["rx"] = status.rxCount;
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
