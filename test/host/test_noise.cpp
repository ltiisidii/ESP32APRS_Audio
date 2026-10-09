// Demodulator quality on synthetic signals: decode rate vs. white noise, and the WAV path.
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include "test.h"
#include "modem_harness.h"
#include "wav_decode.h"

// Signal: DAC swing +-127 x gain 4 => ~+-510 peak (~360 RMS). Noise std in the same units.
static int decodeRate(HostModem m, bool flat, double noiseStd, int packets)
{
    hostModemSetup(m, 0, flat);
    int ok = 0;
    for (int i = 0; i < packets; i++)
    {
        char pkt[96];
        std::snprintf(pkt, sizeof(pkt), "LU1ABC-9>APE32A,WIDE1-1:!3436.22S/05822.80W>packet %03d", i);
        auto audio = hostTransmit({pkt});
        hostReceive(audio, 4.0, noiseStd, 1000 + i);
        for (auto &p : hostReadFrames())
            if (hostTnc2(p) == pkt)
                ok++;
    }
    return ok;
}

// Prints a table for the record and fails if the clean-ish case degrades (regression guard)
TEST(noise_decode_rate_1200)
{
    const int N = 40;
    const double levels[] = {0, 100, 200, 300, 400};
    std::printf("    noise std | SNR dB | normal audio | flat audio\n");
    for (double nz : levels)
    {
        int a = decodeRate(HOST_MODEM_1200, false, nz, N);
        int b = decodeRate(HOST_MODEM_1200, true, nz, N);
        double snr = nz > 0 ? 20 * std::log10(360.0 / nz) : 99;
        std::printf("    %9.0f | %6.1f | %5d/%d      | %5d/%d\n", nz, snr, a, N, b, N);
        if (nz <= 100)
        {
            CHECK(a >= N - 1);
            CHECK(b >= N - 1);
        }
    }
}

// WAV path used for the WA8LMF test CD: write the TX audio as a 44.1 kHz WAV and decode it back
TEST(wav_decode_synthetic_44k1)
{
    hostModemSetup(HOST_MODEM_1200, 0);
    std::vector<uint8_t> dac;
    for (int i = 0; i < 5; i++)
    {
        char pkt[64];
        std::snprintf(pkt, sizeof(pkt), "LU1ABC-%d>APE32A:>wav test %d", i + 1, i);
        auto a = hostTransmit({pkt});
        dac.insert(dac.end(), a.begin(), a.end());
        dac.insert(dac.end(), 38400 / 4, 128); // 250 ms silence
    }
    // 38.4 kHz DAC -> 44.1 kHz 16-bit PCM (linear interpolation)
    std::vector<int16_t> pcm;
    for (double t = 0; t + 1 < dac.size(); t += 38400.0 / 44100.0)
    {
        size_t i = (size_t)t;
        double f = t - i;
        double v = ((dac[i] - 127.5) * (1 - f) + (dac[i + 1] - 127.5) * f) / 128.0;
        pcm.push_back((int16_t)(v * 16000));
    }
    const char *path = "build/synthetic.wav";
    FILE *fp = std::fopen(path, "wb");
    CHECK(fp != nullptr);
    if (!fp)
        return;
    uint32_t dataLen = pcm.size() * 2, rate = 44100, byteRate = rate * 2;
    uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                       0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t riff = 36 + dataLen;
    std::memcpy(hdr + 4, &riff, 4);
    std::memcpy(hdr + 24, &rate, 4);
    std::memcpy(hdr + 28, &byteRate, 4);
    std::memcpy(hdr + 40, &dataLen, 4);
    std::fwrite(hdr, 1, 44, fp);
    std::fwrite(pcm.data(), 2, pcm.size(), fp);
    std::fclose(fp);
    CHECK_EQ_INT(hostDecodeWav(path, HOST_MODEM_1200, false, false), 5);
}
