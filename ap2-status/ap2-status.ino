/**
 * xTool AP2 Luftfilter Monitor
 *
 * Target: Waveshare ESP32-S3-Touch-LCD-2
 * Display: 240x320 ST7789T3 with LVGL
 * Touch: CST816D
 *
 * Shows filter status with remaining hours
 */

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <NimBLEDevice.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "bsp_cst816.h"

Preferences preferences;
WebServer webServer(80);

// Laser runtime tracking
unsigned long laserTotalSeconds = 0;  // Total laser runtime in seconds
unsigned long laserSessionStart = 0;  // When current session started
bool laserRunning = false;

// SD Card & Logging
bool sdCardOK = false;
unsigned long lastLogTime = 0;
#define LOG_INTERVAL_MS (1 * 60 * 1000)  // Log every 1 minute for testing
#define LOG_FILE "/ap2_history.csv"

// WiFi credentials
const char* WIFI_SSID = "neuhaus.nrw";
const char* WIFI_PASS = "galactic.poop.bear";
bool wifiConnected = false;

// BLE AP2 connection
NimBLEClient* bleClient = nullptr;
NimBLERemoteCharacteristic* bleWriteChar = nullptr;
NimBLERemoteCharacteristic* bleNotifyChar = nullptr;
bool bleConnected = false;
bool bleScanning = false;
String ap2Address = "";  // Will be found by scanning

// AP2 BLE UUIDs (from protocol docs)
#define AP2_SERVICE_UUID        "0000ffe0-0000-1000-8000-00805f9b34fb"
#define AP2_CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

// ============================================================================
// Pin Definitions (from Waveshare demo)
// ============================================================================

#define LCD_SCLK   39
#define LCD_MOSI   38
#define LCD_MISO   40
#define LCD_DC     42
#define LCD_RST    -1
#define LCD_CS     45
#define LCD_BL     1

#define TOUCH_SDA  48
#define TOUCH_SCL  47

// Haptic feedback - Buzzer or Piezo Vibrator (connect between GPIO4 and GND)
// GPIO4 is available on the expansion header
#define BUZZER_PIN 4
#define USE_VIBRATION_MOTOR true  // true = piezo vibrator, false = passive buzzer

// SD Card pins (directly on Waveshare ESP32-S3-Touch-LCD-2 TF slot)
#define SD_CS    41
#define SD_MOSI  40  // Directly on TF socket
#define SD_MISO  13
#define SD_SCK   12

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// ============================================================================
// Display Objects
// ============================================================================

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, LCD_RST, 0, true, SCREEN_WIDTH, SCREEN_HEIGHT);

// LVGL
uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
lv_disp_draw_buf_t draw_buf;
lv_color_t *disp_draw_buf;
lv_disp_drv_t disp_drv;

// ============================================================================
// Filter Data
// ============================================================================

struct Filter {
  const char* name;
  const char* id;
  uint16_t maxHours;     // Total lifetime in hours
  int16_t remainingHours; // Current remaining (-1 = not detected)
  lv_color_t color;      // Base color for the filter
};

// Filter specs from xTool (matching the image)
Filter filters[6] = {
  {"Zyklon",           "0", 8,    6,    lv_color_hex(0x4CAF50)},  // Green - needs cleaning every 8h
  {"Vorfilter",        "H", 100,  85,   lv_color_hex(0x8BC34A)},  // Light green
  {"Medium Filter",    "I", 200,  144,  lv_color_hex(0x4CAF50)},  // Green
  {"Aktivkohle",       "J", 600,  480,  lv_color_hex(0x26A69A)},  // Teal
  {"Carbon Mesh",      "K", 300,  195,  lv_color_hex(0x9C27B0)},  // Purple
  {"HEPA",             "M", 300,  234,  lv_color_hex(0xE91E63)},  // Pink
};

struct {
  int connectionState = 0;  // 0 = searching, 1 = connected, 2 = error (no connection possible)
  bool fanRunning = false;
} ap2;

// ============================================================================
// UI Elements
// ============================================================================

lv_obj_t *headerBar;
lv_obj_t *statusLabel;
lv_obj_t *fanLabel;
lv_obj_t *filterBars[6];
lv_obj_t *filterNameLabels[6];
lv_obj_t *filterHourLabels[6];
lv_obj_t *filterHourShadows[6];
lv_obj_t *resetDialog = NULL;
lv_obj_t *pullDownMenu = NULL;
lv_obj_t *fanDelayLabel = NULL;

// Longpress tracking
unsigned long touchStartTime = 0;
bool touchActive = false;
bool dialogShown = false;

// Pull-down menu tracking
int16_t touchStartY = -1;
int16_t touchStartX = -1;
bool pullDownOpen = false;
int fanDelaySeconds = 30;  // Default 30 seconds fan delay

// ============================================================================
// Haptic/Buzzer Functions
// ============================================================================

#if USE_VIBRATION_MOTOR
// ===== Vibration Motor Mode =====

void vibrateOn() {
  digitalWrite(BUZZER_PIN, HIGH);
}

void vibrateOff() {
  digitalWrite(BUZZER_PIN, LOW);
}

void vibratePulse(uint16_t duration) {
  vibrateOn();
  delay(duration);
  vibrateOff();
}

// Burst vibration - rapid on/off feels more intense
void vibrateBurst(uint16_t duration, uint8_t pulseMs = 5) {
  unsigned long start = millis();
  while (millis() - start < duration) {
    vibrateOn();
    delay(pulseMs);
    vibrateOff();
    delay(pulseMs / 2);
  }
}

void buzzerClick() {
  // CHUNKY tap for makerspace
  vibratePulse(100);
}

void buzzerConfirm() {
  // Double THUMP
  vibratePulse(80);
  delay(80);
  vibratePulse(120);
}

void buzzerWarning() {
  // 3 big pulses
  for (int i = 0; i < 3; i++) {
    vibratePulse(100);
    delay(100);
  }
}

void buzzerAlarm() {
  // INTENSE - full power pulses
  for (int i = 0; i < 4; i++) {
    vibratePulse(250);
    delay(150);
  }
}

void buzzerConnect() {
  // Rising THUMP
  vibratePulse(80);
  delay(60);
  vibratePulse(150);
}

void buzzerOomph() {
  // Triple OOMPH - makerspace approved
  for (int i = 0; i < 3; i++) {
    vibratePulse(180);
    delay(150);
  }
}

void buzzerDisconnect() {
  // Falling pattern
  vibratePulse(60);
  delay(40);
  vibratePulse(30);
}

void initHaptic() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

#else
// ===== Passive Buzzer Mode =====

void buzzerTone(uint16_t freq, uint16_t duration) {
  ledcWriteTone(BUZZER_PIN, freq);
  delay(duration);
  ledcWriteTone(BUZZER_PIN, 0);
}

void buzzerClick() {
  // Quick click - deep 200Hz
  buzzerTone(200, 30);
}

void buzzerConfirm() {
  // Happy ascending tone
  buzzerTone(1000, 80);
  delay(50);
  buzzerTone(1500, 80);
  delay(50);
  buzzerTone(2000, 100);
}

void buzzerWarning() {
  // 3 short beeps
  for (int i = 0; i < 3; i++) {
    buzzerTone(2500, 100);
    delay(100);
  }
}

