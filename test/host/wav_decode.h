#pragma once
#include <cstdint>
#include <vector>
#include "modem_harness.h"

// Reads a 16-bit PCM WAV (first channel) as floats in [-1, 1)
bool hostReadWav(const char *path, std::vector<float> &samples, uint32_t &rate);

// Decodes a WAV with the firmware's demodulator; returns frames decoded, -1 if unreadable
int hostDecodeWav(const char *path, HostModem modem, bool flatAudio, bool verbose);

// Writes count test packets transmitted by the firmware's modulator as a 38.4 kHz 16-bit WAV
int hostEncodeWav(const char *path, HostModem modem, uint8_t fx25Mode, int count);
bool hostWriteWav(const char *path, const std::vector<int16_t> &pcm, uint32_t rate);
