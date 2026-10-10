#pragma once
// Live data for the web pages (TNC2 monitor, NMEA) kept in small ring buffers.
//
// Other tasks (APRS, GPS) only push into these buffers; the web server reads them when a page asks
// (GET /api/monitor, /api/gnss). Nothing outside the web server task touches the server's connections:
// the async web server is not safe to drive from other tasks, and doing so (SSE/WebSocket pushes from the
// main loop and the APRS/GPS tasks) could leave its task waiting until the watchdog restarted the station.
//
// The buffers are only allocated while a page is polling, and freed a minute after it stops.
#include <Arduino.h>
#include <ArduinoJson.h>

// Producers (any task). Cheap no-ops while nobody is watching.
void webFeedMonitor(const char *tnc2, size_t len, uint16_t mVrms);
void webFeedNmea(const char *line, size_t len);

// Consumers (web server task): add entries with a sequence number above `after` to `out`.
// Return the newest sequence number.
uint32_t webFeedMonitorRead(uint32_t after, JsonArray out);
uint32_t webFeedNmeaRead(uint32_t after, JsonArray out);
