// Tiny test framework for the host tests (no external dependencies).
#pragma once
#include <cstdio>
#include <cstring>

extern int testChecks;
extern int testFailures;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        testChecks++;                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            testFailures++;                                                      \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                       \
    do                                                                           \
    {                                                                            \
        testChecks++;                                                            \
        long long _a = (long long)(a), _b = (long long)(b);                      \
        if (_a != _b)                                                            \
        {                                                                        \
            testFailures++;                                                      \
            std::printf("    FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__,   \
                        __LINE__, #a, #b, _a, _b);                               \
        }                                                                        \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                       \
    do                                                                           \
    {                                                                            \
        testChecks++;                                                            \
        if (std::strcmp((a), (b)) != 0)                                          \
        {                                                                        \
            testFailures++;                                                      \
            std::printf("    FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
                        (a), (b));                                               \
        }                                                                        \
    } while (0)

typedef void (*TestFn)(void);
struct TestReg
{
    TestReg(const char *name, TestFn fn);
};

#define TEST(name)                                \
    static void name(void);                       \
    static TestReg testreg_##name(#name, name);   \
    static void name(void)
