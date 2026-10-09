// Drives the firmware's real AX.25 + modem code on the PC: TX produces the DAC audio exactly as
// the ESP32 DAC ISR would, RX feeds samples to MODEM_DECODE() as AFSK_Poll() does.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Same values as the firmware's enum ModemType (modem.h). Note: the web setting
// config.modem_type uses another numbering (0=300, 1=1200, 2=V.23, 3=9600), mapped by afskSetModem().
enum HostModem
{
    HOST_MODEM_1200 = 0, // MODEM_1200, Bell 202 1200/2200 Hz
    HOST_MODEM_V23 = 1,  // MODEM_1200_V23, 1300/2100 Hz
    HOST_MODEM_300 = 2,  // MODEM_300, 1600/1800 Hz (HF)
    HOST_MODEM_9600 = 3, // MODEM_9600, G3RUH
};

struct DecodedPacket
{
    std::string src;  // CALL-SSID
    std::string dst;
    std::string path; // comma separated, '*' marks repeated hops
    std::string info;
};

// Same init sequence as afskSetModem(): ModemInit, Ax25Init, Fx25Init.
// fx25Mode: 0 = off, 1 = RX only, 2 = RX + TX
void hostModemSetup(HostModem modem, uint8_t fx25Mode, bool flatAudio = false);

// Queue TNC2 packets ("SRC>DST,PATH:info") and run the TX chain until the DAC stops.
// Returns the DAC samples (0..255 at 38.4 kHz); empty if nothing was transmitted.
std::vector<uint8_t> hostTransmit(const std::vector<std::string> &tnc2);

// Feed DAC audio to the demodulator. gain scales the DAC swing (+-127) to ADC units,
// noiseStd adds white Gaussian noise (ADC units).
void hostReceive(const std::vector<uint8_t> &dac, double gain = 4.0, double noiseStd = 0.0, unsigned seed = 1);

// Frames decoded so far (drains the RX frame buffer)
std::vector<DecodedPacket> hostReadFrames();

struct AX25Msg;
// TNC2 text -> AX25Msg through the firmware's own encoder/decoder (no modem). False if invalid.
bool hostParseTnc2(const std::string &tnc2, AX25Msg *msg);
// AX25Msg -> fields as text
DecodedPacket hostFromMsg(const AX25Msg &msg);

// Same TNC2 text for a decoded packet as the original input (for comparisons)
std::string hostTnc2(const DecodedPacket &p);
