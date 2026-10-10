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
#include "jquery_min_js.h"
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
char *allocateStringMemory(size_t size)
{
	char *ptr = NULL;
#ifdef BOARD_HAS_PSRAM
	// Try to allocate in PSRAM first
	size*=2; // overallocation to reduce fragmentation
	ptr = (char *)ps_calloc(size, sizeof(char));
	if (ptr != NULL)
	{
		memset(ptr, 0, size); // Initialize memory to zero
		return ptr;
	}
	// If PSRAM allocation fails, fall back to regular heap
#endif
	// Regular heap allocation (calloc already zeroes it; callers check for NULL)
	ptr = (char *)calloc(size, sizeof(char));
	if (ptr == NULL)
		log_e("allocateStringMemory(%u) failed", (unsigned)size);
	return ptr;
}

// PATH option label: the "UserDefine" entries show what they contain, e.g.
// "UserDefine 3 (WIDE1-1,WIDE2-1)", instead of a bare name (index values are unchanged)
static const char *pathLabel(uint8_t idx)
{
	static char buf[sizeof(config.path[0]) + 24];
	if (idx >= 13 && idx <= 16 && config.path[idx - 13][0])
	{
		snprintf(buf, sizeof(buf), "%s (%s)", PATH_NAME[idx], config.path[idx - 13]);
		return buf;
	}
	return PATH_NAME[idx];
}

// Helper function to format integers to string using allocateStringMemory
char *intToString(int value)
{
	char *str = allocateStringMemory(12); // Enough for a 32-bit integer + null terminator
	if (str != NULL)
	{
		sprintf(str, "%d", value);
	}
	return str;
}

// Helper function to format floats to string using allocateStringMemory
char *floatToString(float value, int decimals)
{
	char *str = allocateStringMemory(20); // Enough for most float values
	if (str != NULL)
	{
		switch (decimals)
		{
		case 0:
			sprintf(str, "%.0f", value);
			break;
		case 1:
			sprintf(str, "%.1f", value);
			break;
		case 2:
			sprintf(str, "%.2f", value);
			break;
		case 3:
			sprintf(str, "%.3f", value);
			break;
		default:
			sprintf(str, "%.2f", value);
			break;
		}
	}
	return str;
}

// Helper function to convert Arduino String to char* using allocateStringMemory
char *StringToCharPtr(const String &str)
{
	size_t len = str.length() + 1; // +1 for null terminator
	char *charPtr = allocateStringMemory(len);
	if (charPtr != NULL)
	{
		strcpy(charPtr, str.c_str());
	}
	return charPtr;
}

#ifdef PPPOS
#include <PPP.h>
#endif

#ifdef SH1106
#include <Adafruit_SH1106.h>
#else
#include "Adafruit_SSD1306.h"
#endif // SH1106

#define SCREEN_ADDRESS 0x3C

AsyncWebServer async_server(80);
AsyncWebServer async_websocket(81);
AsyncWebSocket ws("/ws");
AsyncWebSocket ws_gnss("/ws_gnss");

#ifdef MQTT
#include <PubSubClient.h>
extern PubSubClient clientMQTT;
#endif

#ifdef PPPOS
extern pppType pppStatus;
#endif

// Create an Event Source on /events
AsyncEventSource lastheard_events("/eventHeard");
AsyncEventSource message_events("/eventMsg");

char *webString;

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

void saveConfig(AsyncWebServerRequest *request)
{
	String html;
	if (saveConfiguration("/default.cfg", config))
	{
		html = "Setup completed successfully";
		request->send(200, "text/html", html); // send to someones browser when asked
	}
	else
	{
		html = "Save config failed.";
		request->send(501, "text/html", html); // Not Implemented
	}
	html.clear();
}

void serviceHandle()
{
	// server.handleClient();
}

void notFound(AsyncWebServerRequest *request)
{
	request->send(404, "text/plain", "Not found");
}

void handle_logout(AsyncWebServerRequest *request)
{
	char *webString = allocateStringMemory(64); // Small buffer for "Log out"
	if (!webString)
	{
		return; // Memory allocation failed
	}
	strcpy(webString, "Log out");
	request->send(200, "text/html", webString);
	free(webString); // Free the allocated memory
}

