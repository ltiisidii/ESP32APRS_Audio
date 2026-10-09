// Test runner.
#include <Arduino.h>
#include <cstdlib>
#include <vector>
#include "test.h"

int testChecks = 0;
int testFailures = 0;

struct TestEntry
{
    const char *name;
    TestFn fn;
};
static std::vector<TestEntry> &registry()
{
    static std::vector<TestEntry> r;
    return r;
}
TestReg::TestReg(const char *name, TestFn fn) { registry().push_back({name, fn}); }

int main(int argc, char **argv)
{
    hostLog = std::getenv("HOST_LOG") != nullptr;
    const char *only = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto &t : registry())
    {
        if (only && std::strstr(t.name, only) == nullptr)
            continue;
        int before = testFailures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", testFailures == before ? " OK " : "FAIL", t.name);
        run++;
    }
    std::printf("\n%d tests, %d checks, %d failures\n", run, testChecks, testFailures);
    return testFailures ? 1 : 0;
}
