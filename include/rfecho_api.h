// Own RF echo (see rfecho.h): what the firmware calls and what the web shows.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "AX25.h"

// A packet of this station goes out on RF (TNC2 text "SRC>DEST,PATH:info"); pkgTxSend calls it
void rfEchoOwnTx(const char *tnc2);
// A frame heard on RF; mV = its audio level. taskAPRS calls it for every decoded frame
void rfEchoRx(const AX25Msg *m, uint16_t mV);
// Counters and the last echoes, for /api/info
void rfEchoInfo(JsonObject out);