void buzzerAlarm() {
  // Warbling tone
  for (int i = 0; i < 4; i++) {
    buzzerTone(2000, 100);
    buzzerTone(3000, 100);
  }
}

void buzzerConnect() {
  // Rising tone
  buzzerTone(800, 100);
  delay(50);
  buzzerTone(1200, 150);
}

void buzzerDisconnect() {
  // Falling tone
  buzzerTone(1200, 100);
  delay(50);
  buzzerTone(800, 150);
}

void initHaptic() {
  ledcAttach(BUZZER_PIN, 2000, 8);
}

#endif

// Colors
#define COLOR_BG       lv_color_hex(0x121212)
#define COLOR_CARD     lv_color_hex(0x1e1e1e)
#define COLOR_HEADER   lv_color_hex(0x1a1a1a)
#define COLOR_GREEN    lv_color_hex(0x4CAF50)
#define COLOR_YELLOW   lv_color_hex(0xFFEB3B)
#define COLOR_ORANGE   lv_color_hex(0xFF9800)
#define COLOR_RED      lv_color_hex(0xf44336)
#define COLOR_GRAY     lv_color_hex(0x424242)
#define COLOR_WHITE    lv_color_hex(0xffffff)
#define COLOR_CYAN     lv_color_hex(0x00BCD4)

// ============================================================================
// LVGL Callbacks
// ============================================================================

void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  lv_disp_flush_ready(disp_drv);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  uint16_t x, y;
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

lv_color_t getFilterColor(int percent) {
  if (percent > 50) return COLOR_GREEN;
  if (percent > 25) return COLOR_YELLOW;
  if (percent > 10) return COLOR_ORANGE;
  return COLOR_RED;
}

// ============================================================================
// UI Update Functions
// ============================================================================