void setMainPage(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}

	// Using dynamic memory allocation instead of String
	char *webString = allocateStringMemory(12000); // Initial buffer size, adjust as needed
	if (!webString)
	{
		return; // Memory allocation failed
	}

	strcpy(webString, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n");
	strcat(webString, "<meta name=\"robots\" content=\"index\" />\n");
	strcat(webString, "<meta name=\"robots\" content=\"follow\" />\n");
	strcat(webString, "<meta name=\"language\" content=\"English\" />\n");
	strcat(webString, "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n");
	strcat(webString, "<meta name=\"GENERATOR\" content=\"configure 20230924\" />\n");
	strcat(webString, "<meta name=\"Author\" content=\"Mr.Somkiat Nakhonthai (HS5TQA)\" />\n");
	strcat(webString, "<meta name=\"Description\" content=\"Web Embedded Configuration\" />\n");
	strcat(webString, "<meta name=\"KeyWords\" content=\"ESP32,ESP32C3,AFSK,APRS\" />\n");
	strcat(webString, "<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\" />\n");
	strcat(webString, "<meta http-equiv=\"pragma\" content=\"no-cache\" />\n");
	strcat(webString, "<link rel=\"shortcut icon\" href=\"http://aprs.nakhonthai.net/favicon.ico\" type=\"image/x-icon\" />\n");
	strcat(webString, "<meta http-equiv=\"Expires\" content=\"0\" />\n");

	char temp_buffer[512];
	if (strlen(config.host_name) > 0)
	{
		snprintf(temp_buffer, sizeof(temp_buffer), "<title>%s</title>\n", config.host_name);
		strcat(webString, temp_buffer);
	}
	else
	{
		strcat(webString, "<title>ESP32APRS_Audio</title>\n");
	}

	strcat(webString, "<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\" />\n");
	strcat(webString, "<script src=\"/jquery-3.7.1.js\"></script>\n");
	strcat(webString, "<script type=\"text/javascript\">\n");
	strcat(webString, "function selectTab(evt, tabName) {\n");
	strcat(webString, "var i, tabcontent, tablinks;\n");
	strcat(webString, "tablinks = document.getElementsByClassName(\"nav-tabs\");\n");
	strcat(webString, "for (i = 0; i < tablinks.length; i++) {\n");
	strcat(webString, "tablinks[i].className = tablinks[i].className.replace(\" active\", \"\");\n");
	strcat(webString, "}\n");
	strcat(webString, "\n");
	strcat(webString, "//document.getElementById(tabName).style.display = \"block\";\n");
	strcat(webString, "if (tabName == 'DashBoard') {\n");
	strcat(webString, "location.href = \"/\"; return;\n"); // dashboard lives in the new UI
	strcat(webString, "} else if (tabName == 'Radio') {\n");
	strcat(webString, "location.href = \"/#radio\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'IGATE') {\n");
	strcat(webString, "location.href = \"/#igate\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'DIGI') {\n");
	strcat(webString, "location.href = \"/#digi\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'TRACKER') {\n");
	strcat(webString, "location.href = \"/#tracker\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'WX') {\n");
	strcat(webString, "location.href = \"/#weather\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'TLM') {\n");
	strcat(webString, "location.href = \"/#telemetry\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'SENSOR') {\n");
	strcat(webString, "location.href = \"/#sensors\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'VPN') {\n");
	strcat(webString, "$(\"#contentmain\").load(\"/vpn\");\n");
#ifdef MQTT
	strcat(webString, "} else if (tabName == 'MQTT') {\n");
	strcat(webString, "$(\"#contentmain\").load(\"/mqtt\");\n");
#endif
	strcat(webString, "} else if (tabName == 'MSG') {\n");
	strcat(webString, "$(\"#contentmain\").load(\"/msg\");\n");
	strcat(webString, "} else if (tabName == 'WiFi') {\n");
	strcat(webString, "location.href = \"/#wifi\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'MOD') {\n");
	strcat(webString, "location.href = \"/#modules\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'System') {\n");
	strcat(webString, "location.href = \"/#system\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'File') {\n");
	strcat(webString, "location.href = \"/#files\"; return;\n"); // migrated to the new UI
	strcat(webString, "} else if (tabName == 'About') {\n");
	strcat(webString, "location.href = \"/#about\"; return;\n"); // migrated to the new UI
	strcat(webString, "}\n");
	strcat(webString, "\n");
	strcat(webString, "if (evt != null) evt.currentTarget.className += \" active\";\n");
	strcat(webString, "}\n");
	strcat(webString, "if (!!window.EventSource) {");
	strcat(webString, "var source = new EventSource('/eventMsg');");

	strcat(webString, "source.addEventListener('open', function(e) {");
	strcat(webString, "console.log(\"Events MSG Connected\");");
	strcat(webString, "}, false);");
	strcat(webString, "source.addEventListener('error', function(e) {");
	strcat(webString, "if (e.target.readyState != EventSource.OPEN) {");
	strcat(webString, "console.log(\"Events MSG Disconnected\");");
	strcat(webString, "}\n}, false);");
	strcat(webString, "source.addEventListener('chatMsg', function(e) {");
	// strcat(webString, "console.log(\"lastHeard\", e.data);");
	strcat(webString, "var lh=document.getElementById(\"chatMsg\");");
	strcat(webString, "if(lh != null) {lh.innerHTML = e.data;}");
	strcat(webString, "}, false);\n}\n");
	//strcat(webString, "</script>\n");

	strcat(webString, "</script>\n");
	strcat(webString, "</head>\n");
	//strcat(webString, "\n");
	strcat(webString, "<body onload=\"selectTab(null, location.hash.slice(1) || 'DashBoard')\">\n");
	strcat(webString, "\n");
	strcat(webString, "<div class=\"container\">\n");
	strcat(webString, "<div class=\"header\">\n");
	// strcat(webString, "<div style=\"font-size: 8px; text-align: right; padding-right: 8px;\">ESP32IGate Firmware V" + String(VERSION) + "</div>\n");
	// strcat(webString, "<div style=\"font-size: 8px; text-align: right; padding-right: 8px;\"><a href=\"/logout\">[LOG OUT]</a></div>\n");
	if (strlen(config.host_name) > 0)
	{
		snprintf(temp_buffer, sizeof(temp_buffer), "<h1>%s</h1>\n", config.host_name);
		strcat(webString, temp_buffer);
	}
	else
	{
		strcat(webString, "<h1>ESP32APRS_Audio</h1>\n");
	}
	strcat(webString, "<div style=\"font-size: 8px; text-align: right; padding-right: 8px;\"><a href=\"/logout\">[LOG OUT]</a></div>\n");
	strcat(webString, "<div class=\"row\">\n");
	strcat(webString, "<ul class=\"nav nav-tabs\" style=\"margin: 5px;\">\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'DashBoard')\">&larr; New UI</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'Radio')\" id=\"btnRadio\">Radio</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'IGATE')\">IGATE</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'DIGI')\">DIGI</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'TRACKER')\">TRACKER</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'WX')\">WX</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'TLM')\">TLM</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'SENSOR')\">SENSOR</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'VPN')\">VPN</button>\n");
#ifdef MQTT
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'MQTT')\">MQTT</button>\n");
#endif
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'MSG')\">MSG</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'WiFi')\">WiFi</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'MOD')\">MOD</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'System')\">System</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'File')\">File</button>\n");
	strcat(webString, "<button class=\"nav-tabs\" onclick=\"selectTab(event, 'About')\">About</button>\n");
	strcat(webString, "</ul>\n");
	strcat(webString, "</div>\n");
	strcat(webString, "</div>\n");
	strcat(webString, "\n");

	strcat(webString, "<div class=\"contentwide\" id=\"contentmain\"  style=\"font-size: 2pt;\">\n");
	strcat(webString, "\n");
	strcat(webString, "</div>\n");
	strcat(webString, "<br />\n");
	strcat(webString, "<div class=\"footer\">\n");
	strcat(webString, "ESP32APRS_Audio Web Configuration<br />Copy right ©2023.\n");
	strcat(webString, "<br />\n");
	strcat(webString, "</div>\n");
	strcat(webString, "</div>\n");
	strcat(webString, "<!-- <script type=\"text/javascript\" src=\"/nice-select.min.js\"></script> -->\n");
	strcat(webString, "<script type=\"text/javascript\">\n");
	strcat(webString, "var selectize = document.querySelectorAll('select')\n");
	strcat(webString, "var options = { searchable: true };\n");
	strcat(webString, "selectize.forEach(function (select) {\n");
	strcat(webString, "if (select.length > 30 && null === select.onchange && !select.name.includes(\"ExtendedId\")) {\n");
	strcat(webString, "select.classList.add(\"small\", \"selectize\");\n");
	strcat(webString, "tabletd = select.closest('td');\n");
	strcat(webString, "tabletd.style.cssText = 'overflow-x:unset';\n");
	strcat(webString, "NiceSelect.bind(select, options);\n");
	strcat(webString, "}\n");
	strcat(webString, "});\n");
	strcat(webString, "</script>\n");
	strcat(webString, "</body>\n");
	strcat(webString, "</html>");


	AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const char *)webString);
	response->addHeader("Sensor", "content");
	response->addHeader("Cache-Control", "no-cache");
	request->send(response);
	free(webString);
	lastHeardTimeout = 0;
	lastHeard_Flag = true;
}

////////////////////////////////////////////////////////////
// handler for web server request: http://IpAddress/      //
////////////////////////////////////////////////////////////

