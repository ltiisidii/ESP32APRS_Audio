# Web UI migration checklist

The web interface is being moved from HTML built in C++ (`src/webservice.cpp`, jQuery) to a static
app in `web/` plus a JSON API (`src/webapi.cpp`). The goal is a technology change only: every item the
old interface shows or lets you change must exist in the new one before the old code is deleted.

The old interface stays reachable at `/classic` until the last stage. Each row below is checked on
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

## Stage 3 and later

Radio, iGate, Digipeater, Tracker, WiFi (stage 3); Weather, Telemetry, Sensors, Messages, MQTT, VPN,
Modules, System, Files, About (stage 4). A table like the ones above is added for each page when it
is migrated.
