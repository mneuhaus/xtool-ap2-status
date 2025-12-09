# xTool AP2 Air Purifier Status Monitor

Standalone status monitor for the xTool AP2 Air Purifier using a **Waveshare ESP32-S3 2" Touch Display**.

## Project Goal

A compact controller that mounts on/near your laser and provides:
- **Real-time filter status** (all 6 filter elements with progress bars)
- **Automatic purifier control** - starts with laser, configurable run-on time
- **GPIO laser detection** - wire to laser interlock/status signal
- **Web dashboard** with settings, history charts, and remote control
- **Historical data logging** - track filter wear over time
- **Touch UI** for local control

## Hardware

### Waveshare ESP32-S3-Touch-LCD-2

| Spec | Value |
|------|-------|
| MCU | ESP32-S3R8 (Xtensa LX7 dual-core, 240MHz) |
| SRAM | 512KB built-in + 384KB ROM |
| Flash | 16MB external |
| PSRAM | 8MB OPI |
| Display | 2.0" IPS LCD, 240×320, 262K colors, ST7789T3 |
| Touch | CST816D capacitive (I2C, single-point) |
| IMU | QMI8658 6-axis (accelerometer + gyroscope) |
| Connectivity | WiFi 802.11 b/g/n + Bluetooth 5 LE |
| USB | USB-C (power, programming, OTG) |
| Power | USB-C or 3.7V LiPo (MX1.25 connector) |
| Storage | TF card slot |
| Camera | OV2640/OV5640 compatible (not used) |