void updateStatusDisplay() {
  // Header background color based on state priority:
  // 1. Fan running -> green
  // 2. Connection error -> red
  // 3. Searching -> yellow
  // 4. Connected (idle) -> neutral/header color

  lv_color_t headerColor;

  if (ap2.fanRunning) {
    headerColor = COLOR_GREEN;
  } else if (ap2.connectionState == 2) {
    headerColor = COLOR_RED;
  } else if (ap2.connectionState == 0) {
    headerColor = COLOR_YELLOW;
  } else {
    headerColor = COLOR_HEADER;  // neutral dark
  }

  lv_obj_set_style_bg_color(headerBar, headerColor, 0);

  // Bluetooth icon (white on colored background)
  lv_label_set_text(statusLabel, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(statusLabel, COLOR_WHITE, 0);

  // Fan status text
  if (ap2.fanRunning) {
    lv_label_set_text(fanLabel, LV_SYMBOL_REFRESH " An");
  } else {
    lv_label_set_text(fanLabel, LV_SYMBOL_REFRESH " Aus");
  }
  lv_obj_set_style_text_color(fanLabel, COLOR_WHITE, 0);
}

// Track if we already alarmed for Zyklon at 0
bool zyklonAlarmTriggered = false;

void updateFilterBars() {
  for (int i = 0; i < 6; i++) {
    Filter &f = filters[i];

    if (f.remainingHours >= 0) {
      int percent = (f.remainingHours * 100) / f.maxHours;
      percent = constrain(percent, 0, 100);

      // Color based on percentage
      lv_color_t color = getFilterColor(percent);

      if (percent == 0) {
        // At 0%: fill entire bar with red background
        lv_bar_set_value(filterBars[i], 100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(filterBars[i], COLOR_RED, LV_PART_MAIN);
        lv_obj_set_style_bg_color(filterBars[i], COLOR_RED, LV_PART_INDICATOR);

        // Alarm when Zyklon (filter 0) hits 0 hours
        if (i == 0 && !zyklonAlarmTriggered) {
          buzzerAlarm();
          zyklonAlarmTriggered = true;
          Serial.println("[ALARM] Zyklon bei 0h - bitte reinigen!");
        }
      } else {
        // Reset alarm flag if Zyklon has hours again
        if (i == 0) zyklonAlarmTriggered = false;
        // Normal: gray background, colored indicator
        lv_bar_set_value(filterBars[i], percent, LV_ANIM_ON);
        lv_obj_set_style_bg_color(filterBars[i], COLOR_GRAY, LV_PART_MAIN);
        lv_obj_set_style_bg_color(filterBars[i], color, LV_PART_INDICATOR);
      }

      // Update hours label and shadow
      char buf[16];
      snprintf(buf, sizeof(buf), "%dh", f.remainingHours);
      lv_label_set_text(filterHourLabels[i], buf);
      lv_label_set_text(filterHourShadows[i], buf);
      lv_obj_set_style_text_color(filterHourLabels[i], COLOR_WHITE, 0);
    } else {
      lv_bar_set_value(filterBars[i], 0, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(filterBars[i], COLOR_GRAY, LV_PART_MAIN);
      lv_obj_set_style_bg_color(filterBars[i], COLOR_GRAY, LV_PART_INDICATOR);
      lv_label_set_text(filterHourLabels[i], "N/A");
      lv_label_set_text(filterHourShadows[i], "N/A");
      lv_obj_set_style_text_color(filterHourLabels[i], COLOR_GRAY, 0);
    }
  }
}

// ============================================================================
// Reset Dialog
// ============================================================================

void resetDialogCallback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  uint32_t btnId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

  if (btnId == 1) {
    // "Ja" pressed - reset Zyklon
    filters[0].remainingHours = filters[0].maxHours;
    updateFilterBars();
    buzzerOomph();  // Oomph oomph oomph confirmation
    Serial.println("Zyklon Filter zurueckgesetzt!");
  } else {
    buzzerClick();  // Cancel click
  }

  // Close dialog
  if (resetDialog) {
    lv_obj_del(resetDialog);
    resetDialog = NULL;
  }
  dialogShown = false;
}

void showResetDialog() {
  if (resetDialog != NULL) return;
  dialogShown = true;

  // Create modal background - larger dialog
  resetDialog = lv_obj_create(lv_scr_act());
  lv_obj_set_size(resetDialog, 220, 220);
  lv_obj_center(resetDialog);
  lv_obj_set_style_bg_color(resetDialog, COLOR_CARD, 0);
  lv_obj_set_style_border_color(resetDialog, COLOR_CYAN, 0);
  lv_obj_set_style_border_width(resetDialog, 2, 0);
  lv_obj_set_style_radius(resetDialog, 10, 0);
  lv_obj_set_style_pad_all(resetDialog, 15, 0);

  // Title (ASCII only - no umlauts)
  lv_obj_t *title = lv_label_create(resetDialog);
  lv_label_set_text(title, "Zyklon gereinigt?");
  lv_obj_set_style_text_color(title, COLOR_WHITE, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  // Message (ASCII only)
  lv_obj_t *msg = lv_label_create(resetDialog);
  lv_label_set_text(msg, "Betriebszeit\nzuruecksetzen?");
  lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 35);

  // "Ja, zuruecksetzen" button - full width, on top
  lv_obj_t *btnYes = lv_btn_create(resetDialog);
  lv_obj_set_size(btnYes, 180, 45);
  lv_obj_align(btnYes, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_color(btnYes, COLOR_GREEN, 0);
  lv_obj_set_style_radius(btnYes, 6, 0);
  lv_obj_add_event_cb(btnYes, resetDialogCallback, LV_EVENT_PRESSED, (void*)1);

  lv_obj_t *labelYes = lv_label_create(btnYes);
  lv_label_set_text(labelYes, "Ja, zuruecksetzen");
  lv_obj_set_style_text_font(labelYes, &lv_font_montserrat_14, 0);
  lv_obj_center(labelYes);

  // "Abbrechen" button - full width, below
  lv_obj_t *btnNo = lv_btn_create(resetDialog);
  lv_obj_set_size(btnNo, 180, 45);
  lv_obj_align(btnNo, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(btnNo, COLOR_GRAY, 0);
  lv_obj_set_style_radius(btnNo, 6, 0);
  lv_obj_add_event_cb(btnNo, resetDialogCallback, LV_EVENT_PRESSED, (void*)0);

  lv_obj_t *labelNo = lv_label_create(btnNo);
  lv_label_set_text(labelNo, "Abbrechen");
  lv_obj_set_style_text_font(labelNo, &lv_font_montserrat_14, 0);
  lv_obj_center(labelNo);
}

// ============================================================================
// Pull-Down Menu
// ============================================================================

void saveSettings() {
  preferences.begin("ap2", false);
  preferences.putInt("fanDelay", fanDelaySeconds);
  preferences.putULong("laserTotal", laserTotalSeconds);
  preferences.end();
  Serial.printf("Einstellungen gespeichert: Nachlauf=%ds, Laser=%lus\n", fanDelaySeconds, laserTotalSeconds);
}

void loadSettings() {
  preferences.begin("ap2", true);
  fanDelaySeconds = preferences.getInt("fanDelay", 30);  // Default 30s
  laserTotalSeconds = preferences.getULong("laserTotal", 0);
  preferences.end();
  Serial.printf("Einstellungen geladen: Nachlauf=%ds, Laser=%lus\n", fanDelaySeconds, laserTotalSeconds);
}

String formatDuration(unsigned long totalSeconds) {
  unsigned long hours = totalSeconds / 3600;
  unsigned long mins = (totalSeconds % 3600) / 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%luh %02lum", hours, mins);
  return String(buf);
}

// ============================================================================
// SD Card & Data Logging
// ============================================================================

SPIClass sdSPI(HSPI);

bool initSDCard() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("[SD] Karte nicht gefunden!");
    return false;
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("[SD] Karte erkannt: %lluMB\n", cardSize);

  // Create CSV header if file doesn't exist
  if (!SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (f) {
      f.println("timestamp,uptime_s,laser_s,zyklon_h,vorfilter_h,medium_h,aktivkohle_h,carbon_h,hepa_h,fan_on,connected");
      f.close();
      Serial.println("[SD] Log-Datei erstellt");
    }
  }

  return true;
}

void logDataPoint() {
  if (!sdCardOK) return;

  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("[SD] Fehler beim Oeffnen der Log-Datei!");
    return;
  }

  // Format: timestamp,uptime_s,laser_s,filter1_h,...,filter6_h,fan_on,connected
  unsigned long uptime = millis() / 1000;

  char line[256];
  snprintf(line, sizeof(line), "%lu,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d",
    uptime,  // Will be replaced with real timestamp when NTP works
    uptime,
    laserTotalSeconds,
    filters[0].remainingHours,
    filters[1].remainingHours,
    filters[2].remainingHours,
    filters[3].remainingHours,
    filters[4].remainingHours,
    filters[5].remainingHours,
    ap2.fanRunning ? 1 : 0,
    ap2.connectionState == 1 ? 1 : 0
  );

  f.println(line);
  f.close();

  Serial.printf("[SD] Datenpunkt geloggt: %s\n", line);
}

// Calculate estimated days until filter needs replacement based on usage rate
String getFilterPrediction(int filterIdx) {
  if (!sdCardOK) return "?";

  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) return "?";

  // We need at least 2 data points to calculate rate
  // Read first and last values for this filter
  f.readStringUntil('\n');  // Skip header

  int firstValue = -1;
  int lastValue = -1;
  unsigned long firstTime = 0;
  unsigned long lastTime = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Parse CSV
    int idx = 0;
    String parts[11];
    int partIdx = 0;
    for (int i = 0; i <= (int)line.length() && partIdx < 11; i++) {
      if (i == (int)line.length() || line[i] == ',') {
        parts[partIdx++] = line.substring(idx, i);
        idx = i + 1;
      }
    }

    if (partIdx >= 9) {
      unsigned long timestamp = parts[1].toInt();
      int filterValue = parts[3 + filterIdx].toInt();  // Filter values start at index 3

      if (firstValue < 0) {
        firstValue = filterValue;
        firstTime = timestamp;
      }
      lastValue = filterValue;
      lastTime = timestamp;
    }
  }
  f.close();

  // Calculate usage rate
  if (firstValue < 0 || lastValue < 0 || lastTime <= firstTime) {
    return "?";
  }

  int hoursDiff = firstValue - lastValue;  // Hours consumed
  unsigned long timeDiffSec = lastTime - firstTime;

  if (hoursDiff <= 0 || timeDiffSec < 3600) {
    // Not enough usage data yet (need at least 1 hour of data)
    return "...";
  }

  // Calculate hours per day usage rate
  float hoursPerDay = (float)hoursDiff / ((float)timeDiffSec / 86400.0);

  if (hoursPerDay < 0.1) {
    return ">1 Jahr";
  }

  // Days until empty
  float daysRemaining = (float)lastValue / hoursPerDay;

  if (daysRemaining > 365) {
    return ">1 Jahr";
  } else if (daysRemaining > 30) {
    int months = (int)(daysRemaining / 30);
    return String(months) + " Mon.";
  } else if (daysRemaining > 7) {
    int weeks = (int)(daysRemaining / 7);
    return String(weeks) + " Wo.";
  } else {
    return String((int)daysRemaining) + " Tage";
  }
}

String getHistoryData(int maxPoints = 288) {
  // Return last N data points as JSON array (288 = 48h bei 10min interval)
  if (!sdCardOK) return "[]";

  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) return "[]";

  // Count lines first
  int lineCount = 0;
  while (f.available()) {
    if (f.read() == '\n') lineCount++;
  }
  f.seek(0);

  // Skip header
  f.readStringUntil('\n');
  lineCount--;

  // Skip to last N lines
  int skipLines = max(0, lineCount - maxPoints);
  for (int i = 0; i < skipLines; i++) {
    f.readStringUntil('\n');
  }

  String json = "[";
  bool first = true;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Parse CSV: timestamp,uptime_s,laser_s,zyklon_h,vorfilter_h,medium_h,aktivkohle_h,carbon_h,hepa_h,fan_on,connected
    int idx = 0;
    String parts[11];
    int partIdx = 0;
    for (int i = 0; i <= line.length() && partIdx < 11; i++) {
      if (i == line.length() || line[i] == ',') {
        parts[partIdx++] = line.substring(idx, i);
        idx = i + 1;
      }
    }

    if (partIdx >= 9) {
      if (!first) json += ",";
      first = false;

      json += "{\"t\":" + parts[1];  // uptime as timestamp
      json += ",\"l\":" + parts[2];  // laser
      json += ",\"f\":[" + parts[3] + "," + parts[4] + "," + parts[5] + "," + parts[6] + "," + parts[7] + "," + parts[8] + "]";
      json += "}";
    }
  }

  f.close();
  json += "]";
  return json;
}

