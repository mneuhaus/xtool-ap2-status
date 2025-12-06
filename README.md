# xTool AP2 Air Purifier Status Monitor

Standalone status monitor for the xTool AP2 Air Purifier using a **Waveshare ESP32-S3 2" Touch Display**.

## Project Goal

A compact, standalone device that displays:
- Real-time filter status (all 6 filter elements with progress bars)
- Fan speed and mode (Auto/Manual)
- Touch controls to adjust settings
- Web interface for remote monitoring

## Hardware

### Waveshare ESP32-S3 2" Capacitive Touch Display

| Spec | Value |
|------|-------|
| MCU | ESP32-S3 (Dual-Core LX7, 240MHz) |
| Display | 2.0" IPS LCD, 240×320 pixels, 262K colors |
| Touch | Capacitive touch (CST816S) |
| Connectivity | WiFi 802.11 b/g/n + Bluetooth 5 (BLE) |
| USB | USB-C with USB-OTG support |
| Flash | 16MB |
| PSRAM | 2MB |
| Camera | OV2640 (not used in this project) |

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

## BLE Scanning (TODO)

To discover the AP2's BLE services, use **nRF Connect** app (iOS/Android):

### Steps:
1. Power on the AP2
2. Open nRF Connect and scan for devices
3. Look for a device named `xTool-*` or similar
4. Connect and explore services

### What to Look For:

The AP2 likely uses a **BLE UART** service. Common UUIDs:

| Service | UUID |
|---------|------|
| Nordic UART Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Nordic UART TX | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| Nordic UART RX | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| HM-10 BLE Serial | `0000FFE0-0000-1000-8000-00805F9B34FB` |
| HM-10 Characteristic | `0000FFE1-0000-1000-8000-00805F9B34FB` |

### Record:
- [ ] Device advertisement name
- [ ] Primary service UUID
- [ ] TX characteristic UUID (write to AP2)
- [ ] RX characteristic UUID (read from AP2)
- [ ] Any security/pairing requirements

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

| Command | Function |
|---------|----------|
| `M9039 V<0-4>` | Auto mode speed |
| `M9039 W<0-4>` | Manual mode speed |

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

## Web Interface

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
  "filters": {
    "H": {"name": "Pre-Filter", "percent": 85},
    "I": {"name": "Medium", "percent": 62},
    "J": {"name": "Carbon", "percent": 100},
    "K": {"name": "Cloth", "percent": 95},
    "L": {"name": "Formaldehyde", "percent": 45, "warning": true},
    "M": {"name": "HEPA", "percent": 78}
  },
  "buzzer": true
}
```

**POST** `/api/fan`
```json
{"speed": 3, "mode": "auto"}
```

**POST** `/api/buzzer`
```json
{"enabled": true}
```

### Web Dashboard

Simple responsive HTML page served from ESP32-S3 flash.

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

### Pin Configuration (Waveshare ESP32-S3-Touch-LCD-2)

| Function | GPIO | Description |
|----------|------|-------------|
| LCD_CS | 37 | SPI Chip Select |
| LCD_DC | 38 | Data/Command |
| LCD_RST | 39 | Reset |
| LCD_BL | 40 | Backlight |
| LCD_SCLK | 41 | SPI Clock |
| LCD_MOSI | 42 | SPI Data |
| TOUCH_SDA | 5 | I2C Data |
| TOUCH_SCL | 6 | I2C Clock |
| TOUCH_INT | 4 | Touch Interrupt |
| USB_D+ | 20 | USB OTG |
| USB_D- | 19 | USB OTG |
| BAT_ADC | 1 | Battery Voltage |

---

## Project Structure

```
xtool-ap2-status/
├── firmware/
│   └── ap2-status.ino      # Main Arduino sketch
├── Makefile                # Build system
└── README.md
```

---

## Roadmap

- [x] Protocol documentation
- [ ] BLE scan - test if AP2 is directly accessible
- [ ] ESP-IDF project setup
- [ ] LVGL display driver for Waveshare board
- [ ] F0F7 protocol implementation
- [ ] BLE client (direct connection)
- [ ] USB-OTG host (dongle fallback)
- [ ] Touch UI with filter bars
- [ ] Web API server
- [ ] OTA updates
- [ ] 3D printed case/stand
- [ ] Home Assistant integration

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
