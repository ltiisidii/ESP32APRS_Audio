# Web UI migration checklist

The web interface is being moved from HTML built in C++ (`src/webservice.cpp`, jQuery) to a static
app in `web/` plus a JSON API (`src/webapi.cpp`). The goal is a technology change only: every item the
old interface shows or lets you change must exist in the new one before the old code is deleted.

The old interface stayed reachable at `/classic` until the last stage. Each row below was checked on
the device before the migration of that page is merged.

## Stage 2: real-time pages

### Dashboard (old `/dashboard`, `/sysinfo`, `/sidebarInfo`)

Layout, in a fixed order: a status strip (uptime, RAM, CPU temperature, WiFi, APRS-IS, packets, mode
and link tags), the last heard table, then detail cards in even rows: Radio, Network, Statistics,
System, GPS, Bluetooth.

| Old item | New location | Data source |
| --- | --- | --- |
| Up time | Dashboard > status strip | `/api/info` `uptime` |
| RAM free / total | Dashboard > System (plus lowest free since boot) | `heap`, `heapSize`, `heapMin` |
| PSRAM free / total (boards with PSRAM) | Dashboard > System | `psram`, `psramSize` |
| Storage used / total | Dashboard > System | `fsUsed`, `fsTotal` |
| Battery voltage (when measured) | Dashboard > System | `vbat` |
| CPU MHz | Dashboard > System (chip line) | `cpuMhz` |
| CPU temperature | Dashboard > status strip | `temp` |
| Modes enabled: IGATE, DIGI, WX, TRACKER | Dashboard > status strip tags | `modes` |
| Network status: APRS-IS, VPN, PPPoS, MQTT, FX.25 | Dashboard > status strip tags | `net`, `radio.fx25` |
| Statistics: radio RX, packet RX, packet TX, RF2INET, INET2RF, DIGI, DROP/ERR | Dashboard > Statistics (plus digi duplicates dropped) | `stats` |
| GPS info: lat, lon, alt, satellites, link to the GPS page | Dashboard > GPS (only when GPS is enabled) | `gps` |
| Radio info: freq TX/RX, TX power (when RF module enabled), modem, FX.25 | Dashboard > Radio | `radio` |
| (new) RF module version as answered at boot (SA868/SR_FRS); "no answer" means the UART link failed | Dashboard > Radio | `radio.version` |
| APRS-IS server host and port (when iGate enabled) | Dashboard > Network (always shown) | `net.aprsHost`, `net.aprsPort` |
| WiFi mode, SSID, RSSI | Dashboard > Network (plus IP and AP clients) | `wifi` |
| Bluetooth master, name, mode (builds with Bluetooth) | Dashboard > Bluetooth | `bt` |
| Last heard table: time (with time zone), icon, callsign (object/item name), via last path, DX (km/bearing), packets, audio dBV | Dashboard > Last heard | SSE `/eventHeard` (same JSON as before) |
| Last heard sorting by time, callsign, DX, packets, audio | Click a column header (path also sortable) | in the browser |
| Last heard live updates | Same SSE stream; the full table is also sent when the page connects | `lastheard_events` |
| [RAW] link to the TNC2 monitor | Last heard title link | `#terminal` |
| Opening the dashboard pushes back the power-save standby | Every `/api/info` call (every 10 s while the dashboard is open) | `StandByTick` |

### TNC2 monitor (old `/tnc2`)

| Old item | New location |
| --- | --- |
| RX audio VU meter, -40 to 0 dBV with four colour bands | TNC2 terminal > RX audio level (plain CSS, no Highcharts) |
| Terminal line per packet: date, Vrms, dBV, then the TNC2 text | TNC2 terminal > Monitor, same format |
| Live feed from WebSocket `ws://<device>:81/ws` | Same socket, reconnects by itself |
| Auto-scroll to the newest packet | Kept; stops while you scroll up to read |
| (new) Clear and save the log as a text file | Buttons under the monitor |

The old page loaded jQuery 2.1.4 and Highcharts from the internet, so it did not work without an
internet connection. The new page needs nothing outside the device.

### GPS (old `/gnss`)

| Old item | New location |
| --- | --- |
| Enable, latitude, longitude, altitude, speed, course, HDOP, satellites, time | GPS > GNSS information (time shown as hh:mm:ss UTC) |
| Raw NMEA terminal | GPS > NMEA |
| Live feed from WebSocket `ws://<device>:81/ws_gnss` | Same socket, reconnects by itself |

Like the TNC2 page, the old GPS page loaded scripts from the internet; the new one does not.

### Security fixes made while migrating