**Product Link:** [Waveshare ESP32-S3-Touch-LCD-2](https://www.waveshare.com/esp32-s3-touch-lcd-2.htm)

### Why This Board?

1. **All-in-One** - Display + Touch + MCU in one unit, no wiring needed
2. **USB-OTG** - Can host the original xTool BLE dongle if needed
3. **Native BLE** - Can connect directly to AP2 (if BLE is exposed)
4. **Compact** - Perfect size for a status monitor
5. **Touch UI** - Adjust fan speed directly on device

---

## Communication Options

### Option 1: Direct BLE (Preferred)

The AP2 has **built-in Bluetooth** - the xTool dongle is only needed for laser machines to connect to the AP2. We should be able to connect directly from the ESP32-S3.

**Status:** Needs BLE service discovery - see "BLE Scanning" section below.

**Architecture (xTool Studio):**
```
xTool Studio App
      ↓ (USB Serial / CH340)
xTool BLE Dongle  ←-- only for LASER MACHINES
      ↓ (BLE)
AP2 Air Purifier  ←-- has built-in BLE!
```

**Our Goal:**
```
ESP32-S3 Touch Display
      ↓ (BLE - direct connection)
AP2 Air Purifier
```

### Option 2: USB-OTG with Original Dongle

If direct BLE doesn't work:
- Connect original xTool BLE dongle via USB-OTG
- ESP32-S3 acts as USB host
- Communicate via USB-Serial (CH340)

### Original Dongle Specs

| Spec | Value |
|------|-------|
| Chip | CH340 USB-Serial |
| VID | `1a86` |
| PID | `7523` |
| Baud | 115200 |

---

## BLE Connection (Implemented)

The AP2 uses **Nordic UART Service** (NUS) for BLE communication:

| Service | UUID | Status |
|---------|------|--------|
| Nordic UART Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | ✅ Confirmed |
| Nordic UART TX (write) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | ✅ Confirmed |
| Nordic UART RX (notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | ✅ Confirmed |

### Pairing Mode

**IMPORTANT:** To connect to the AP2, you must first put it in pairing mode:

1. **Hold the power button for 5 seconds**
2. The AP2 will enter pairing/advertising mode
3. The ESP32 will automatically scan and connect

Without pairing mode, the AP2 may not advertise or may use a random BLE MAC address.

### Auto-Reconnection

The firmware saves the AP2's BLE address after successful pairing. On subsequent boots:
- If a saved address exists, it attempts direct connection
- If connection fails, it falls back to scanning
- Scans every 10 seconds until connected

### Web Dashboard BLE Controls

The web interface (`/dashboard`) provides:
- **Connection status** with indicator
- **Scan button** to search for AP2
- **Forget button** to clear saved address
- **Pairing tip** shown when not connected

---

## Protocol Documentation

### F0F7 Frame Format

```
┌──────────┬──────────────┬──────────┬──────────┐
│ START    │ COMMAND_BODY │ CHECKSUM │ END      │
│ 0xF0     │ ...          │ 1 byte   │ 0xF7     │
└──────────┴──────────────┴──────────┴──────────┘

COMMAND_BODY = PREFIX + COMMAND + '\n'
CHECKSUM = sum(COMMAND_BODY) & 0x7F
```

### AP2 Prefix Bytes

```c
const uint8_t AP2_PREFIX[] = {0x4C, 0x73, 0x6B, 0x01, 0x00};
```

### Device IDs (AccessoriesType)

From xTool Studio code analysis:

| Device | Hex | Decimal | Internal Name |
|--------|-----|---------|---------------|
| AP2 (V3/Max) | `4C` | 76 | LargePurifierV3 |
| AP2 (V2) | `45` | 69 | LargePurifier |
| IF2 Duct Fan V3 | `4E` | 78 | DuctFanV3 |
| IF2 Duct Fan | `46` | 70 | DuctFan |
| Fire Extinguisher | `4A` | 74 | FireExtinguisher |
| UV Sensor | `4B` | 75 | UvSensor |
| Air Pump V2 | `40` | 64 | AirPumpV2 |
| Air Pump | `3D` | 61 | AirPump |
| Purifier (small) | `34` | 52 | Purifier |

### Firmware Names

| Device | Firmware Key |
|--------|--------------|
| AP2 Max | `xTool-BigPurifier2.0-max-firmware` |
| AP2 | `xTool-bigPurifier-firmware` |
| IF2 V3 | `xTool-ductFan2.0-firmware` |
| Dongle | `xTool-dongle-firmware` |

---

## AP2 Commands

### M9033 - Get Status

```
Request:  M9033
Response: A<ver> V<spd>|W<spd> H<n> I<n> J<n> K<n> L<n> M<n> F<b> E:<sn>
```

**Example:**
```
A1.2.3 V3 H95 I87 J100 K100 L78 M92 F1 E:XYZ123
```

| Field | Description | Values |
|-------|-------------|--------|
| `A` | Firmware version | String |
| `V` | Auto mode speed | 0-4 (present = auto mode) |
| `W` | Manual mode speed | 0-4 (present = manual mode) |
| `H` | Pre-Filter | 0-100% or -1 |
| `I` | Medium Filter | 0-100% or -1 |
| `J` | Activated Carbon | 0-100% or -1 |
| `K` | Carbon Cloth | 0-100% or -1 |
| `L` | Formaldehyde (AP2 Max) | 0-100% or -1 |
| `M` | HEPA Filter | 0-100% or -1 |
| `F` | Buzzer | 0=off, 1=on |
| `E:` | Serial number | String |

**Filter Values:**
- `0-100` = Remaining life %
- `-1` = Not detected (RFID missing)
- `≤10` = Low life warning

### M9039 - Set Fan Speed

Two command formats exist (discovered via reverse engineering):

**xTool Studio (JavaScript) Format:**
| Command | Function |
|---------|----------|
| `M9039 V<0-4>` | Auto mode speed |
| `M9039 W<0-4>` | Manual mode speed |

**xTool D2 Firmware (Ghidra) Format:**
| Command | Function |
|---------|----------|
| `M9039 D0 C0` | Auto mode off |
| `M9039 D1 C<1-4>` | Auto mode on with speed |
| `M9039 A0` | Manual mode off |
| `M9039 A1` | Manual mode on |

The firmware uses `D`/`C` parameters for auto mode (D=enable, C=speed) and `A` for manual mode toggle. Both formats may work - the implementation tries the firmware format first.

**Speed Levels:** 0=Off, 1=Low, 2=Med-Low, 3=Med-High, 4=High

### M9046 - Buzzer Control

| Command | Function |
|---------|----------|
| `M9046 F0` | Buzzer off |
| `M9046 F1` | Buzzer on |

---

## Filter Types

| ID | Filter | Color Code |
|----|--------|------------|
| H | Pre-Filter | Gray |
| I | Medium Filter | Blue |
| J | Activated Carbon | Black |
| K | Carbon Cloth | Dark Gray |
| L | Formaldehyde Removal* | Green |
| M | HEPA | White |

*Only on AP2 Max

### RFID Recognition

Filters use internal RFID tags. The AP2 firmware handles:
- Filter detection (present/missing)
- Usage tracking (remaining %)
- Authenticity verification

---

## Display UI Design (240×320)

```
┌────────────────────────┐
│   xTool AP2 Monitor    │  ← Header (32px)
├────────────────────────┤
│  ┌──────────────────┐  │
│  │ FAN  ████░  3    │  │  ← Fan gauge + speed
│  │ [AUTO]    [MAN]  │  │  ← Touch mode buttons
│  └──────────────────┘  │
├────────────────────────┤
│  H Pre     ████████ 85 │  ← Filter bars
│  I Med     ██████░░ 62 │
│  J Carbon  ██████████ 100│
│  K Cloth   █████████ 95 │
│  L Formal  ████░░░░ 45 ⚠│  ← Warning icon
│  M HEPA    ███████░ 78 │
├────────────────────────┤
│  WiFi ✓  192.168.1.42  │  ← Status bar (24px)
└────────────────────────┘
```

### Touch Interactions

| Area | Action |
|------|--------|
| AUTO button | Switch to auto mode |
| MAN button | Switch to manual mode |
| Fan gauge | Swipe to adjust speed |
| Filter bar | Show filter details |
| IP address | Open web interface QR |

---

## Laser Control & Auto-Start

The controller can automatically start/stop the purifier based on laser activity.

### How It Works

```
Laser Signal  ─────┐
                   │  GPIO 7 (configurable)
                   ▼
              ESP32 detects laser ON
                   │
                   ▼
         ┌─────────────────────┐
         │ Lead-In Time (5s)   │  ← Purifier starts immediately
         │ Laser runs...       │
         │ Laser stops         │
         │ Run-On Time (60s)   │  ← Purifier continues
         │ Purifier stops      │
         └─────────────────────┘
```

### Settings

| Setting | Default | Description |
|---------|---------|-------------|
| Lead-In Time | 5 sec | How long before laser to start purifier |
| Run-On Time | 60 sec | How long to run after laser stops |
| Laser Fan Speed | 4 (High) | Speed when laser is active |
| Auto-Start | Enabled | Enable/disable automatic control |

### Wiring

Connect your laser's status/interlock signal to GPIO 7:

| Signal | GPIO | Notes |
|--------|------|-------|
| Laser Active | 7 | Active HIGH by default |
| GND | GND | Common ground |

*Set `LASER_ACTIVE_LOW = true` in firmware if your signal is inverted.*

---

## Historical Data Logging

The controller logs filter status periodically to track wear over time.

### Storage

- **Format:** CSV in LittleFS
- **File:** `/filter_log.csv`
- **Fields:** `timestamp,H,I,J,K,L,M,operationSeconds`
- **Interval:** Configurable (default: 15 minutes)
- **Capacity:** ~1000 entries (~2 weeks at 15min intervals)

### Predictions

By tracking filter degradation over time, you can:
- Estimate remaining filter life
- Plan maintenance windows
- Compare usage patterns

---

## Web Interface

### Web Dashboard

Responsive HTML dashboard served from ESP32-S3 flash with:
- Real-time status updates
- Filter progress bars with warning colors
- Laser activity indicator
- Settings editor
- Filter history chart (Chart.js)

Access via `http://<device-ip>/dashboard`

### REST API

**GET** `/api/status`
```json
{
  "connected": true,
  "firmware": "1.2.3",
  "serial": "XYZ123",
  "fan": {
    "speed": 3,
    "mode": "auto"
  },
  "laser": {
    "active": false,
    "purifierRunning": false,
    "inRunOn": false,
    "runOnRemaining": 0
  },
  "filters": {
    "H": {"name": "Pre-Filter", "percent": 85},
    "I": {"name": "Medium", "percent": 62},
    "J": {"name": "Carbon", "percent": 100},
    "K": {"name": "Cloth", "percent": 95},
    "L": {"name": "Formaldehyde", "percent": 45},
    "M": {"name": "HEPA", "percent": 78}
  },
  "settings": {"filterWarningPercent": 15},
  "buzzer": true,
  "totalOperationHours": 42.5
}
```

**GET** `/api/settings`
```json
{
  "leadInTime": 5,
  "runOnTime": 60,
  "laserFanSpeed": 4,
  "autoStartEnabled": true,
  "filterWarningPercent": 15,
  "logIntervalMinutes": 15,
  "brightness": 100
}
```

**POST** `/api/settings`
```json
{
  "leadInTime": 5,
  "runOnTime": 120,
  "laserFanSpeed": 4,
  "autoStartEnabled": true,
  "filterWarningPercent": 20
}
```

**GET** `/api/history?entries=50`
```json
[
  {"t": 1234567890, "H": 85, "I": 72, "J": 100, "K": 95, "L": 45, "M": 78, "op": 3600},
  ...
]
```

**POST** `/api/fan`
```json
{"speed": 3, "mode": "auto"}
```

**POST** `/api/control`
```json
{"action": "start"}
```
or
```json
{"action": "stop"}
```

---

## Development

### Framework

**Arduino IDE** with LVGL for display/touch. Uses `arduino-cli` for command-line builds.

### Quick Start

```bash
# First time setup
make setup

# Build and flash
make flash

# Monitor serial output
make monitor

# Or all at once
make all
```

### Available Make Commands

| Command | Description |
|---------|-------------|
| `make setup` | Install ESP32 core + libraries |
| `make flash` | Compile and upload |
| `make monitor` | Serial monitor |
| `make all` | Flash + monitor |
| `make clean` | Clean build |
| `make boards` | List connected boards |
| `make config` | Show configuration |

### Dependencies

Installed automatically via `make setup`:
- ESP32 Arduino Core
- LVGL
- GFX Library for Arduino
- TFT_eSPI
- ArduinoJson
- LittleFS

### Pin Configuration (Waveshare ESP32-S3-Touch-LCD-2)

Pin mappings verified against official Waveshare demo code.

| Function | GPIO | Description |
|----------|------|-------------|
| LCD_SCLK | 39 | SPI Clock |
| LCD_MOSI | 38 | SPI Data |
| LCD_DC | 42 | Data/Command |
| LCD_CS | 45 | SPI Chip Select |
| LCD_BL | 1 | Backlight (PWM) |
| TOUCH_SDA | 48 | I2C Data |
| TOUCH_SCL | 47 | I2C Clock |
| LASER_DETECT | 7 | Laser status input (external) |

---

## Project Structure

```
xtool-ap2-status/
├── ap2-status/
│   └── ap2-status.ino      # Main Arduino sketch
├── Makefile                # Build system
└── README.md
```

---

## Roadmap

### Completed
- [x] Protocol documentation
- [x] F0F7 protocol implementation
- [x] Web API server (REST endpoints)
- [x] Web dashboard with Chart.js
- [x] GPIO laser detection
- [x] Auto start/stop with run-on time
- [x] Settings storage (LittleFS)
- [x] Historical filter data logging
- [x] BLE scan - discovered AP2's Nordic UART Service UUIDs
- [x] NimBLE client implementation with auto-reconnect
- [x] BLE pairing UI in web dashboard
- [x] M9039 command format (D/C parameters from firmware analysis)

### In Progress
- [ ] Real-world BLE testing with AP2 hardware

### Planned
- [ ] LVGL display driver for Waveshare board
- [ ] Touch UI with filter bars
- [ ] USB-OTG host (dongle fallback)
- [ ] OTA updates
- [ ] NTP time sync for accurate timestamps
- [ ] Filter replacement predictions
- [ ] 3D printed case/stand
- [ ] Home Assistant integration

---

## Analyse-Protokoll

Dieses Dokument enthält die vollständige Reverse-Engineering-Analyse des xTool AP2 Air Purifier Kommunikationsprotokolls.

---

### 2025-12-06: Protocol Deep Dive

**xTool Studio Analysis** (`/Users/mneuhaus/Downloads/webcrack-export/Untitled-1.js`):

Found protocol constants at line ~99701:
```javascript
PROTOCOL_CONSTANTS = {
  F0F7: {
    START_BYTE: 240,    // 0xF0
    END_BYTE: 247       // 0xF7
  },
  LINE: { DELIMITER: "\n" }
};
checksumMask: 127       // 0x7F
```

Found device IDs at line ~69605:
```javascript
AccessoriesType = {
  LargePurifierV3: "4C",  // 0x4C = 76 (AP2 Max)
  LargePurifier: "45",    // 0x45 = 69 (AP2 V2)
  DuctFanV3: "4E",        // 0x4E = 78
  DuctFan: "46",          // 0x46 = 70
  Purifier: "34",         // 0x34 = 52
  ...
}
```

**Key Finding:** The desktop app uses `serialport` npm package to talk to USB dongle (CH340). **No BLE library in the app** - the dongle handles BLE internally.

**BLE Scanning (macOS):**
- Used `bleak` via `uvx` to scan for BLE devices
- Found 11 devices but no xTool AP2 visible
- AP2 was not powered on at test location
- **Next step:** Scan with AP2 powered on to discover service UUIDs

**Bluetooth Type:** Most likely **BLE** (not classic Bluetooth):
- IoT devices typically use BLE for low power
- GATT support indicated in code patterns
- No SPP/RFCOMM references found

**macOS Sniffing Options:**
1. `bleak` Python library via `uvx --from bleak python`
2. Apple's PacketLogger (needs Xcode Additional Tools)
3. Wireshark with BLE capture

### 2025-12-08: BLE Scanning at Makerspace

**Dongle Hardware Analysis:**

| Component | Details |
|-----------|---------|
| Model | BleDongle V1_0 |
| BLE Module | BoBlinker BB160S |
| USB Chip | CH340 (VID `1a86`, PID `7523`) |
| FCC ID | 2AH9Q-BTD01 (Makeblock Co., Ltd.) |
| Baud Rate | 115200 |
| Connector | Micro-USB |

**BLE Scanning Results:**

Used on/off comparison to identify AP2's BLE advertisement:
- Scanned with AP2 on, then off, compared device lists
- Identified candidate devices that appeared/disappeared with AP2 power state

**Successful Connection (temporary):**

Connected to device `5CD78D8E-F0E2-35E1-D5A9-35726228D181`:
```
Manufacturer: Nations
Model: NS-BLE-1.0
Firmware: 1.0.0.0-LE

Services:
- 0000180a-... (Device Information)
- 6e400001-b5a3-f393-e0a9-e50e24dcca9e (Nordic UART Service) ✓
  └─ TX: 6e400002-... (write, write-without-response)
  └─ RX: 6e400003-... (notify)
```

**Nordic UART Service UUIDs (confirmed):**
| Purpose | UUID |
|---------|------|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (write to AP2) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (notify from AP2) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

**Issue:** Device disappeared after initial connection and couldn't be found again.
- AP2 may use random BLE MAC addresses
- May only advertise briefly after boot
- Or the "Nations" device was a different device in the makerspace

**Next Steps:**
1. Test with dongle via USB-Serial (need Micro-USB cable)
2. Verify if "Nations NS-BLE-1.0" is actually the AP2
3. Try to capture BLE traffic with dongle connected

### 2025-12-08: Dongle Hardware & Firmware Analysis

**Dongle Physical Inspection:**

The BLE dongle is NOT a standard USB device - it has a proprietary 12-pin connector for xTool lasers.

| Component | Part Number | Function |
|-----------|-------------|----------|
| BLE Module | BoBlinker BB160S (= DB809S) | Nordic nRF52-based BLE 5.0 |
| RS-485 Chip | BL3085 | Half-duplex RS-485 transceiver, 500kbps |
| LDO | SGM2021-3.3 (YL33) | 3.3V voltage regulator |
| Test Points | TP1-TP7, RST | Programming/debug access |

**Communication Architecture:**

```
┌──────────────────┐    RS-485     ┌────────────────┐    BLE    ┌──────────┐
│  xTool Laser     │ ──────────>   │  BLE Dongle    │ ────────> │   AP2    │
│  (GD32 MCU)      │    UART       │  (nRF52)       │           │          │
└──────────────────┘               └────────────────┘           └──────────┘
```

**xTool D2 Firmware Analysis (Ghidra):**

Downloaded firmware: `xtool_d2_gd470_V40.32.014.2025.01.bin` (425KB)

Key decompiled functions:

1. **`FUN_00013c00`** - Purifier Control:
   ```c
   // Turn off purifier
   FUN_0000be8c(4, "M353 S0", 8, 2);

   // Manual mode off
   FUN_0000be8c(4, "M9039 A0", 9, 0xb);

   // Auto mode off with speed 0
   FUN_0000be8c(4, "M9039 D0 C0", 0xc, 0xb);

   // Duct fan control
   FUN_0000be8c(4, "M9064 A0", 9, 0xc);
   FUN_0000be8c(4, "M9064 D0 B0", 0xc, 0xc);
   ```

2. **`FUN_0000be8c`** - Command Router:
   ```c
   void FUN_0000be8c(type, cmd, len, device_id) {
       buffer = FUN_00022b48(queue, cmd, len, device_id);
       FUN_000242a4(type, queue, buffer);
   }
   ```
   - `type=4` routes to RS-485 (→ dongle → AP2)
   - `device_id=0xb` = Purifier (M9039)
   - `device_id=0xc` = Duct Fan (M9064)

3. **`FUN_00022ac8`** - F0F7 Frame Builder:
   ```c
   // Frame construction:
   buffer[0] = 0xF0;                    // START
   buffer[1] = device_id_byte1;         // Device header
   buffer[2] = device_id_byte2;
   if (device_id_byte2 > 0x60)
       buffer[3] = device_id_byte3;
   buffer[n] = 0x01;                    // Separator
   buffer[n+1] = 0x00;
   // ... payload (M9039 command) ...
   // Checksum
   for (i = 1; i < len; i++)
       checksum += buffer[i];
   buffer[len] = checksum & 0x7F;
   buffer[len+1] = 0xF7;                // END
   ```

**Confirmed Protocol:**

```
F0F7 Frame Format (from firmware):
┌──────┬──────────────────┬──────┬──────────────┬──────────┬──────┐
│ 0xF0 │ Device ID (2-3)  │ 0x01 │   Payload    │ Checksum │ 0xF7 │
│      │ from lookup table│ 0x00 │ "M9039 D1 C4"│ sum&0x7F │      │
└──────┴──────────────────┴──────┴──────────────┴──────────┴──────┘
```

**M-Command Summary:**

| Command | Parameters | Function |
|---------|-----------|----------|
| `M9039 D0 C0` | D=on/off, C=speed | Purifier auto mode off |
| `M9039 D1 C<1-4>` | | Purifier auto mode on, speed 1-4 |
| `M9039 A0` | A=auto toggle | Purifier manual mode off |
| `M9064 D0 B0` | D=on/off, B=speed | Duct fan auto mode off |
| `M9064 A0` | | Duct fan manual mode off |
| `M353 S0` | S=state | Legacy purifier command |

**Key Finding:** The laser sends M-commands (M9039, M9064) wrapped in F0F7 frames via RS-485 to the dongle, which then forwards them via BLE to the AP2. The commands visible in xTool Studio ARE the actual commands sent to the hardware.

### 2025-12-08: xTool Studio App Analysis & Pairing Mode Discovery

**AP2 Pairing Mode:**

**WICHTIG:** Um den AP2 via BLE zu finden, muss er im Pairing-Modus sein:
- **Power-Button 5 Sekunden gedrückt halten** → AP2 geht in Pairing-Modus
- Ohne Pairing-Modus advertised der AP2 möglicherweise nicht oder nur kurz

**xTool Studio App Bundle Analysis:**

Extracted `/Applications/xTool Studio.app/Contents/Resources/app.asar`:

```
.vite/
├── build/
│   ├── app-qxKv21Vz.js          # Main Electron app (2MB)
│   └── preload.mjs              # Preload scripts
└── renderer/
    └── apps/
        ├── firmware-update/      # Firmware update UI
        └── devices/              # Device management
```

**ext.json** - Device extension configuration:
- Lists all supported laser models (D1, P2, S1, F1, etc.)
- Each device has `contentId` like `xTool-s1-ext`
- Extensions downloaded dynamically via `extVersionChecker`

**Firmware Update Infrastructure:**

| Component | URL |
|-----------|-----|
| Main API | `https://xcs-api.xtool.com` |
| Support API | `https://support-api.xtool.com` |
| File Storage | `https://storage-us.atomm.com/resource/xtool/support-attachment/` |
| i18n Resources | `https://storage-us.atomm.com/resource` |

**Firmware URL Pattern (Laser):**
```
https://storage-us.atomm.com/resource/xtool/support-attachment/xtool_d2_esp32_s3_app_V40.32.012.2224.01.V01_B4.bin
https://storage-us.atomm.com/resource/xtool/support-attachment/xtool_d2_gd470_V40.32.014.2025.01.V01_B8.bin
```

**AP2/Dongle Firmware:** URLs werden dynamisch über API abgerufen wenn Laser verbunden ist. Kein direkter Download ohne Laser möglich gefunden.

**Firmware Update Process (from app analysis):**
1. `extVersionChecker` prüft auf Updates
2. `downloader` lädt Firmware
3. `installer` installiert via F0F7 Protocol
4. "f0f7 uploader, firmware version chunk" - Firmware wird in Chunks übertragen

---

## Next Steps / TODO

### Zum Testen (benötigt AP2 Zugang):

1. **AP2 in Pairing-Modus versetzen:**
   ```bash
   # Power-Button 5 Sekunden halten!
   ```

2. **BLE Scan durchführen:**
   ```bash
   uvx --from bleak python -c "
   import asyncio
   from bleak import BleakScanner
   async def scan():
       devices = await BleakScanner.discover(timeout=10)
       for d in devices:
           print(f'{d.address}: {d.name}')
   asyncio.run(scan())
   "
   ```

3. **Verbinden und Services erkunden:**
   ```python
   import asyncio
   from bleak import BleakClient

   AP2_MAC = "..."  # From scan

   async def explore():
       async with BleakClient(AP2_MAC) as client:
           for service in client.services:
               print(f"Service: {service.uuid}")
               for char in service.characteristics:
                   print(f"  Char: {char.uuid} - {char.properties}")

   asyncio.run(explore())
   ```

4. **F0F7 Frame senden (Status abfragen):**
   ```python
   def build_f0f7_frame(payload: bytes) -> bytes:
       # Device ID für AP2: 0x4C, 0x73, 0x6B (aus JS AccessoriesType)
       frame = bytearray([0xF0, 0x4C, 0x73, 0x6B, 0x01, 0x00])
       frame.extend(payload)
       checksum = sum(frame[1:]) & 0x7F
       frame.append(checksum)
       frame.append(0xF7)
       return bytes(frame)

   # Status Command
   cmd = build_f0f7_frame(b"M9033\n")
   await client.write_gatt_char(NUS_TX_UUID, cmd)
   ```

### Offene Fragen:

- [ ] Wird F0F7 Frame direkt über BLE gesendet oder nur der Payload?
- [ ] Welche Device-ID Bytes erwartet der AP2 direkt (vs. über Dongle)?
- [ ] Gibt es Pairing/Bonding Requirements?
- [ ] Antwortet der AP2 auf M9033 mit Filterstand?

---

## Analyse-Zusammenfassung

### Was wir wissen:

| Aspekt | Status | Details |
|--------|--------|---------|
| **Protokoll-Format** | ✅ Bekannt | F0F7-Frame mit START (0xF0), Payload, Checksum (sum & 0x7F), END (0xF7) |
| **Device-ID AP2** | ✅ Bekannt | `0x4C, 0x73, 0x6B` (LargePurifierV3) |
| **M-Commands** | ✅ Bekannt | M9033 (Status), M9039 (Fan), M9046 (Buzzer) |
| **BLE Service** | ✅ Bestätigt | Nordic UART Service (6e400001-...) |
| **BLE TX UUID** | ✅ Bestätigt | 6e400002-b5a3-f393-e0a9-e50e24dcca9e |
| **BLE RX UUID** | ✅ Bestätigt | 6e400003-b5a3-f393-e0a9-e50e24dcca9e |
| **Dongle Hardware** | ✅ Analysiert | BB160S (nRF52), CH340 USB-Serial, RS-485 |
| **Laser→Dongle** | ✅ Bekannt | RS-485 UART mit F0F7-Frames |
| **Pairing-Modus** | ✅ Bekannt | Power-Button 5s gedrückt halten |

### Was noch getestet werden muss:

| Test | Priorität | Status |
|------|-----------|--------|
| BLE-Scan mit AP2 im Pairing-Modus | Hoch | ⏳ Ausstehend |
| Direktverbindung ESP32 → AP2 | Hoch | ⏳ Ausstehend |
| M9033 Statusabfrage via BLE | Hoch | ⏳ Ausstehend |
| Frame-Format bei Direktverbindung | Mittel | ⏳ Ausstehend |
| Dongle-Sniffing via USB-Serial | Niedrig | ⏳ Ausstehend |

### Analysierte Quellen:

1. **xTool Studio (Electron App)**
   - Pfad: `/Applications/xTool Studio.app/Contents/Resources/app.asar`
   - Entpackt mit: `asar extract app.asar ./extracted`
   - Haupt-JS: `.vite/build/app-qxKv21Vz.js` (2MB, minifiziert)
   - Webcrack für Deobfuskierung verwendet

2. **xTool D2 Firmware**
   - Firmware: `xtool_d2_gd470_V40.32.014.2025.01.bin` (425KB)
   - Analyse mit Ghidra (ARM Cortex-M)
   - Purifier-Commands in `FUN_00013c00` gefunden
   - F0F7-Frame-Builder in `FUN_00022ac8` analysiert

3. **BLE Dongle Hardware**
   - Physische Inspektion der PCB
   - BoBlinker BB160S (nRF52-basiert) identifiziert
   - RS-485 Transceiver BL3085 identifiziert
   - Proprietärer 12-Pin Connector (nicht USB-direkt)

---

## Schnellstart für Entwickler

### 1. BLE-Scan durchführen

```bash
# AP2 in Pairing-Modus versetzen (Power 5s halten!)
uvx --from bleak python -c "
import asyncio
from bleak import BleakScanner
async def scan():
    devices = await BleakScanner.discover(timeout=10)
    for d in devices:
        if d.name and ('xTool' in d.name or 'NS-BLE' in d.name or 'Nations' in d.name):
            print(f'>>> FOUND: {d.address}: {d.name}')
        else:
            print(f'{d.address}: {d.name}')
asyncio.run(scan())
"
```

### 2. Services erkunden

```python
import asyncio
from bleak import BleakClient

AP2_MAC = "5CD78D8E-F0E2-35E1-D5A9-35726228D181"  # Anpassen!

async def explore():
    async with BleakClient(AP2_MAC) as client:
        print(f"Connected: {client.is_connected}")
        for service in client.services:
            print(f"\nService: {service.uuid}")
            for char in service.characteristics:
                print(f"  {char.uuid}: {char.properties}")

asyncio.run(explore())
```

### 3. Status abfragen (F0F7 Frame)

```python
import asyncio
from bleak import BleakClient

NUS_TX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

def build_f0f7_frame(payload: bytes) -> bytes:
    """Build F0F7 frame with AP2 device ID prefix."""
    # AP2 prefix: 0x4C 0x73 0x6B 0x01 0x00
    frame = bytearray([0xF0, 0x4C, 0x73, 0x6B, 0x01, 0x00])
    frame.extend(payload)
    checksum = sum(frame[1:]) & 0x7F
    frame.append(checksum)
    frame.append(0xF7)
    return bytes(frame)

async def get_status():
    def rx_handler(_, data):
        print(f"RX: {data.hex()} = {data}")

    async with BleakClient(AP2_MAC) as client:
        await client.start_notify(NUS_RX, rx_handler)

        # Status command
        cmd = build_f0f7_frame(b"M9033\n")
        print(f"TX: {cmd.hex()}")
        await client.write_gatt_char(NUS_TX, cmd)

        await asyncio.sleep(2)

asyncio.run(get_status())
```

---

## Fake Device Injection (2025-12-08)

To unlock more xTool Studio functionality without a physical device connected, you can inject a fake device entry into the app's localStorage.

### How It Works

xTool Studio stores device information in several localStorage keys:
- `newProjectDefaultExtInfo` - Default device for new projects
- `material-device-basic-info` - Device info for material database
- `SELECT_DEVICE` - Currently selected device code
- `DiagnosticsDeviceList` - List of known devices
- `DEFAULT_DEVICE_MODES` - Device mode configuration
- `extList` - List of installed extensions

### Usage

1. Open xTool Studio
2. Open DevTools: `Cmd+Option+I` (Mac) or `Ctrl+Shift+I` (Windows)
3. Go to Console tab
4. Copy and paste the contents of `inject-fake-device.js`
5. Press Enter
6. Restart xTool Studio or create a new project

### Script Location

See `inject-fake-device.js` in this repository.

### Supported Fake Devices

The script supports injecting any of these devices:
- M1 (deviceCode: MLM)
- S1 (deviceCode: MD2)
- D1 Pro (deviceCode: MDP)
- F1 (deviceCode: MF1)
- P2 (deviceCode: MP2)

Edit the `SELECTED_DEVICE` variable in the script to change the device type.

### Limitations

- The device will show as "disconnected" (since it's virtual)
- Some features requiring actual device communication won't work
- May help unlock editor features, material database, and firmware update UI for inspection

---

## Firmware API Investigation (2025-12-08)

### API Endpoint Discovery

Extracted from xTool Studio (Electron app.asar):

**Endpoint:** `POST https://api.xtool.com/efficacy/v1/package/version/latest`

**Request Format:**
```json
{
  "Domain": "xcs",
  "contentId": "<content-id>",
  "contentVersion": "<current-version>"
}
```

### Known Content IDs

From `ext.json` (device extensions):
| Device | contentId |
|--------|-----------|
| D1 | xTool-d1-ext |
| D1 Pro | xTool-d1pro-ext |
| D1 Pro 2.0 | xTool-d1pro2.0-ext |
| M1 | xTool-m1-ext |
| M1 Ultra | xTool-m1-ultra-ext |
| S1 | xTool-s1-ext |
| P1 | xTool-p1-ext |
| P2 | xTool-p2-ext |
| P2S | xTool-p2s-ext |
| F1 | xTool-f1-ext |
| F1 Ultra | xTool-f1-ultra-ext |

From firmware-update module:
| Component | contentId |
|-----------|-----------|
| S1 Firmware | xTool-s1-firmware |
| D1 Firmware | xTool-d1-firmware |
| D1 Pro Firmware | xTool-d1pro-firmware |
| D1 Pro 2.0 Firmware | xTool-d1pro-firmware-2.0 |
| Air Pump 2.0 | xTool-airpump2.0-firmware |
| Fire Extinguisher | xTool-extinguisherBoxEnforce-firmware |
| Extension List | extList |

### API Testing Results

```bash
# Working request (returns empty when version is current):
curl -X POST "https://api.xtool.com/efficacy/v1/package/version/latest" \
  -H "Content-Type: application/json" \
  -d '{"Domain":"xcs","contentId":"extList","contentVersion":"0.0.0"}'

# Response: {"code":0,"data":{},"message":""}
```

**Findings:**
- Domain "xcs" is required (other values like "xcs-web" return "record not found")
- The API validates contentId strictly - only registered IDs work
- "extList" is the master list of device extensions
- Firmware contentIds (e.g., xTool-s1-firmware) return "资源id不对" (resource ID wrong) with domain "xcs"
- Device extension contentIds also return "资源id不对"

### Blocked: Firmware Download

The firmware API appears to require:
1. Specific domain/contentId combinations not discoverable from static analysis
2. Possibly additional authentication headers (user tokens, device binding)
3. The firmware files may only be served to authenticated xTool Studio clients

**Alternative approaches to investigate:**
- Network traffic capture during actual firmware update in xTool Studio
- Check if firmware files are cached locally after update
- Look for firmware URLs in device communication during update process

---

## References

- Protocol reverse-engineered from xTool Studio (Electron app)
- Waveshare Wiki: [ESP32-S3-Touch-LCD-2](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2)
- ESP-IDF: [docs.espressif.com](https://docs.espressif.com/projects/esp-idf/)
- LVGL: [docs.lvgl.io](https://docs.lvgl.io/)

## License

MIT License - Use at your own risk.

## Disclaimer

This is an unofficial project, not affiliated with xTool. Use at your own risk.
