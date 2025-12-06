# xTool AP2 Air Purifier Status Monitor

Reverse-engineered protocol documentation and status monitor for the xTool AP2 Air Purifier.

## Project Goal

Build a Raspberry Pi Pico W (or ESP32-S3) with TFT display showing:
- Filter status (all 6 filter elements)
- Fan speed / gear
- Auto/Manual mode
- Web interface for remote monitoring

## Protocol Documentation

### Communication Architecture

- **USB-Dongle**: CH340 USB-Serial Chip (VID: `1a86`, PID: `7523`)
- **Baud Rate**: `115200`
- **Protocol**: F0F7 Frame-based with M-Codes

### F0F7 Protocol Frame Format

```
[START_BYTE 0xF0] [COMMAND_BODY] [CHECKSUM] [END_BYTE 0xF7]

COMMAND_BODY = [PREFIX_BYTES] + [COMMAND_STRING] + [DELIMITER \n]
```

#### Protocol Constants

| Constant | Value | Description |
|----------|-------|-------------|
| START_BYTE | `0xF0` (240) | Frame start marker |
| END_BYTE | `0xF7` (247) | Frame end marker |
| CHECKSUM_MASK | `0x7F` (127) | Only lower 7 bits |
| DELIMITER | `\n` | Command terminator |

#### Checksum Calculation

```javascript
function calculateChecksum(commandBody) {
  let sum = 0;
  for (let i = 0; i < commandBody.length; i++) {
    sum += commandBody[i];
  }
  return sum & 0x7F;
}
```

### Device Identification

#### AccessoriesType Enum

| Type | Hex ID | Decimal | Description |
|------|--------|---------|-------------|
| LargePurifierV3 | `4C` | 76 | **AP2 Air Purifier** |
| LargePurifier | `45` | 69 | AP1 Air Purifier (older) |
| DuctFanV3 | `4E` | 78 | Inline Fan V3 |
| DuctFan | `46` | 70 | Inline Fan V1 |
| AirPumpV2 | `40` | 64 | Air Assist Pump V2 |
| AirPump | `3D` | 61 | Air Assist Pump V1 |
| Purifier | `34` | 52 | Small Purifier |

#### Prefix Bytes (for F0F7 Frame)

```javascript
// AP2 (LargePurifierV3) - This project's target
const AP2_PREFIX = [76, 115, 107, 1, 0];  // 0x4C 0x73 0x6B 0x01 0x00

// AP1 (LargePurifier) - For reference
const AP1_PREFIX = [69, 115, 96, 1, 0];   // 0x45 0x73 0x60 0x01 0x00

// DuctFan V3
const DUCTFAN_V3_PREFIX = [78, 115, 99, 1, 0];  // 0x4E 0x73 0x63 0x01 0x00
```

---

## AP2 Commands (LargePurifierV3)

### M9033 - Get Status

**Request:**
```
M9033
```

**Response Format:**
```
A<version> V<speed>|W<speed> H<val> I<val> J<val> K<val> L<val> M<val> F<buzzer> E:<serial>
```

**Example Response:**
```
A1.2.3 V3 H95 I87 J100 K100 L78 M92 F1 E:ABC123XYZ
```

#### Response Fields

| Field | Description | Values |
|-------|-------------|--------|
| `A<version>` | Firmware version | e.g., `A1.2.3` |
| `V<speed>` | Auto mode speed | 0-4 (present = auto mode active) |
| `W<speed>` | Manual mode speed | 0-4 (present = manual mode active) |
| `H` | Pre-Filter | 0-100% or -1 |
| `I` | Medium Filter | 0-100% or -1 |
| `J` | Activated Carbon | 0-100% or -1 |
| `K` | Carbon Cloth | 0-100% or -1 |
| `L` | Formaldehyde Removal (AP2 Max only) | 0-100% or -1 |
| `M` | HEPA Filter | 0-100% or -1 |
| `F<0\|1>` | Buzzer enabled | 0=off, 1=on |
| `E:<serial>` | Serial number | String |

#### Filter Value Interpretation

| Value | Meaning |
|-------|---------|
| 0-100 | Remaining life percentage |
| -1 | Filter not detected (RFID tag missing/defective) |
| ≤10 | Low filter life warning threshold |

### M9039 - Set Fan Speed

| Command | Function |
|---------|----------|
| `M9039 V<0-4>` | Set auto mode speed |
| `M9039 W<0-4>` | Set manual mode speed |
| `M9039 C<0-4>` | Set gear directly (AP1 compatibility) |

**Speed Levels:**
- `0` = Off
- `1` = Low
- `2` = Medium-Low
- `3` = Medium-High
- `4` = High

### M9046 - Buzzer Control

| Command | Function |
|---------|----------|
| `M9046 F0` | Buzzer off |
| `M9046 F1` | Buzzer on |

### M9032 - Get RC Version

```
M9032
```
Returns firmware RC version.

### M9055 - Filter Life Debug (Testing Only!)

```
M9055 W<which> A<which> B<total> C<used>
```
Used internally for filter life simulation/testing.

---

## Passthrough Commands (G198)

When communicating through the xTool laser (not direct BLE), commands are wrapped:

```
G198 P<accessory_id> "<command>"
```

### Examples

| Command | Function |
|---------|----------|
| `G198 P76 "M9033"` | Get AP2 status |
| `G198 P76 "M9039 V3"` | Set AP2 to auto mode speed 3 |
| `G198 P76 "M9039 W2"` | Set AP2 to manual mode speed 2 |
| `G198 P76 "M9039 V0"` | Turn AP2 off |
| `G198 P76 "M9046 F1"` | Enable AP2 buzzer |

---

