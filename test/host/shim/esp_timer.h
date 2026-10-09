#pragma once
#include <cstdint>
extern int64_t hostTimeUs;
inline int64_t esp_timer_get_time(void) { return hostTimeUs; }
