# Test plan: does it reach the level of a KPC-3+?

Goal: show with measurements, not with code, that an ESP32APRS_Audio station can be left alone on
a hilltop without maintenance, with reliability comparable to a dedicated TNC proven over years
(Kantronics KPC-3+ or similar).

Four things are evaluated:

1. **Decoding**: does it hear as many packets as a KPC-3+?
2. **Long run**: does it run for weeks without degrading or hanging?
3. **Stress**: does it recover by itself from power cuts, WiFi, Internet and transmission problems?
4. **Physical environment**: does it withstand its own RF, the site's power supply and temperature?

Fill in the tables of each section. If a test fails, write down date, time and the matching log
before repeating it.

---

## 0. Before the hardware: automated tests on a PC

Run the host tests first ([test/host/README.md](../test/host/README.md)). They compile the
firmware's real modem, AX.25, configuration and digipeater code for a PC and check it with
AddressSanitizer/UBSan in a couple of minutes:

```powershell
powershell -File test\host\run.ps1
```

The result must end with `0 failures`. Then check interoperability with Dire Wolf in every modem
mode (`powershell -File test\host\interop.ps1`): every TX row must read 100/100. Hardware testing
only makes sense on a build that passes both.

---

## 0.1 Preparation

### Test firmware (with logs)

The normal firmware is built with `CORE_DEBUG_LEVEL=0`, which **removes every log message**,
including the `HEALTH` line and the supervisor's restart reason. For testing, build a firmware with
level 3 (info, warning and error):

```powershell
$env:PLATFORMIO_BUILD_FLAGS = "-DCORE_DEBUG_LEVEL=3"
pio run -e TTGO-TWR -t upload        # change the environment to match the hardware
Remove-Item Env:PLATFORMIO_BUILD_FLAGS
```

When testing is over, flash the normal firmware again (without the variable) for the site.

### Capturing the log

Keep a PC connected over USB for the whole test, saving the log with date and time:

```powershell
pio device monitor -e TTGO-TWR --filter time --filter log2file --filter esp32_exception_decoder
```

The `platformio-device-monitor-*.log` file is written to the project folder. If the unit restarts
because of a crash, the decoder prints the backtrace: keep it.

### What to look for in the log

| Message | Meaning |
|---|---|
| `HEALTH up ... heap ... min ... maxblk ... adc ... drop ...` | Every 10 min: uptime (s), free heap, lowest free heap ever, largest free block, ADC sample counter, dropped audio samples |
| `stack free <task> N bytes` | Stack margin of each task (lowest ever) |
| `SUPERVISOR: last restart was forced: ...` | At boot: the previous restart was forced by the supervisor (and why) |
| `SUPERVISOR: ... restarting` | The supervisor found a hung task or a stopped ADC |
| `PTT time-out` | The TX time-out cut a transmission that did not end |
| `APRS-IS no data for 120 s, reconnecting` | Zombie APRS-IS connection detected |
| `WiFi: reconnected after N attempts` | WiFi recovery |
| `Guru Meditation` / `abort()` / `Backtrace` | Crash: **keep the backtrace** |

### Reference

Ideally, borrow a **KPC-3+ (or another trusted TNC)** and measure it with the same setup.
Otherwise, compare with published results (see 1.5).

---

## 1. Decoding (WA8LMF TNC Test CD)

This is the standard test radio amateurs use to compare TNCs: real 1200 baud APRS traffic
recordings, always the same, so results can be compared.

### 1.1 Material

- **WA8LMF TNC Test CD** (WAV tracks): <http://wa8lmf.net/TNCtest/>
  - Track 1: 40 min of real Los Angeles traffic, flat audio.
  - Track 2: the same traffic with de-emphasized audio (as it comes out of a radio speaker).
- A PC with a sound card to play them.

### 1.2 First, the demodulator alone (no hardware)

Run the tracks through the firmware's demodulator on the PC with `wav_decode`
([test/host/README.md](../test/host/README.md)):

```powershell
docker run --rm -v "${PWD}:/src" -w /src/test/host gcc:13 make wav WAV=/src/track1.wav ARGS="1200 flat"
docker run --rm -v "${PWD}:/src" -w /src/test/host gcc:13 make wav WAV=/src/track2.wav ARGS="1200"
```

This measures the demodulator without the ESP32's ADC. The difference between this number and the
one measured on the hardware (1.4) is what the analog front-end (ADC noise, levels) costs.

### 1.3 Feeding the audio to the hardware

- **Boards with an external radio** (audio into the ADC): from the sound card output to the
  ESP32 audio input, with an attenuator/potentiometer to set the level.
- **TTGO-TWR** (built-in SA868 radio, no audio input): transmit the track with **another radio**
  on the receive frequency, at low power, into a dummy load or with the antenna far away, so the
  TTGO receives it over RF. Adjust until the signal arrives clean and strong.

### 1.4 Procedure

1. Write down the **RADIO RX** counter of the web dashboard (STATISTICS section) before starting.
2. Play the whole track without touching anything.
3. Write down the counter at the end. Decoded packets = end − start.
4. Repeat at three audio levels (−6 dB, normal, +6 dB) to see how sensitive it is to level
   adjustment. A good TNC decodes well over a wide range.
5. Repeat the normal level **with the web page open and reloading it** during the whole track.
   The result must be practically the same as without the web page. At the end, check that
   `drop` in `HEALTH` is 0.

### 1.5 Table

