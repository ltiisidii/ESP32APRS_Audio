// Own RF echo: which digipeater repeated a packet of this station (rfecho.h).
#include "test.h"
#include "rfecho.h"

static const char *OWN[] = {"LU4YYY-10"};

static AX25Msg msg(const char *src, uint8_t ssid)
{
    AX25Msg m;
    memset(&m, 0, sizeof(m));
    strcpy(m.src.call, src);
    m.src.ssid = ssid;
    strcpy(m.dst.call, "APE32A");
    return m;
}

static void hop(AX25Msg &m, const char *call, uint8_t ssid, bool repeated)
{
    strcpy(m.rpt_list[m.rpt_count].call, call);
    m.rpt_list[m.rpt_count].ssid = ssid;
    if (repeated)
        m.rpt_flags |= 1 << m.rpt_count;
    m.rpt_count++;
}

TEST(rfecho_digi_with_call)
{
    // LU4YYY-10>APE32A,LU8EB-2*,WIDE2*  (seen on 2026-11-10)
    AX25Msg m = msg("LU4YYY", 10);
    hop(m, "LU8EB", 2, true);
    hop(m, "WIDE2", 0, true);
    char by[12];
    CHECK_EQ_INT(rfEchoBy(&m, OWN, 1, by, sizeof(by)), 1);
    CHECK(strcmp(by, "LU8EB-2") == 0);
}

TEST(rfecho_last_digi_is_the_one_heard)
{
    // Two hops: LU5VQV-1 repeated after LU8EB-2, we heard LU5VQV-1
    AX25Msg m = msg("LU4YYY", 10);
    hop(m, "LU8EB", 2, true);
    hop(m, "LU5VQV", 1, true);
    hop(m, "WIDE2", 0, true);
    char by[12];
    CHECK_EQ_INT(rfEchoBy(&m, OWN, 1, by, sizeof(by)), 1);
    CHECK(strcmp(by, "LU5VQV-1") == 0);
}

TEST(rfecho_digi_without_call)
{
    // LU4YYY-10>APE32A,WIDE2*  : repeated, but the digipeater left no call
    AX25Msg m = msg("LU4YYY", 10);
    hop(m, "WIDE2", 0, true);
    char by[12];
    CHECK_EQ_INT(rfEchoBy(&m, OWN, 1, by, sizeof(by)), 0);
    CHECK(strcmp(by, "?") == 0);
}

TEST(rfecho_not_repeated_or_not_ours)
{
    char by[12];
    // Our call, path not used yet: not proof of anything
    AX25Msg m = msg("LU4YYY", 10);
    hop(m, "WIDE2", 1, false);
    CHECK_EQ_INT(rfEchoBy(&m, OWN, 1, by, sizeof(by)), -1);
    // Same call, other SSID: another station
    AX25Msg o = msg("LU4YYY", 9);
    hop(o, "LU8EB", 2, true);
    CHECK_EQ_INT(rfEchoBy(&o, OWN, 1, by, sizeof(by)), -1);
    // Someone else's packet
    AX25Msg x = msg("LU8EB", 2);
    hop(x, "LU5VQV", 1, true);
    CHECK_EQ_INT(rfEchoBy(&x, OWN, 1, by, sizeof(by)), -1);
}

TEST(rfecho_own_digi_call_skipped)
{
    // Our digipeater identity in the path is not "the one who repeated it"
    const char *own[] = {"LU4YYY-10", "LU4YYY-1"};
    AX25Msg m = msg("LU4YYY", 10);
    hop(m, "LU4YYY", 1, true);
    hop(m, "WIDE1", 0, true);
    char by[12];
    CHECK_EQ_INT(rfEchoBy(&m, own, 2, by, sizeof(by)), 0);
    // SSID 0 is written without "-0"
    AX25Msg z = msg("LU4YYY", 0);
    const char *own0[] = {"LU4YYY"};
    hop(z, "LU8EB", 2, true);
    CHECK_EQ_INT(rfEchoBy(&z, own0, 1, by, sizeof(by)), 1);
}
