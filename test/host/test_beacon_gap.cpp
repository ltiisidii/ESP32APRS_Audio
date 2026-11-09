// Status beacon kept at least 60 s away from the position beacon of the same station.
#include "test.h"
#include "beacon_gap.h"

static const uint64_t S = 1000;

TEST(beacon_gap_after_save_status_follows_position)
{
    // Both timers restarted together: position and status due at the same moment
    uint64_t t = 100 * S;
    CHECK_EQ_INT(statusSendAt(t, 0, t), t + 60 * S);
    // Position already sent this loop (iGate order is status first, but be safe either way)
    CHECK_EQ_INT(statusSendAt(t, t, t + 1800 * S), t + 60 * S);
}

TEST(beacon_gap_equal_intervals_stay_60s_apart)
{
    // Simulate 1800 s / 1800 s for 10 hours with the firmware's scheduling
    uint64_t pos = 10 * S, sts = 10 * S, lastPos = 0;
    int sent = 0;
    for (uint64_t now = 10 * S; now < 36000 * S; now += 100) // 100 ms loop
    {
        if (now >= sts)
        {
            uint64_t at = statusSendAt(now, lastPos, pos);
            if (at > now)
                sts = at;
            else
            {
                CHECK(lastPos == 0 || now - lastPos >= 60 * S);
                CHECK(pos - now >= 60 * S);
                sts = now + 1800 * S;
                sent++;
            }
        }
        if (now >= pos)
        {
            lastPos = now;
            pos = now + 1800 * S;
        }
    }
    CHECK_EQ_INT(sent, 20);
}

TEST(beacon_gap_far_from_position_sends_now)
{
    uint64_t t = 5000 * S;
    CHECK_EQ_INT(statusSendAt(t, t - 900 * S, t + 900 * S), t);
    CHECK_EQ_INT(statusSendAt(t, 0, 0), t); // no position beacon at all
}

TEST(beacon_gap_position_due_soon_waits)
{
    uint64_t t = 5000 * S;
    CHECK_EQ_INT(statusSendAt(t, t - 1000 * S, t + 30 * S), t + 90 * S);
}

TEST(beacon_gap_stuck_position_does_not_block_status)
{
    // Position due long ago but never sent (GPS without fix): the status still goes out
    uint64_t t = 5000 * S;
    CHECK_EQ_INT(statusSendAt(t, 0, t - 600 * S), t);
}
