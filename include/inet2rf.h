#ifndef INET2RF_H
#define INET2RF_H

#include <Arduino.h>
#include <ArduinoJson.h>

// A station heard on RF (source callsign of a received frame); taskAPRS calls it
void inet2rfHeardRf(const char *call);
// One line received from APRS-IS (taskNetwork). Notes stations connected straight to APRS-IS and, when
// "Internet -> RF" or its log-only test is on, decides whether the packet goes to RF. Returns true and fills
// tnc2 (the third-party frame to transmit) only when it must really be sent.
bool inet2rfLine(const char *line, uint16_t type, char *tnc2, size_t tnc2Size);
// Last decisions, oldest first (web)
void inet2rfLogRead(JsonArray out);

#endif
