// End-to-end tests of the firmware's AX.25 / FX.25 / AFSK modem code:
// TNC2 text -> ax25_encode -> hdlcFrame -> AX.25 TX -> modulator -> audio -> demodulator -> frame.
#include <Arduino.h>
#include "test.h"
#include "modem_harness.h"
#include "AX25.h"

static const char *PKT = "LU1ABC-9>APE32A,WIDE1-1,WIDE2-1:!3436.22S/05822.80W>Test 144.390";

static bool sameAsSent(const DecodedPacket &p, const std::string &sent)
{
    return hostTnc2(p) == sent;
}

TEST(modem1200_roundtrip_single)
{
    hostModemSetup(HOST_MODEM_1200, 0);
    auto audio = hostTransmit({PKT});
    CHECK(!audio.empty());
    hostReceive(audio);
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK_EQ_STR(hostTnc2(rx[0]).c_str(), PKT);
}

TEST(modem1200_roundtrip_three_frames_one_tx)
{
    hostModemSetup(HOST_MODEM_1200, 0);
    std::vector<std::string> pk = {
        "LU1ABC>APE32A:>status one",
        "LU2DEF-7>APE32A,WIDE1-1:=3436.22S/05822.80W-two",
        "LU3GHI-10>APE32A,WIDE2-2:T#001,100,200,300,400,500,00000000",
    };
    hostReceive(hostTransmit(pk));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 3);
    for (size_t i = 0; i < rx.size() && i < pk.size(); i++)
        CHECK(sameAsSent(rx[i], pk[i]));
}

TEST(modem1200_roundtrip_long_info)
{
    hostModemSetup(HOST_MODEM_1200, 0);
    std::string info(250, 'x');
    std::string pkt = "LU1ABC-9>APE32A:>" + info;
    hostReceive(hostTransmit({pkt}));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK(sameAsSent(rx[0], pkt));
}

TEST(modem1200_roundtrip_eight_hop_path)
{
    hostModemSetup(HOST_MODEM_1200, 0);
    std::string pkt = "LU1ABC-9>APE32A,LU1AAA-1,LU1BBB-2,LU1CCC-3,LU1DDD-4,LU1EEE-5,LU1FFF-6,LU1GGG-7,WIDE2-1:>8 hops";
    hostReceive(hostTransmit({pkt}));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK(sameAsSent(rx[0], pkt));
}

TEST(modem300_roundtrip)
{
    hostModemSetup(HOST_MODEM_300, 0);
    hostReceive(hostTransmit({PKT}));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK_EQ_STR(hostTnc2(rx[0]).c_str(), PKT);
}

TEST(modemV23_roundtrip)
{
    hostModemSetup(HOST_MODEM_V23, 0);
    hostReceive(hostTransmit({PKT}));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK_EQ_STR(hostTnc2(rx[0]).c_str(), PKT);
}

TEST(modem9600_roundtrip)
{
    hostModemSetup(HOST_MODEM_9600, 0);
    hostReceive(hostTransmit({PKT}), 4.0);
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK_EQ_STR(hostTnc2(rx[0]).c_str(), PKT);
}

TEST(fx25_roundtrip)
{
    hostModemSetup(HOST_MODEM_1200, 2); // FX.25 RX + TX
    hostReceive(hostTransmit({PKT}));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
    if (rx.size() == 1)
        CHECK_EQ_STR(hostTnc2(rx[0]).c_str(), PKT);
}

// The TX must end by itself (no stuck PTT) and within the time-out budget
TEST(tx_finishes_within_timeout)
{
    extern bool hostPtt;
    extern volatile bool pttOff;
    hostModemSetup(HOST_MODEM_1200, 0);
    pttOff = false;
    std::string big = "LU1ABC-9>APE32A,WIDE1-1:>" + std::string(250, 'y');
    auto audio = hostTransmit({big, big, big});
    double seconds = audio.size() / 38400.0;
    CHECK(!audio.empty());
    CHECK(pttOff); // ModemTransmitStop() asked for PTT release
    CHECK(seconds * 1000.0 < Ax25TxTimeoutMs());
}

// Ax25TxAbort() must leave the TX state machine ready for the next transmission
TEST(tx_abort_then_transmit_again)
{
    extern volatile bool hw_afsk_dac_isr;
    hostModemSetup(HOST_MODEM_1200, 0);
    std::vector<std::string> one = {PKT};
    // start a TX and abort it half way
    {
        static AX25Ctx ctx;
        std::vector<char> b(one[0].begin(), one[0].end());
        b.push_back(0);
        ax25frame f;
        ax25_encode(f, b.data(), (int)one[0].size());
        uint8_t data[AX25_FRAME_MAX_SIZE];
        int n = hdlcFrame(data, sizeof(data), &ctx, &f);
        Ax25WriteTxFrame(data, n);
        Ax25TransmitBuffer();
        for (int t = 0; t < 1000 && !hw_afsk_dac_isr; t++)
        {
            hostAdvanceMs(10);
            Ax25TransmitCheck();
        }
        extern uint8_t MODEM_BAUDRATE_TIMER_HANDLER(void);
        for (int i = 0; i < 20000 && hw_afsk_dac_isr; i++)
            MODEM_BAUDRATE_TIMER_HANDLER();
        CHECK(hw_afsk_dac_isr); // still transmitting
        Ax25TxAbort();
        CHECK(!hw_afsk_dac_isr);
    }
    hostReceive(hostTransmit(one));
    auto rx = hostReadFrames();
    CHECK_EQ_INT(rx.size(), 1);
}

// PR2: transmissions keep working across the 49.7-day millis() wrap
TEST(tx_across_millis_wrap)
{
    hostTimeUs = (int64_t)(0xFFFFFFFFull - 20) * 1000; // 20 ms before millis() wraps
    hostModemSetup(HOST_MODEM_1200, 0);
    for (int i = 0; i < 3; i++)
    {
        auto audio = hostTransmit({PKT});
        CHECK(!audio.empty());
        hostReceive(audio);
        CHECK_EQ_INT(hostReadFrames().size(), 1);
        hostAdvanceMs(15);
    }
}

// PR5 (A1): oversized input must be rejected without writing out of bounds (ASan checks)
TEST(encode_oversized_packet_is_dropped)
{
    static AX25Ctx ctx;
    std::string huge = "LU1ABC-9>APE32A,WIDE1-1,WIDE2-1:>" + std::string(600, 'z');
    std::vector<char> b(huge.begin(), huge.end());
    b.push_back(0);
    ax25frame f;
    CHECK(ax25_encode(f, b.data(), (int)huge.size()));
    uint8_t data[AX25_FRAME_MAX_SIZE];
    CHECK_EQ_INT(hdlcFrame(data, sizeof(data), &ctx, &f), 0); // too long for one frame
}

// PR5 (A1): header longer than 127 characters (char index went negative)
TEST(encode_header_longer_than_127)
{
    static AX25Ctx ctx;
    std::string pkt = "LU1ABC-15>APE32A";
    for (int i = 0; i < 8; i++)
        pkt += ",LONGCALL-" + std::to_string(i + 1);
    pkt += std::string(70, ' ') + ":>x"; // pushes ':' beyond 127
    std::vector<char> b(pkt.begin(), pkt.end());
    b.push_back(0);
    ax25frame f;
    ax25_encode(f, b.data(), (int)pkt.size()); // must not crash / overflow
    uint8_t data[AX25_FRAME_MAX_SIZE];
    hdlcFrame(data, sizeof(data), &ctx, &f);
    CHECK(true);
}
