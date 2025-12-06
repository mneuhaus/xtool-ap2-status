/**
 * xTool AP2 Air Purifier Status Monitor
 *
 * Target: Waveshare ESP32-S3-Touch-LCD-2
 * Display: 2.0" IPS 240x320, ST7789
 * Touch: CST816S (I2C)
 *
 * Features:
 * - Real-time filter status (6 filters H-M)
 * - Fan speed control via touch
 * - Auto/Manual mode switching
 * - WiFi web interface
 * - BLE connection to AP2 (or USB-OTG dongle fallback)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ============================================================================
// Pin Definitions - Waveshare ESP32-S3-Touch-LCD-2
// ============================================================================

// LCD (ST7789) - SPI
#define LCD_CS     37
#define LCD_DC     38
#define LCD_RST    39
#define LCD_BL     40
#define LCD_SCLK   41
#define LCD_MOSI   42
#define LCD_MISO   -1  // Not used

// Touch (CST816S) - I2C
#define TOUCH_SDA  5
#define TOUCH_SCL  6
#define TOUCH_INT  4
#define TOUCH_RST  -1  // Connected to LCD_RST

// QMI8658 IMU - I2C (same bus as touch)
#define IMU_SDA    5
#define IMU_SCL    6

// SD Card - SPI
#define SD_CS      47
#define SD_MOSI    48
#define SD_MISO    45
#define SD_SCLK    46

// USB OTG (for dongle fallback)
#define USB_DP     20
#define USB_DN     19

// Battery ADC
#define BAT_ADC    1

// ============================================================================
// Display Configuration
// ============================================================================

#define SCREEN_WIDTH   240
#define SCREEN_HEIGHT  320

// ============================================================================
// AP2 Protocol Constants
// ============================================================================

const uint8_t AP2_PREFIX[] = {0x4C, 0x73, 0x6B, 0x01, 0x00};
const uint8_t FRAME_START = 0xF0;
const uint8_t FRAME_END = 0xF7;
const uint8_t CHECKSUM_MASK = 0x7F;

// ============================================================================
// AP2 State Structure
// ============================================================================

struct AP2State {
  bool connected = false;
  String firmware = "";
  String serial = "";

  uint8_t fanSpeed = 0;      // 0-4
  bool isAutoMode = false;
  bool buzzerEnabled = false;

  // Filter life (0-100, -1 = not detected)
  int8_t filterH = -1;  // Pre-Filter
  int8_t filterI = -1;  // Medium Filter
  int8_t filterJ = -1;  // Activated Carbon
  int8_t filterK = -1;  // Carbon Cloth
  int8_t filterL = -1;  // Formaldehyde (AP2 Max)
  int8_t filterM = -1;  // HEPA
};

AP2State ap2;

// ============================================================================
// WiFi Configuration
// ============================================================================

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

WebServer server(80);

// ============================================================================
// Forward Declarations
// ============================================================================

void setupDisplay();
void setupTouch();
void setupWiFi();
void setupWebServer();
void updateDisplay();
void handleRoot();
void handleApiStatus();
void handleApiFan();

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("   xTool AP2 Status Monitor");
  Serial.println("   Waveshare ESP32-S3-Touch-LCD-2");
  Serial.println("========================================");
  Serial.println();

  // Initialize display backlight
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  Serial.println("[INIT] Display...");
  setupDisplay();

  Serial.println("[INIT] Touch...");
  setupTouch();

  Serial.println("[INIT] WiFi...");
  setupWiFi();

  Serial.println("[INIT] Web Server...");
  setupWebServer();

  Serial.println();
  Serial.println("[READY] System initialized");
  Serial.println();

  // Show initial state
  updateDisplay();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Handle web requests
  server.handleClient();

  // TODO: Poll AP2 status via BLE or USB
  // TODO: Handle touch events
  // TODO: Update display periodically

  delay(10);
}

// ============================================================================
// Display Functions (TODO: Implement with LVGL)
// ============================================================================

void setupDisplay() {
  // TODO: Initialize ST7789 with LVGL
  // For now, just log
  Serial.println("  Display: 240x320 ST7789");
}

void setupTouch() {
  // TODO: Initialize CST816S touch controller
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Serial.println("  Touch: CST816S @ I2C");
}

void updateDisplay() {
  // TODO: Update LVGL UI with current AP2 state
  Serial.println("[UI] Display update");
}

// ============================================================================
// WiFi Functions
// ============================================================================

void setupWiFi() {
  // For now, create AP mode for testing
  WiFi.mode(WIFI_AP);
  WiFi.softAP("AP2-Monitor", "ap2status");

  Serial.print("  WiFi AP: AP2-Monitor");
  Serial.print(" @ ");
  Serial.println(WiFi.softAPIP());
}

// ============================================================================
// Web Server Functions
// ============================================================================

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/fan", HTTP_POST, handleApiFan);
  server.begin();
  Serial.println("  HTTP Server started");
}

void handleRoot() {
  // Simple status page - data loaded via API
  String html = F("<!DOCTYPE html><html><head><title>AP2 Status</title>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<style>");
  html += F("body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px;background:#1a1a2e;color:#eee}");
  html += F("h1{color:#00d4ff;font-size:1.5em}");
  html += F(".card{background:#16213e;border-radius:12px;padding:16px;margin:12px 0}");
  html += F(".fan{font-size:2em;text-align:center}");
  html += F(".bar{height:20px;background:#0f3460;border-radius:10px;margin:8px 0}");
  html += F(".fill{height:100%;background:#00d4ff;border-radius:10px}");
  html += F("</style></head><body>");
  html += F("<h1>xTool AP2 Monitor</h1>");
  html += F("<div class='card'><div class='fan' id='fan'>-- %</div></div>");
  html += F("<div class='card'><h3>Filters</h3><div id='filters'>Loading...</div></div>");
  html += F("<div class='card'><div id='status'>Connecting...</div></div>");
  html += F("<script>");
  html += F("setInterval(()=>fetch('/api/status').then(r=>r.json()).then(d=>{");
  html += F("document.getElementById('fan').textContent=d.fan.speed+' ('+d.fan.mode+')';");
  html += F("document.getElementById('status').textContent=d.connected?'Connected':'Disconnected';");
  html += F("let h='';for(let k in d.filters){let f=d.filters[k];");
  html += F("h+='<div>'+f.name+': '+f.percent+'%</div>';");
  html += F("h+='<div class=bar><div class=fill style=width:'+Math.max(0,f.percent)+'%></div></div>';}");
  html += F("document.getElementById('filters').textContent='';");
  html += F("let div=document.createElement('div');div.textContent=h;");
  html += F("document.getElementById('filters').appendChild(div);");
  html += F("}),2000);");
  html += F("</script></body></html>");

  server.send(200, "text/html", html);
}

void handleApiStatus() {
  String json = "{";
  json += "\"connected\":" + String(ap2.connected ? "true" : "false") + ",";
  json += "\"firmware\":\"" + ap2.firmware + "\",";
  json += "\"serial\":\"" + ap2.serial + "\",";
  json += "\"fan\":{";
  json += "\"speed\":" + String(ap2.fanSpeed) + ",";
  json += "\"mode\":\"" + String(ap2.isAutoMode ? "auto" : "manual") + "\"";
  json += "},";
  json += "\"filters\":{";
  json += "\"H\":{\"name\":\"Pre-Filter\",\"percent\":" + String(ap2.filterH) + "},";
  json += "\"I\":{\"name\":\"Medium\",\"percent\":" + String(ap2.filterI) + "},";
  json += "\"J\":{\"name\":\"Carbon\",\"percent\":" + String(ap2.filterJ) + "},";
  json += "\"K\":{\"name\":\"Cloth\",\"percent\":" + String(ap2.filterK) + "},";
  json += "\"L\":{\"name\":\"Formaldehyde\",\"percent\":" + String(ap2.filterL) + "},";
  json += "\"M\":{\"name\":\"HEPA\",\"percent\":" + String(ap2.filterM) + "}";
  json += "},";
  json += "\"buzzer\":" + String(ap2.buzzerEnabled ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleApiFan() {
  // TODO: Parse JSON and send command to AP2
  server.send(200, "application/json", "{\"ok\":true}");
}

// ============================================================================
// AP2 Protocol Functions (TODO: Implement)
// ============================================================================

/**
 * Calculate F0F7 frame checksum
 */