void handle_css(AsyncWebServerRequest *request)
{
	const char *css = ".container{width:900px;text-align:left;margin:auto;border-radius:10px 10px 10px 10px;-moz-border-radius:10px 10px 10px 10px;-webkit-border-radius:10px 10px 10px 10px;-khtml-border-radius:10px 10px 10px 10px;-ms-border-radius:10px 10px 10px 10px;box-shadow:3px 3px 3px #707070;background:#fff;border-color: #2194ec;padding: 0px;border-width: 5px;border-style:solid;}body,font{font:12px verdana,arial,sans-serif;color:#fff}.header{background:#2194ec;text-decoration:none;color:#fff;font-family:verdana,arial,sans-serif;text-align:left;padding:5px 0;border-radius:10px 10px 0 0;-moz-border-radius:10px 10px 0 0;-webkit-border-radius:10px 10px 0 0;-khtml-border-radius:10px 10px 0 0;-ms-border-radius:10px 10px 0 0}.content{margin:0 0 0 166px;padding:1px 5px 5px;color:#000;background:#fff;text-align:center;font-size: 8pt;}.contentwide{padding:50px 5px 5px;color:#000;background:#fff;text-align:center}.contentwide h2{color:#000;font:1em verdana,arial,sans-serif;text-align:center;font-weight:700;padding:0;margin:0;font-size: 12pt;}.footer{background:#2194ec;text-decoration:none;color:#fff;font-family:verdana,arial,sans-serif;font-size:9px;text-align:center;padding:10px 0;border-radius:0 0 10px 10px;-moz-border-radius:0 0 10px 10px;-webkit-border-radius:0 0 10px 10px;-khtml-border-radius:0 0 10px 10px;-ms-border-radius:0 0 10px 10px;clear:both}#tail{height:450px;width:805px;overflow-y:scroll;overflow-x:scroll;color:#0f0;background:#000}table{vertical-align:middle;text-align:center;empty-cells:show;padding-left:3;padding-right:3;padding-top:3;padding-bottom:3;border-collapse:collapse;border-color:#0f07f2;border-style:solid;border-spacing:0px;border-width:3px;text-decoration:none;color:#fff;background:#000;font-family:verdana,arial,sans-serif;font-size : 12px;width:100%;white-space:nowrap}table th{cursor: pointer;user-select: none;font-size: 10pt;font-family:lucidia console,Monaco,monospace;text-shadow:1px 1px #0e038c;text-decoration:none;background:#0525f7;border:1px solid silver}table tr:nth-child(even){background:#f7f7f7}table tr:nth-child(odd){background:#eeeeee}table td{color:#000;font-family:lucidia console,Monaco,monospace;text-decoration:none;border:1px solid #010369}body{background:#edf0f5;color:#000}a{text-decoration:none}a:link,a:visited{text-decoration:none;color:#0000e0;font-weight:400}th:last-child a.tooltip:hover span{left:auto;right:0}ul{padding:5px;margin:10px 0;list-style:none;float:left}ul li{float:left;display:inline;margin:0 10px}ul li a{text-decoration:none;float:left;color:#999;cursor:pointer;font:900 14px/22px arial,Helvetica,sans-serif}ul li a span{margin:0 10px 0 -10px;padding:1px 8px 5px 18px;position:relative;float:left}h1{text-shadow:2px 2px #303030;text-align:center}.toggle{position:absolute;margin-left:-9999px;visibility:hidden}.toggle+label{display:block;position:relative;cursor:pointer;outline:none}input.toggle-round-flat+label{padding:1px;width:33px;height:18px;background-color:#ddd;border-radius:10px;transition:background .4s}input.toggle-round-flat+label:before,input.toggle-round-flat+label:after{display:block;position:absolute;}input.toggle-round-flat+label:before{top:1px;left:1px;bottom:1px;right:1px;background-color:#fff;border-radius:10px;transition:background .4s}input.toggle-round-flat+label:after{top:2px;left:2px;bottom:2px;width:16px;background-color:#ddd;border-radius:12px;transition:margin .4s,background .4s}input.toggle-round-flat:checked+label{background-color:#dd4b39}input.toggle-round-flat:checked+label:after{margin-left:14px;background-color:#dd4b39}@-moz-document url-prefix(){select,input{margin:0;padding:0;border-width:1px;font:12px verdana,arial,sans-serif}input[type=button],button,input[type=submit]{padding:0 3px;border-radius:3px 3px 3px 3px;-moz-border-radius:3px 3px 3px 3px}}.nice-select.small,.nice-select-dropdown li.option{height:24px!important;min-height:24px!important;line-height:24px!important}.nice-select.small ul li:nth-of-type(2){clear:both}.nav{margin-bottom:0;padding-left:10;list-style:none}.nav>li{position:relative;display:block}.nav>li>a{position:relative;display:block;padding:5px 10px}.nav>li>a:hover,.nav>li>a:focus{text-decoration:none;background-color:#eee}.nav>li.disabled>a{color:#999}.nav>li.disabled>a:hover,.nav>li.disabled>a:focus{color:#999;text-decoration:none;background-color:initial;cursor:not-allowed}.nav .open>a,.nav .open>a:hover,.nav .open>a:focus{background-color:#eee;border-color:#428bca}.nav .nav-divider{height:1px;margin:9px 0;overflow:hidden;background-color:#e5e5e5}.nav>li>a>img{max-width:none}.nav-tabs{border-bottom:1px solid #ddd}.nav-tabs>li{float:left;margin-bottom:-1px}.nav-tabs>li>a{margin-right:0;line-height:1.42857143;border:1px solid #ddd;border-radius:10px 10px 0 0}.nav-tabs>li>a:hover{border-color:#eee #eee #ddd}.nav-tabs>button{margin-right:0;line-height:1.42857143;border:2px solid #ddd;border-radius:10px 10px 0 0}.nav-tabs>button:hover{background-color:#25bbfc;border-color:#428bca;color:#eaf2f9;border-bottom-color:transparent;}.nav-tabs>button.active,.nav-tabs>button.active:hover,.nav-tabs>button.active:focus{color:#f7fdfd;background-color:#1aae0d;border:1px solid #ddd;border-bottom-color:transparent;cursor:default}.nav-tabs>li.active>a,.nav-tabs>li.active>a:hover,.nav-tabs>li.active>a:focus{color:#428bca;background-color:#e5e5e5;border:1px solid #ddd;border-bottom-color:transparent;cursor:default}.nav-tabs.nav-justified{width:100%;border-bottom:0}.nav-tabs.nav-justified>li{float:none}.nav-tabs.nav-justified>li>a{text-align:center;margin-bottom:5px}.nav-tabs.nav-justified>.dropdown .dropdown-menu{top:auto;left:auto}.nav-status{float:left;margin:0;padding:3px;width:160px;font-weight:400;min-height:600}#bar,#prgbar {background-color: #f1f1f1;border-radius: 14px}#bar {background-color: #3498db;width: 0%;height: 14px}.switch{position:relative;display:inline-block;width:34px;height:16px}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#f55959;-webkit-transition:.4s;transition:.4s}.slider:before{position:absolute;content:\"\";height:12px;width:12px;left:2px;bottom:2px;background-color:#fff;-webkit-transition:.4s;transition:.4s}input:checked+.slider{background-color:#5ca30a}input:focus+.slider{box-shadow:0 0 1px #5ca30a}input:checked+.slider:before{-webkit-transform:translateX(16px);-ms-transform:translateX(16px);transform:translateX(16px)}.slider.round{border-radius:34px}.slider.round:before{border-radius:50%}.button{border:1px solid #06c;background-color:#09c;color:#fff;padding:5px 10px;border-radius: 3px}.button:hover{border:1px solid #09c;background-color:#0ac;color:#fff}.button:disabled,button[disabled]{border:1px solid #999;background-color:#ccc;color:#666}.arrow {margin-left: 5px;font-size: 12px;}\n";
	request->send_P(200, "text/css", css);
}