void updateFanDelayLabel() {
  if (fanDelayLabel) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", fanDelaySeconds);
    lv_label_set_text(fanDelayLabel, buf);
  }
}

void pullDownBtnCallback(lv_event_t *e) {
  uint32_t btnId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

  if (btnId == 1) {
    // Zyklon Reset button - show confirmation dialog
    buzzerClick();
    closePullDownMenu();
    showResetDialog();
  } else if (btnId == 2) {
    // Fan delay minus (5s steps)
    if (fanDelaySeconds >= 5) {
      fanDelaySeconds -= 5;
      updateFanDelayLabel();
      saveSettings();
      buzzerClick();
    }
  } else if (btnId == 3) {
    // Fan delay plus (5s steps, max 120s = 2min)
    if (fanDelaySeconds < 120) {
      fanDelaySeconds += 5;
      updateFanDelayLabel();
      saveSettings();
      buzzerClick();
    }
  }
}

void closePullDownMenu() {
  if (pullDownMenu) {
    lv_obj_del(pullDownMenu);
    pullDownMenu = NULL;
    fanDelayLabel = NULL;
  }
  pullDownOpen = false;
}

void showPullDownMenu() {
  if (pullDownMenu != NULL) return;
  pullDownOpen = true;

  // Create pull-down panel with shadow effect
  pullDownMenu = lv_obj_create(lv_scr_act());
  lv_obj_set_size(pullDownMenu, 240, 180);
  lv_obj_set_pos(pullDownMenu, 0, 0);
  lv_obj_set_style_bg_color(pullDownMenu, COLOR_CARD, 0);
  lv_obj_set_style_border_width(pullDownMenu, 1, 0);
  lv_obj_set_style_border_color(pullDownMenu, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_side(pullDownMenu, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(pullDownMenu, 0, 0);
  lv_obj_set_style_pad_all(pullDownMenu, 15, 0);
  lv_obj_set_style_shadow_width(pullDownMenu, 20, 0);
  lv_obj_set_style_shadow_color(pullDownMenu, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(pullDownMenu, LV_OPA_50, 0);
  lv_obj_set_style_shadow_ofs_y(pullDownMenu, 8, 0);
  lv_obj_clear_flag(pullDownMenu, LV_OBJ_FLAG_SCROLLABLE);  // No scrolling

  // Title
  lv_obj_t *title = lv_label_create(pullDownMenu);
  lv_label_set_text(title, "Einstellungen");
  lv_obj_set_style_text_color(title, COLOR_WHITE, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  // Pull tab indicator (swipe up to close)
  lv_obj_t *pullTab = lv_obj_create(pullDownMenu);
  lv_obj_set_size(pullTab, 40, 4);
  lv_obj_align(pullTab, LV_ALIGN_BOTTOM_MID, 0, 5);
  lv_obj_set_style_bg_color(pullTab, COLOR_GRAY, 0);
  lv_obj_set_style_border_width(pullTab, 0, 0);
  lv_obj_set_style_radius(pullTab, 2, 0);

  // ===== Zyklon Reset Button =====
  lv_obj_t *btnZyklon = lv_btn_create(pullDownMenu);
  lv_obj_set_size(btnZyklon, 200, 40);
  lv_obj_align(btnZyklon, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_style_bg_color(btnZyklon, COLOR_ORANGE, 0);
  lv_obj_set_style_radius(btnZyklon, 6, 0);
  lv_obj_add_event_cb(btnZyklon, pullDownBtnCallback, LV_EVENT_PRESSED, (void*)1);

  lv_obj_t *labelZyklon = lv_label_create(btnZyklon);
  lv_label_set_text(labelZyklon, "Zyklon zuruecksetzen");
  lv_obj_set_style_text_font(labelZyklon, &lv_font_montserrat_16, 0);
  lv_obj_center(labelZyklon);

  // ===== Fan Delay Spinner =====
  lv_obj_t *fanDelayTitle = lv_label_create(pullDownMenu);
  lv_label_set_text(fanDelayTitle, "Nachlaufzeit");
  lv_obj_set_style_text_color(fanDelayTitle, COLOR_WHITE, 0);
  lv_obj_set_style_text_font(fanDelayTitle, &lv_font_montserrat_16, 0);
  lv_obj_align(fanDelayTitle, LV_ALIGN_TOP_MID, 0, 80);

  // Minus button
  lv_obj_t *btnMinus = lv_btn_create(pullDownMenu);
  lv_obj_set_size(btnMinus, 50, 40);
  lv_obj_align(btnMinus, LV_ALIGN_TOP_LEFT, 20, 105);
  lv_obj_set_style_bg_color(btnMinus, COLOR_GRAY, 0);
  lv_obj_set_style_radius(btnMinus, 6, 0);
  lv_obj_add_event_cb(btnMinus, pullDownBtnCallback, LV_EVENT_PRESSED, (void*)2);

  lv_obj_t *labelMinus = lv_label_create(btnMinus);
  lv_label_set_text(labelMinus, "-");
  lv_obj_set_style_text_font(labelMinus, &lv_font_montserrat_20, 0);
  lv_obj_center(labelMinus);

  // Value display
  fanDelayLabel = lv_label_create(pullDownMenu);
  lv_obj_set_style_text_color(fanDelayLabel, COLOR_WHITE, 0);
  lv_obj_set_style_text_font(fanDelayLabel, &lv_font_montserrat_20, 0);
  lv_obj_align(fanDelayLabel, LV_ALIGN_TOP_MID, 0, 112);
  updateFanDelayLabel();

  // Plus button
  lv_obj_t *btnPlus = lv_btn_create(pullDownMenu);
  lv_obj_set_size(btnPlus, 50, 40);
  lv_obj_align(btnPlus, LV_ALIGN_TOP_RIGHT, -20, 105);
  lv_obj_set_style_bg_color(btnPlus, COLOR_GRAY, 0);
  lv_obj_set_style_radius(btnPlus, 6, 0);
  lv_obj_add_event_cb(btnPlus, pullDownBtnCallback, LV_EVENT_PRESSED, (void*)3);

  lv_obj_t *labelPlus = lv_label_create(btnPlus);
  lv_label_set_text(labelPlus, "+");
  lv_obj_set_style_text_font(labelPlus, &lv_font_montserrat_20, 0);
  lv_obj_center(labelPlus);
}

// ============================================================================
// UI Creation
// ============================================================================

void createUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

  // ========== Header ==========
  headerBar = lv_obj_create(scr);
  lv_obj_set_size(headerBar, 240, 50);
  lv_obj_set_pos(headerBar, 0, 0);
  lv_obj_set_style_bg_color(headerBar, COLOR_HEADER, 0);
  lv_obj_set_style_border_width(headerBar, 0, 0);
  lv_obj_set_style_radius(headerBar, 0, 0);
  lv_obj_set_style_pad_all(headerBar, 10, 0);

  lv_obj_t *title = lv_label_create(headerBar);
  lv_label_set_text(title, "AP2 Luftfilter");
  lv_obj_set_style_text_color(title, COLOR_WHITE, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

  // Fan status (left of bluetooth)
  fanLabel = lv_label_create(headerBar);
  lv_obj_set_style_text_font(fanLabel, &lv_font_montserrat_12, 0);
  lv_obj_align(fanLabel, LV_ALIGN_RIGHT_MID, -30, 0);

  // Bluetooth status indicator (right)
  statusLabel = lv_label_create(headerBar);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(statusLabel, LV_ALIGN_RIGHT_MID, 0, 0);

  // ========== Filter Bars ==========
  int startY = 55;
  int barHeight = 38;
  int spacing = 5;

  for (int i = 0; i < 6; i++) {
    int y = startY + i * (barHeight + spacing);

    // Container for the filter row
    lv_obj_t *container = lv_obj_create(scr);
    lv_obj_set_size(container, 230, barHeight);
    lv_obj_set_pos(container, 5, y);
    lv_obj_set_style_bg_color(container, COLOR_GRAY, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 6, 0);
    lv_obj_set_style_pad_all(container, 0, 0);

    // Progress bar (full width of container)
    filterBars[i] = lv_bar_create(container);
    lv_obj_set_size(filterBars[i], 230, barHeight);
    lv_obj_set_pos(filterBars[i], 0, 0);
    lv_bar_set_range(filterBars[i], 0, 100);
    lv_obj_set_style_bg_color(filterBars[i], COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(filterBars[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(filterBars[i], 6, LV_PART_MAIN);
    lv_obj_set_style_radius(filterBars[i], 6, LV_PART_INDICATOR);

    // Filter name shadow (for better readability)
    lv_obj_t *nameShadow = lv_label_create(container);
    lv_label_set_text(nameShadow, filters[i].name);
    lv_obj_set_style_text_color(nameShadow, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(nameShadow, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(nameShadow, 11, (barHeight - 18) / 2 + 1);  // Offset for shadow effect

    // Filter name (inside the bar, left aligned)
    filterNameLabels[i] = lv_label_create(container);
    lv_label_set_text(filterNameLabels[i], filters[i].name);
    lv_obj_set_style_text_color(filterNameLabels[i], COLOR_WHITE, 0);
    lv_obj_set_style_text_font(filterNameLabels[i], &lv_font_montserrat_16, 0);
    lv_obj_set_pos(filterNameLabels[i], 10, (barHeight - 18) / 2);

    // Hours shadow
    filterHourShadows[i] = lv_label_create(container);
    lv_obj_set_style_text_font(filterHourShadows[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(filterHourShadows[i], lv_color_hex(0x000000), 0);
    lv_obj_align(filterHourShadows[i], LV_ALIGN_RIGHT_MID, -9, 1);  // Offset for shadow

    // Hours remaining (right aligned)
    filterHourLabels[i] = lv_label_create(container);
    lv_obj_set_style_text_font(filterHourLabels[i], &lv_font_montserrat_16, 0);
    lv_obj_align(filterHourLabels[i], LV_ALIGN_RIGHT_MID, -10, 0);
  }

  // ========== Initialize displays ==========
  updateStatusDisplay();
  updateFilterBars();

  Serial.println("UI created");
}

// ============================================================================
// BLE Functions
// ============================================================================

NimBLEAddress foundAP2Address;

class AP2ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    std::string name = advertisedDevice->getName();
    if (name.find("xTool") != std::string::npos || name.find("AP2") != std::string::npos) {
      Serial.printf("[BLE] Found AP2: %s (%s)\n", name.c_str(), advertisedDevice->getAddress().toString().c_str());
      foundAP2Address = advertisedDevice->getAddress();
      ap2Address = advertisedDevice->getAddress().toString().c_str();
      NimBLEDevice::getScan()->stop();
      bleScanning = false;
    }
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    Serial.printf("[BLE] Scan complete, found %d devices\n", results.getCount());
    bleScanning = false;
    if (ap2Address.length() == 0) {
      ap2.connectionState = 2;  // Not found
      updateStatusDisplay();
    }
  }
};

AP2ScanCallbacks scanCallbacks;

void startBLEScan() {
  if (bleScanning || bleConnected) return;

  Serial.println("[BLE] Scanning for AP2...");
  bleScanning = true;
  ap2.connectionState = 0;  // Searching
  updateStatusDisplay();

  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setActiveScan(true);
  pScan->start(10);  // Scan for 10 seconds
}

void connectToAP2() {
  if (ap2Address.length() == 0) {
    Serial.println("[BLE] No AP2 address found yet");
    return;
  }

  Serial.printf("[BLE] Connecting to %s...\n", ap2Address.c_str());

  bleClient = NimBLEDevice::createClient();
  if (bleClient->connect(foundAP2Address)) {
    NimBLERemoteService* pService = bleClient->getService(AP2_SERVICE_UUID);
    if (pService) {
      bleWriteChar = pService->getCharacteristic(AP2_CHARACTERISTIC_UUID);
      bleNotifyChar = bleWriteChar;  // Same characteristic for read/write

      if (bleWriteChar) {
        bleConnected = true;
        ap2.connectionState = 1;  // Connected
        Serial.println("[BLE] Connected to AP2!");
        buzzerConnect();  // Connection sound
        updateStatusDisplay();
        return;
      }
    }
    bleClient->disconnect();
  }

  Serial.println("[BLE] Connection failed");
  ap2.connectionState = 2;  // Error
  updateStatusDisplay();
}

// ============================================================================
// Web Server Functions
// ============================================================================

void handleWebRoot() {
  String html = R"rawliteral(<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AP2 Luftfilter Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
:root {
  --bg-primary: #0f1419;
  --bg-secondary: #15202b;
  --bg-tertiary: #192734;
  --bg-card: #1c2938;
  --border: #38444d;
  --text-primary: #ffffff;
  --text-secondary: #8899a6;
  --text-muted: #657786;
  --accent: #1da1f2;
  --accent-hover: #1a91da;
  --success: #17bf63;
  --warning: #ffad1f;
  --danger: #e0245e;
  --orange: #f45d22;
}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;background:var(--bg-primary);color:var(--text-primary);line-height:1.5;min-height:100vh}
.container{max-width:960px;margin:0 auto;padding:24px}
/* Header */
.header{display:flex;justify-content:space-between;align-items:center;padding:16px 0 32px}
.logo{display:flex;align-items:center;gap:12px}
.logo-icon{width:40px;height:40px;background:linear-gradient(135deg,var(--success),#0d9048);border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:20px}
.logo h1{font-size:1.25rem;font-weight:700;color:var(--text-primary)}
.logo span{font-size:0.75rem;color:var(--text-muted);font-weight:400}
.settings-btn{width:40px;height:40px;border-radius:50%;background:var(--bg-tertiary);border:1px solid var(--border);color:var(--text-secondary);cursor:pointer;display:flex;align-items:center;justify-content:center;transition:all 0.2s}
.settings-btn:hover{background:var(--bg-card);color:var(--text-primary);border-color:var(--text-muted)}
.settings-btn svg{width:20px;height:20px}
/* Grid */
.grid{display:grid;gap:20px}
.grid-stats{grid-template-columns:1fr 1fr}
@media(max-width:640px){.grid-stats{grid-template-columns:1fr}}
/* Cards */
.card{background:var(--bg-card);border:1px solid var(--border);border-radius:16px;padding:24px}
.card-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}
.card-title{font-size:0.75rem;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:0.1em}
/* Status */
.status-grid{display:flex;gap:32px}
.status-item{display:flex;align-items:center;gap:10px}
.status-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}
.status-dot.success{background:var(--success);box-shadow:0 0 12px rgba(23,191,99,0.4)}
.status-dot.warning{background:var(--warning);box-shadow:0 0 12px rgba(255,173,31,0.4)}
.status-dot.danger{background:var(--danger);box-shadow:0 0 12px rgba(224,36,94,0.4)}
.status-dot.muted{background:var(--text-muted)}
.status-label{font-size:0.9rem;color:var(--text-secondary)}
/* Laser stat */
.stat-value{font-size:2.5rem;font-weight:700;color:var(--success);letter-spacing:-0.02em;line-height:1.1}
.stat-label{font-size:0.875rem;color:var(--text-muted);margin-top:4px}
/* Filter bars */
.filter-list{display:flex;flex-direction:column;gap:12px}
.filter-bar{position:relative;height:48px;background:var(--bg-tertiary);border-radius:12px;overflow:hidden;border:1px solid var(--border)}
.filter-fill{position:absolute;inset:0;border-radius:11px;transition:width 0.5s ease}
.filter-fill.green{background:linear-gradient(90deg,#0d9048,var(--success))}
.filter-fill.yellow{background:linear-gradient(90deg,#e6960a,var(--warning))}
.filter-fill.orange{background:linear-gradient(90deg,#c44d1a,var(--orange))}
.filter-fill.red{background:linear-gradient(90deg,#a11d42,var(--danger))}
.filter-content{position:absolute;inset:0;display:flex;align-items:center;justify-content:space-between;padding:0 16px;z-index:1}
.filter-name{font-weight:600;font-size:0.875rem;color:var(--text-primary);text-shadow:0 1px 2px rgba(0,0,0,0.3)}
.filter-info{text-align:right}
.filter-hours{font-weight:600;font-size:0.875rem;color:var(--text-primary);text-shadow:0 1px 2px rgba(0,0,0,0.3)}
.filter-eta{font-size:0.75rem;color:rgba(255,255,255,0.7);margin-top:1px}
/* Chart */
.chart-container{height:300px;margin-top:8px}
/* Buttons */
.btn{display:inline-flex;align-items:center;gap:8px;padding:12px 20px;border-radius:9999px;font-size:0.875rem;font-weight:600;cursor:pointer;transition:all 0.2s;border:none;text-decoration:none}
.btn-primary{background:var(--accent);color:#fff}
.btn-primary:hover{background:var(--accent-hover)}
.btn-secondary{background:var(--bg-tertiary);color:var(--text-secondary);border:1px solid var(--border)}
.btn-secondary:hover{background:var(--bg-card);color:var(--text-primary)}
/* Footer */
.footer{text-align:center;padding:40px 0 24px;color:var(--text-muted);font-size:0.8rem}
/* Modal */
.modal-backdrop{position:fixed;inset:0;background:rgba(0,0,0,0.7);backdrop-filter:blur(4px);display:none;align-items:center;justify-content:center;z-index:1000;padding:24px}
.modal-backdrop.open{display:flex}
.modal{background:var(--bg-card);border:1px solid var(--border);border-radius:20px;width:100%;max-width:420px;animation:modalSlide 0.2s ease}
@keyframes modalSlide{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}
.modal-header{display:flex;justify-content:space-between;align-items:center;padding:20px 24px;border-bottom:1px solid var(--border)}
.modal-header h2{font-size:1.125rem;font-weight:700}
.modal-close{width:32px;height:32px;border-radius:50%;background:transparent;border:none;color:var(--text-muted);cursor:pointer;display:flex;align-items:center;justify-content:center;font-size:1.25rem;transition:all 0.15s}
.modal-close:hover{background:var(--bg-tertiary);color:var(--text-primary)}
.modal-body{padding:24px}
.form-group{margin-bottom:20px}
.form-label{display:block;font-size:0.8rem;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:0.05em;margin-bottom:8px}
.form-input{width:100%;padding:14px 16px;background:var(--bg-tertiary);border:1px solid var(--border);border-radius:12px;color:var(--text-primary);font-size:1rem;transition:border-color 0.15s}
.form-input:focus{outline:none;border-color:var(--accent)}
.form-divider{height:1px;background:var(--border);margin:24px 0}
.info-list{display:flex;flex-direction:column;gap:12px}
.info-row{display:flex;justify-content:space-between;align-items:center}
.info-key{color:var(--text-muted);font-size:0.875rem}
.info-val{color:var(--text-secondary);font-size:0.875rem;font-weight:500}
.modal-footer{display:flex;gap:12px;padding:0 24px 24px}
.modal-footer .btn{flex:1;justify-content:center}
</style>
</head><body>
<div class="container">

<header class="header">
  <div class="logo">
    <div class="logo-icon">🌀</div>
    <div>
      <h1>AP2 Luftfilter</h1>
      <span>xTool Air Purifier Monitor</span>
    </div>
  </div>
  <button class="settings-btn" onclick="openSettings()" aria-label="Einstellungen">
    <svg fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"/><circle cx="12" cy="12" r="3"/></svg>
  </button>
</header>

<div class="grid grid-stats">
  <div class="card">
    <div class="card-header"><span class="card-title">System Status</span></div>
    <div class="status-grid">
      <div class="status-item">
        <span class="status-dot )rawliteral";

  // Connection status
  if (ap2.connectionState == 1) html += "success";
  else if (ap2.connectionState == 0) html += "warning";
  else html += "danger";
  html += R"rawliteral("></span>
        <span class="status-label">BLE )rawliteral";
  if (ap2.connectionState == 1) html += "Verbunden";
  else if (ap2.connectionState == 0) html += "Suche...";
  else html += "Fehler";
  html += R"rawliteral(</span>
      </div>
      <div class="status-item">
        <span class="status-dot )rawliteral";
  html += ap2.fanRunning ? "success" : "muted";
  html += R"rawliteral("></span>
        <span class="status-label">Luefter )rawliteral";
  html += ap2.fanRunning ? "An" : "Aus";
  html += R"rawliteral(</span>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card-header"><span class="card-title">Laser Laufzeit</span></div>
    <div class="stat-value">)rawliteral";
  html += formatDuration(laserTotalSeconds);
  html += R"rawliteral(</div>
    <div class="stat-label">Gesamt Betriebsstunden</div>
  </div>
</div>

<div class="card" style="margin-top:20px">
  <div class="card-header"><span class="card-title">Filter Status</span></div>
  <div class="filter-list">
)rawliteral";

  // Filter bars with new design
  for (int i = 0; i < 6; i++) {
    int percent = (filters[i].remainingHours * 100) / filters[i].maxHours;
    percent = constrain(percent, 0, 100);
    String colorClass = percent > 50 ? "green" : (percent > 25 ? "yellow" : (percent > 10 ? "orange" : "red"));

    String prediction = getFilterPrediction(i);
    html += "<div class='filter-bar'>";
    html += "<div class='filter-fill " + colorClass + "' style='width:" + String(max(percent, 3)) + "%'></div>";
    html += "<div class='filter-content'>";
    html += "<span class='filter-name'>" + String(filters[i].name) + "</span>";
    html += "<div class='filter-info'><span class='filter-hours'>" + String(filters[i].remainingHours) + "h / " + String(filters[i].maxHours) + "h</span>";
    html += "<div class='filter-eta'>" + prediction + "</div></div>";
    html += "</div></div>";
  }

  html += R"rawliteral(
  </div>
</div>

<div class="card" style="margin-top:20px">
  <div class="card-header">
    <span class="card-title">Filter Verlauf</span>
    <a href="/download/history.csv" class="btn btn-secondary">CSV Export</a>
  </div>
  <div class="chart-container">
    <canvas id="historyChart"></canvas>
  </div>
</div>

<footer class="footer">
  AP2 Luftfilter Monitor &bull; Waveshare ESP32-S3
</footer>

</div><!-- /container -->

<!-- Settings Modal -->
<div class="modal-backdrop" id="settingsModal">
  <div class="modal">
    <div class="modal-header">
      <h2>Einstellungen</h2>
      <button class="modal-close" onclick="closeSettings()">&times;</button>
    </div>
    <div class="modal-body">
      <form id="settingsForm">
        <div class="form-group">
          <label class="form-label">Nachlaufzeit (Sekunden)</label>
          <input class="form-input" type="number" id="fanDelay" value=")rawliteral";
  html += String(fanDelaySeconds);
  html += R"rawliteral(" min="0" max="300" step="5">
        </div>
        <div class="form-group">
          <label class="form-label">Laser Betriebsstunden</label>
          <input class="form-input" type="number" id="laserHours" value=")rawliteral";
  html += String(laserTotalSeconds / 3600);
  html += R"rawliteral(" min="0" step="1">
        </div>
        <div class="form-divider"></div>
        <div class="info-list">
          <div class="info-row"><span class="info-key">IP Adresse</span><span class="info-val">)rawliteral";
  html += WiFi.localIP().toString();
  html += R"rawliteral(</span></div>
          <div class="info-row"><span class="info-key">OTA Hostname</span><span class="info-val">ap2-luftfilter.local</span></div>
          <div class="info-row"><span class="info-key">Uptime</span><span class="info-val">)rawliteral";
  html += formatDuration(millis() / 1000);
  html += R"rawliteral(</span></div>
          <div class="info-row"><span class="info-key">SD Karte</span><span class="info-val">)rawliteral";
  html += sdCardOK ? "OK" : "Nicht gefunden";
  html += R"rawliteral(</span></div>
        </div>
      </form>
    </div>
    <div class="modal-footer">
      <button type="button" class="btn btn-secondary" onclick="closeSettings()">Abbrechen</button>
      <button type="submit" form="settingsForm" class="btn btn-primary">Speichern</button>
    </div>
  </div>
</div>

<script>
// Modal
function openSettings() {
  document.getElementById('settingsModal').classList.add('open');
}
function closeSettings() {
  document.getElementById('settingsModal').classList.remove('open');
}
document.getElementById('settingsModal').addEventListener('click', (e) => {
  if (e.target.classList.contains('modal-backdrop')) closeSettings();
});

// Settings form
document.getElementById('settingsForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const fanDelay = document.getElementById('fanDelay').value;
  const laserHours = document.getElementById('laserHours').value;

  const response = await fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ fanDelay: parseInt(fanDelay), laserHours: parseInt(laserHours) })
  });

  if (response.ok) {
    closeSettings();
    location.reload();
  }
});

// History Chart
fetch('/api/history')
  .then(r => r.json())
  .then(data => {
    if (data.length === 0) return;

    const labels = data.map((d, i) => {
      const mins = Math.floor(d.t / 60);
      const hrs = Math.floor(mins / 60);
      return hrs + 'h';
    });

    const filterNames = ['Zyklon', 'Vorfilter', 'Medium', 'Aktivkohle', 'Carbon', 'HEPA'];
    const colors = ['#17bf63', '#8BC34A', '#1da1f2', '#26A69A', '#9C27B0', '#e0245e'];

    const datasets = filterNames.map((name, i) => ({
      label: name,
      data: data.map(d => d.f[i]),
      borderColor: colors[i],
      backgroundColor: colors[i] + '15',
      borderWidth: 2,
      fill: true,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 4
    }));

    new Chart(document.getElementById('historyChart'), {
      type: 'line',
      data: { labels, datasets },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { intersect: false, mode: 'index' },
        plugins: {
          legend: {
            position: 'bottom',
            labels: { color: '#8899a6', boxWidth: 12, padding: 16, font: { size: 11 } }
          },
          tooltip: {
            backgroundColor: '#1c2938',
            titleColor: '#fff',
            bodyColor: '#8899a6',
            borderColor: '#38444d',
            borderWidth: 1,
            padding: 12,
            cornerRadius: 8
          }
        },
        scales: {
          y: {
            beginAtZero: true,
            grid: { color: '#192734', drawBorder: false },
            ticks: { color: '#657786', font: { size: 11 } },
            title: { display: true, text: 'Stunden', color: '#657786', font: { size: 11 } }
          },
          x: {
            grid: { display: false },
            ticks: { color: '#657786', maxTicksLimit: 10, font: { size: 11 } }
          }
        }
      }
    });
  });

// Auto-refresh every 30 seconds
setTimeout(() => location.reload(), 30000);
</script>
</body></html>)rawliteral";

  webServer.send(200, "text/html", html);
}

void handleWebApi() {
  String json = "{";
  json += "\"connected\":" + String(ap2.connectionState == 1 ? "true" : "false") + ",";
  json += "\"fanRunning\":" + String(ap2.fanRunning ? "true" : "false") + ",";
  json += "\"fanDelay\":" + String(fanDelaySeconds) + ",";
  json += "\"laserSeconds\":" + String(laserTotalSeconds) + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"filters\":[";
  for (int i = 0; i < 6; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + String(filters[i].name) + "\",";
    json += "\"max\":" + String(filters[i].maxHours) + ",";
    json += "\"remaining\":" + String(filters[i].remainingHours) + "}";
  }
  json += "]}";

  webServer.send(200, "application/json", json);
}

void handleWebSettings() {
  if (webServer.method() == HTTP_POST) {
    String body = webServer.arg("plain");

    // Parse fanDelay
    int fanDelayIdx = body.indexOf("\"fanDelay\":");
    if (fanDelayIdx >= 0) {
      int start = fanDelayIdx + 11;
      int end = body.indexOf(",", start);
      if (end < 0) end = body.indexOf("}", start);
      String val = body.substring(start, end);
      fanDelaySeconds = val.toInt();
      Serial.printf("[WEB] Nachlaufzeit: %ds\n", fanDelaySeconds);
    }

    // Parse laserHours
    int laserIdx = body.indexOf("\"laserHours\":");
    if (laserIdx >= 0) {
      int start = laserIdx + 13;
      int end = body.indexOf(",", start);
      if (end < 0) end = body.indexOf("}", start);
      String val = body.substring(start, end);
      laserTotalSeconds = val.toInt() * 3600UL;  // Convert hours to seconds
      Serial.printf("[WEB] Laser Stunden: %lus\n", laserTotalSeconds);
    }

    saveSettings();
    webServer.send(200, "application/json", "{\"success\":true}");
  } else {
    String json = "{\"fanDelay\":" + String(fanDelaySeconds);
    json += ",\"laserHours\":" + String(laserTotalSeconds / 3600) + "}";
    webServer.send(200, "application/json", json);
  }
}

void handleWebHistory() {
  String json = getHistoryData(288);  // Last 48h
  webServer.send(200, "application/json", json);
}

void handleWebDownload() {
  if (!sdCardOK) {
    webServer.send(500, "text/plain", "SD Card not available");
    return;
  }

  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    webServer.send(404, "text/plain", "Log file not found");
    return;
  }

  webServer.sendHeader("Content-Disposition", "attachment; filename=ap2_history.csv");
  webServer.streamFile(f, "text/csv");
  f.close();
}

void setupWebServer() {
  webServer.on("/", handleWebRoot);
  webServer.on("/api/status", handleWebApi);
  webServer.on("/api/settings", HTTP_GET, handleWebSettings);
  webServer.on("/api/settings", HTTP_POST, handleWebSettings);
  webServer.on("/api/history", handleWebHistory);
  webServer.on("/download/history.csv", handleWebDownload);
  webServer.begin();
  Serial.println("[WEB] Server gestartet auf Port 80");
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("   xTool AP2 Luftfilter Monitor");
  Serial.println("   Waveshare ESP32-S3-Touch-LCD-2");
  Serial.println("========================================");

  // Load saved settings
  loadSettings();

  // Init Display
  Serial.println("[INIT] Display...");
  if (!gfx->begin()) {
    Serial.println("  ERROR: Display init failed!");
  }
  gfx->fillScreen(BLACK);

  // Backlight
  ledcAttach(LCD_BL, 5000, 10);
  ledcWrite(LCD_BL, 900);

  // Haptic feedback (buzzer or vibration motor)
  initHaptic();
  buzzerOomph();  // Startup oomph oomph oomph

  // Init touch
  Serial.println("[INIT] Touch...");
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  bsp_touch_init(&Wire, gfx->getRotation(), gfx->width(), gfx->height());

  // Init LVGL
  Serial.println("[INIT] LVGL...");
  lv_init();

  screenWidth = gfx->width();
  screenHeight = gfx->height();
  bufSize = screenWidth * screenHeight;

  disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
  }

  if (!disp_draw_buf) {
    Serial.println("  ERROR: Buffer allocation failed!");
    return;
  }

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, bufSize);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.direct_mode = true;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Create UI
  Serial.println("[INIT] Creating UI...");
  createUI();

  // Init SD Card
  Serial.println("[INIT] SD Card...");
  sdCardOK = initSDCard();
  if (sdCardOK) {
    logDataPoint();  // Log initial state
  }

  // Init WiFi & OTA
  Serial.println("[INIT] WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Don't block - WiFi connects in background
  Serial.println("  Connecting in background...");

  // Setup OTA
  ArduinoOTA.setHostname("ap2-luftfilter");
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Update gestartet...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update fertig!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  // Init BLE
  Serial.println("[INIT] BLE...");
  NimBLEDevice::init("AP2-Monitor");

  Serial.println();
  Serial.println("[READY] Luftfilter Monitor gestartet");
  Serial.println();

  // Start scanning for AP2
  startBLEScan();
}

// ============================================================================
// Main Loop
// ============================================================================

unsigned long lastUpdate = 0;

void handleTouchGestures() {
  uint16_t x, y;
  bsp_touch_read();
  bool isTouched = bsp_touch_get_coordinates(&x, &y);

  if (isTouched) {
    if (!touchActive) {
      // Touch just started - record start position
      touchActive = true;
      touchStartTime = millis();
      touchStartY = y;
      touchStartX = x;
    } else {
      // Touch ongoing - check for swipe gestures
      int16_t deltaY = y - touchStartY;

      // Swipe down from top edge to open menu
      if (!pullDownOpen && !dialogShown && touchStartY < 60 && deltaY > 50) {
        Serial.println("Swipe down - oeffne Menu");
        buzzerClick();
        showPullDownMenu();
        touchActive = false;
        touchStartY = -1;
        return;
      }

      // Swipe up to close menu
      if (pullDownOpen && deltaY < -50) {
        Serial.println("Swipe up - schliesse Menu");
        buzzerClick();
        closePullDownMenu();
        touchActive = false;
        touchStartY = -1;
        return;
      }

      // Longpress detection (only when no menu/dialog open)
      if (!dialogShown && !pullDownOpen && millis() - touchStartTime >= 5000) {
        Serial.println("Longpress erkannt - zeige Reset Dialog");
        showResetDialog();
        touchActive = false;
      }
    }
  } else {
    // Touch released
    touchActive = false;
    touchStartY = -1;
    touchStartX = -1;
  }
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Check WiFi connection status (once)
  if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println();
    Serial.print("[WiFi] Verbunden! IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("[OTA] Bereit fuer Updates (ap2-luftfilter.local)");
    setupWebServer();
  }

  // Handle web server requests
  if (wifiConnected) {
    webServer.handleClient();
  }

  // BLE connection management
  static unsigned long lastBLECheck = 0;
  if (millis() - lastBLECheck > 5000) {
    lastBLECheck = millis();

    if (!bleConnected && !bleScanning) {
      if (ap2Address.length() > 0) {
        connectToAP2();
      } else {
        startBLEScan();
      }
    }
  }

  // Data logging to SD card
  if (sdCardOK && millis() - lastLogTime > LOG_INTERVAL_MS) {
    lastLogTime = millis();
    logDataPoint();
  }

  lv_timer_handler();

  // Handle touch gestures (swipe, longpress)
  handleTouchGestures();

  // Draw to display
  #if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, screenWidth, screenHeight);
  #else
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, screenWidth, screenHeight);
  #endif

  // Simulate updates every 10 seconds
  if (millis() - lastUpdate > 10000) {
    lastUpdate = millis();

    // Cycle through states for demo
    // connectionState: 0=searching, 1=connected, 2=error
    static int demoState = 0;
    demoState = (demoState + 1) % 4;

    if (demoState == 0) {
      ap2.connectionState = 0;  // searching (yellow)
      ap2.fanRunning = false;
    } else if (demoState == 1) {
      ap2.connectionState = 1;  // connected (neutral)
      ap2.fanRunning = false;
    } else if (demoState == 2) {
      ap2.connectionState = 1;  // connected + fan (green)
      ap2.fanRunning = true;
    } else {
      ap2.connectionState = 2;  // error (red)
      ap2.fanRunning = false;
    }
    updateStatusDisplay();

    // Simulate filter usage (decrease hours) - including Zyklon now
    for (int i = 0; i < 6; i++) {
      if (filters[i].remainingHours > 0) {
        filters[i].remainingHours = max(0, (int)(filters[i].remainingHours - random(0, 2)));
      }
    }
    updateFilterBars();
  }

  delay(5);
}