uint8_t calculateChecksum(const uint8_t* data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum & CHECKSUM_MASK;
}

/**
 * Build F0F7 protocol frame
 * Frame: [0xF0] [PREFIX] [CMD] [\n] [CHECKSUM] [0xF7]
 */
size_t buildFrame(uint8_t* buffer, const char* cmd) {
  size_t pos = 0;

  // Start byte
  buffer[pos++] = FRAME_START;

  // Prefix
  for (int i = 0; i < 5; i++) {
    buffer[pos++] = AP2_PREFIX[i];
  }

  // Command
  size_t cmdLen = strlen(cmd);
  memcpy(&buffer[pos], cmd, cmdLen);
  pos += cmdLen;

  // Delimiter
  buffer[pos++] = '\n';

  // Checksum (over prefix + cmd + delimiter)
  uint8_t checksum = calculateChecksum(&buffer[1], pos - 1);
  buffer[pos++] = checksum;

  // End byte
  buffer[pos++] = FRAME_END;

  return pos;
}

/**
 * Parse M9033 status response
 * Format: A<ver> V<spd>|W<spd> H<n> I<n> J<n> K<n> L<n> M<n> F<b> E:<sn>
 */
void parseStatusResponse(const String& response) {
  // TODO: Parse response and update ap2 state
  Serial.print("[AP2] Response: ");
  Serial.println(response);
}
