/*
 Name:		ESP32APRS_Audio
 Created:	13-10-2023 14:27:23
 Author:	HS5TQA/Atten
 Github:	https://github.com/nakhonthai
 Facebook:	https://www.facebook.com/atten
 Support IS: host:aprs.nakhonthai.net port:14580 or aprs.hs5tqa.ampr.org:14580
 Support IS monitor: http://aprs.nakhonthai.net:14501 or http://aprs.hs5tqa.ampr.org:14501
*/
#include <Arduino.h>
#include "webservice.h"
#include "base64.hpp"
#include "wireguard_vpn.h"
#include <LibAPRSesp.h>
#include <parse_aprs.h>
#include "webapi.h"
#include <ESPCPUTemp.h>
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include <ArduinoJson.h>

extern SemaphoreHandle_t psramMutex;
extern bool psramLock(TickType_t timeout = portMAX_DELAY);
extern void psramUnlock();

extern int offset;

// Helper function to allocate memory with PSRAM support
#ifdef PPPOS
#include <PPP.h>
#endif

#ifdef SH1106
#include <Adafruit_SH1106.h>
#else
#include "Adafruit_SSD1306.h"
#endif // SH1106

AsyncWebServer async_server(80);
AsyncWebServer async_websocket(81);
AsyncWebSocket ws("/ws");
AsyncWebSocket ws_gnss("/ws_gnss");

#ifdef PPPOS
extern pppType pppStatus;
#endif

// Server-sent events: last heard list and message list (JSON)
AsyncEventSource lastheard_events("/eventHeard");
AsyncEventSource message_events("/eventMsg");

extern uint64_t waitISRetry;
extern volatile int8_t adcEn;
extern volatile int8_t dacEn;
extern unsigned long upTimeStamp;
extern double VBat;
extern bool VBat_Flag;

#ifdef OLED
#ifdef SH1106
extern Adafruit_SH1106 display;
#else
extern Adafruit_SSD1306 display;
#endif
#elif defined(GUI_LCD)
extern Adafruit_SH1106 display;
#endif // OLED

bool defaultSetting = false;

void serviceHandle()
{
	// server.handleClient();
}

void notFound(AsyncWebServerRequest *request)
{
	request->send(404, "text/plain", "Not found");
}

static void jsonEscapeCopy(char *dest, size_t destSize, const char *src)
{
	size_t j = 0;
	for (size_t i = 0; src[i] != '\0' && j + 1 < destSize; i++)
	{
		unsigned char c = (unsigned char)src[i];
		if (c == '"' || c == '\\')
		{
			if (j + 2 >= destSize)
				break;
			dest[j++] = '\\';
			dest[j++] = (char)c;
		}
		else if (c < 0x20)
		{
			continue; // strip control bytes
		}
		else
		{
			dest[j++] = (char)c;
		}
	}
	dest[j] = '\0';
}

