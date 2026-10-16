## Arduino core for the ESP32, ESP32-C3, ESP32-C6 and ESP32-S3

# ESP32APRS Audio(Speaker/Mic) Project

ESP32APRS Audio is a Internet Gateway(IGate)/Digital Repeater(DiGi)/Tracker/Weather(WX)/Telemetry(TLM) with AFSK/GFSK TNC Built in that is implemented for Espressif ESP32 processor.
 

## About this fork

This is a fork of [nakhonthai/ESP32APRS_Audio](https://github.com/nakhonthai/ESP32APRS_Audio) by HS5TQA,
maintained by Jonatan, LU6EWB. Its goal is a station that can run unattended for years (for example a digipeater
or iGate on a hilltop), at least as reliable as a dedicated TNC such as the Kantronics KPC-3+.

- **Based on:** upstream firmware V1.8, commit `9a41b72` (2026-07-12).
- **What changed:** see [Changes in this fork](#changes-in-this-fork) below.
- The original author's work and credits are kept.

## Feature

* Supported hardware: ESP32DR Simple,ESP32DR,D.I.Y Other
* Supported RF Module: SA8x8/FRS VHF/UHF/350 model
* Support APRS internet gateway (IGATE)
* Support APRS digital repeater (DIGI)
* Support APRS tracker (TRACKER)
* Support APRS Weather (WX)
* Support APRS telemetry (TLM)
* Support APRS Message (MSG)
* Support GNSS External mod select UART0-2 and TCP Client
* Support TNC External mod select UART0-2 and Yaesu packet
* Support APRS IGATE/DIGI/WX/Telemetry with fix position for move position from GNSS
* Using ESP-Arduino development on Visual studio code + Platform IO
* Implementing software modem, decoding and encoding
* Support monitor display information and statistices
* Support Wi-Fi multi station or WiFi Access point
* support Web Service config and control system
* support filter packet rx/tx on igate,digi,display
* support audio filter BPF,HPF
* support VPN wireguard
* support global time zone
* support web service auth login
* support BLUETOOTH SPP/BLE
* support PPPoS (PPP Over Serial GSM network)
* support MQTT
* support AT-Command config/ctl by channel UART,MSG,Bluetooth
* support Automatic REBOOT by interval time
* display received and transmit packet on the LED and display OLED or led color strip
* Multiple modems: credit coding from project [vp-digi](https://github.com/sq8vps/vp-digi)
  * 1200bps AFSK Bell 202 1200/2200Hz (VHF standard)
  * 300bps AFSK Bell 103 1600/1800Hz (HF standard)
  * 9600bps GFSK G3RUH (For ESP32-S3)
  * 1200bps AFSK V.23
* Analog-digital busy channel detection (data carrier detection)
* AX.25 coder/decoder
* FX.25 (AX.25 with error correction) coder/decoder, fully compatible with [Direwolf](https://github.com/wb2osz/direwolf) and [UZ7HO Soundmodem](http://uz7.ho.ua/packetradio.htm)
* APRS objects: up to 5 objects of the station (repeaters, events, places), with start-up decay and kill (this fork)
* Web interface rebuilt as a small offline app with a JSON API; works on phones (this fork)
* Task supervisor: restarts the station if a task, the RX audio or the main loop stops, and shows why (this fork)

## Hardware screen short
![esp32c3aprs_radio](image/ESP32C3APRS_Radio.jpg) ![esp32c3aprs_oled](image/ESP32C3APRS_OLED.jpg)
![esp32dr_simple](image/ESP32DR_Simple_Test.png) ![esp32dr_sa868](image/ESP32DR_SA868_2.png)
![esp32dr_sa868_pcb](doc/ESP32DR_SA868/ESP32DR_SA868_Block.png)

## Hardware mod

**SQL (squelch) pin.** The firmware samples the receive audio only while the radio's squelch is open, read on
*SQL GPIO* (Modules page, Radio wiring). If the radio's squelch output is **not wired** to the ESP32 (for example
the ESP32DR Simple circuit), set **SQL GPIO = -1**: the station then listens all the time and finds packets by the
audio level. With the pin set but not wired, the firmware reads a floating input and may never decode anything.

| Board | SQL GPIO |
|---|---|
| ESP32DR Simple (external radio by cable) | `-1` (not wired) |
| ESP32DR SA868 | `33`, active LOW (the module's AUDIO_ON output) |
| Other radio with its squelch output wired | the GPIO it is wired to |

![SQL GPIO set to -1 on the Modules page](docs/images/ui-sql-simple.png)

## Web interface

The station is set up and watched from a web browser (computer or phone). Screenshots of the current interface,
taken on an ESP32DR SA868 board receiving the local APRS network:

**Dashboard** - last heard stations with their comment/data, distance and audio level, plus statistics, radio, network and system status.
![Dashboard](docs/images/ui-dashboard.png)

**TNC2 terminal** - every packet decoded on RF, with the RX audio level meter used to set the receive level.
![TNC2 terminal](docs/images/ui-terminal.png)

**Radio** - frequency, power and module settings.
![Radio](docs/images/ui-radio.png)

**iGate** - station, APRS-IS server, gateway rules and position.
![iGate](docs/images/ui-igate.png)

**Objects** - up to 5 objects of the station (repeaters, events, places).
![Objects](docs/images/ui-objects.png)

**Modules** - how the hardware is wired (radio GPIOs, buses, GNSS, ports).
![Modules](docs/images/ui-modules.png)

Decoding performance (WA8LMF TNC Test CD and Dire Wolf comparison) is in [Testing](#testing).

## ESP32DR_SA868
Share project [here](https://oshwlab.com/APRSTH/esp32sa818) \
Schematic [here](doc/ESP32DR_SA868/ESP32DR_SA868_sch.pdf) \
PCB Gerber [here](doc/ESP32DR_SA868/ESP32DR_SA868_Gerber.zip)

**RX audio level.** The board has two trimpots: **SPK** (receive audio from the SA868 to the ESP32 ADC) and **MIC**
(transmit audio, i.e. deviation). Set the receive level with **SPK** only: open *TNC2 terminal* and adjust until
packets show **-10 to -20 dBV** (green on the meter). Above -10 dBV the ADC clips and packets are lost; the decoder
needs at least 10 mV. Do not touch **MIC** to fix reception.

## ESP32DR Simple

![esp32dr_simple_3d](image/ESP32DR_Simple_Model.png)

ESP32DR Simple Circut is small interface board for connecting to a transceiver.

* PCB size is 64x58mm
* PCB Single size
* RJ11 6 Pin out to Radio

### Fix Audio input/output port
|ESP32 Core|ADC(Speaker)|DAC(Mic)|
|---|:---:|---|
|ESP32|GPIO36|GPIO26|
|ESP32-C3|GPIO0|GPIO1|
|ESP32-S3|GPIO1|GPIO2|

### Schematic
[![ESP32C3APRS](image/Schematic_ESPC3_APRS.png)](image/Schematic_ESPC3_APRS.png)

[![ESP32DR_Simple](image/ESP32DR_SimpleCircuit.png)](image/ESP32DR_SimpleCircuit.png)

### CAD data
 
The gerber data is [here](doc/Gerber_ESP32DR_Simple.zip)

The PCB film positive is [here](doc/PCB_Bottom.pdf)

The PCB film negative is [here](doc/PCB_Bottom_Invert.pdf)

The PCB Layout is [here](doc/PCB_Layout.pdf)

The ESP32DR_Simple Schematic PDF is [here](doc/ESP32DR_Simple_Schematic.pdf)

The ESP32C3APRS Schematic PDF is [here](doc/Schematic_ESPC3_APRS.pdf)

### BOM list  

|Reference|Value|Description|
|---|:---:|---|
|U1|ESP32 DEVKIT|DOIT ESP32 DEVKIT (ESP32 module)|
|RP2|1K|VR 3362W trimmer, audio output level (to the radio)|
|RP1|10K|VR 3362W trimmer, audio input level (from the radio)|
|RJ11|RJ11-6P6C|RJ11 modular jack, 6 pins|
|R13,R12,R11,R5,R3,R9|1K|R 1K 1/4W (colour code: brown black red)|
|R7,R18,R19|100R|R 100R 1/4W (colour code: brown black black)|
|R6,R2,R1|10K|R 10k 1/4W (colour code: brown black orange)|
|R4|3K|R 3k 1/4W (colour code: orange black red)|
|R10|33K|R 33K 1/4W (colour code: orange orange orange)|
|Q1|2N3904|NPN transistor (TO-92)|
|LED3|LED 3.5mm|Yellow, TX indicator|
|LED2|LED 3.5mm|Green, RX indicator|
|LED1|LED 3.5mm|Red, power indicator|
|L1|L or JMP|L Isolate or Jumper|
|C11|100uF/6.3V|Electrolytic capacitor|
|C4,C5|100nF|Multilayer ceramic capacitor|
|C6|470uF/10V|Electrolytic capacitor|
|C1,C3,C10|100nF or 0.1uF|Polypropylene capacitor|
|C2|10nF or 0.01uF|Polypropylene capacitor|
|D2,D1|1N4148|Diode, or a 0.01uF capacitor instead|

*R18 and R19 may be left out.  
*D2, D1 can be replaced by 10nF multilayer ceramic capacitors. 
*When connected to the external microphone input of a transceiver, change R4 to 100K.

The kit is sold by the original author on Shopee Thailand: [link](https://shopee.co.th/product/45191268/13373396785)

The DIY how-to (in Thai) is [here](doc/ESP32DR_DIY-Thai.pdf)

### Mounting drawing

![mounting](image/ESP32DR_SimpleLayout.png)

### Transceiver connection

Solder jumper is needed depending on a transceiver.

![ESP32DR_Pinout](image/RJ12Pinout.png)

|Manufacture|RJ11-1 (+VIN)|RJ11-2 (SPK)|RJ11-3 (PTT)|RJ11-4 (GND)|RJ11-5 (MIC)|RJ11-6 (SQL)|
|---|---|---|---|---|---|---|
|Alinco DR-135(DB9)|-|2|7|5|9|1|
|IC2200(RJ45)|-|SP|4|5|6|-|
|FT-2800(RJ11)|-|SP|1|3|2|-|
|HT Mic Cable|-|SPK|PTT|GND|MIC|-|

for Alinco DR-135(DB9)

![Alinco](image/ESP32DR_DR135.png)

for ICOM IC2200(RJ45)

![IC2200](image/ESP32DR_IC2200.png)

for Yaesu FT-2800(RJ11)

![FT2800](image/ESP32DR_FT2800.png)

for Handheld

![Handheld](image/ESP32DR_HT.png)

![HT-RX](image/ESP32DR_RxOnly.png)


## Getting started

### 1. Pick the file for your board
Every release on the [Releases](https://github.com/ltiisidii/ESP32APRS_Audio/releases) page has two files per board:
`<board>-full.bin` (first install by USB) and `<board>-update.bin` (updates from the web page).

| Your board | Files |
|---|---|
| ESP32DR SA868 or ESP32DR Simple (ESP32 DevKit v1) with a 1.3" SH1106 OLED | `esp32-sh1106` |
| ESP32 DevKit v1 with a 0.96" SSD1306 OLED | `esp32-ssd1306` |
| ESP32 DevKit v1 without a display | `esp32-nodisp` |
| ESP32-C3 with an SH1106 / SSD1306 OLED, or without a display | `esp32c3-sh1106`, `esp32c3-ssd1306`, `esp32c3-nodisp` |
| ESP32-S3 DevKitC (8 MB flash) with an SH1106 OLED | `esp32s3-sh1106` |
| ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM) | `esp32s3-N16R8` |
| LilyGO T-TWR | `TTGO-TWR` |
| ESP32-C6 | `esp32c6` |
| `esp32-NOOTA`, `esp32c3-NOOTA`, `esp32s3-NOOTA` | Same boards, one large firmware slot instead of two: no updates from the web page and no automatic return to the previous firmware. Updates only by USB. Use them only if you know you need them. |

### 2. First install (USB)
Use this for a new board, a board with another firmware, or a board that does not start any more.
Connect the board by USB and write `<board>-full.bin` at address **0x0**. It holds everything (bootloader,
partition table and firmware), so there is only one file to write.

- In the browser (Chrome or Edge): open [ESP Tool](https://espressif.github.io/esptool-js/), *Connect*, choose
  the port, add the file at address `0x0` and press *Program*.
- Or from the command line:
  `esptool.py write_flash 0x0 esp32-sh1106-full.bin`
- Or with the Espressif [Flash Download Tool](https://www.espressif.com/en/support/download/other-tools): one file,
  `<board>-full.bin`, at `0x0`.

If the port does not answer, hold the **BOOT** button (GPIO0 to GND) while it connects.
Settings already stored on the board are kept. To start clean, erase the flash first (`esptool.py erase_flash`,
or *Erase Flash* in ESP Tool).

### 3. First setup
1. Join the WiFi access point **ESP32APRS_Audio** (password `esp32aprs`) and open http://192.168.4.1.
   The web login is `admin` / `admin`.
2. **Change both passwords**: the web login on the **System** page and the access point on the **WiFi** page.
3. **WiFi**: add your network under *Client*, so the station reaches the Internet (APRS-IS, time).
4. **Radio**: frequency (144.390 MHz in most of the Americas, 144.800 MHz in Europe), and check the audio levels.
5. Turn on what the station does: **iGate** (callsign with SSID and server; the APRS-IS passcode is computed by the station), **Digipeater**, **Tracker**,
   **Weather**, **Telemetry**, **Objects**. Each page explains its settings.
6. Watch the **Dashboard**: the last heard list, the audio level of each packet and the counters show whether
   it receives well.

The **BOOT** button: a short press shows the next OLED screen. Holding it more than 2 s does the action of the
screen shown (STATISTIC: send the tracker position now, only with the tracker on; WiFi: WiFi off for 10 minutes,
or back on; SYSTEM: restart). Holding it at the end of the 3-second countdown at power-on restores the factory
defaults.

### 4. Updating
On the **About** page, *Firmware update*: choose `<board>-update.bin` and *Upload and install*. The settings are
kept. The new firmware must run well for 2 minutes; if it fails to start or keeps restarting, the board goes back
to the previous firmware by itself. Each release lists its changes; versions can be skipped (1.8b to 1.8d is fine).

### 5. Coming from the original firmware (nakhonthai V1.8)
This fork keeps the same flash layout and the same settings file (`/default.cfg`), so you can update from the
original firmware's web update page with `<board>-update.bin` and keep your settings. Settings that did not exist
in the original (APRS objects, KISS over TCP, ...) start with their defaults. The web interface is new: old
bookmarks to its pages no longer work, start from http://&lt;station address&gt;/.

### 6. Going back
- To a previous version of this fork: install its `<board>-update.bin` from the About page.
- To the original firmware: install the original `.bin` the same way (or by USB). Your settings stay; the ones
  added by this fork are ignored.

## PlatformIO Quick Start

1. Install [Visual Studio Code](https://code.visualstudio.com/) and [Python](https://www.python.org/)
2. Search for the `PlatformIO` plugin in the `VisualStudioCode` extension and install it.
3. After the installation is complete, you need to restart `VisualStudioCode`
4. After restarting `VisualStudioCode`, select `File` in the upper left corner of `VisualStudioCode` -> `Open Folder` -> select the `ESP32APRS` directory
5. Click on the `platformio.ini` file, and in the `platformio` column, cancel the sample line that needs to be used, please make sure that only one line is valid
6. Click the (✔) symbol in the lower left corner to compile
7. Connect the board to the computer USB
8. Click (→) to upload firmware and reboot again
9. After reboot display monitor and reconfig

## Testing

This fork has two levels of testing:

- **Automated tests on a PC (no hardware)** — [test/host/README.md](test/host/README.md).
  The firmware's real AX.25, AFSK/FX.25 modem, configuration and digipeater code is compiled for
  a PC and run with AddressSanitizer and UndefinedBehaviorSanitizer: modem loopback at
  1200/300/9600 baud, decode rate vs. noise, configuration save with simulated power cuts,
  digipeater path handling. Run them with Docker Desktop:
  ```powershell
  powershell -File test\host\run.ps1
  ```
  The same folder has `wav_decode`, which runs audio recordings (e.g. the WA8LMF TNC Test CD)
  through the firmware's demodulator, and `interop.ps1`, which checks every modem mode (1200, 300,
  9600, V.23, FX.25) against [Dire Wolf](https://github.com/wb2osz/direwolf) in both directions.

  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/test-results-dark.png">
    <img alt="Bar charts: frames decoded by the ESP32APRS_Audio firmware and by Dire Wolf 1.6. Packets from Dire Wolf with rising noise, out of 100: 1200 AFSK 75 vs 67, 300 AFSK 66 vs 68, 9600 G3RUH 44 vs 61, 1200 V.23 80 vs 77, 1200 FX.25 97 vs 76. WA8LMF TNC Test CD: track 1 1006 vs 993, track 2 997 vs 988." src="docs/images/test-results-light.png" width="720">
  </picture>
- **Hardware test plan** — [docs/test-plan.md](docs/test-plan.md).
  Decoding (WA8LMF TNC Test CD), long-run stability, stress and recovery, and physical
  environment tests, to qualify a station for unattended operation and compare it with a
  dedicated TNC such as the Kantronics KPC-3+.

All 13 PlatformIO environments in `platformio.ini` are expected to build.

## Changes in this fork

Changes made to the original V1.8 firmware (commit `9a41b72`), grouped by topic. Details are in the git history.

**Stability (unattended operation)**
- Task supervisor: restarts the station when a critical task, the RX audio sampling or the main loop stops, and
  keeps the reason across the restart (shown on the dashboard).
- OTA updates are confirmed only after 2 minutes of healthy running; otherwise the previous firmware comes back.
- Fixed hangs: dead RX after a WiFi reconnect, stuck PTT (PTT time-out), APRS-IS connection that never recovered.
- Fixed the `millis()` wrap at 49.7 days that flooded beacons and telemetry.
- Configuration saved atomically with a backup copy: a power cut while saving no longer loses the settings.
- Data shared between tasks protected; buffers fed from RF and APRS-IS bounded; heap leak in the gateway ping fixed.
- RX in real time: demodulator priority, larger ADC buffer, no sample overwritten when the buffer is full.
- Web server hardened: survives bursts of parallel requests and refuses new work when memory is low instead of crashing.
- ESP32 with the PTT on GPIO32 (ESP32DR SA868): configuring the PTT also turned GPIO0 (BOOT) into an output held
  low, so the long-press actions fired by themselves and the main loop froze. Fixed.
- SA868 deaf after a firmware update or a software restart: the board now always starts with a full reset, so the
  module receives again without a power cycle.
- APRS-IS no longer breaks the heap into small pieces (the web stopped answering after hours with a busy filter);
  the supervisor restarts the station if the largest free block stays too small, and the web shows it.
- Lower memory peak when web pages load.
- BOOT button safe for an unattended station: the long press restarts instead of powering off, turns WiFi off for
  10 minutes only, and sends no manual beacon when the tracker is off.

**Radio and modem**
- Modem, AX.25 and parser bugs fixed (found with the host tests and static analysis).
- Digipeater: duplicate suppression, traceable WIDEn-N, clearer PATH list.
- SA868: every setup command is checked and retried; module RSSI and RX audio level shown on the web.
- APRS objects (up to 5) on the new Objects page.

**Web interface**
- New interface (JSON API + small offline app) replacing the old pages and jQuery, with every setting kept.
- Dashboard with last heard (comment/data column, aprs.fi links, audio level colours), TNC2 terminal with an audio
  level meter, GPS and messages; a "session expired" notice instead of a surprise login box.
- OLED: screens rotate by themselves (configurable), showing only the screens that apply to the station.
- Statistics: "Packet TX (radio)" counts only RF transmissions; packets sent to APRS-IS are counted apart
  ("Sent to Internet", "RF→INET").

**Defaults**
- Thailand-specific defaults replaced by neutral ones (worldwide APRS-IS server, `pool.ntp.org`, generic AP name).

**Code and build**
- Every comment in the firmware sources in English.
- GitHub Actions builds the 13 firmwares on every change; a version tag creates a draft release with the files.

**Testing**
- Host tests that run the real firmware code on a PC (AddressSanitizer/UBSan), interoperability tests against
  Dire Wolf in every modem mode, WA8LMF TNC Test CD results and a hardware test plan. All 13 environments build.

**Releases**
- **1.8b**: first release of this fork (stability, new web interface, objects, SA868 support).
- **1.8c**: SA868 receives after updates and restarts, APRS-IS heap fragmentation fixed, safe BOOT button, radio
  and Internet TX counted apart.
- **1.8d**: KISS over TCP (off by default, RX only unless TX is allowed), Internet -> RF rules with a log-only test,
  FX.25 counter, iGate uploads no longer counted as radio TX, path column and About page fixes.

## APRS Server service

- Default APRS-IS server: `rotate.aprs2.net:14580` (the worldwide tier-2 network). Any server can be set on the iGate page.
- The original author also runs APRS servers in Thailand: T2THAI at [aprs.dprns.com:14580](http://aprs.dprns.com:14501),
  CBAPRS at [aprs.dprns.com:24580](http://aprs.dprns.com:24501), ampr host [aprs.hs5tqa.ampr.org:14580](http://aprs.hs5tqa.ampr.org:14501),
  map at [aprs.dprns.com](http://aprs.dprns.com).

## Developer/Support Information

- Author:	Mr.Somkiat Nakhonthai
- Callsign:	HS5TQA,Atten,Nakhonthai
- Country:	Bangkok,Thailand
- Github:	[https://github.com/nakhonthai](https://github.com/nakhonthai)
- Youtube:	[https://www.youtube.com/@HS5TQA](https://www.youtube.com/@HS5TQA)
- TikTok:   [https://www.tiktok.com/@hs5tqa](https://www.tiktok.com/@hs5tqa)
- Facebook:	[https://www.facebook.com/atten](https://www.facebook.com/atten)
- Telegram: [https://t.me/APRSTH](https://t.me/APRSTH)
- TelegramID: @HS5TQA
- WeChatID: HS5TQA

## Donate

To support the development of ESP32APRS you can make us a donation using [github sponsors](https://github.com/sponsors/nakhonthai). \
If you want to donate some hardware to facilitate APRS porting and development, [contact us](https://www.facebook.com/atten). \
<a href="https://www.paypal.me/0hs5tqa0"><img src="https://github.com/nakhonthai/ESP32IGate/raw/master/blue.svg" height="40"></a> 


## Credits & Reference

- ESP32APRS_Audio original firmware and hardware by HS5TQA (Somkiat Nakhonthai), see above

- ESP32TNC project by amedes [ESP32TNC](https://github.com/amedes/ESP32TNC)
- APRS Library by markqvist [LibAPRS](https://github.com/markqvist/LibAPRS)
- vp-digi project by sq8vps [vp-digi](https://github.com/sq8vps/vp-digi)

## Note
This project works with APRS text (TNC2 raw) only; it does not support a null byte (0x00) inside a packet.