void handle_jquery(AsyncWebServerRequest *request)
{
#if defined(CONFIG_IDF_TARGET_ESP32)
	adcEn = -1;
	delay(100);
#endif
	AsyncWebServerResponse *response = request->beginResponse_P(200, "application/javascript", (const uint8_t *)jquery_3_7_1_min_js_gz, jquery_3_7_1_min_js_gz_len);
	response->addHeader("Content-Encoding", "gzip");
	response->addHeader("Cache-Control", "no-cache");
	response->setContentLength(jquery_3_7_1_min_js_gz_len);
	request->send(response);
#if defined(CONFIG_IDF_TARGET_ESP32)
	delay(200);
	adcEn = 1;
#endif
}

// Copy src into dest escaping characters that would break JSON string syntax
// (", \) and stripping raw control bytes, so text taken from a TNC2 packet
// (callsign, path, object/item name) can be embedded safely inside a JSON string.
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
	char *html = allocateStringMemory(16384); // Initial buffer size, adjust as needed
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

String event_chatMessage(bool gethtml)
{
	// log_d("Event count: %d",lastheard_events.count());
	// if (message_events.count() == 0)
	//	return "NO";

	struct tm tmstruct, tmNow;

	// Using dynamic memory allocation instead of String
	char *html = allocateStringMemory(4096); // Initial buffer size, adjust as needed
	if (!html)
	{
		return String(""); // Memory allocation failed
	}

	time_t timen = time(NULL);
	localtime_r(&timen, &tmNow);

	strcpy(html, "<tr>\n");
	strcat(html, "<th style=\"width:60pt\"><span><b>Time (");
	if (config.timeZone >= 0)
		strcat(html, "+");

	// Convert timezone to string
	char temp_buffer[64];
	if (config.timeZone == (int)config.timeZone)
	{
		snprintf(temp_buffer, sizeof(temp_buffer), "%d", (int)config.timeZone);
		strcat(html, temp_buffer);
		strcat(html, ")</b></span></th>\n");
	}
	else
	{
		snprintf(temp_buffer, sizeof(temp_buffer), "%.1f", config.timeZone);
		strcat(html, temp_buffer);
		strcat(html, ")</b></span></th>\n");
	}
	// strcat(html, "<th style=\"min-width:16px\">ICON</th>\n");

	strcat(html, "<th style=\"width:70pt\">Callsign</th>\n");
	strcat(html, "<th>Message</th>\n");
	strcat(html, "<th style=\"width:10pt\">ACK</th>\n");
	strcat(html, "<th style=\"width:20pt\">msgID</th>\n");
	strcat(html, "</tr>\n");

	pkgMsgSort(msgQueue);
	for (int i = 0; i < PKGLISTSIZE; i++)
	{
		if (i >= PKGLISTSIZE)
			break;
		msgType pkg = getMsgList(i);
		if (pkg.time > 0)
		{
			// String line = String(pkg.text); // Not needed anymore

			pkg.callsign[10] = 0;
			// time_t tm = pkg.time;
			localtime_r(&pkg.time, &tmstruct);
			char strTime[10];
			// sprintf(strTime, "%02d:%02d:%02d", tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
			if (tmNow.tm_mday == tmstruct.tm_mday)
				sprintf(strTime, "%02d:%02d:%02d", tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
			else
				sprintf(strTime, "%dd %02d:%02d", tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min);
			// String str = String(tmstruct.tm_hour, DEC) + ":" + String(tmstruct.tm_min, DEC) + ":" + String(tmstruct.tm_sec, DEC);

			if (pkg.ack > 0)
			{
				strcat(html, "<tr style=\"background-color: #f1697dff;\">\n");
			}
			else if (pkg.ack == -1)
			{
				strcat(html, "<tr style=\"background-color: #7ff1c5ff;\">\n");
			}
			else if (pkg.ack == -2)
			{
				strcat(html, "<tr style=\"background-color: #73caf0ff;\">\n");
			}
			else
			{
				strcat(html, "<tr style=\"background-color: #f55353ff;\">\n");
			}

			strcat(html, "<td>");
			strcat(html, strTime);
			strcat(html, "</td>");

			strcat(html, "<td>");
			strcat(html, pkg.callsign);
			strcat(html, "</td>");

			strcat(html, "<td style=\"text-align: left;\">");
			strcat(html, pkg.text);
			strcat(html, "</td>");

			if (pkg.ack > 0)
			{
				snprintf(temp_buffer, sizeof(temp_buffer), "<td>%d/%d</td>", pkg.ack, config.msg_retry);
				strcat(html, temp_buffer);
			}
			else if (pkg.ack == -1)
			{
				strcat(html, "<td>RX</td>");
			}
			else if (pkg.ack == -2)
			{
				strcat(html, "<td>TX</td>");
			}
			else
			{
				strcat(html, "<td>TF</td>");
			}

			snprintf(temp_buffer, sizeof(temp_buffer), "<td>%d</td></tr>\n", pkg.msgID);
			strcat(html, temp_buffer);
		}
	}

	size_t html_len = strlen(html);
	log_d("HTML Length=%d Byte gethtml:%d event_cnt:%d", html_len, gethtml, message_events.count());

	if (gethtml)
	{
		String result = String(html); // Convert back to String for return
		free(html);					  // Free the allocated memory
		return result;
	}

	if (message_events.count() > 0)
	{
		char *info = (char *)calloc(html_len + 1, sizeof(char)); // +1 for null terminator
		if (info)
		{
			strcpy(info, html);
			message_events.send(info, "chatMsg", time(NULL), 5000);
			free(info);
		}
	}

	free(html); // Free the allocated memory
	return String("");
}

void handle_vpn(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	StandByTick = millis() + (config.pwr_stanby_delay * 1000);

	if (request->hasArg("commitVPN"))
	{
		bool vpnEn = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "vpnEnable")
			{
				if (request->arg(i) != "")
				{
					// if (isValidNumber(request->arg(i)))
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						vpnEn = true;
				}
			}

			// if (request->argName(i) == "taretime") {
			//	if (request->arg(i) != "")
			//	{
			//		//if (isValidNumber(request->arg(i)))
			//		if (strcmp(request->arg(i).c_str(), "OK") == 0)
			//			taretime = true;
			//	}
			// }
			if (request->argName(i) == "wg_port")
			{
				if (request->arg(i) != "")
				{
					config.wg_port = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "wg_public_key")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wg_public_key, request->arg(i).c_str());
					config.wg_public_key[44] = 0;
				}
			}

			if (request->argName(i) == "wg_private_key")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wg_private_key, request->arg(i).c_str());
					config.wg_private_key[44] = 0;
				}
			}

			if (request->argName(i) == "wg_peer_address")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wg_peer_address, request->arg(i).c_str());
				}
			}

			if (request->argName(i) == "wg_local_address")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wg_local_address, request->arg(i).c_str());
				}
			}

			if (request->argName(i) == "wg_netmask_address")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wg_netmask_address, request->arg(i).c_str());
				}
			}

			if (request->argName(i) == "wg_gw_address")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wg_gw_address, request->arg(i).c_str());
				}
			}
		}

		config.vpn = vpnEn;
		saveConfig(request);
	}
	else
	{
		// Using dynamic memory allocation instead of String
		char *html = allocateStringMemory(8192); // Initial buffer size, adjust as needed
		if (!html)
		{
			return; // Memory allocation failed
		}

		strcpy(html, "<script type=\"text/javascript\">\n");
		strcat(html, "$('form').submit(function (e) {\n");
		strcat(html, "e.preventDefault();\n");
		strcat(html, "var data = new FormData(e.currentTarget);\n");
		strcat(html, "if(e.currentTarget.id===\"formVPN\") document.getElementById(\"submitVPN\").disabled=true;\n");
		strcat(html, "$.ajax({\n");
		strcat(html, "url: '/vpn',\n");
		strcat(html, "type: 'POST',\n");
		strcat(html, "data: data,\n");
		strcat(html, "contentType: false,\n");
		strcat(html, "processData: false,\n");
		strcat(html, "success: function (data) {\n");
		strcat(html, "alert(\"Submited Successfully\");\n");
		strcat(html, "},\n");
		strcat(html, "error: function (data) {\n");
		strcat(html, "alert(\"An error occurred.\");\n");
		strcat(html, "}\n");
		strcat(html, "});\n");
		strcat(html, "});\n");

		char temp_buffer[512];
		strcat(html, "</script>\n");
		// ===== JavaScript AJAX =====
		// strcat(html, "<script>\n");
		// strcat(html, "function loadVPNConfig() {\n");
		// strcat(html, "  $.ajax({\n");
		// strcat(html, "    url: '/api/vpnreq',\n");
		// strcat(html, "    method: 'GET',\n");
		// strcat(html, "    dataType: 'json',\n");
		// strcat(html, "    success: function(data) {\n");
		// strcat(html, "       console.log(data);\n");
		// strcat(html, "       let ep = data.Enpoint.split(':');\n");
		// strcat(html, "       $('#wg_peer_address').val(ep[0]);\n");
		// strcat(html, "       $('#wg_port').val(ep[1]);\n");
		// strcat(html, "       $('#wg_local_address').val(data.Address);\n");
		// strcat(html, "       $('#wg_public_key').val(data.PublicKey);\n");
		// strcat(html, "       $('#wg_private_key').val(data.PrivateKey);\n");
		// strcat(html, "    },\n");
		// strcat(html, "    error: function(e) {\n");
		// strcat(html, "       alert('โหลดข้อมูล VPN ไม่สำเร็จ');\n");
		// strcat(html, "       console.log(e);\n");
		// strcat(html, "    }\n");
		// strcat(html, "  });\n");
		// strcat(html, "}\n");
		// strcat(html, "</script>");

		// strcat(html, "<h2>System Setting</h2>\n");
		strcat(html, "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromVPN\" method=\"post\">\n");
		strcat(html, "<table>\n");
		strcat(html, "<th colspan=\"2\"><span><b>Wireguard Configuration</b></span></th>\n");
		strcat(html, "<tr>");

		// Handle sync flag
		if (config.vpn)
		{
			strcat(html, "<td align=\"right\"><b>Enable</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" id=\"wg_enable\" name=\"vpnEnable\" value=\"OK\" checked><span class=\"slider round\"></span></label></td>\n");
		}
		else
		{
			strcat(html, "<td align=\"right\"><b>Enable</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" id=\"wg_enable\" name=\"vpnEnable\" value=\"OK\" ><span class=\"slider round\"></span></label></td>\n");
		}
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Server Address</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input  size=\"20\" maxlength=\"32\" id=\"wg_peer_address\" name=\"wg_peer_address\" type=\"text\" value=\"%s\" /></td>\n", config.wg_peer_address);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Server Port</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input id=\"wg_port\" size=\"5\" name=\"wg_port\" type=\"number\" value=\"%d\" /></td>\n", config.wg_port);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Local Address</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input id=\"wg_local_address\" name=\"wg_local_address\" type=\"text\" value=\"%s\" /></td>\n", config.wg_local_address);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Netmask</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input id=\"wg_netmask_address\" name=\"wg_netmask_address\" type=\"text\" value=\"%s\" /></td>\n", config.wg_netmask_address);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Gateway</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input id=\"wg_gw_address\" name=\"wg_gw_address\" type=\"text\" value=\"%s\" /></td>\n", config.wg_gw_address);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Public Server Key</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input size=\"50\" maxlength=\"44\" id=\"wg_public_key\" name=\"wg_public_key\" type=\"text\" value=\"%s\" /></td>\n", config.wg_public_key);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Private Client Key</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input size=\"50\" maxlength=\"44\" id=\"wg_private_key\" name=\"wg_private_key\" type=\"text\" value=\"%s\" /></td>\n", config.wg_private_key);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr><td colspan=\"2\" align=\"right\">\n");
		strcat(html, "<div><button class=\"button\" type='submit' id='submitVPN'  name=\"commitVPN\"> Apply Change </button></div>\n");
		strcat(html, "<input type=\"hidden\" name=\"commitVPN\"/>\n");
		strcat(html, "</td></tr></table><br />\n");
		strcat(html, "</form><br /><br />");


		// request->send(200, "text/html", html); // send to someones browser when asked
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const char *)html);
		response->addHeader("VPN", "content");
		response->addHeader("Cache-Control", "no-cache");
		request->send(response);
		free(html); // Free the allocated memory
	}
}

