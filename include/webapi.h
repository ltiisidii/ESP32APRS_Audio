#pragma once
#include <ESPAsyncWebServer.h>

// Registers the embedded web app (web/) and the JSON API (/api/*) on the server
void webApiRegister(AsyncWebServer &server);
