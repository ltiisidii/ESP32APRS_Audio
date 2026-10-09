// Decodes a WAV file (e.g. a WA8LMF TNC Test CD track) with the firmware's demodulator.
// Used by the tests and, built with -DWAV_DECODE_MAIN, as a command line tool:
//   wav_decode <file.wav> [1200|300|9600] [flat] [-v]
// Prints the number of decoded frames (and, with -v, each frame prefixed by its time in seconds).
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "modem_harness.h"
#include "modem.h"
#include "wav_decode.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

bool hostReadWav(const char *path, std::vector<float> &samples, uint32_t &rate)
{
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 44 || std::memcmp(&d[0], "RIFF", 4) || std::memcmp(&d[8], "WAVE", 4))
        return false;
    uint16_t channels = 0, bits = 0, format = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size())
    {
        uint32_t len = rd32(&d[pos + 4]);
        if (!std::memcmp(&d[pos], "fmt ", 4) && pos + 24 <= d.size())
        {
            format = rd16(&d[pos + 8]);
            channels = rd16(&d[pos + 10]);
            rate = rd32(&d[pos + 12]);
            bits = rd16(&d[pos + 22]);
        }
        else if (!std::memcmp(&d[pos], "data", 4))
        {
            if (format != 1 || bits != 16 || channels == 0)
                return false; // only 16-bit PCM
            size_t end = std::min(d.size(), pos + 8 + (size_t)len);
            for (size_t i = pos + 8; i + 2 * channels <= end; i += 2 * channels)
                samples.push_back((int16_t)rd16(&d[i]) / 32768.0f); // first channel
            return true;
        }
        pos += 8 + len + (len & 1);
    }
    return false;
}

// Low-pass (windowed sinc) + linear interpolation to the demodulator rate
static std::vector<float> resample(const std::vector<float> &in, double inRate, double outRate, double cutoffHz)
{
    const int taps = 63;
    double fc = std::min(cutoffHz, outRate * 0.45) / inRate;
    std::vector<double> h(taps);
    double sum = 0;
    for (int i = 0; i < taps; i++)
    {
        int m = i - taps / 2;
        double sinc = m == 0 ? 2 * fc : std::sin(2 * M_PI * fc * m) / (M_PI * m);
        double w = 0.54 - 0.46 * std::cos(2 * M_PI * i / (taps - 1));
        h[i] = sinc * w;
        sum += h[i];
    }
    std::vector<float> lp(in.size());
    for (size_t n = 0; n < in.size(); n++)
    {
        double acc = 0;
        for (int k = 0; k < taps; k++)
        {
            long idx = (long)n - k + taps / 2;
            if (idx >= 0 && idx < (long)in.size())
                acc += in[idx] * h[k];
        }
        lp[n] = (float)(acc / sum);
    }
    std::vector<float> out;
    double step = inRate / outRate;
    for (double t = 0; t + 1 < lp.size(); t += step)
    {
        size_t i = (size_t)t;
        double f = t - i;
        out.push_back((float)(lp[i] * (1 - f) + lp[i + 1] * f));
    }
    return out;
}

int hostDecodeWav(const char *path, HostModem modem, bool flat, bool verbose)
{
    std::vector<float> s;
    uint32_t rate = 0;
    if (!hostReadWav(path, s, rate))
    {
        std::printf("cannot read %s (16-bit PCM WAV only)\n", path);
        return -1;
    }
    hostModemSetup(modem, 1, flat); // FX.25 RX on, as in the firmware default
    double outRate = (modem == HOST_MODEM_9600) ? 38400.0 : 9600.0;
    // 4 kHz anti-alias for the AFSK modems; 9600 baud G3RUH needs the full band
    double cutoff = (modem == HOST_MODEM_9600) ? outRate * 0.45 : 4000.0;
    std::vector<float> r = resample(s, rate, outRate, cutoff);
    // Full-scale WAV -> +-2048 (12-bit ADC range after DC removal)
    int frames = 0;
    for (size_t i = 0; i < r.size(); i++)
    {
        MODEM_DECODE((int16_t)std::lround(r[i] * 2048.0f), 100);
        if ((i & 1023) == 0)
        {
            for (auto &p : hostReadFrames())
            {
                frames++;
                if (verbose)
                    std::printf("%8.2f %s\n", i / outRate, hostTnc2(p).c_str()); // time in s
            }
        }
    }
    for (auto &p : hostReadFrames())
    {
        frames++;
        if (verbose)
            std::printf("%8.2f %s\n", r.size() / outRate, hostTnc2(p).c_str());
    }
    return frames;
}

