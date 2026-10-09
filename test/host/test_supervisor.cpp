// Supervisor stall check (PR4). Found on hardware: a feed stored 1 ms after the supervisor read
// millis() looked 49 days old and the station restarted every few seconds.
#include <Arduino.h>
#include "test.h"
#include "supervisor.h"

TEST(supervisor_feed_from_the_future_is_fresh)
{
    CHECK(!svStalled(1000, 1001, 30000)); // feed stored after 'now' was read (other core, | 1)
    CHECK(!svStalled(1000, 1000, 30000));
    CHECK(!svStalled(0x12345678, 0x12345678 | 1, 30000));
}

TEST(supervisor_detects_real_stall)
{
    CHECK(!svStalled(31000, 1000, 30000)); // exactly at the limit
    CHECK(svStalled(31001, 1000, 30000));
    CHECK(svStalled(400000, 1000, 300000));
}

TEST(supervisor_across_millis_wrap)
{
    CHECK(!svStalled(5, 0xFFFFFFF0u, 30000));   // 21 ms old across the wrap
    CHECK(svStalled(40000, 0xFFFFFFF0u, 30000)); // 40 s old across the wrap
}