void event_lastHeard(bool gethtml)
{
	// log_d("Event count: %d",lastheard_events.count());
	// if (lastheard_events.count() == 0)
	//	return;

	struct pbuf_t aprs;
	ParseAPRS aprsParse;
	struct tm tmstruct, tmNow;

	// Using dynamic memory allocation instead of String
	// Sized to fit a fully-escaped path/LPath (up to 256 raw chars -> 512 escaped) plus JSON key/quote overhead
	char temp_html[600];
	// 16 KB working buffer: PSRAM when the board has it, else the normal heap
	char *html = nullptr;
#ifdef BOARD_HAS_PSRAM
	html = (char *)ps_calloc(16384, 1);
#endif
	if (!html)
		html = (char *)calloc(16384, 1);
	if (html==nullptr)
	{
		return; // Memory allocation failed
	}
	time_t timeNow = time(NULL);

	// log_d("Create html last heard");
	localtime_r(&timeNow, &tmNow);
//strcat(webString, "  { time: \"21:54:23\", icon: \"91-1.png\", callsign: \"HS5TQA-7\", path: \"RF: WIDE1-1\", dx: 0.0, packet: 2, audio: -19.6 },\n");
	strcpy(html, "[");
	char pkgRaw[512];
	for (int i = 0; i < PKGLISTSIZE; i++)
	{
		if (i >= PKGLISTSIZE)
			break;
		pkgListType pkg = getPkgList(i, pkgRaw, sizeof(pkgRaw));
		if (pkg.time > 0)
		{
			// if (pkg.raw == nullptr || pkg.length == 0)
			// 	continue;
			// bool validText = true;
			// for (size_t ci = 0; ci < pkg.length-1; ci++)
			// {
			// 	//if (!isprint((unsigned char)pkg.raw[ci]))
			// 	if((unsigned char)pkg.raw[ci] < 32 || (unsigned char)pkg.raw[ci] > 126)
			// 	{
			// 		validText = false;
			// 		break;
			// 	}
			// }
			// if (!validText)
			// 	continue;

			int packet = pkg.pkg;
			char *pos_gt = strchr(pkg.raw, '>'); // Find first position of '>'
			char *pos_colon = strchr(pkg.raw, ':');
			if(pos_gt == nullptr || pos_colon == nullptr)
				continue;
			if(pos_colon < pos_gt)
				continue;
			int start_val = pos_gt ? (pos_gt - pkg.raw) : -1;
			if (start_val > 3 && start_val < 10)
			{
				// Extract src_call substring
				char src_call[11];
				strncpy(src_call, pkg.raw, start_val);
				src_call[start_val] = '\0';
				memset(&aprs, 0, sizeof(pbuf_t));
				aprs.buf_len = 300;
				aprs.packet_len = pkg.length;
				strncpy((char *)aprs.data, pkg.raw, aprs.packet_len < sizeof(aprs.data) ? aprs.packet_len : sizeof(aprs.data) - 1);

				//char *pos_colon = strchr(pkg.raw, ':');
				char *pos_comma = strchr(pkg.raw, ',');
				char *pos_gt2 = strstr(pkg.raw + 2, ">"); // Find '>' starting from position 2
				char *pos_dash = pos_gt2 ? strchr(pos_gt2, '-') : NULL;

				int start_info = pos_colon ? (pos_colon - pkg.raw) : -1;
				int end_ssid = pos_comma ? (pos_comma - pkg.raw) : -1;
				int start_dst = pos_gt2 ? (pos_gt2 - pkg.raw) : -1;
				int start_dstssid = pos_dash ? (pos_dash - pkg.raw) : -1;

				char path[256] = "";

				if ((end_ssid > start_dst) && (end_ssid < start_info) && (end_ssid < (int)strlen(pkg.raw)))
				{
					int path_len = start_info - end_ssid - 1;
					strncpy(path, pkg.raw + end_ssid + 1, path_len);
					path[path_len] = '\0';
				}
				if (end_ssid < 5)
					end_ssid = start_info;
				if ((start_dstssid > start_dst) && (start_dstssid < start_dst + 10))
				{
					aprs.dstcall_end_or_ssid = &aprs.data[start_dstssid];
				}
				else
				{
					aprs.dstcall_end_or_ssid = &aprs.data[end_ssid];
				}
				aprs.info_start = &aprs.data[start_info + 1];
				aprs.dstname = &aprs.data[start_dst + 1];
				aprs.dstname_len = end_ssid - start_dst;
				aprs.dstcall_end = &aprs.data[end_ssid];
				aprs.srccall_end = &aprs.data[start_dst];

				// Serial.println(aprs.info_start);
				if (aprsParse.parse_aprs(&aprs))
				{
					pkg.calsign[10] = 0;
					// time_t tm = pkg.time;
					localtime_r(&pkg.time, &tmstruct);
					char strTime[20];
					//if (tmNow.tm_mday == tmstruct.tm_mday)
					//	sprintf(strTime, "%02d:%02d:%02d", tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
					//else
						sprintf(strTime, "%02d %02d:%02d:%02d", tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
					// String str = String(tmstruct.tm_hour, DEC) + ":" + String(tmstruct.tm_min, DEC) + ":" + String(tmstruct.tm_sec, DEC);

					// Append to html
					// strcat(html, "  { time: \"21:54:23\", icon: \"91-1.png\", callsign: \"HS5TQA-7\", path: \"RF: WIDE1-1\", dx: 0.0, packet: 2, rssi: -19.6 },\n");
					
					snprintf(temp_html, sizeof(temp_html), "{\"time\":\"%s\",", strTime);
					strcat(html, temp_html);
					char fileImg[64] = "";
					uint8_t sym = (uint8_t)aprs.symbol[1];
					if (sym > 31 && sym < 127)
					{
						if (aprs.symbol[0] > 64 && aprs.symbol[0] < 91) // table A-Z
						{
							snprintf(fileImg, sizeof(fileImg), "%d", sym);
							// if (aprs.symbol[0] == 92)
							// {
							// 	strcat(fileImg, "-2.png");
							// }
							// else if (aprs.symbol[0] == 47)
							// {
								strcat(fileImg, "-1.png");
							//}

							//snprintf(temp_html, sizeof(temp_html), "<td><b>%c</b></td>", aprs.symbol[0]);
							snprintf(temp_html, sizeof(temp_html), "\"icon\":\"%s\",", fileImg);
							strcat(html, temp_html);
						}
						else
						{
							snprintf(fileImg, sizeof(fileImg), "%d", sym);
							if (aprs.symbol[0] == 92)
							{
								strcat(fileImg, "-2.png");
							}
							else if (aprs.symbol[0] == 47)
							{
								strcat(fileImg, "-1.png");
							}
							else
							{
								strcpy(fileImg, "dot.png");
							}
							//snprintf(temp_html, sizeof(temp_html), "<td><img src=\"https://aprs.p00lack.cc/symbols/icons/%s\"></td>", fileImg);
							snprintf(temp_html, sizeof(temp_html), "\"icon\":\"%s\",", fileImg);
							strcat(html, temp_html);
						}
					}
					else
					{
						//strcat(html, "<td><img src=\"https://aprs.p00lack.cc/symbols/icons/dot.png\"></td>");
						strcat(html, "\"icon\":\"dot.png\",");
					}
					
					char src_call_esc[23];
					jsonEscapeCopy(src_call_esc, sizeof(src_call_esc), src_call);
					if (aprs.srcname_len > 0 && aprs.srcname_len < 10) // Get Item/Object
					{
						char itemname[10];
						memset(&itemname, 0, 10);
						memcpy(&itemname, aprs.srcname, aprs.srcname_len);
						char itemname_esc[21];
						jsonEscapeCopy(itemname_esc, sizeof(itemname_esc), itemname);
						snprintf(temp_html, sizeof(temp_html), "\"callsign\":\"%s(%s)\",", itemname_esc, src_call_esc);
						strcat(html, temp_html);
					}else{
						snprintf(temp_html, sizeof(temp_html), "\"callsign\":\"%s\",", src_call_esc);
						strcat(html, temp_html);
					}
					//strcat(html, "</td>");
					if (strlen(path) == 0)
					{
						strcat(html, "\"path\":\"RF: DIRECT\",");
					}
					else
					{
						// Find last occurrence of ','
						char *last_comma = strrchr(path, ',');
						char LPath[256] = "";
						if (last_comma != NULL)
						{
							strncpy(LPath, last_comma + 1, sizeof(LPath) - 1);
							LPath[sizeof(LPath) - 1] = '\0';
						}
						else
						{
							strncpy(LPath, path, sizeof(LPath) - 1);
							LPath[sizeof(LPath) - 1] = '\0';
						}
						char path_esc[513];
						// if(path.indexOf("qAR")>=0 || path.indexOf("qAS")>=0 || path.indexOf("qAC")>=0){ //Via from Internet Server
						if (strstr(path, "qA") != NULL || strstr(path, "TCPIP") != NULL)
						{
							jsonEscapeCopy(path_esc, sizeof(path_esc), LPath);
							snprintf(temp_html, sizeof(temp_html), "\"path\":\"INET:%s\",", path_esc);
							strcat(html, temp_html);
						}
						else
						{
							jsonEscapeCopy(path_esc, sizeof(path_esc), path);
							if (strchr(path, '*') != NULL)
							{
								snprintf(temp_html, sizeof(temp_html), "\"path\":\"DIGI: %s\",", path_esc);
								strcat(html, temp_html);
							}
							else
							{
								snprintf(temp_html, sizeof(temp_html), "\"path\":\"RF: %s\",", path_esc);
								strcat(html, temp_html);
							}
						}
					}
					// html += "<td>" + path + "</td>";
					if (aprs.flags & F_HASPOS)
					{
						double lat, lon;
						if (gps.location.isValid())
						{
							lat = gps.location.lat();
							lon = gps.location.lng();
						}
						else
						{
							lat = config.igate_lat;
							lon = config.igate_lon;
						}
						double dtmp = aprsParse.direction(lon, lat, aprs.lng, aprs.lat);
						double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
						snprintf(temp_html, sizeof(temp_html), "\"dx\":\"%.1fkm/%.0f°\",", dist, dtmp);
						strcat(html, temp_html);
					}
					else
					{
						strcat(html, "\"dx\":\"-\",");
					}
					snprintf(temp_html, sizeof(temp_html), "\"packet\":\"%d\",", packet);
					strcat(html, temp_html);
					if (pkg.audio_level == 0)
					{
						strcat(html, "\"audio\":\"-\"},");
					}
					else
					{
						double Vrms = (double)pkg.audio_level / 1000;
						double audBV = 20.0F * log10(Vrms);
						// if (audBV < -20.0F)
						// {
						// 	strcat(html, " audio:\"");
						// }
						// else if (audBV > -5.0F)
						// {
						// 	strcat(html, "<td style=\"color: #f00000;\">");
						// }
						// else
						// {
						// 	strcat(html, "<td style=\"color: #008000;\">");
						// }
						snprintf(temp_html, sizeof(temp_html), "\"audio\":\"%.1f\"},", audBV);
						strcat(html, temp_html);
						//strcat(html, "dBV</td></tr>\n");
					}
				}
			}
		}
	}
	html[strlen(html) - 1] = '\0'; // Remove the last comma
	if (html[0] == '[')
	strcat(html, "]");

	size_t len = strlen(html);
	// char *info = (char *)calloc(len + 1, sizeof(char));
	// if (info)
	// {
	// 	strcpy(info, html);
	if (len > 10)
		lastheard_events.send(html, "lastHeard", millis() / 1000, 1000);
	// 	free(info);
	// }

	free(html);
}

// Message list as JSON for the web UI: [{t, call, id, text, ack, rx}], newest first.
// ack: >0 = retries left, 0 = no acknowledgement, -1 = received, -2 = acknowledged / sent without retry.
// gethtml=true returns it (initial load); false pushes it to the /eventMsg clients.
String event_chatMessage(bool gethtml)
{
	JsonDocument doc;
	JsonArray a = doc.to<JsonArray>();
	pkgMsgSort(msgQueue);
	for (int i = 0; i < PKGLISTSIZE; i++)
	{
		msgType m = getMsgList(i);
		if (m.time <= 0)
			continue;
		m.callsign[sizeof(m.callsign) - 1] = 0;
		JsonObject o = a.add<JsonObject>();
		o["t"] = (uint32_t)m.time;
		o["call"] = m.callsign;
		o["id"] = m.msgID;
		o["text"] = m.text ? m.text : "";
		o["ack"] = m.ack;
		o["rx"] = m.rxtx;
	}
	String out;
	serializeJson(doc, out);
	if (gethtml)
		return out;
	if (message_events.count() > 0)
		message_events.send(out.c_str(), "chatMsg", time(NULL), 5000);
	return String("");
}


void handle_ws(char *Raw, size_t len, uint16_t mVrms)
{
	if (ws.count() < 1)
		return;

	char *jsonMsg;
	time_t timeStamp;
	time(&timeStamp);

	if (len > 5)
	{
		int input_length = len;
		jsonMsg = (char *)calloc((input_length * 2) + 200, sizeof(char));
		if (jsonMsg)
		{
			char *input_buffer = (char *)calloc(input_length + 2, sizeof(char));
			char *output_buffer = (char *)calloc(input_length * 2, sizeof(char));
			if (output_buffer)
			{
				memset(input_buffer, 0, (input_length + 2));
				memset(output_buffer, 0, (input_length * 2));
				// lastPkgRaw.toCharArray(input_buffer, input_length, 0);
				memcpy(input_buffer, Raw, len);
				encode_base64((unsigned char *)input_buffer, input_length, (unsigned char *)output_buffer);
				// Serial.println(output_buffer);
				sprintf(jsonMsg, "{\"Active\":\"1\",\"mVrms\":\"%d\",\"RAW\":\"%s\",\"timeStamp\":\"%li\"}", mVrms, output_buffer, timeStamp);
				// Serial.println(jsonMsg);
				free(input_buffer);
				free(output_buffer);
			}
			ws.textAll(jsonMsg);
			free(jsonMsg);
		}
	}
	else
	{
		jsonMsg = (char *)calloc(300, sizeof(char));
		if (jsonMsg)
		{
			if (mVrms > 0)
				sprintf(jsonMsg, "{\"Active\":\"1\",\"mVrms\":\"%d\",\"RAW\":\"REVDT0RFIEZBSUwh\",\"timeStamp\":\"%li\"}", mVrms, timeStamp);
			else
				sprintf(jsonMsg, "{\"Active\":\"0\",\"mVrms\":\"0\",\"RAW\":\"\",\"timeStamp\":\"%li\"}", timeStamp);
			ws.textAll(jsonMsg);
			free(jsonMsg);
		}
	}
}

void handle_ws_gnss(char *nmea, size_t size)
{
	if (ws_gnss.count() < 1)
		return;

	time_t timeStamp;
	time(&timeStamp);
	// unsigned int output_length = encode_base64_length(size);
	// unsigned char nmea_enc[output_length];
	// char jsonMsg[output_length + 100];
	// encode_base64((unsigned char *)nmea, size, (unsigned char *)nmea_enc);
	// sprintf(jsonMsg, "{\"en\":\"%d\",\"lat\":\"%.5f\",\"lng\":\"%.5f\",\"alt\":\"%.2f\",\"spd\":\"%.2f\",\"csd\":\"%.1f\",\"hdop\":\"%.2f\",\"sat\":\"%d\",\"time\":\"%d\",\"timeStamp\":\"%li\",\"RAW\":\"", (int)config.gnss_enable, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.kmph(), gps.course.deg(), gps.hdop.hdop(), gps.satellites.value(), gps.time.value(), timeStamp);
	// strncat(jsonMsg, (const char *)nmea_enc, output_length);
	// strcat(jsonMsg, "\"}");
	unsigned int output_length = encode_base64_length(size);
	unsigned char *nmea_enc = (unsigned char *)calloc(output_length + 2, sizeof(unsigned char));
	char *jsonMsg = (char *)calloc(output_length + 200, sizeof(char));
	if (nmea_enc && jsonMsg)
	{
		encode_base64((unsigned char *)nmea, size, (unsigned char *)nmea_enc);
		sprintf(jsonMsg, "{\"en\":\"%d\",\"lat\":\"%.5f\",\"lng\":\"%.5f\",\"alt\":\"%.2f\",\"spd\":\"%.2f\",\"csd\":\"%.1f\",\"hdop\":\"%.2f\",\"sat\":\"%d\",\"time\":\"%d\",\"timeStamp\":\"%li\",\"RAW\":\"", (int)config.gnss_enable, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.kmph(), gps.course.deg(), gps.hdop.hdop(), gps.satellites.value(), gps.time.value(), timeStamp);
		strncat(jsonMsg, (const char *)nmea_enc, output_length);
		strcat(jsonMsg, "\"}");
		ws_gnss.textAll(jsonMsg);
		free(nmea_enc);
		free(jsonMsg);
	}
	
}

static void ota_url_task(void *pvParameters)
{
	char *url = (char *)pvParameters;
	HTTPClient http;
	http.begin(url);
	http.setTimeout(30000);
	int httpCode = http.GET();
	if (httpCode == HTTP_CODE_OK) {
		int contentLength = http.getSize();
		WiFiClient *stream = http.getStreamPtr();
		if (contentLength > 0 && Update.begin(contentLength)) {
			disableLoopWDT();
			disableCore0WDT();
			Update.writeStream(*stream);
			if (Update.end(true)) {
				log_i("OTA URL update success, rebooting");
				delay(500);
				esp_restart();
			} else {
				Update.printError(Serial);
			}
		} else {
			log_e("OTA URL: bad content length or Update.begin failed");
		}
	} else {
		log_e("OTA URL: HTTP GET failed, code=%d", httpCode);
	}
	http.end();
	free(url);
	vTaskDelete(NULL);
}

void handle_ota_url(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password)) {
		return request->requestAuthentication();
	}
	if (!request->hasParam("url", true)) {
		request->send(400, "text/plain", "Missing url parameter");
		return;
	}
	String urlStr = request->getParam("url", true)->value();
	if (urlStr.length() == 0) {
		request->send(400, "text/plain", "Empty url");
		return;
	}
	char *urlBuf = (char *)malloc(urlStr.length() + 1);
	if (!urlBuf) {
		request->send(500, "text/plain", "Out of memory");
		return;
	}
	strcpy(urlBuf, urlStr.c_str());
	request->send(200, "text/plain", "OTA update started");
	xTaskCreate(ota_url_task, "ota_url_task", 8192, urlBuf, 5, NULL);
}

