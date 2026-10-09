// Tests of src/digirepeater.cpp: path handling for WIDEn-N / TRACEn-N, filters, full paths (PR5 A4).
#include <Arduino.h>
#include "test.h"
#include "modem_harness.h"
#include "main.h"
#include "digirepeater.h"

Configuration config; // the firmware's global configuration (digirepeater.cpp reads it)

static void setupDigi()
{
    memset(&config, 0, sizeof(config));
    strlcpy(config.digi_mycall, "LU1DIG", sizeof(config.digi_mycall));
    config.digi_ssid = 1;
}

// Runs digiProcess() on a TNC2 packet. Returns its result; *out gets the resulting path.
static int digi(const char *tnc2, std::string *outPath)
{
    static AX25Msg msg;
    if (!hostParseTnc2(tnc2, &msg))
        return -1;
    int r = digiProcess(msg);
    if (outPath)
        *outPath = hostFromMsg(msg).path;
    return r;
}

TEST(digi_wide1_1_is_replaced_by_own_call)
{
    setupDigi();
    std::string path;
    CHECK(digi("LU1ABC-9>APE32A,WIDE1-1,WIDE2-1:>test packet", &path) > 0);
    CHECK_EQ_STR(path.c_str(), "LU1DIG-1*,WIDE2-1");
}

TEST(digi_wide2_2_decrements)
{
    setupDigi();
    std::string path;
    CHECK(digi("LU1ABC-9>APE32A,WIDE2-2:>test packet", &path) > 0);
    CHECK_EQ_STR(path.c_str(), "WIDE2-1");
}

TEST(digi_second_hop_after_used_hop)
{
    setupDigi();
    std::string path;
    CHECK(digi("LU1ABC-9>APE32A,LU2XYZ*,WIDE2-1:>test packet", &path) > 0);
    CHECK_EQ_STR(path.c_str(), "LU2XYZ*,LU1DIG-1*");
}

TEST(digi_trace_inserts_own_call)
{
    setupDigi();
    std::string path;
    CHECK(digi("LU1ABC-9>APE32A,TRACE3-3:>test packet", &path) > 0);
    CHECK_EQ_STR(path.c_str(), "LU1DIG-1*,TRACE3-2");
}

TEST(digi_ignores_used_path)
{
    setupDigi();
    CHECK_EQ_INT(digi("LU1ABC-9>APE32A,LU2XYZ*,LU3XYZ*:>test packet", nullptr), 0);
}

TEST(digi_ignores_no_path)
{
    setupDigi();
    CHECK_EQ_INT(digi("LU1ABC-9>APE32A:>test packet", nullptr), 0);
}

TEST(digi_ignores_internet_packets)
{
    setupDigi();
    CHECK_EQ_INT(digi("LU1ABC-9>APE32A,TCPIP*,qAC,T2TEST:>test packet", nullptr), 0);
    CHECK_EQ_INT(digi("LU1ABC-9>APE32A,WIDE1-1,qAR,LU1IGT:>test packet", nullptr), 0);
}

TEST(digi_ignores_nocall)
{
    setupDigi();
    CHECK_EQ_INT(digi("NOCALL>APE32A,WIDE1-1:>test packet", nullptr), 0);
}

// PR5 (A4): with 8 hops there is no room to insert our call; must not write past rpt_list[8]
TEST(digi_full_path_trace_does_not_overflow)
{
    setupDigi();
    std::string path;
    int r = digi("LU1ABC-9>APE32A,LU1AAA*,LU1BBB*,LU1CCC*,LU1DDD*,LU1EEE*,LU1FFF*,LU1GGG*,TRACE2-2:>test packet", &path);
    CHECK_EQ_INT(r, 0);
    CHECK_EQ_INT(std::count(path.begin(), path.end(), ',') + 1, 8);
}
