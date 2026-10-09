#ifndef MILLIS64_H_
#define MILLIS64_H_

#include <stdint.h>
#include "esp_timer.h"

/**
 * @brief Milliseconds since boot as 64-bit, never wraps (millis() wraps after 49.7 days)
 * @note Use for deadlines stored as "millis64() + interval" and compared with ">" / "<".
 *       Its low 32 bits are equal to millis().
 */
static inline uint64_t millis64(void)
{
	return (uint64_t)esp_timer_get_time() / 1000ULL;
}

#endif /* MILLIS64_H_ */