#ifdef MQTT
void handle_mqtt(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	StandByTick = millis() + (config.pwr_stanby_delay * 1000);

	if (request->hasArg("commitMQTT"))
	{
		bool mqttEn = false;
		config.mqtt_topic_flag = 0;
		config.mqtt_subscribe_flag = 0;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			if (request->argName(i) == "enable")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						mqttEn = true;
				}
			}

			if (request->argName(i) == "host")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.mqtt_host, request->arg(i).c_str());
				}
			}
			if (request->argName(i) == "port")
			{
				if (request->arg(i) != "")
				{
					config.mqtt_port = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "user")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.mqtt_user, request->arg(i).c_str());
				}
			}
			if (request->argName(i) == "pass")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.mqtt_pass, request->arg(i).c_str());
				}
			}
			if (request->argName(i) == "topic")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.mqtt_topic, request->arg(i).c_str());
				}
			}
			if (request->argName(i) == "subscribe")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.mqtt_subscribe, request->arg(i).c_str());
				}
			}

			if (request->argName(i) == "TopicTNC")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_TOPIC_TNC;
				}
			}
			if (request->argName(i) == "TopicSts")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_TOPIC_STATUS;
				}
			}
			if (request->argName(i) == "TopicTlm")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_TOPIC_TELEMETRY;
				}
			}
			if (request->argName(i) == "TopicWX")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_TOPIC_WX;
				}
			}
			if (request->argName(i) == "TopicSensor")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_TOPIC_SENSOR;
				}
			}

			if (request->argName(i) == "subCMD")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_SUBSCRIBE_CMD;
				}
			}
			if (request->argName(i) == "subTNC")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_SUBSCRIBE_TNC;
				}
			}
			if (request->argName(i) == "subMsg")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						config.mqtt_topic_flag |= MQTT_SUBSCRIBE_MESSAGE;
				}
			}
		}

		config.en_mqtt = mqttEn;
		clientMQTT.disconnect();
		// Using dynamic memory allocation instead of String
		char *html = allocateStringMemory(256); // Buffer for response message
		if (html)
		{
			if (saveConfiguration("/default.cfg", config))
			{
				strcpy(html, "Setup completed successfully");
				request->send(200, "text/html", html); // send to someones browser when asked
			}
			else
			{
				strcpy(html, "Save config failed.");
				request->send(501, "text/html", html); // Not Implemented
			}
			free(html); // Free the allocated memory
		}
	}
	else
	{
		// Using dynamic memory allocation instead of String
		char *html = allocateStringMemory(8192); // Initial buffer size, adjust as needed
		if (!html)
		{
			return; // Memory allocation failed
		}

		strcpy(html, "<script type=\"text/javascript\">\n");
		strcat(html, "$('form').submit(function (e) {\n");
		strcat(html, "e.preventDefault();\n");
		strcat(html, "var data = new FormData(e.currentTarget);\n");
		strcat(html, "if(e.currentTarget.id===\"formVPN\") document.getElementById(\"submitMQTT\").disabled=true;\n");
		strcat(html, "$.ajax({\n");
		strcat(html, "url: '/mqtt',\n");
		strcat(html, "type: 'POST',\n");
		strcat(html, "data: data,\n");
		strcat(html, "contentType: false,\n");
		strcat(html, "processData: false,\n");
		strcat(html, "success: function (data) {\n");
		strcat(html, "alert(\"Submited Successfully\");\n");
		strcat(html, "},\n");
		strcat(html, "error: function (data) {\n");
		strcat(html, "alert(\"An error occurred.\");\n");
		strcat(html, "}\n");
		strcat(html, "});\n");
		strcat(html, "});\n");
		strcat(html, "</script>\n");

		// strcat(html, "<h2>System Setting</h2>\n");
		strcat(html, "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromMQTT\" method=\"post\">\n");
		strcat(html, "<table>\n");
		strcat(html, "<th colspan=\"2\"><span><b>MQTT Configuration</b></span></th>\n");
		strcat(html, "<tr>");

		// Handle sync flag
		if (config.en_mqtt)
		{
			strcat(html, "<td align=\"right\"><b>Enable</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"enable\" value=\"OK\" checked><span class=\"slider round\"></span></label></td>\n");
		}
		else
		{
			strcat(html, "<td align=\"right\"><b>Enable</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"enable\" value=\"OK\" ><span class=\"slider round\"></span></label></td>\n");
		}
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Server Address:</b></td>\n");
		char temp_buffer[512];
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input  size=\"30\" maxlength=\"32\" name=\"host\" type=\"text\" value=\"%s\" /></td>\n", config.mqtt_host);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Server Port:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input size=\"5\"  maxlength=\"5\"  name=\"port\" type=\"number\" value=\"%d\" /></td>\n", config.mqtt_port);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>User:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input maxlength=\"32\" name=\"user\" type=\"text\" value=\"%s\" /></td>\n", config.mqtt_user);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Password:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input size=\"40\" maxlength=\"63\" name=\"pass\" type=\"password\" value=\"%s\" /></td>\n", config.mqtt_pass);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Topic:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input size=\"50\" maxlength=\"32\" id=\"topic\" name=\"topic\" type=\"text\" value=\"%s\" /></td>\n", config.mqtt_topic);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Topic Flag:</b></td>\n");

		strcat(html, "<td align=\"center\">\n");
		strcat(html, "<fieldset id=\"TopicGrp\">\n");
		strcat(html, "<legend>Topic Flags Send out MQTT</legend>\n<table style=\"text-align:unset;border-width:0px;background:unset\">");
		strcat(html, "<tr style=\"background:unset;\">");

		// Handle topic flags
		if (config.mqtt_topic_flag & MQTT_TOPIC_TNC)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicTNC\" type=\"checkbox\" value=\"OK\" checked/>TNC</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicTNC\" type=\"checkbox\" value=\"OK\" />TNC</td>\n");
		}

		if (config.mqtt_topic_flag & MQTT_TOPIC_STATUS)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicSts\" type=\"checkbox\" value=\"OK\" checked/>Status</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicSts\" type=\"checkbox\" value=\"OK\" />Status</td>\n");
		}

		if (config.mqtt_topic_flag & MQTT_TOPIC_TELEMETRY)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicTlm\" type=\"checkbox\" value=\"OK\" checked/>Telemetry</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicTlm\" type=\"checkbox\" value=\"OK\" />Telemetry</td>\n");
		}

		if (config.mqtt_topic_flag & MQTT_TOPIC_WX)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicWX\" type=\"checkbox\" value=\"OK\" checked/>Weather</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicWX\" type=\"checkbox\" value=\"OK\" />Weather</td>\n");
		}

		if (config.mqtt_topic_flag & MQTT_TOPIC_SENSOR)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicSensor\" type=\"checkbox\" value=\"OK\" checked/>Sensor</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"TopicSensor\" type=\"checkbox\" value=\"OK\" />Sensor</td>\n");
		}

		strcat(html, "<td style=\"border:unset;\"></td>");
		strcat(html, "</tr></table></fieldset>\n");
		strcat(html, "</td></tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Subscription:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input size=\"50\" maxlength=\"32\" name=\"subscribe\" type=\"text\" value=\"%s\" /></td>\n", config.mqtt_subscribe);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Subscription Flag:</b></td>\n");

		strcat(html, "<td align=\"center\">\n");
		strcat(html, "<fieldset id=\"SubGrp\">\n");
		strcat(html, "<legend>Subscription Flags Receive</legend>\n<table style=\"text-align:unset;border-width:0px;background:unset\">");
		strcat(html, "<tr style=\"background:unset;\">");

		// Handle subscription flags
		if (config.mqtt_subscribe_flag & MQTT_SUBSCRIBE_CMD)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"subCMD\" type=\"checkbox\" value=\"OK\" checked/>AT-Command</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"subCMD\" type=\"checkbox\" value=\"OK\" />AT-Command</td>\n");
		}

		if (config.mqtt_subscribe_flag & MQTT_SUBSCRIBE_TNC)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"subTNC\" type=\"checkbox\" value=\"OK\" checked/>TNC</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"subTNC\" type=\"checkbox\" value=\"OK\" />TNC</td>\n");
		}

		if (config.mqtt_subscribe_flag & MQTT_SUBSCRIBE_MESSAGE)
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"subMsg\" type=\"checkbox\" value=\"OK\" checked/>Message</td>\n");
		}
		else
		{
			strcat(html, "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"subMsg\" type=\"checkbox\" value=\"OK\" />Message</td>\n");
		}

		strcat(html, "<td style=\"border:unset;\"></td>");
		strcat(html, "</tr></table></fieldset>\n");
		strcat(html, "</td></tr>\n");

		strcat(html, "</table><br />\n");
		strcat(html, "<td><input class=\"button\" id=\"submitMQTT\" name=\"commitMQTT\" type=\"submit\" value=\"Save Config\" maxlength=\"80\"/></td>\n");
		strcat(html, "<input type=\"hidden\" name=\"commitMQTT\"/>\n");
		strcat(html, "</form>\n");

		// request->send(200, "text/html", html); // send to someones browser when asked
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const char *)html);
		response->addHeader("MQTT", "content");
		response->addHeader("Cache-Control", "no-cache");
		request->send(response);
		free(html); // Free the allocated memory
	}
}
#endif