// Remote version file published by copy_firmware.py, checked against the
// running VERSION/VERSION_BUILD to let the "about" page report new releases.
// Base URL of your own firmware server (with trailing "/"). It must serve version.json and the
// .bin files named like FirmwareOTA in handle_about(). Empty = online OTA disabled: the device
// never contacts any firmware server. Can also be set with -DOTA_SERVER_URL='"http://..."'.
#ifndef OTA_SERVER_URL
#define OTA_SERVER_URL ""
#endif
#define VERSION_CHECK_URL OTA_SERVER_URL "version.json"

void handle_check_version(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}

	if (OTA_SERVER_URL[0] == 0)
	{
		request->send(404, "application/json", "{\"error\":\"Online OTA not configured\"}");
		return;
	}

	HTTPClient http;
	http.begin(VERSION_CHECK_URL);
	http.setTimeout(5000);
	int httpCode = http.GET();

	if (httpCode != HTTP_CODE_OK)
	{
		http.end();
		char errBuf[100];
		snprintf(errBuf, sizeof(errBuf), "{\"error\":\"HTTP GET failed, code=%d\"}", httpCode);
		request->send(500, "application/json", errBuf);
		return;
	}

	String payload = http.getString();
	http.end();

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, payload);
	if (err)
	{
		request->send(500, "application/json", "{\"error\":\"Invalid version data\"}");
		return;
	}

	const char *latestVersion = doc["version"] | "";
	const char *latestBuild = doc["build"] | "";
	const char *latestDate = doc["date"] | "";

	bool updateAvailable = (strcmp(latestVersion, VERSION) != 0) || (strcmp(latestBuild, VERSION_BUILD) != 0);

	char resp[400];
	snprintf(resp, sizeof(resp),
			 "{\"current_version\":\"%s\",\"current_build\":\"%s\",\"latest_version\":\"%s\",\"latest_build\":\"%s\",\"latest_date\":\"%s\",\"update_available\":%s}",
			 VERSION, VERSION_BUILD, latestVersion, latestBuild, latestDate, updateAvailable ? "true" : "false");
	request->send(200, "application/json", resp);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{

	if (type == WS_EVT_CONNECT)
	{

		log_d("Websocket client connection received");
	}
	else if (type == WS_EVT_DISCONNECT)
	{

		log_d("Client disconnected");
	}
}

