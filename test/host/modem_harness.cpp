#include <Arduino.h>
#include <random>
#include "modem_harness.h"
#include "AX25.h"
#include "modem.h"
#include "fx25.h"

extern volatile bool hw_afsk_dac_isr;
extern struct ModemDemodConfig ModemConfig;

static HostModem currentModem = HOST_MODEM_1200;

void hostModemSetup(HostModem modem, uint8_t fx25Mode, bool flatAudio)
{
    currentModem = modem;
    hw_afsk_dac_isr = false;
    ModemConfig.modem = (enum ModemType)modem;
    ModemConfig.flatAudioIn = flatAudio ? 1 : 0;
    ModemConfig.usePWM = 1;
    ModemInit();
    Ax25Init(fx25Mode);
    if (fx25Mode > 0)
        Fx25Init();
    Ax25TimeSlot(0);  // no extra slot delay
    Ax25TxDelay(100); // short preamble keeps tests fast
    Ax25TxAbort();    // clear frames/state left by a previous test
    hostReadFrames(); // drop anything left from a previous test
}

std::vector<uint8_t> hostTransmit(const std::vector<std::string> &tnc2)
{
    std::vector<uint8_t> dac;
    static AX25Ctx ctx;
    for (const std::string &t : tnc2)
    {
        std::vector<char> buf(t.begin(), t.end());
        buf.push_back(0);
        ax25frame frame;
        if (!ax25_encode(frame, buf.data(), (int)t.size()))
            continue;
        uint8_t data[AX25_FRAME_MAX_SIZE];
        int size = hdlcFrame(data, sizeof(data), &ctx, &frame);
        if (size > 0)
            Ax25WriteTxFrame(data, size);
    }
    Ax25TransmitBuffer();
    // taskAPRS polls Ax25TransmitCheck() every 10 ms; TX starts after the quiet time
    for (int t = 0; t < 1000 && !hw_afsk_dac_isr; t++)
    {
        hostAdvanceMs(10);
        Ax25TransmitCheck(); // ModemTransmitStart() sets hw_afsk_dac_isr
    }
    // DAC timer ISR: one call per DAC sample until ModemTransmitStop() clears hw_afsk_dac_isr
    size_t guard = 38400u * 120u;
    while (hw_afsk_dac_isr && dac.size() < guard)
        dac.push_back(MODEM_BAUDRATE_TIMER_HANDLER());
    return dac;
}

void hostReceive(const std::vector<uint8_t> &dac, double gain, double noiseStd, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noiseStd > 0 ? noiseStd : 1.0);
    // 1200/300 baud demodulators run at 9.6 kHz (DAC is 38.4 kHz: average 4 -> decimate),
    // 9600 baud runs at 38.4 kHz
    int decim = (currentModem == HOST_MODEM_9600) ? 1 : 4;
    auto feed = [&](double v) {
        if (noiseStd > 0)
            v += noise(rng);
        if (v > 32767)
            v = 32767;
        if (v < -32768)
            v = -32768;
        MODEM_DECODE((int16_t)v, 100);
    };
    // some silence before and after, like a real channel
    for (int i = 0; i < 2000; i++)
        feed(0);
    for (size_t i = 0; i + decim <= dac.size(); i += decim)
    {
        double acc = 0;
        for (int k = 0; k < decim; k++)
            acc += (double)dac[i + k] - 127.5;
        feed(acc / decim * gain);
    }
    for (int i = 0; i < 2000; i++)
        feed(0);
}

static std::string callStr(const AX25Call &c)
{
    std::string s(c.call, strnlen(c.call, sizeof(c.call)));
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    if (c.ssid)
        s += "-" + std::to_string(c.ssid);
    return s;
}

std::vector<DecodedPacket> hostReadFrames()
{
    std::vector<DecodedPacket> out;
    uint8_t *buf;
    uint16_t size, mV;
    int8_t peak, valley;
    uint8_t level, corrected;
    while (Ax25ReadNextRxFrame(&buf, &size, &peak, &valley, &level, &corrected, &mV))
    {
        static AX25Msg msg;
        memset(&msg, 0, sizeof(msg));
        ax25_decode(buf, size, mV, &msg);
        out.push_back(hostFromMsg(msg));
    }
    return out;
}

DecodedPacket hostFromMsg(const AX25Msg &msg)
{
    DecodedPacket p;
    p.src = callStr(msg.src);
    p.dst = callStr(msg.dst);
    for (int i = 0; i < msg.rpt_count && i < AX25_MAX_RPT; i++)
    {
        if (i)
            p.path += ",";
        p.path += callStr(msg.rpt_list[i]);
        if (msg.rpt_flags & (1 << i))
            p.path += "*";
    }
    size_t n = msg.len < sizeof(msg.info) ? msg.len : sizeof(msg.info);
    p.info.assign((const char *)msg.info, n);
    return p;
}

bool hostParseTnc2(const std::string &tnc2, AX25Msg *msg)
{
    static AX25Ctx ctx;
    std::vector<char> buf(tnc2.begin(), tnc2.end());
    buf.push_back(0);
    ax25frame frame;
    if (!ax25_encode(frame, buf.data(), (int)tnc2.size()))
        return false;
    uint8_t data[AX25_FRAME_MAX_SIZE];
    int size = hdlcFrame(data, sizeof(data), &ctx, &frame);
    if (size <= 0)
        return false;
    memset(msg, 0, sizeof(*msg));
    ax25_decode(data, size, 0, msg);
    return true;
}

std::string hostTnc2(const DecodedPacket &p)
{
    std::string s = p.src + ">" + p.dst;
    if (!p.path.empty())
        s += "," + p.path;
    return s + ":" + p.info;
}
