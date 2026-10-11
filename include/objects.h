/*
 APRS objects of this station (Objects page): up to OBJ_COUNT fixed objects such as repeaters,
 events or points of interest, sent to RF and/or APRS-IS (APRS 1.01, chapter 11).
*/
#ifndef OBJECTS_H
#define OBJECTS_H

#include <ArduinoJson.h>
#include "config.h"

// Call from taskAPRS on every loop: sends what is due (start-up decay, interval, kills, expiry)
void objectsTick(void);
// Web API: the user turned objects on/off or edited them (before = settings before the change)
void objectsConfigChanged(const Configuration &before, Configuration &now);
// Web API: runtime state of every object
void objectsStatus(JsonArray out);
// True once when objects.cpp changed the settings itself (an expired object turned off); the caller saves them
bool objectsTakeSaveRequest(void);
// The info field of object i (";NAME_____*DDHHMMz..."), for tests and the web preview; false if it can't be built
bool objectsInfo(int i, bool killed, char *out, size_t len);

#endif
