// Minimal Arduino/ESP32 API for compiling the portable modem/AX.25 code on a PC (host tests).
// Only what the code under test needs. Time is simulated: tests advance it with hostAdvanceMs().
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>

// Minimal Arduino String (only what the code under test uses)
class String
{
    std::string s;

public:
    String() {}
    String(const char *c) : s(c ? c : "") {}
    String(const std::string &x) : s(x) {}
    String(int v) : s(std::to_string(v)) {}
    const char *c_str() const { return s.c_str(); }
    unsigned length() const { return (unsigned)s.size(); }
    String operator+(const char *c) const { return String(s + (c ? c : "")); }
    String operator+(const String &o) const { return String(s + o.s); }
    String &operator+=(const char *c) { s += (c ? c : ""); return *this; }
    bool operator==(const char *c) const { return s == (c ? c : ""); }
};

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define PROGMEM
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1

extern int64_t hostTimeUs; // simulated time, see host_stubs.cpp
void hostAdvanceMs(uint32_t ms);

inline unsigned long millis() { return (unsigned long)(hostTimeUs / 1000); }
inline unsigned long micros() { return (unsigned long)hostTimeUs; }
inline void delay(uint32_t ms) { hostAdvanceMs(ms); }
inline long random(long lo, long hi) { return hi > lo ? lo + (std::rand() % (hi - lo)) : lo; }
inline long random(long hi) { return random(0, hi); }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }

// Logging: silent unless HOST_LOG is set (keeps test output readable)
extern int hostLog;
#define HOST_LOGF(lvl, fmt, ...) do { if (hostLog) std::printf("[" lvl "] " fmt "\n", ##__VA_ARGS__); } while (0)
#define log_e(fmt, ...) HOST_LOGF("E", fmt, ##__VA_ARGS__)
#define log_w(fmt, ...) HOST_LOGF("W", fmt, ##__VA_ARGS__)
#define log_i(fmt, ...) HOST_LOGF("I", fmt, ##__VA_ARGS__)
#define log_d(fmt, ...) HOST_LOGF("D", fmt, ##__VA_ARGS__)
#define log_v(fmt, ...) HOST_LOGF("V", fmt, ##__VA_ARGS__)

#ifndef _BV
#define _BV(b) (1UL << (b))
#endif

typedef bool boolean;

// strlcpy/strlcat (newlib on the ESP32 has them, older glibc doesn't)
inline size_t host_strlcpy(char *d, const char *s, size_t n)
{
    size_t len = std::strlen(s);
    if (n)
    {
        size_t c = len < n - 1 ? len : n - 1;
        std::memcpy(d, s, c);
        d[c] = 0;
    }
    return len;
}
inline size_t host_strlcat(char *d, const char *s, size_t n)
{
    size_t dl = strnlen(d, n);
    if (dl == n)
        return n + std::strlen(s);
    return dl + host_strlcpy(d + dl, s, n - dl);
}
#define strlcpy host_strlcpy
#define strlcat host_strlcat

// FreeRTOS types referenced by declarations in AFSK.h / main.h
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;

// Spinlocks are no-ops: host tests are single-threaded
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
