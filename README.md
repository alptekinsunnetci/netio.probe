# netio.probe

**A self-contained SNMP v2c temperature probe for the ESP8266 + DS18B20 — zero external libraries.**

`netio.probe` turns a NodeMCU (ESP8266) and a single DS18B20 sensor into a proper, monitorable network device. It reads temperature over 1-Wire and publishes it as a **real SNMP v2c agent** (hand-written BER/ASN.1 — `snmpwalk` / `snmpget` work out of the box), exposes a **modern, password-protected web management UI**, supports **browser-based OTA** firmware updates, and ships a **first-boot Wi-Fi captive portal** so it can be provisioned without recompiling.

Everything lives in one `.ino` file and depends only on the **ESP8266 Arduino Core** — no SNMP, OneWire, or DallasTemperature libraries required.

![platform](https://img.shields.io/badge/platform-ESP8266-blue)
![framework](https://img.shields.io/badge/framework-Arduino%20Core%203.x-teal)
![protocol](https://img.shields.io/badge/SNMP-v2c-success)
![libraries](https://img.shields.io/badge/external%20libs-none-brightgreen)

---

## Table of contents

- [Features](#features)
- [How it works](#how-it-works)
- [Hardware](#hardware)
- [Build & flash](#build--flash)
- [First-time setup (Wi-Fi captive portal)](#first-time-setup-wi-fi-captive-portal)
- [Web management interface](#web-management-interface)
- [OTA firmware updates](#ota-firmware-updates)
- [Factory reset](#factory-reset)
- [SNMP usage](#snmp-usage)
- [OID map](#oid-map)
- [Configuration & defaults](#configuration--defaults)
- [Security considerations](#security-considerations)
- [Limitations & roadmap](#limitations--roadmap)
- [License](#license)

---

## Features

- **Real SNMP v2c agent** — hand-written BER/ASN.1 encoder/decoder. Supports `GET`, `GETNEXT`, and `GETBULK` (single-repetition), so `snmpwalk` and `snmpbulkwalk` terminate correctly. Read-only; the configured community is validated and malformed or mismatched packets are dropped silently.
- **Standards-aligned MIB** — full SNMPv2-MIB *System* group plus the **Entity Sensor MIB** (RFC 3433) for the temperature reading, plus an enterprise subtree with diagnostics (heap, RSSI, uptime, counters).
- **Robust DS18B20 driver** — standard 1-Wire timing with interrupts disabled only around the short reset/bit windows (so Wi-Fi ISRs can't corrupt a read), and a **non-blocking** conversion state machine (the 750 ms conversion never stalls `loop()`).
- **First-boot Wi-Fi provisioning** — boots as an Access Point with a captive portal; pick your network from a live scan, no hardcoded credentials.
- **Password-protected web UI** (run mode) — live telemetry dashboard plus editable SNMP/system/Wi-Fi/admin settings, behind HTTP Basic Auth.
- **Browser OTA** — upload a compiled `.bin` from the web UI with a progress bar; classic IDE OTA (espota) also works.
- **Triple factory reset** — from the web UI, by shorting two pins on the board, or via the FLASH button.
- **Single file, no dependencies** — drops straight into the Arduino IDE; nothing to `lib install`.

---

## How it works

```
power on
  └─► load config from EEPROM (magic + XOR checksum)
        ├─ configured AND Wi-Fi connects within 20 s ─► RUN MODE
        │     • SNMP agent            UDP/161
        │     • Web management UI     TCP/80   (HTTP Basic Auth)
        │     • Web OTA               /update
        │     • IDE OTA (espota)      + mDNS
        │
        └─ not configured OR connect fails ──────────► SETUP MODE
              • SoftAP "netio.probe-XXXX"  (WPA2)
              • captive portal at http://192.168.4.1/
              • live Wi-Fi scan, save → reboot into RUN
```

In RUN mode, if Wi-Fi stays down for more than 120 seconds the device reboots and falls back to the captive portal so it can be re-provisioned. Configuration is persisted in EEPROM as a single versioned struct guarded by a magic number and an XOR checksum.

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
- **No libraries to install** — everything used is part of the core (`ESP8266WiFi`, `ESP8266WebServer`, `DNSServer`, `ESP8266mDNS`, `WiFiUdp`, `ArduinoOTA`, `EEPROM`, `Updater`).

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

Once on your network, browse to the device IP (or `netio-probe-<chip-id>.local`). The UI is protected by **HTTP Basic Auth**.

| | |
|---|---|
| **Default username** | `admin` |
| **Default password** | `netioprobe` |

> 🔐 **Change these on first login.** They are configurable in both the setup portal and the management UI.

The dashboard shows live telemetry (temperature, RSSI, uptime, free heap, SNMP request count, read/error counters) refreshed every few seconds via a `/status` JSON endpoint, and lets you:

- edit the **SNMP read community** (applied live — no reboot) and `sysName` / `sysLocation` / `sysContact`;
- change **admin** credentials, the **OTA** (espota) password, and **Wi-Fi** credentials (changing Wi-Fi triggers a reboot);
- open the **firmware update** page;
- **reboot** or **factory reset** the device.

| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/` | GET | ✅ | Management dashboard |
| `/status` | GET | ✅ | Live telemetry (JSON) |
| `/save` | POST | ✅ | Apply settings |
| `/update` | GET / POST | ✅ | Web OTA page / upload |
| `/reboot` | POST | ✅ | Restart |
| `/reset` | POST | ✅ | Factory reset |

---

## OTA firmware updates

### Web OTA (browser)

1. In the management UI, click **Firmware güncelle / Firmware update** (or browse to `/update`).
2. Select a compiled `.bin` (Arduino IDE: *Sketch → Export Compiled Binary*).
3. Upload — a progress bar tracks it. On success the image is verified and the device reboots automatically. A failed or interrupted upload leaves the running firmware untouched.

### IDE OTA (espota)

After the first USB flash, the board also advertises itself for over-the-air uploads from the Arduino IDE (it appears under *Tools → Port → Network ports*). The OTA password defaults to `esp8266ota` and is configurable.

---

## Factory reset

Any of the following wipes **all** settings (Wi-Fi, communities, admin password, …) and returns the device to the setup AP:

1. **Web UI** — the *Factory reset* button (with confirmation).
2. **Hardware jumper** — short **GPIO14 (D5) ↔ GND** for ~2 seconds (works at boot or at runtime).
3. **FLASH button** — hold GPIO0 for 3 seconds.

---

## SNMP usage

Replace `<IP>` with the device address and `public` with your community if you changed it.

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
| Admin user / password | `admin` / `netioprobe` | **change on first login** |
| STA connect timeout | `20 s` | |
| Wi-Fi-lost → reboot | `120 s` | falls back to portal |
| Sensor interval | `5 s` | |
| Factory-reset hold | `~2 s` | GPIO14 ↔ GND |
| Enterprise PEN | `63333` | placeholder — get your own |

---

## Security considerations

- **SNMP v2c is plaintext.** The community string is not encrypted. Use it on a trusted/management network or VLAN, and change the default community.
- **The web UI uses HTTP Basic Auth, not HTTPS.** Credentials are base64-encoded, not encrypted, in transit. **Change the default `admin` / `netioprobe`** and prefer a trusted network or a TLS-terminating reverse proxy in front of the device.
- **Web OTA writes only for an authenticated client**, but firmware upload is a powerful capability — restrict access at the network layer (ACLs / VLAN) as well.
- For production-grade security, **SNMPv3 (auth + priv)** and HTTPS are recommended. This firmware targets SNMP v2c and is intended for monitored, trusted environments.

---

## Limitations & roadmap

- SNMP is **read-only** (no `SET`) and **v2c only** (no v3).
- `GETBULK` is handled as a single repetition (protocol-legal; walks still complete).
- One DS18B20 per device (Skip ROM addressing); multi-sensor 1-Wire bus enumeration is not implemented.
- Possible future work: SNMPv3, multi-sensor support, configurable GPIO/PEN from the UI, MQTT export.

---

## License

This firmware is provided **as-is, without warranty**. MIT is a sensible default if you want one — add a `LICENSE` file with your chosen terms.

---

<sub>netio.probe · ESP8266 + DS18B20 · SNMP v2c temperature probe firmware (v2.1)</sub>