| Unit | Track | Level | Decoded packets | Notes |
|---|---|---|---|---|
| PC (`wav_decode`) | 1 | — | | |
| PC (`wav_decode`) | 2 | — | | |
| ESP32 | 1 | −6 dB | | |
| ESP32 | 1 | normal | | |
| ESP32 | 1 | +6 dB | | |
| ESP32 | 1 | normal + web | | |
| ESP32 | 2 | normal | | |
| KPC-3+ (reference) | 1 | normal | | |
| KPC-3+ (reference) | 2 | normal | | |

Published results to compare with (other TNCs and software demodulators): "A Better APRS Packet
Demodulator, Part 1, 1200 baud" by Dire Wolf (WB2OSZ), in
<https://github.com/wb2osz/direwolf/tree/master/doc>.

**Pass**: equal to or better than the KPC-3+ measured with the same setup, and the result with the
web page loaded no more than 1–2 % below the one without it.

---

## 2. Long run

### 2.1 Setup

The unit **as it will be on the site**: same radio, antenna (or load), power supply and
configuration (iGate, digi, beacons), on a channel with real traffic. At least **72 hours**;
ideally **14 days**.

### 2.2 What to record (once a day)

| Day | Uptime (s) | heap | min | maxblk | drop | Lowest stack free (task) | Restarts | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | | | | | | | | |
| 2 | | | | | | | | |
| 3 | | | | | | | | |

### 2.3 How to read it

- **Uptime** always increases. If it drops back to small values there was a restart: find the
  cause in the log.
- **heap** and **min** must settle in the first hours. If they go down day after day there is a
  memory leak (the unit would end up restarting because of low memory).
- **maxblk** (largest free block) stable. If it falls a lot while `heap` holds, memory is
  fragmenting.
- **drop** at 0 or close: if it grows, the demodulator cannot keep up and packets are lost.
- **stack free** of every task: none below ~500 bytes.

**Pass**: zero unplanned restarts in 72 h (and in 14 days for the long version), stable heap,
`drop` ≈ 0. With the daily restart enabled (`reset_timeout = 1440`) those restarts are planned and
do not count.

---

## 3. Stress and recovery

Repeat each test several times. What matters is that **the unit recovers by itself**, without
anyone touching it.

| # | Test | How | Pass criterion | Times | OK |
|---|---|---|---|---|---|
| 3.1 | Power cut while saving | Save the configuration from the web page and cut the power right after. | Boots with the previous or the new configuration, **never** with NOCALL or factory values | 20 | |
| 3.2 | Random power cut | Cut the power at any moment (RX, TX, web open). | Boots and runs normally | 20 | |
| 3.3 | Router down | Turn the router off for 6 min and back on. | While off, the `ESP32APRS_Audio` AP stays visible at `192.168.4.1`; when back, it reconnects by itself | 5 | |
| 3.4 | Backup router | With 2 networks configured, turn the main one off. | Moves to the secondary in ~2 min | 3 | |
| 3.5 | Router without ping | Block ICMP on the router (iGate active). | Does not disconnect; `Ping WiFi Fail, but APRS-IS is receiving: ignored` appears | 1 h | |
| 3.6 | Internet down | Cut the router's Internet uplink without turning WiFi off. | Reconnects to APRS-IS in < 3 min when it comes back | 5 | |
| 3.7 | Back-to-back TX | Force many transmissions (short beacons, digi on a busy channel, MANUAL TX). | None gets stuck; no `PTT time-out` | 50 TX | |
| 3.8 | Web during TX | Open Storage or save the Radio configuration while transmitting. | PTT is released; the TX ends or the time-out cuts it | 10 | |
| 3.9 | Heavy web use | Reload web pages non-stop for 1 h with traffic. | No restarts; `drop` ≈ 0 | 1 h | |
| 3.10 | OTA and rollback | Upload firmware through the web page and wait 2 min. | `new firmware confirmed` appears. Repeat with a firmware that restarts before 2 min: it goes back to the previous one by itself | 2 | |
| 3.11 | Long packets | Inject over KISS/APRS-IS packets of 300–500 characters and 8-hop paths. | No restarts; `TX dropped` in the log when a packet does not fit a frame | — | |

---

## 4. Physical environment

What a KPC-3+ solves with its hardware and metal case.

| # | Test | How | Pass criterion | OK |
|---|---|---|---|---|
| 4.1 | Own RF | Transmit 100 times at **full power** with the real antenna (at the distance it will have on site). | Zero restarts and zero hangs during or after TX | |
| 4.2 | Power supply | Vary the supply voltage over the whole range expected on site (e.g. 11–14.5 V on a 12 V battery system). | Works normally over the whole range, no restarts | |
| 4.3 | Voltage dips | Cause short dips (another device switching on from the same supply, the radio transmitting). | No brownout restarts | |
| 4.4 | Temperature | 24 h inside the final enclosure, in the sun or at the heat expected on site. | No restarts; decoding equal to test 1 | |
| 4.5 | Lightning / static | Check surge arrestors, grounding and filters on antenna and power. | Installation checked | |

---

## 5. Side by side in the field (optional, the most convincing)

If a KPC-3+ is available, leave **both units for a week** on the same site, with equivalent
antennas and the same digi configuration.

- Compare how many stations and packets each one hears (RADIO RX counter and "Last Heard").
- Compare digipeats on aprs.fi (the *raw packets* tab of each callsign).

**Pass**: the ESP32 hears and digipeats at least as much as the KPC-3+.

---

## 6. Final result

| Area | Result | Passed |
|---|---|---|
| 0. Automated tests | | |
| 1. Decoding | | |
| 2. Long run | | |
| 3. Stress | | |
| 4. Physical environment | | |
| 5. Side by side | | |

If everything passes: flash the **normal firmware** (without logs), enable the daily restart
(`reset_timeout = 1440`) as an extra safety net and only then take it to the site.