- Text that comes from the air (callsigns, paths, object names, NMEA, TNC2 frames) is now always
  inserted as text, never as HTML. The old last heard table inserted it as HTML, so a crafted packet
  could run script in the browser of whoever had the dashboard open.

## Stage 3: main configuration pages

All five pages are drawn from field lists in `web/forms.js` and read `GET /api/config`. Save sends
only the changed keys to `POST /api/config`. Passwords come back masked and are kept unless retyped.
Each page also has: section tabs, an Enabled/Disabled badge, a warning before leaving with unsaved
changes, and an Undo button.

### What saving does (same as the old pages)

| Old page action | New behaviour |
| --- | --- |
| Radio: save, then re-program the RF module (`RF_MODULE`) | Done when any RF module field changed, after the HTTP answer |
| TNC: save, then re-init the modem (`afskSetModem`) | Done when modem, FX.25, de-emphasis, time slot or preamble changed |
| iGate: save, restart the beacon timers, drop APRS-IS (`aprsIsStop`) | Timers restart on any change; APRS-IS reconnects when callsign, SSID, server, port, filter or enable changed |
| Digi / Tracker: save, restart the beacon timers | Same |
| WiFi: save, set the WiFi TX power at once | Same; mode, AP and network changes need a reboot, and the page offers one |
| Callsign trimmed and upper-cased | Same (browser and firmware) |

### Radio (old `/radio`)

| Old item | New location / key |
| --- | --- |
| RF module enable, module type | Radio > RF module (`rfEnable`, `rfType`) |
| TX / RX frequency, limited to the module's band | Same, range follows the module type (`rfFreqTX`, `rfFreqRX`) |
| TX / RX CTCSS | Same (`rfToneTX`, `rfToneRX`) |
| Narrow / wide, TX power, volume 1-8, squelch 0-8 | Same (`rfBand`, `rfPwr`, `rfVolume`, `rfSql`) |
| Modem type (9600 only on ESP32-S3) | Radio > AFSK / TNC (`rfModem`, list from the firmware) |
| FX.25 mode, de-emphasis, TX time slot, preamble 100-1000 ms | Same (`fx25Mode`, `audioLPF`, `txTimeSlot`, `rfPreamble`) |

### iGate (old `/igate`)

| Old item | New location / key |
| --- | --- |
| Enable, callsign, SSID, symbol (with picker), item/object name, path | iGate > Station |
| Server host, port, filter | iGate > APRS-IS server |
| Comment, status text and interval | iGate > Text |
| RF2INET, INET2RF, time stamp | iGate > Gateway (`igateTime` is now saved; before it was lost on reboot) |
| RF2INET and INET2RF type filters (form "IGATE Filter") | iGate > Gateway (`rf2inetFilter`, `inet2rfFiltger`) |
| Beacon, interval, fixed/GPS location, send to RF / Internet, lat, lon, alt | iGate > Position |
| PHG text and calculator | iGate > PHG (same formula) |
| Telemetry interval and 5 channels (sensor, name, unit, precision, offset, EQNS a/b/c) | iGate > Telemetry (precision and offset still fill b and c) |

### Digipeater (old `/digi`)

| Old item | New location / key |
| --- | --- |
| Enable, auto enable when APRS-IS is down, callsign, SSID, symbol, path | Digipeater > Station |
| Repeat delay, repeat type filter | Digipeater > Repeating |
| Comment, status text and interval | Digipeater > Text |
| Beacon, interval, location, send to RF / Internet, lat, lon, alt, time stamp | Digipeater > Position |
| PHG, telemetry | Digipeater > PHG, Telemetry |

### Tracker (old `/tracker`)

| Old item | New location / key |
| --- | --- |
| Enable, callsign, SSID, symbol, item/object name, path | Tracker > Station |
| Comment, status text and interval | Tracker > Text |
| Interval, location, send to RF / Internet, lat, lon, alt, time stamp, compressed, Mic-E type, options (telemetry, altitude, audio request) | Tracker > Position |
| Smart beacon enable, moving / stopped symbols, high / low speed, slow / max / min interval, min angle | Tracker > Smart beacon |
| Telemetry | Tracker > Telemetry |

### WiFi (old `/wireless`)

| Old item | New location / key |
| --- | --- |
| Access point enable, SSID, password | WiFi > Access point (bit 1 of `WiFiMode`) |
| Client enable, WiFi TX power | WiFi > Client (bit 2 of `WiFiMode`, `WiFiPwr`) |
| 5 networks: enable, SSID, password | WiFi > Client (`WiFiSTA`) |
| Bluetooth: enable, name, PIN, mode, UUIDs (not on classic ESP32) | WiFi > Bluetooth (builds with Bluetooth only) |