bool hostWriteWav(const char *path, const std::vector<int16_t> &pcm, uint32_t rate)
{
    FILE *fp = std::fopen(path, "wb");
    if (!fp)
        return false;
    uint32_t dataLen = pcm.size() * 2, byteRate = rate * 2, riff = 36 + dataLen;
    uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                       0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    std::memcpy(hdr + 4, &riff, 4);
    std::memcpy(hdr + 24, &rate, 4);
    std::memcpy(hdr + 28, &byteRate, 4);
    std::memcpy(hdr + 40, &dataLen, 4);
    std::fwrite(hdr, 1, 44, fp);
    std::fwrite(pcm.data(), 2, pcm.size(), fp);
    std::fclose(fp);
    return true;
}

int hostEncodeWav(const char *path, HostModem modem, uint8_t fx25Mode, int count)
{
    hostModemSetup(modem, fx25Mode, false);
    std::vector<int16_t> pcm(38400 / 2, 0); // 0.5 s of silence first
    for (int i = 0; i < count; i++)
    {
        char pkt[128];
        std::snprintf(pkt, sizeof(pkt), "LU1ABC-9>APE32A,WIDE1-1:>ESP32APRS_Audio TX test %04d", i + 1);
        auto dac = hostTransmit({pkt});
        // DAC (0..255 at 38.4 kHz) -> 16-bit PCM, as the radio's modulator input would see it
        for (uint8_t v : dac)
            pcm.push_back((int16_t)((v - 127.5) * 160));
        pcm.insert(pcm.end(), 38400 / 4, 0); // 250 ms between packets
    }
    return hostWriteWav(path, pcm, 38400) ? count : -1;
}

#ifdef WAV_DECODE_MAIN
static bool parseModem(const char *a, HostModem &m)
{
    if (!std::strcmp(a, "300"))
        m = HOST_MODEM_300;
    else if (!std::strcmp(a, "1200"))
        m = HOST_MODEM_1200;
    else if (!std::strcmp(a, "9600"))
        m = HOST_MODEM_9600;
    else if (!std::strcmp(a, "v23"))
        m = HOST_MODEM_V23;
    else
        return false;
    return true;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !std::strcmp(argv[1], "--tx"))
    {
        // wav_decode --tx <out.wav> [1200|300|9600|v23] [fx25] [count]
        HostModem m = HOST_MODEM_1200;
        uint8_t fx25 = 0;
        int count = 100;
        for (int i = 3; i < argc; i++)
        {
            if (!std::strcmp(argv[i], "fx25"))
                fx25 = 2;
            else if (!parseModem(argv[i], m) && std::atoi(argv[i]) > 0)
                count = std::atoi(argv[i]);
        }
        int n = hostEncodeWav(argv[2], m, fx25, count);
        std::printf("%s: %d frames written\n", argv[2], n);
        return n < 0;
    }
    if (argc < 2)
    {
        std::printf("usage: wav_decode <file.wav> [1200|300|9600|v23] [flat] [-v]\n"
                    "       wav_decode --tx <out.wav> [1200|300|9600|v23] [fx25] [count]\n");
        return 2;
    }
    HostModem m = HOST_MODEM_1200;
    bool flat = false, verbose = false;
    for (int i = 2; i < argc; i++)
    {
        if (!std::strcmp(argv[i], "flat"))
            flat = true;
        else if (!std::strcmp(argv[i], "-v"))
            verbose = true;
        else
            parseModem(argv[i], m);
    }
    int n = hostDecodeWav(argv[1], m, flat, verbose);
    if (n < 0)
        return 1;
    std::printf("%s: %d frames decoded\n", argv[1], n);
    return 0;
}
#endif