## Filter Types

### AP2 Max (6 Filters)

| Field | Filter Type | German | Typical Lifetime |
|-------|-------------|--------|------------------|
| H | Pre-Filter | Vorfilter | ~300h |
| I | Medium Filter | Mittelfilter | ~300h |
| J | Activated Carbon | Aktivkohlefilter | ~300h |
| K | Carbon Cloth | Kohletuch | ~300h |
| L | Formaldehyde Removal | Formaldehydfilter | ~300h |
| M | High Efficiency (HEPA) | HEPA-Filter | ~300h |

### AP2 Standard (5 Filters)

Same as above, but without the `L` (Formaldehyde Removal) filter.

### RFID Recognition

Filters are recognized internally via **RFID tags**. The firmware tracks:
- Whether a filter is inserted (value or -1)
- Remaining lifetime as percentage
- Filter authenticity (original vs. third-party)

The software only receives percentage values - all RFID handling is internal to the AP2 firmware.

---

## Error Codes

| Code | Meaning |
|------|---------|
| `m9039_delete` | Purifier stopped abnormally |
| `m9039_s4` | Filter life low |
| `m9039_s2` | Filter not correctly inserted |
| `m9039_s1` | Filter door not closed |
| `NFC_ERROR` | RFID/NFC read error |
| `MULTIPLE IDENTICAL FILTER` | Duplicate filter RFID detected |

---

## State Object (JavaScript Reference)

```javascript
// LargePurifierV3 (AP2) State
{
  accessoryVersion: "",      // Firmware version e.g., "1.2.3"
  snCode: "",                // Serial number
  purifierGear: 0,           // 0=off, 1-4=speed levels
  isAuto: false,             // Auto mode active
  purifierTimeout: 0,        // Run-on time in ms
  purifierBuzzerEnable: false,

  // Filter life (percent, 0-100, -1=not detected)
  filterElementH: -1,        // Pre-Filter
  filterElementI: -1,        // Medium Filter
  filterElementJ: -1,        // Activated Carbon
  filterElementK: -1,        // Carbon Cloth
  filterElementL: -1,        // Formaldehyde Removal (AP2 Max only)
  filterElementM: -1         // HEPA Filter
}
```

---

## Hardware Options

### Option A: Raspberry Pi Pico W (BLE)

| Component | Description | Price |
|-----------|-------------|-------|
| Raspberry Pi Pico W | RP2040 + WiFi + BLE | ~8€ |
| TFT Display 2.4" ILI9341 | 320x240, SPI | ~10€ |
| 3D Printed Case | Optional | - |

**Pros:** Cheap, low power, native BLE
**Cons:** No USB-Host (can't use original dongle)

### Option B: ESP32-S3 (USB-OTG)

| Component | Description | Price |
|-----------|-------------|-------|
| ESP32-S3 DevKit | Dual-Core + WiFi + BLE + USB-OTG | ~12€ |
| TFT Display 2.4" ILI9341 | 320x240, SPI | ~10€ |

**Pros:** Can use original dongle via USB-OTG, powerful
**Cons:** Slightly more expensive

### Option C: Raspberry Pi Zero 2 W

Full Linux with USB-Host capability. Best for complex setups.

---

## Pinout (Pico W → TFT ILI9341)

```
Pico W          TFT ILI9341
-------         -----------
GP18 (SCK)  →   CLK
GP19 (MOSI) →   MOSI
GP17        →   CS
GP20        →   DC
GP21        →   RST
3V3         →   VCC
GND         →   GND
GP16 (MISO) →   MISO (optional)
```

---

## Planned Features

### Display UI
```
┌──────────────────────────┐
│  xTool AP2 Monitor       │
├──────────────────────────┤
│  Fan: ████░░ Gear 3      │
│  Mode: Auto              │
│                          │
│  Filters:                │
│  H ████████░░ 85%        │
│  I ██████░░░░ 62%        │
│  J ██████████ 100%       │
│  K █████████░ 95%        │
│  L ████░░░░░░ 45%  ⚠     │
│  M ███████░░░ 78%        │
│                          │
│  WiFi: ✓  192.168.1.42   │
└──────────────────────────┘
```

### Web Interface API

**Endpoint:** `http://<device-ip>/api/status`

```json
{
  "connected": true,
  "device": "AP2 Max",
  "firmware": "1.2.3",
  "serial": "ABC123XYZ",
  "fan": {
    "gear": 3,
    "auto": true
  },
  "filters": {
    "H": { "name": "Pre-Filter", "percent": 85 },
    "I": { "name": "Medium Filter", "percent": 62 },
    "J": { "name": "Activated Carbon", "percent": 100 },
    "K": { "name": "Carbon Cloth", "percent": 95 },
    "L": { "name": "Formaldehyde", "percent": 45, "warning": true },
    "M": { "name": "HEPA", "percent": 78 }
  },
  "buzzer": true
}
```

---

## TODO

- [ ] BLE scan with nRF Connect - is AP2 directly accessible?
- [ ] Live traffic sniffing (if xTool laser access available)
- [ ] Determine exact BLE service/characteristic UUIDs
- [ ] Hardware prototype
- [ ] MicroPython/CircuitPython firmware
- [ ] Web interface
- [ ] 3D printed case
- [ ] Home Assistant integration (optional)

---

## References

- Protocol reverse-engineered from xTool Studio app (Electron/ASAR)
- Device extensions extracted from `exts.zip`
- M-Codes documented from decompiled JavaScript

## License

MIT License - Use at your own risk. This is unofficial and not affiliated with xTool.

## Disclaimer

This project is for educational purposes. Modifying or controlling your AP2 with unofficial tools may void your warranty.