// void handle_vpn_request(AsyncWebServerRequest *request) {
//     HTTPClient http;

//     String url = "http://vpn.nakhonthai.net:82/wg/create";

//     String mac = WiFi.macAddress();
//     mac.replace(":", "");

//     String payload = "{\"name\":\"" + mac + "\"}";

//     http.begin(url);
//     http.addHeader("Content-Type", "application/json");

//     int httpCode = http.POST(payload);

//     if (httpCode > 0) {
//         String res = http.getString();
//         request->send(200, "application/json", res);
//     } else {
//         request->send(500, "text/plain", "Error contacting VPN server");
//     }

//     http.end();
// }

bool webServiceBegin = true;
void webService()
{
	if (webServiceBegin)
	{
		webServiceBegin = false;
	}
	else
	{
		return;
	}
	ws.onEvent(onWsEvent);

	// web client handlers
	webApiRegister(async_server); // the web UI (web/) and its JSON API
	// async_server.on("/symbol2", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
	// 				{ handle_symbol2(request); });
	// async_server.on("/realtime", HTTP_GET, [](AsyncWebServerRequest *request)
	// 				{ handle_realtime(request); });
	// async_server.on("/lastHeard", HTTP_GET, [](AsyncWebServerRequest *request)
	// 				{ handle_lastHeard(request); });

	// async_server.on("/api/vpnreq", HTTP_GET, handle_vpn_request);
	async_server.on(
		"/update", HTTP_POST, [](AsyncWebServerRequest *request)
		{
  		bool espShouldReboot = !Update.hasError();
  		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", espShouldReboot ? "<h1><strong>Update DONE</strong></h1><br><a href='/'>Return Home</a>" : "<h1><strong>Update FAILED</strong></h1><br><a href='/updt'>Retry?</a>");
  		response->addHeader("Connection", "close");
  		request->send(response); },
		[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
		{
			if (!index)
			{
				log_d("Update Start: %s\n", filename.c_str());
				if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000))
				{
					Update.printError(Serial);
				}
				else
				{
					adcEn = -1;
					delay(500);
					// disableLoopWDT();
					// disableCore0WDT();
					// disableCore1WDT();
					//  vTaskSuspend(taskAPRSPollHandle);
					//  vTaskSuspend(taskAPRSHandle);
					//  vTaskSuspend(taskSensorHandle);
					//  vTaskSuspend(taskSerialHandle);
					//  vTaskSuspend(taskGPSHandle);
					//  vTaskSuspend(taskSensorHandle);
				}
			}
			if (!Update.hasError())
			{
				if (Update.write(data, len) != len)
				{
					Update.printError(Serial);
				}
			}
			if (final)
			{
				if (Update.end(true))
				{
					log_d("Update Success: %uByte\n", index + len);
					delay(1000);
					esp_restart();
				}
				else
				{
					Update.printError(Serial);
				}
			}
		});

	async_server.on("/ota_url", HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_ota_url(request); });

	async_server.on("/check_version", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_check_version(request); });		

	lastheard_events.onConnect([](AsyncEventSourceClient *client)
							   {
    if(client->lastId()){
      log_d("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    // the table is sent from the main loop (event_lastHeard) as soon as possible
    lastHeardTimeout = 0;
    lastHeard_Flag = true;
});
	async_server.addHandler(&lastheard_events);

	message_events.onConnect([](AsyncEventSourceClient *client)
							 {
    if(client->lastId()){
      log_d("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
	String html = event_chatMessage(true);
    client->send(html.c_str(), "chatMsg", time(NULL), 5000); });
	async_server.addHandler(&message_events);

	async_server.onNotFound(notFound);
	async_server.begin();
	async_websocket.addHandler(&ws);
	async_websocket.addHandler(&ws_gnss);
	async_websocket.begin();
}