The old WiFi page sent the stored passwords to the browser in the page source; the new one never does.
The page refuses to save with both access point and client disabled, and an access point password
shorter than 8 characters.

### Removed with this stage

`/radio`, `/igate`, `/digi`, `/tracker`, `/wireless` and the `/symbol` picker popup (about 3,650 lines).
The classic shell sends those tabs to the new pages.

## Stage 4a: System, About, Files

### System (old `/system`)

| Old item | New location / key | Behaviour |
| --- | --- | --- |
| Host name | System > General (`hostName`) | |
| Auto reboot (minutes, 0 = no) | System > General (`resetTimeout`) | |
| Local date/time "Time Update" | System > Clock: "Set to this computer's time" or a date picker | `POST /api/time` |
| NTP host, time zone (40 zones) | System > Clock (`ntpHost`, `timeZone`) | `configTime` re-run at once, as before |
| REBOOT | System > System control | Telemetry sequences reset as before |
| Factory Reset | System > System control, after typing RESET | Now saves the defaults and restarts; the old button only reset RAM and never answered |
| Load Default (reload `/default.cfg`) | System > System control > Reload | `POST /api/reload` |
| Web user and password | System > Web login (`httpUser`, `httpPass`) | Password masked, kept unless retyped |
| PATH user define 1-4 | System > User defined paths (`path`) | |
| Power save: enable, GPIO, active level, sleep interval, standby delay, mode A/B/C, wake events | System > Power save (`pwr*`) | |
| Log file events (builds with `LOG_FILE`; none today) | System > Log file (`logFile`) | |
| Display: enable, flip, TX/RX display, heading up, brightness, popup delay, sleep, RF/Internet popup, max distance, popup types | System > Display (`dsp*`), builds with a display only | TFT brightness applied at once, as before. "Heading up" is now saved (the old form never sent it) |

### About (old `/about`)

| Old item | New location |
| --- | --- |
| Hardware version, firmware version, RF module, chip model, revision, chip ID, flash, PSRAM, file system | About > System (`GET /api/about`) |
| Developer/support information (Mr. Somkiat Nakhonthai, HS5TQA) with all links | About > Credits, unchanged, plus the notice of this modified version (GPL v3 section 5a): modified fork by Jonatan, LU6EWB |
| WiFi status: mode and protocol, MAC, channel, TX power, SSID, IP, gateway, DNS | About > WiFi |
| PPPoS status: manufacturer, model, IMEI, IMSI, operator, RSSI, IP, gateway | About > PPPoS (builds with PPPoS) |
| Manual firmware update with progress | About > Firmware update (same `/update` endpoint) |
| Online OTA update and version check | About > Firmware update, only when the build sets `OTA_SERVER_URL` (off by default) |

### Files (old `/storage`)

| Old item | New location |
| --- | --- |
| Total and used space | Files > Storage |
| File list with size, download, delete | Files > Files (`GET /api/files`, `/api/files/get`, `/api/files/delete`) |
| Upload | Files > Upload (`POST /api/files/upload`) |
| Format (was commented out of the page, endpoint still open) | Files > Format, after typing FORMAT; the running settings are written back |

The old `/download`, `/delete`, `/format` and `/upload` endpoints had no login: anyone on the network could
download `default.cfg` with every password in it, delete files or format the flash. They are gone; the new
endpoints need the web login and accept plain file names only (no paths, no `..`).

### Removed with this stage

`/system`, `/about`, `/storage`, `/download`, `/delete`, `/format`, `/upload` (about 2,160 lines).
`/update`, `/ota_url` and `/check_version` stay: the new About page uses them.

## Stage 4b: Weather, Telemetry, Sensors, Modules

### Weather (old `/wx`)

| Old item | New location / key |
| --- | --- |
| Enable, callsign, SSID, object name, path, comment, time stamp | Weather > Station (`wx*`); the comment is limited to its real size (24), the old form allowed 50 and overflowed |
| Interval, fixed/GPS location, send to RF / Internet, lat, lon, alt | Weather > Position |
| 26 weather values: use, sensor channel, sample or average | Weather > Sensors (`wxSenEn`, `wxSenCH`, `wxSenAvg`) |

### Telemetry (old `/tlm`, system telemetry)

