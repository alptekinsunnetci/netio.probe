# netio.probe
 
**A self-contained SNMP v2c temperature probe for the ESP8266 + DS18B20 — zero external libraries.**
 
`netio.probe` turns a NodeMCU (ESP8266) and a single DS18B20 sensor into a proper, monitorable network device. It reads temperature over 1-Wire and publishes it as a **real SNMP v2c agent** (hand-written BER/ASN.1 — `snmpwalk` / `snmpget` work out of the box), exposes a **modern, password-protected web management UI**, supports **browser-based OTA** firmware updates, and ships a **first-boot Wi-Fi captive portal** so it can be provisioned without recompiling.
 
As of **v1.2** it is also hardened and observable: a **source-IP ACL**, a **salted-hash admin password with brute-force lockout**, a **Prometheus `/metrics` endpoint**, and **remote syslog** for security and operational events — all still in one `.ino` with no libraries to install.
 
Everything depends only on the **ESP8266 Arduino Core** — no SNMP, OneWire, DallasTemperature, or crypto libraries required (SHA-256 uses the core's bundled BearSSL).
 
![platform](https://img.shields.io/badge/platform-ESP8266-blue)
![framework](https://img.shields.io/badge/framework-Arduino%20Core%203.x-teal)
![protocol](https://img.shields.io/badge/SNMP-v2c-success)
![metrics](https://img.shields.io/badge/metrics-Prometheus-orange)
![libraries](https://img.shields.io/badge/external%20libs-none-brightgreen)
![version](https://img.shields.io/badge/firmware-v1.2-informational)
 
---
 
## Table of contents
 
- [What's new in v1.2](#whats-new-in-v12)
- [Features](#features)
- [How it works](#how-it-works)
- [Hardware](#hardware)
- [Build & flash](#build--flash)
- [Upgrading from v1.1](#upgrading-from-v11)
- [First-time setup (Wi-Fi captive portal)](#first-time-setup-wi-fi-captive-portal)
- [Web management interface](#web-management-interface)
- [Access control (ACL)](#access-control-acl)
- [Monitoring & integration](#monitoring--integration)
- [OTA firmware updates](#ota-firmware-updates)
- [Factory reset](#factory-reset)
- [SNMP usage](#snmp-usage)
- [OID map](#oid-map)
- [Configuration & defaults](#configuration--defaults)
- [Security considerations](#security-considerations)
- [Limitations & roadmap](#limitations--roadmap)
- [License](#license)
---
 
## What's new in v1.2
 
v1.2 is a **security + observability + reliability** release. It is backward-compatible: flashing it over v1.1 **keeps your existing Wi-Fi and settings** (see [Upgrading from v1.1](#upgrading-from-v11)).
 
**Security**
- **Admin password is no longer stored in plaintext.** It is kept as a random **salt + SHA-256 hash** (BearSSL) in EEPROM; the firmware compares hashes in constant time.
- **Per-IP brute-force lockout.** After 5 failed logins from an address, that address is locked out for 60 seconds (HTTP `429`).
- **Source-IP ACL (CIDR).** Restrict both the SNMP agent **and** the web/metrics interface to a management subnet. Off by default (allow all).
- **Password-field leak fixed.** The setup portal and the management UI no longer pre-fill the admin password into the page source; the field is empty and means "leave unchanged".
**Observability**
- **Prometheus `/metrics` endpoint** — temperature, sensor state, RSSI, heap, uptime, and SNMP/read/error counters in text-exposition format. ACL-gated, no login (so a scraper can read it).
- **Remote syslog (RFC 5424 / UDP).** Emits boot, config-change, OTA, sensor fault/recovery, and **web auth-failure / ACL-deny** events to your log pipeline. The auth-failure events are easy to alert on (e.g. a Sigma rule).
**Reliability**
- **Seamless EEPROM migration** across firmware updates — settings are migrated, not wiped.
- **DS18B20 read hardening** — rejects an all-`0x00` / all-`0xFF` scratchpad (a stuck-bus fault that can otherwise pass the CRC and look like 0 °C), and logs sensor state transitions.
- **Watchdog feed + low-heap self-heal** — a sustained low-heap condition triggers a clean reboot.
- **Optional MD5 verification for web OTA** — paste the firmware's MD5 and the device refuses a corrupted image.
---
 
## Features
 
- **Real SNMP v2c agent** — hand-written BER/ASN.1 encoder/decoder. Supports `GET`, `GETNEXT`, and `GETBULK` (single-repetition), so `snmpwalk` and `snmpbulkwalk` terminate correctly. Read-only; the configured community is validated, and packets from outside the ACL or with a wrong community are dropped silently.
- **Standards-aligned MIB** — full SNMPv2-MIB *System* group plus the **Entity Sensor MIB** (RFC 3433) for the temperature reading, plus an enterprise subtree with diagnostics (heap, RSSI, uptime, counters).
- **Prometheus `/metrics`** — the same telemetry in a format your existing monitoring stack scrapes directly.
- **Remote syslog** — security and operational events to a central collector (VictoriaLogs, ClickHouse, rsyslog, …).
- **Hardened web UI** — HTTP Basic Auth backed by a **salted SHA-256** secret, **per-IP brute-force lockout**, and an optional **source-IP ACL**.
- **Robust DS18B20 driver** — standard 1-Wire timing with interrupts disabled only around the short reset/bit windows (so Wi-Fi ISRs can't corrupt a read), a **non-blocking** conversion state machine (the 750 ms conversion never stalls `loop()`), CRC8 validation, and stuck-bus detection.
- **First-boot Wi-Fi provisioning** — boots as an Access Point with a captive portal; pick your network from a live scan, no hardcoded credentials.
- **Browser OTA** — upload a compiled `.bin` from the web UI with a progress bar and optional MD5 check; classic IDE OTA (espota) also works.
- **Triple factory reset** — from the web UI, by shorting two pins on the board, or via the FLASH button.
- **Single file, no dependencies** — drops straight into the Arduino IDE; nothing to `lib install`.
---
 
## How it works
 
```
power on
  └─► load config from EEPROM (magic + XOR checksum)
        │     • if a v1.1 layout is found, MIGRATE it (Wi-Fi/settings preserved)
        │
        ├─ configured AND Wi-Fi connects within 20 s ─► RUN MODE
        │     • SNMP agent            UDP/161   (ACL-filtered)
        │     • Web management UI     TCP/80    (Basic Auth + ACL + lockout)
        │     • Prometheus metrics    /metrics  (ACL, no login)
        │     • Web OTA               /update   (auth + optional MD5)
        │     • IDE OTA (espota)      + mDNS
        │     • Remote syslog         UDP/514   (events)
        │
        └─ not configured OR connect fails ──────────► SETUP MODE
              • SoftAP "netio.probe-XXXX"  (WPA2)
              • captive portal at http://192.168.4.1/
              • live Wi-Fi scan, save → reboot into RUN
```
 
In RUN mode, if Wi-Fi stays down for more than 120 seconds the device reboots and falls back to the captive portal so it can be re-provisioned. Configuration is persisted in EEPROM as a single versioned struct guarded by a magic number and an XOR checksum; a watchdog and a low-heap guard reboot the device cleanly if it ever gets wedged.
 
---
 
## Hardware
 
### Bill of materials
 
| Qty | Part |
|----|------|
| 1 | ESP8266 board (NodeMCU 1.0 / ESP-12E) |
| 1 | DS18B20 temperature sensor |
| 1 | 4.7 kΩ resistor (1-Wire pull-up) |
| — | jumper wire (for factory-reset header, optional) |
 
### Wiring
 
| DS18B20 | ESP8266 (NodeMCU) |
|---------|-------------------|
| VDD | 3V3 |
| GND | GND |
| DATA | **GPIO4 (D2)** |
 
> ⚠️ **An external 4.7 kΩ pull-up between DATA and 3V3 is required.** The ESP8266's internal pull-up (~30 kΩ+) is too weak for reliable 1-Wire timing, especially with Wi-Fi active. If you absolutely must rely on the internal pull-up, set `USE_INTERNAL_PULLUP` to `1` in the sketch — but this is **not recommended**.
 
### Factory-reset header
 
| Function | Pin |
|----------|-----|
| Factory reset jumper | **GPIO14 (D5) ↔ GND** (short ~2 s) |
 
Shorting D5 to GND for ~2 seconds — at boot or at runtime — wipes all settings. The onboard FLASH button (GPIO0) held for 3 s does the same.
 
---
 
## Build & flash
 
### Prerequisites
 
- **Arduino IDE** (or `arduino-cli`)
- **ESP8266 Arduino Core 3.x** — add this Boards Manager URL, then install "esp8266 by ESP8266 Community":
  ```
  https://arduino.esp8266.com/stable/package_esp8266com_index.json
  ```
- Board: **NodeMCU 1.0 (ESP-12E Module)**
- **No libraries to install** — everything used is part of the core (`ESP8266WiFi`, `ESP8266WebServer`, `DNSServer`, `ESP8266mDNS`, `WiFiUdp`, `ArduinoOTA`, `EEPROM`, `Updater`, and BearSSL for SHA-256).
### ⚠️ One sketch = one folder
 
The Arduino build concatenates **every `.ino` file in the sketch folder** into a single program. Keep exactly one `.ino`, and its name must match the folder:
 
```
netio_probe/
└── netio_probe.ino     ← the only .ino in this folder
```
 
If you see a wall of `redefinition of '...'` / `multiple definition of '...'` errors, you have more than one `.ino` (or an old copy) in the folder. Move/delete the extras so only `netio_probe.ino` remains.
 
### Steps
 
1. Put `netio_probe.ino` alone in a folder named `netio_probe`.
2. Open it in the Arduino IDE — you should see a **single tab**.
3. Select board **NodeMCU 1.0 (ESP-12E Module)** and the correct COM port.
4. **Upload**. Open the Serial Monitor at **115200 baud** to watch boot logs.
> **Resource footprint (v1.2, NodeMCU 1.0):** ~33% of flash and ~46% of static RAM, leaving the upper flash half free for OTA. IRAM is the tightest segment (~92% used) — relevant only if you later add interrupt-heavy code.
 
---
 
## Upgrading from v1.1
 
Just flash v1.2 over v1.1 (USB or OTA). On the first boot the firmware detects the older EEPROM layout and **migrates it automatically**:
 
- Wi-Fi SSID/password, SNMP read community, `sysName` / `sysLocation` / `sysContact`, OTA password, and admin username are **preserved**.
- Your old (plaintext) admin password is **re-hashed** into the new salted-SHA-256 form — your existing password keeps working; nothing to re-enter.
- New settings (ACL, syslog) start **disabled** (allow-all / off), so behaviour is unchanged until you configure them.
No factory reset and no re-provisioning are needed. (A factory reset is still available if you *want* a clean slate.)
 
---
 
## First-time setup (Wi-Fi captive portal)
 
On first boot (or after a factory reset) the device starts an Access Point:
 
| | |
|---|---|
| **SSID** | `netio.probe-XXXX` (`XXXX` = chip-ID suffix) |
| **Password** | `snmpsetup` |
| **Portal URL** | `http://192.168.4.1/` |
 
1. Join the `netio.probe-XXXX` network. Most phones open the captive portal automatically; if not, browse to `http://192.168.4.1/`.
2. The page performs a **live Wi-Fi scan** — tap your network (signal strength and a lock icon are shown), enter the password.
3. Optionally expand **Advanced** to set the SNMP read community, `sysName`/`sysLocation`/`sysContact`, OTA password, and admin credentials.
4. **Save** → the device reboots and joins your network.
After connecting it is reachable at `http://netio-probe-<chip-id>.local/` (mDNS) or by its DHCP IP.
 
---
 
## Web management interface
 
Once on your network, browse to the device IP (or `netio-probe-<chip-id>.local`). The UI is protected by **HTTP Basic Auth** and, if configured, by the source-IP ACL.
 
| | |
|---|---|
| **Default username** | `admin` |
| **Default password** | `netioprobe` |
 
> 🔐 **Change these on first login.** The password is stored only as a salted hash. Both the setup portal and the management UI let you change credentials; leaving a password field blank keeps the current one.
 
The dashboard shows live telemetry (temperature, RSSI, uptime, free heap, SNMP request count, read/error counters) refreshed every few seconds via a `/status` JSON endpoint, and lets you:
 
- edit the **SNMP read community** (applied live — no reboot) and `sysName` / `sysLocation` / `sysContact`;
- set the **access-control CIDR**, **syslog server IP**, and **syslog port**;
- change **admin** credentials, the **OTA** (espota) password, and **Wi-Fi** credentials (changing Wi-Fi triggers a reboot);
- open the **firmware update** page;
- **reboot** or **factory reset** the device.
| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/` | GET | ✅ Basic + ACL | Management dashboard |
| `/status` | GET | ✅ Basic + ACL | Live telemetry (JSON) |
| `/save` | POST | ✅ Basic + ACL | Apply settings |
| `/update` | GET / POST | ✅ Basic + ACL | Web OTA page / upload |
| `/reboot` | POST | ✅ Basic + ACL | Restart |
| `/reset` | POST | ✅ Basic + ACL | Factory reset |
| `/metrics` | GET | 🔓 ACL only | Prometheus metrics |
 
---
 
## Access control (ACL)
 
The ACL restricts **who** can reach the device, by source IP, expressed as a single CIDR:
 
- Applies to the **SNMP agent** (out-of-range packets are dropped silently), the **web UI** (returns `403`), and **`/metrics`** (returns `403`).
- Set it in the management UI under **Security & Wi-Fi → Access list (CIDR)**.
- `0.0.0.0/0` or an empty value means **allow all** — this is the default, so the device is reachable from anywhere on first boot.
Examples:
 
| Value | Effect |
|-------|--------|
| `0.0.0.0/0` | allow all (default) |
| `192.168.10.0/24` | only the `192.168.10.x` management subnet |
| `10.0.5.20/32` | only a single host (e.g. your NMS / Prometheus server) |
 
> The ACL is an IP filter, not a substitute for encryption. Combine it with a trusted VLAN; SNMP v2c and HTTP Basic Auth are still cleartext on the wire (see [Security considerations](#security-considerations)).
 
---
 
## Monitoring & integration
 
### Prometheus (`/metrics`)
 
`/metrics` serves text-exposition metrics with **no login** (so a scraper can read it) but **subject to the ACL** — point the ACL at your Prometheus host to lock it down.
 
```yaml
# prometheus.yml
scrape_configs:
  - job_name: netio-probe
    metrics_path: /metrics
    static_configs:
      - targets: ['netio-probe-abcd.local:80']   # or the device IP
```
 
Exposed series:
 
| Metric | Type | Meaning |
|--------|------|---------|
| `netio_temp_celsius` | gauge | temperature in °C (omitted while the sensor is unavailable) |
| `netio_sensor_up` | gauge | `1` = sensor OK, `0` = unavailable |
| `netio_rssi_dbm` | gauge | Wi-Fi RSSI |
| `netio_free_heap_bytes` | gauge | free heap |
| `netio_uptime_seconds` | counter | seconds since boot |
| `netio_snmp_requests_total` | counter | SNMP requests served |
| `netio_sensor_reads_total` | counter | successful sensor reads |
| `netio_sensor_errors_total` | counter | failed sensor reads |
 
### Syslog (RFC 5424 / UDP)
 
Set a **syslog server IP** (and optionally a port; default `514`) in the management UI to stream events to a central collector. Messages use facility `local0`. Logged events include:
 
- `boot up ip=… host=….local`
- `config saved`
- `OTA basladi: …` / `OTA tamam: … byte` / `OTA yazma/dogrulama hatasi`
- `sensor fault: …` / `sensor recovered`
- `web auth fail src=… user=…` / `web ACL deny src=…`
- `dusuk heap … -> reboot`
Example line (a failed login):
 
```
<132>1 - netio-probe-abcd netio.probe - - - web auth fail src=192.168.10.50 user=admin
```
 
> **Blue-team tip:** the `web auth fail` and `web ACL deny` events are clean signals for a detection rule — forward them into your SIEM/log pipeline and alert on bursts. (Timestamps are currently sent as the RFC 5424 NILVALUE `-`; your collector's receive-time is authoritative. NTP-based timestamps are on the roadmap.)
 
---
 
## OTA firmware updates
 
### Web OTA (browser)
 
1. In the management UI, click **Firmware güncelle / Firmware update** (or browse to `/update`).
2. Select a compiled `.bin` (Arduino IDE: *Sketch → Export Compiled Binary*).
3. *(Optional)* paste the firmware's **MD5** in the field provided; the device will verify it and reject a corrupted upload.
4. Upload — a progress bar tracks it. On success the image is verified and the device reboots automatically. A failed or interrupted upload leaves the running firmware untouched.
> Get the MD5 with `md5sum firmware.bin` (Linux/macOS) or `CertUtil -hashfile firmware.bin MD5` (Windows).
 
### IDE OTA (espota)
 
After the first USB flash, the board also advertises itself for over-the-air uploads from the Arduino IDE (it appears under *Tools → Port → Network ports*). The OTA password defaults to `esp8266ota` and is configurable.
 
---
 
## Factory reset
 
Any of the following wipes **all** settings (Wi-Fi, communities, admin password, ACL, syslog, …) and returns the device to the setup AP:
 
1. **Web UI** — the *Factory reset* button (with confirmation).
2. **Hardware jumper** — short **GPIO14 (D5) ↔ GND** for ~2 seconds (works at boot or at runtime).
3. **FLASH button** — hold GPIO0 for 3 seconds.
---
 
## SNMP usage
 
Replace `<IP>` with the device address and `public` with your community if you changed it. If you set an ACL, queries must come from an allowed source IP.
 
### Quick test
 
```bash
# System group
snmpwalk  -v2c -c public <IP> .1.3.6.1.2.1.1
 
# Entity Sensor MIB (the temperature)
snmpwalk  -v2c -c public <IP> .1.3.6.1.2.1.99
 
# Temperature ×10 (e.g. 235 → 23.5 °C)
snmpget   -v2c -c public <IP> .1.3.6.1.2.1.99.1.1.1.4.1
 
# Enterprise diagnostics subtree
snmpwalk  -v2c -c public <IP> .1.3.6.1.4.1.63333
 
# Temperature in milli-°C (e.g. 23500 → 23.500 °C)
snmpget   -v2c -c public <IP> .1.3.6.1.4.1.63333.10.1.0
```
 
> The temperature is published per the Entity Sensor MIB with `type = celsius`, `scale = units`, and `precision = 1`, i.e. the raw `entPhySensorValue` is the temperature ×10. Most NMS platforms render this automatically.
 
---
 
## OID map
 
### SNMPv2-MIB — System group
 
| OID | Object | Type |
|-----|--------|------|
| `.1.3.6.1.2.1.1.1.0` | sysDescr | OCTET STRING |
| `.1.3.6.1.2.1.1.2.0` | sysObjectID | OBJECT IDENTIFIER → `.1.3.6.1.4.1.63333.1` |
| `.1.3.6.1.2.1.1.3.0` | sysUpTime | TimeTicks |
| `.1.3.6.1.2.1.1.4.0` | sysContact | OCTET STRING |
| `.1.3.6.1.2.1.1.5.0` | sysName | OCTET STRING |
| `.1.3.6.1.2.1.1.6.0` | sysLocation | OCTET STRING |
| `.1.3.6.1.2.1.1.7.0` | sysServices | INTEGER (72) |
 
### Entity Sensor MIB (RFC 3433)
 
| OID | Object | Value |
|-----|--------|-------|
| `.1.3.6.1.2.1.99.1.1.1.1.1` | entPhySensorType | `8` (celsius) |
| `.1.3.6.1.2.1.99.1.1.1.2.1` | entPhySensorScale | `9` (units) |
| `.1.3.6.1.2.1.99.1.1.1.3.1` | entPhySensorPrecision | `1` |
| `.1.3.6.1.2.1.99.1.1.1.4.1` | entPhySensorValue | temperature ×10 |
| `.1.3.6.1.2.1.99.1.1.1.5.1` | entPhySensorOperStatus | `1` ok / `2` unavailable |
 
### Enterprise subtree — `.1.3.6.1.4.1.63333.10`
 
| OID | Meaning | SNMP type |
|-----|---------|-----------|
| `.1.0` | temperature ×1000 (m°C) | Integer32 |
| `.2.0` | free heap (bytes) | Gauge32 |
| `.3.0` | Wi-Fi RSSI (dBm) | Integer32 |
| `.4.0` | uptime (seconds) | Gauge32 |
| `.5.0` | read count | Counter32 |
| `.6.0` | error count | Counter32 |
| `.7.0` | SNMP request count | Counter32 |
 
> **`63333` is a placeholder Private Enterprise Number.** For real deployments, request your own PEN from IANA and replace `ENTERPRISE_PEN` in the sketch.
 
---
 
## Configuration & defaults
 
Compile-time constants live at the top of the sketch; runtime settings are stored in EEPROM and editable via the portal / web UI.
 
| Setting | Default | Notes |
|---------|---------|-------|
| DS18B20 data pin | `GPIO4` (D2) | `DS18B20_PIN` |
| Internal pull-up | off | `USE_INTERNAL_PULLUP` (use an external 4.7 kΩ) |
| SNMP port | `161` | UDP |
| Web port | `80` | TCP |
| AP password | `snmpsetup` | WPA2, min 8 chars |
| SNMP read community | `public` | change it |
| `sysName` | `netio.probe` | |
| OTA (espota) password | `esp8266ota` | |
| Admin user / password | `admin` / `netioprobe` | **change on first login**; stored as salted SHA-256 |
| Access-control CIDR | `0.0.0.0/0` (allow all) | restrict to your management subnet |
| Syslog server IP | *(empty → disabled)* | RFC 5424 / UDP |
| Syslog port | `514` | UDP |
| Auth lockout | 5 fails → 60 s | per source IP (`AUTH_MAX_FAILS`, `AUTH_LOCKOUT_MS`) |
| Low-heap reboot | < 6 KB for 15 s | `HEAP_FLOOR`, `HEAP_FLOOR_MS` |
| STA connect timeout | `20 s` | |
| Wi-Fi-lost → reboot | `120 s` | falls back to portal |
| Sensor interval | `5 s` | |
| Factory-reset hold | `~2 s` | GPIO14 ↔ GND |
| Enterprise PEN | `63333` | placeholder — get your own |
 
---
 
## Security considerations
 
v1.2 hardens the device meaningfully, but be clear about what it does and does not protect:
 
- **Admin password at rest is safe.** It is stored only as a random-salt + SHA-256 hash and compared in constant time — the plaintext is never written to EEPROM and never pre-filled into a web page.
- **Online password guessing is throttled.** Five failed logins from an IP lock that IP out for a minute (`429`).
- **Reachability is controllable.** The source-IP ACL limits both SNMP and HTTP/metrics to a subnet or host you choose.
- **Transport is still cleartext.** SNMP v2c and HTTP Basic Auth are **not encrypted**: the community string and the base64-encoded credentials are visible to anyone who can sniff the segment. Run the device on a **trusted/management VLAN**, and for browser access prefer a **TLS-terminating reverse proxy** (e.g. Caddy/nginx) in front of it.
- **OTA is a powerful capability.** Web OTA requires authentication and supports an optional MD5 integrity check, but it can replace the firmware — keep it behind the ACL and your network controls. For cryptographically signed updates, use the ESP8266 core's signed-OTA toolchain.
- **For zero-trust environments,** SNMPv3 (auth + priv) and HTTPS are the right answer; this firmware targets SNMP v2c and is intended for monitored, trusted networks.
---
 
## Limitations & roadmap
 
**Current limitations**
 
- SNMP is **read-only** (no `SET`) and **v2c only** (no v3).
- `GETBULK` is handled as a single repetition (protocol-legal; walks still complete).
- One DS18B20 per device (Skip ROM addressing); multi-sensor 1-Wire enumeration is not implemented.
- Syslog timestamps are sent as NILVALUE (`-`); the collector's receive-time applies.
**Roadmap (not in v1.2)**
 
- Temperature **thresholds + hysteresis** driving an alarm state, an onboard LED/relay, and **SNMP TRAP/INFORM** push notifications.
- **SNMPv3** (USM, SHA + AES).
- **MQTT** publishing (Telegraf / Home Assistant).
- **NTP** time sync for real syslog/event timestamps.
- **Multi-sensor** 1-Wire enumeration and optional SHT3x humidity.
- Configurable GPIO / PEN from the UI.
---
 
## License
 
This firmware is provided **as-is, without warranty**. MIT is a sensible default if you want one — add a `LICENSE` file with your chosen terms.
 
---
 
<sub>netio.probe · ESP8266 + DS18B20 · SNMP v2c temperature probe firmware (v1.2)</sub>
