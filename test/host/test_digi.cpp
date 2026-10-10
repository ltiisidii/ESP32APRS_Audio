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

TEST(digi_wide2_2_traceable)
{
    setupDigi();
    std::string path;
    CHECK(digi("LU1ABC-9>APE32A,WIDE2-2:>test packet", &path) > 0);
    CHECK_EQ_STR(path.c_str(), "LU1DIG-1*,WIDE2-1"); // New-N paradigm: own call inserted
}

TEST(digi_wide1_1_wide2_2_second_hop_traceable)
{
    setupDigi();
    std::string path;
    CHECK(digi("LU1ABC-9>APE32A,LU2XYZ*,WIDE2-2:>test packet", &path) > 0);
    CHECK_EQ_STR(path.c_str(), "LU2XYZ*,LU1DIG-1*,WIDE2-1");
}

TEST(digi_wide_n_full_path_only_decrements)
{
    setupDigi();
    std::string path;
    int r = digi("LU1ABC-9>APE32A,LU1AAA*,LU1BBB*,LU1CCC*,LU1DDD*,LU1EEE*,LU1FFF*,LU1GGG*,WIDE2-2:>test packet", &path);
    CHECK(r > 0);
    CHECK_EQ_STR(path.c_str(), "LU1AAA*,LU1BBB*,LU1CCC*,LU1DDD*,LU1EEE*,LU1FFF*,LU1GGG*,WIDE2-1");
}

// ---- duplicate suppression
static AX25Msg parsed(const char *tnc2)
{
    AX25Msg m;
    hostParseTnc2(tnc2, &m);
    return m;
}

TEST(digi_dup_same_packet_other_path_within_30s)
{
    setupDigi();
    AX25Msg a = parsed("LU1DUP-9>APE32A,WIDE1-1,WIDE2-1:>dup test");
    AX25Msg b = parsed("LU1DUP-9>APE32A,LU2XYZ*,WIDE2-1:>dup test"); // heard back via another digi
    CHECK(!digiIsDuplicate(a, 100000));
    digiRemember(a, 100000);
    CHECK(digiIsDuplicate(b, 105000));
    CHECK(digiIsDuplicate(b, 129999));
    CHECK(!digiIsDuplicate(b, 130000)); // window over
}

TEST(digi_dup_different_info_or_source_is_not_dup)
{
    setupDigi();
    digiRemember(parsed("LU1DUP-9>APE32A,WIDE1-1:>first"), 200000);
    CHECK(!digiIsDuplicate(parsed("LU1DUP-9>APE32A,WIDE1-1:>second"), 201000));
    CHECK(!digiIsDuplicate(parsed("LU1DUP-8>APE32A,WIDE1-1:>first"), 201000));
    CHECK(!digiIsDuplicate(parsed("LU1DUP-9>APRS,WIDE1-1:>first"), 201000));
}

TEST(digi_dup_across_millis_wrap)
{
    setupDigi();
    AX25Msg a = parsed("LU1WRP>APE32A,WIDE1-1:>wrap");
    digiRemember(a, 0xFFFFFF00u);
    CHECK(digiIsDuplicate(a, 0x00000100u));            // 512 ms later, across the wrap
    CHECK(!digiIsDuplicate(a, 0xFFFFFF00u + 30000u));  // 30 s later
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