| Old item | New location / key |
| --- | --- |
| Enable, callsign, SSID, path, comment, info and data intervals, send to RF / Internet | Telemetry > Station (`tlm*`); the comment is now saved (`tlmComment`, it was lost on reboot) |
| Channels A1-A5: source, parameter, unit, EQNS a/b/c | Telemetry > Analog channels (`tlmDataCH`, `tlmPARM`, `tlmUNIT`, `tlmEQNS`) |
| Channels B1-B8: source, parameter, unit, active LOW/HIGH | Telemetry > Digital channels (`tlmBIT`) |

### Sensors (old `/sensor`)

| Old item | New location |
| --- | --- |
| Sensor monitor (value and unit of each sensor, read once at page load) | Sensors > Readings, refreshed every 5 s (`GET /api/sensors`, also the average) |
| 10 sensors: enable, type, name, unit, port, address/register/GPIO, sample and average time, EQNS | Sensors > Setup (`Sensor`, 11 values per sensor) |
| Type fills name and unit; port fills the usual address | Same |
| Saving re-initialises the sensors (`sensorInit`) | Same, after the HTTP answer |

### Modules (old `/mod`)

| Old item | New location / key |
| --- | --- |
| UART0, UART1: enable, RX, TX, RTS/DE, baudrate | Modules > UART0, UART1 (`uart0*`, `uart1*`) |
| 1-Wire: enable, GPIO | Modules > 1-Wire bus |
| RF GPIO: ADC attenuation and DC offset, module baudrate, RX, TX, PD, H/L, SQL, PTT with active levels | Modules > Radio wiring (`adc*`, `rf*`) |
| I2C 0 and I2C 1: enable, SDA, SCK, frequency | Modules > I2C 0, I2C 1 |
| Counter 0 and 1: enable, GPIO, active level | Modules > Counter 0, Counter 1. The old Counter 1 switch wrote Counter 0's enable, so Counter 1 could never be turned on |
| GNSS: enable, port, AT command, TCP host and port | Modules > GNSS |
| Modbus: enable, port, address, DE GPIO | Modules > Modbus |
| External TNC: enable, port, mode | Modules > External TNC |
| AT command channels: MQTT, message, Bluetooth, UART port | Modules > AT command channels |
| PPPoS: enable, GNSS, NAPT, APN, PIN, port, baudrate, RX, TX, reset GPIO, level and delay | Modules > Cellular modem (builds with PPPoS); the PIN is masked; the modem's UART is still turned off for other uses |
| Baudrate list | Same list; "2880" corrected to 28800 |

Wiring changes are saved at once and take effect after a reboot; the page offers one.

### Removed with this stage

`/wx`, `/tlm`, `/sensor`, `/mod` (about 3,470 lines). `webservice.cpp` is down to about 2,160 lines from 12,400.

## Stage 4c: Messages, VPN, MQTT and the end of the old interface

### Messages (old `/msg`)

| Old item | New location |
| --- | --- |
| Chat table: time, callsign, message, ACK state, message ID | Messages: one conversation per station, bubbles with time, ID and state (acknowledged, try n of N, not acknowledged, received) |
| Live updates on `/eventMsg` (an HTML table) | Same stream, now JSON (`[{t, call, id, text, ack, rx}]`); also `GET /api/messages` |
| Send: TO and MSG fields | Composer with a 67 character counter; `POST /api/messages/send` checks the callsign and refuses the characters APRS reserves (`\|`, `~`, `{`) |
| Settings: enable, my callsign, RF / Internet, encryption, AES key, retries, retry interval, path | Messages > Message settings (`msg*`); the AES key is masked |

The old chat inserted received text as HTML, so a message from the air could run script in the browser.
The new page always shows it as text.

### VPN (old `/vpn`)

| Old item | New location / key |
| --- | --- |
| Enable, server address and port, local address, netmask, gateway, server public key, client private key | VPN (`vpn*`); the private key is masked (the old page sent it in the page source); a reboot is offered after saving |

### MQTT

No build defines `MQTT`, and the MQTT flag constants are missing from the sources, so the old MQTT page
was never compiled into any firmware. It is not migrated; its settings stay in the configuration file.

### Removed with this stage

`/vpn`, `/msg`, the MQTT page code, the old shell `/classic`, `/style.css`, `/jquery-3.7.1.js`
(`include/jquery_min_js.h`, 189 KB of source), `/logout` and `/default`. `GET /default` had no login
and reset the running configuration to defaults; the factory reset is now only on System, with login and
confirmation. `webservice.cpp` keeps the live data streams (last heard, messages, TNC2 and GNSS
WebSockets) and the firmware update endpoints: about 740 lines, from 12,400.

## Result

Every page of the old interface has a place in the new one (tables above). All old pages and jQuery
are gone; the web app is about 29 KB gzipped and is served from flash.