void handle_msg(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	StandByTick = millis() + (config.pwr_stanby_delay * 1000);

	if (request->hasArg("commitChat"))
	{
		// Using char arrays instead of String
		char toCall[10];
		char msg[256];
		memset(toCall, 0, sizeof(toCall));
		memset(msg, 0, sizeof(msg));

		for (uint8_t i = 0; i < request->args(); i++)
		{
			if (request->argName(i) == "toCall")
			{
				if (request->arg(i) != "")
				{
					strncpy(toCall, request->arg(i).c_str(), sizeof(toCall) - 1);
				}
			}
			if (request->argName(i) == "msg")
			{
				if (request->arg(i) != "")
				{
					strncpy(msg, request->arg(i).c_str(), sizeof(msg) - 1);
				}
			}
		}
		log_d("Chat to %s | msg %s", toCall, msg);
		sendAPRSMessage(String(toCall), String(msg), config.msg_encrypt);
		// Using dynamic memory allocation instead of String
		char *html = allocateStringMemory(64); // Small buffer for "Send completed"
		if (html)
		{
			strcpy(html, "Send completed");
			request->send(200, "text/html", html); // send to someones browser when asked
			free(html);							   // Free the allocated memory
		}
	}
	else if (request->hasArg("commitMSG"))
	{
		bool msgEn = false;
		bool msgRf = false;
		bool msgInet = false;
		bool msgEncrypt = false;

		for (uint8_t i = 0; i < request->args(); i++)
		{
			if (request->argName(i) == "enable")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						msgEn = true;
				}
			}
			if (request->argName(i) == "msgRf")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						msgRf = true;
				}
			}
			if (request->argName(i) == "msgInet")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						msgInet = true;
				}
			}
			if (request->argName(i) == "encrypt")
			{
				if (request->arg(i) != "")
				{
					if (strcmp(request->arg(i).c_str(), "OK") == 0)
						msgEncrypt = true;
				}
			}

			if (request->argName(i) == "mycall")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.msg_mycall, request->arg(i).c_str());
					config.msg_mycall[9] = 0;
				}
			}

			if (request->argName(i) == "key")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.msg_key, request->arg(i).c_str());
					config.msg_key[32] = 0;
				}
			}

			if (request->argName(i) == "retry")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.msg_retry = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "path")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.msg_path = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "timeout")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.msg_interval = request->arg(i).toInt();
				}
			}
		}

		config.msg_enable = msgEn;
		config.msg_rf = msgRf;
		config.msg_inet = msgInet;
		config.msg_encrypt = msgEncrypt;

		// Using dynamic memory allocation instead of String
		char *html = allocateStringMemory(256); // Buffer for response message
		if (html)
		{
			if (saveConfiguration("/default.cfg", config))
			{
				strcpy(html, "Setup completed successfully");
				request->send(200, "text/html", html); // send to someones browser when asked
			}
			else
			{
				strcpy(html, "Save config failed.");
				request->send(501, "text/html", html); // Not Implemented
			}
			free(html); // Free the allocated memory
		}
	}
	else
	{
		// Using dynamic memory allocation instead of String
		char *html = allocateStringMemory(8192); // Initial buffer size, adjust as needed
		if (!html)
		{
			return; // Memory allocation failed
		}

		strcpy(html, "<script type=\"text/javascript\">\n");
		strcat(html, "$('form').submit(function (e) {\n");
		strcat(html, "e.preventDefault();\n");
		strcat(html, "var data = new FormData(e.currentTarget);\n");
		strcat(html, "if(e.currentTarget.id===\"formMSG\") document.getElementById(\"submitMSG\").disabled=true;\n");
		// strcat(html, "if(e.currentTarget.id===\"formChat\") document.getElementById(\"submitI2C0\").disabled=true;\n");
		strcat(html, "$.ajax({\n");
		strcat(html, "url: '/msg',\n");
		strcat(html, "type: 'POST',\n");
		strcat(html, "data: data,\n");
		strcat(html, "contentType: false,\n");
		strcat(html, "processData: false,\n");
		strcat(html, "success: function (data) {\n");
		strcat(html, "if(e.currentTarget.id===\"formMSG\") alert(\"Submited Successfully\");\n");
		strcat(html, "},\n");
		strcat(html, "error: function (data) {\n");
		strcat(html, "if(e.currentTarget.id===\"formMSG\") alert(\"An error occurred.\");\n");
		strcat(html, "}\n");
		strcat(html, "});\n");
		strcat(html, "});\n");

		// strcat(html, "if (!!window.EventSource) {";
		// strcat(html, "var source = new EventSource('/eventMsg');");

		// strcat(html, "source.addEventListener('open', function(e) {";
		// strcat(html, "console.log(\"Events MSG Connected\");";
		// strcat(html, "}, false);";
		// strcat(html, "source.addEventListener('error', function(e) {";
		// strcat(html, "if (e.target.readyState != EventSource.OPEN) {";
		// strcat(html, "console.log(\"Events MSG Disconnected\");";
		// strcat(html, "}\n}, false);";
		// strcat(html, "source.addEventListener('chatMsg', function(e) {";
		// // strcat(html, "console.log(\"lastHeard\", e.data);";
		// strcat(html, "var lh=document.getElementById(\"chatMsg\");";
		// strcat(html, "if(lh != null) {lh.innerHTML = e.data;}";
		// strcat(html, "}, false);\n}";
		strcat(html, "</script>\n");

		// strcat(html, "<h2>System Setting</h2>\n");
		strcat(html, "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"formMSG\" method=\"post\">\n");
		strcat(html, "<table width=\"90%\">\n");
		strcat(html, "<th colspan=\"2\"><span><b>Message Configuration</b></span></th>\n");
		strcat(html, "<tr>");

		// Handle sync flag
		if (config.msg_enable)
		{
			strcat(html, "<td align=\"right\"><b>Enable</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"enable\" value=\"OK\" checked><span class=\"slider round\"></span></label></td>\n");
		}
		else
		{
			strcat(html, "<td align=\"right\"><b>Enable</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"enable\" value=\"OK\" ><span class=\"slider round\"></span></label></td>\n");
		}
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>My Callsign:</b></td>\n");
		char temp_buffer[512];
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input  size=\"20\" maxlength=\"9\" name=\"mycall\" type=\"text\" value=\"%s\" /> *<i>Callsign with SSID (Ex. LU1ABC-12)</i></td>\n", config.msg_mycall);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		// Handle RF and Internet flags
		if (config.msg_rf && config.msg_inet)
		{
			strcat(html, "<tr><td style=\"text-align: right;\"><b>TX Channel:</b></td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"msgRf\" value=\"OK\" checked/>RF <input type=\"checkbox\" name=\"msgInet\" value=\"OK\" checked/>Internet </td></tr>\n");
		}
		else if (config.msg_rf)
		{
			strcat(html, "<tr><td style=\"text-align: right;\"><b>TX Channel:</b></td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"msgRf\" value=\"OK\" checked/>RF <input type=\"checkbox\" name=\"msgInet\" value=\"OK\" />Internet </td></tr>\n");
		}
		else if (config.msg_inet)
		{
			strcat(html, "<tr><td style=\"text-align: right;\"><b>TX Channel:</b></td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"msgRf\" value=\"OK\" />RF <input type=\"checkbox\" name=\"msgInet\" value=\"OK\" checked/>Internet </td></tr>\n");
		}
		else
		{
			strcat(html, "<tr><td style=\"text-align: right;\"><b>TX Channel:</b></td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"msgRf\" value=\"OK\" />RF <input type=\"checkbox\" name=\"msgInet\" value=\"OK\" />Internet </td></tr>\n");
		}

		strcat(html, "<tr>");
		if (config.msg_encrypt)
		{
			strcat(html, "<td align=\"right\"><b>Encryption</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"encrypt\" value=\"OK\" checked><span class=\"slider round\"></span></label></td>\n");
		}
		else
		{
			strcat(html, "<td align=\"right\"><b>Encryption</b></td>\n");
			strcat(html, "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"encrypt\" value=\"OK\" ><span class=\"slider round\"></span></label></td>\n");
		}
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>AES Key:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input  size=\"40\" maxlength=\"33\" name=\"key\" type=\"text\" value=\"%s\" /> *<i>ASCII HEX 16Byte</i></td>\n", config.msg_key);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Send Retry:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input  min=\"0\" max=\"99\"   name=\"retry\" type=\"number\" value=\"%d\" /></td>\n", config.msg_retry);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>Send Timeout:</b></td>\n");
		snprintf(temp_buffer, sizeof(temp_buffer), "<td style=\"text-align: left;\"><input  min=\"1\" max=\"9999\"   name=\"timeout\" type=\"number\" value=\"%d\" /> Sec.</td>\n", config.msg_interval);
		strcat(html, temp_buffer);
		strcat(html, "</tr>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"right\"><b>PATH:</b></td>\n");
		strcat(html, "<td style=\"text-align: left;\">\n");
		strcat(html, "<select name=\"path\" id=\"path\">\n");
		for (uint8_t pthIdx = 0; pthIdx < PATH_LEN; pthIdx++)
		{
			snprintf(temp_buffer, sizeof(temp_buffer), "<option value=\"%d\" ", pthIdx);
			strcat(html, temp_buffer);
			if (config.msg_path == pthIdx)
			{
				strcat(html, "selected>");
			}
			else
			{
				strcat(html, ">");
			}
			snprintf(temp_buffer, sizeof(temp_buffer), "%s</option>\n", pathLabel(pthIdx));
			strcat(html, temp_buffer);
		}
		strcat(html, "</select></td>\n");

		strcat(html, "<tr><td colspan=\"2\" align=\"right\">\n");
		strcat(html, "<div><button class=\"button\" type='submit' id='submitMSG'  name=\"commitMSG\"> Apply Change </button></div>\n");
		strcat(html, "<input type=\"hidden\" name=\"commitMSG\"/>\n");
		strcat(html, "</td></tr></table><br />\n");
		strcat(html, "</form><br /><br />");

		strcat(html, "<table width=\"90%\">\n");
		strcat(html, "<th style=\"background-color: #070ac2;\">CHAT MESSAGE</th>\n");

		strcat(html, "<tr><td>\n");
		strcat(html, "<table id=\"chatMsg\">\n");
		strcat(html, event_chatMessage(true).c_str());
		strcat(html, "</table>\n");

		strcat(html, "</td></tr><tr><td colspan=\"5\">");

		strcat(html, "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"formChat\" method=\"post\">\n");
		strcat(html, "<table>\n");

		strcat(html, "<tr>\n");
		strcat(html, "<td align=\"left\"><b>TO:</b><input size=\"10\" name=\"toCall\" id=\"toCall\" type=\"text\" value=\"\" oninput=\"this.value=this.value.toUpperCase();\" /> <b>MSG:</b><input size=\"80\" name=\"msg\" id=\"msg\" type=\"text\" value=\"\" /></td>\n");
		strcat(html, "<td align=\"right\">\n");
		strcat(html, "<input class=\"button\" id=\"submitChat\" name=\"commitChat\" type=\"submit\" value=\"Send\"/>\n");
		strcat(html, "<input type=\"hidden\" name=\"commitChat\"/>\n");
		strcat(html, "</td></tr></table>\n");
		strcat(html, "</form><br />\n");

		strcat(html, "</td></tr></table>");

		// request->send(200, "text/html", html); // send to someones browser when asked
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const char *)html);
		response->addHeader("MSG", "content");
		response->addHeader("Cache-Control", "no-cache");
		request->send(response);
		free(html); // Free the allocated memory
	}
}

// void handle_ws(String Raw,uint16_t mVrms)
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

void handle_default()
{
	defaultSetting = true;
	defaultConfig();
	defaultSetting = false;
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
	async_server.on("/classic", HTTP_GET, [](AsyncWebServerRequest *request)
					{ setMainPage(request); });
	webApiRegister(async_server); // new UI on "/", old one stays on /classic until migrated
	// async_server.on("/symbol2", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
	// 				{ handle_symbol2(request); });
	async_server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_logout(request); });
	async_server.on("/vpn", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_vpn(request); });
#ifdef MQTT
	async_server.on("/mqtt", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_mqtt(request); });
#endif
	async_server.on("/msg", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_msg(request); });
	async_server.on("/default", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_default(); });
	// async_server.on("/realtime", HTTP_GET, [](AsyncWebServerRequest *request)
	// 				{ handle_realtime(request); });
	// async_server.on("/lastHeard", HTTP_GET, [](AsyncWebServerRequest *request)
	// 				{ handle_lastHeard(request); });
	async_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_css(request); });
	async_server.on("/jquery-3.7.1.js", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_jquery(request); });

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
