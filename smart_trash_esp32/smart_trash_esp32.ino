/*
 * Smart Trash Bin Fill + Odor Monitoring System
 * ESP32 firmware
 *
 * Features:
 *  - HC-SR04 ultrasonic fill-level measurement
 *  - MQ-series / Flying Fish gas sensor analog + digital reading
 *  - LCD status display
 *  - 4x4 keypad calibration menu
 *  - Local LED + buzzer warnings
 *  - Wi-Fi HTTP POST to C++ REST API
 *  - Emptying event detection
 *
 * Required Arduino libraries:
 *  - LiquidCrystal_I2C
 *  - Keypad
 *
 * IMPORTANT:
 *  - Update WIFI_SSID, WIFI_PASSWORD and API_URL before uploading.
 *  - HC-SR04 ECHO is 5V. Use a voltage divider / level shifter before ESP32 GPIO18.
 *  - MQ sensor AO/DO can be 5V. Use voltage divider / level shifter for ESP32 safety.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Preferences.h>

// =========================
// Network configuration
// =========================
const char* WIFI_SSID = "readme note on iki puro";
const char* WIFI_PASSWORD = "jonganma";

// Change the IP address to the PC/server running server.cpp
const char* API_URL = "http://10.200.132.232:8080/api/trash";

// =========================
// Device configuration
// =========================
const char* DEVICE_ID = "trash_bin_01";
const char* FIRMWARE_VERSION = "2.0.0";

// =========================
// Pin configuration
// =========================
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;

const int MQ_AO_PIN = 34;     // ADC input only
const int MQ_DO_PIN = 32;     // Digital output from MQ module
const bool MQ_DO_ACTIVE_LOW = true;

const int LED_GREEN_PIN = 25;
const int LED_YELLOW_PIN = 26;
const int LED_RED_PIN = 27;
const int BUZZER_PIN = 33;

// LCD: common I2C address is 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Keypad wiring - change if your hardware uses different pins
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {4, 16, 17, 19};
byte colPins[COLS] = {23, 13, 12, 14};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// =========================
// Thresholds
// =========================
const int FILL_WARNING = 70;
const int FILL_ALARM = 90;
const int GAS_WARNING = 400;
const int GAS_ALARM = 800;

// =========================
// Timing
// =========================
const unsigned long SENSOR_INTERVAL_MS = 1000;
const unsigned long POST_INTERVAL_MS = 10000;
const unsigned long LCD_INTERVAL_MS = 1000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;

// =========================
// Calibration defaults
// =========================
// emptyDistanceCm: distance from sensor to trash when bin is empty
// fullDistanceCm : distance from sensor to trash when bin is considered full
float emptyDistanceCm = 35.0;
float fullDistanceCm = 6.0;
int cleanGasBaseline = 0;

// =========================
// Runtime values
// =========================
Preferences prefs;

float distanceCm = -1.0;
int fillPercent = 0;
int gasRaw = 0;
float gasVoltage = 0.0;
bool gasDigitalAlarm = false;
String systemStatus = "NORMAL";

int previousFillPercent = 0;
bool forceEmptiedEvent = false;
int emptiedEvent = 0;

unsigned long lastSensorMs = 0;
unsigned long lastPostMs = 0;
unsigned long lastLcdMs = 0;
unsigned long lastWifiRetryMs = 0;

byte lcdPage = 0;


// Forward declaration
bool sendDataToServer();

// =========================
// Utility
// =========================
int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

String urlEncode(const String& value) {
  String encoded = "";
  char buf[4];

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];

    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }

  return encoded;
}

void beepShort() {
  tone(BUZZER_PIN, 1800, 120);
}

void beepAlarm() {
  tone(BUZZER_PIN, 2200, 250);
}

void setLocalIndicators(const String& status) {
  digitalWrite(LED_GREEN_PIN, status == "NORMAL" ? HIGH : LOW);
  digitalWrite(LED_YELLOW_PIN, status == "WARNING" ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, status == "ALARM" ? HIGH : LOW);

  static unsigned long lastBeep = 0;
  unsigned long now = millis();

  if (status == "ALARM" && now - lastBeep > 2000) {
    beepAlarm();
    lastBeep = now;
  } else if (status == "WARNING" && now - lastBeep > 5000) {
    beepShort();
    lastBeep = now;
  }
}

// =========================
// Wi-Fi
// =========================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection failed. Will retry later.");
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryMs = now;
    connectWiFi();
  }
}

// =========================
// Sensor reading
// =========================
float readUltrasonicDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Timeout 30 ms ~= 5 meters, enough for trash bin
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1.0;
  }

  // Speed of sound: 343 m/s -> 0.0343 cm/us, divide by 2 for round trip
  return duration * 0.0343 / 2.0;
}

float readUltrasonicDistanceAverage(byte sampleCount = 5) {
  float total = 0;
  int valid = 0;

  for (byte i = 0; i < sampleCount; i++) {
    float d = readUltrasonicDistanceOnce();
    if (d > 0 && d < 400) {
      total += d;
      valid++;
    }
    delay(40);
  }

  if (valid == 0) {
    return -1.0;
  }

  return total / valid;
}

int calculateFillPercent(float distance) {
  if (distance < 0) {
    return fillPercent; // keep last valid value if sensor fails
  }

  float denominator = emptyDistanceCm - fullDistanceCm;
  if (denominator <= 0.5) {
    return 0;
  }

  float ratio = (emptyDistanceCm - distance) / denominator;
  int percent = round(ratio * 100.0);

  return clampInt(percent, 0, 100);
}

int readGasAverage(byte sampleCount = 10) {
  long total = 0;

  for (byte i = 0; i < sampleCount; i++) {
    total += analogRead(MQ_AO_PIN);
    delay(5);
  }

  return total / sampleCount;
}

bool readGasDigitalAlarm() {
  int value = digitalRead(MQ_DO_PIN);

  if (MQ_DO_ACTIVE_LOW) {
    return value == LOW;
  }

  return value == HIGH;
}

String evaluateStatus(int fill, int gas, bool gasDo) {
  if (fill >= FILL_ALARM || gas >= GAS_ALARM || gasDo) {
    return "ALARM";
  }

  if (fill >= FILL_WARNING || gas >= GAS_WARNING) {
    return "WARNING";
  }

  return "NORMAL";
}

void readSensors() {
  distanceCm = readUltrasonicDistanceAverage();
  fillPercent = calculateFillPercent(distanceCm);

  gasRaw = readGasAverage();
  gasVoltage = (gasRaw / 4095.0) * 3.3;
  gasDigitalAlarm = readGasDigitalAlarm();

  systemStatus = evaluateStatus(fillPercent, gasRaw, gasDigitalAlarm);

  // Emptying detection:
  // If the bin was high and suddenly becomes low, count it as an emptying event.
  emptiedEvent = 0;
  if ((previousFillPercent >= 70 && fillPercent <= 20) || forceEmptiedEvent) {
    emptiedEvent = 1;
    forceEmptiedEvent = false;
  }

  previousFillPercent = fillPercent;

  setLocalIndicators(systemStatus);

  Serial.printf(
    "Distance=%.1f cm | Fill=%d%% | Gas=%d | Voltage=%.2f | DO=%d | Status=%s | Emptied=%d\n",
    distanceCm,
    fillPercent,
    gasRaw,
    gasVoltage,
    gasDigitalAlarm,
    systemStatus.c_str(),
    emptiedEvent
  );
}

// =========================
// Calibration
// =========================
void loadCalibration() {
  prefs.begin("trashcal", false);

  emptyDistanceCm = prefs.getFloat("emptyCm", 35.0);
  fullDistanceCm = prefs.getFloat("fullCm", 6.0);
  cleanGasBaseline = prefs.getInt("cleanGas", 0);

  prefs.end();

  if (emptyDistanceCm <= fullDistanceCm) {
    emptyDistanceCm = 35.0;
    fullDistanceCm = 6.0;
  }
}

void saveCalibration() {
  prefs.begin("trashcal", false);

  prefs.putFloat("emptyCm", emptyDistanceCm);
  prefs.putFloat("fullCm", fullDistanceCm);
  prefs.putInt("cleanGas", cleanGasBaseline);

  prefs.end();
}

void showMessage(const String& line1, const String& line2, unsigned long holdMs = 1200) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
  delay(holdMs);
}

void calibrateEmptyDistance() {
  showMessage("Calibrating", "Empty distance", 900);

  float measured = readUltrasonicDistanceAverage(10);
  if (measured > 0) {
    emptyDistanceCm = measured;
    saveCalibration();
    showMessage("Empty saved", String(emptyDistanceCm, 1) + " cm", 1500);
  } else {
    showMessage("Sensor error", "Empty not saved", 1500);
  }
}

void calibrateFullDistance() {
  showMessage("Calibrating", "Full distance", 900);

  float measured = readUltrasonicDistanceAverage(10);
  if (measured > 0 && measured < emptyDistanceCm) {
    fullDistanceCm = measured;
    saveCalibration();
    showMessage("Full saved", String(fullDistanceCm, 1) + " cm", 1500);
  } else {
    showMessage("Invalid value", "Full not saved", 1500);
  }
}

void calibrateCleanGas() {
  showMessage("Calibrating", "Clean air gas", 900);

  cleanGasBaseline = readGasAverage(25);
  saveCalibration();

  showMessage("Gas baseline", String(cleanGasBaseline), 1500);
}

// =========================
// Keypad
// =========================
void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  beepShort();

  switch (key) {
    case 'A':
      calibrateEmptyDistance();
      break;

    case 'B':
      calibrateFullDistance();
      break;

    case 'C':
      calibrateCleanGas();
      break;

    case 'D':
      forceEmptiedEvent = true;
      showMessage("Manual event", "Emptied=1", 1200);
      break;

    case '#':
      lcdPage = (lcdPage + 1) % 3;
      break;

    case '*':
      sendDataToServer();
      showMessage("Manual upload", "Request sent", 1000);
      break;

    default:
      break;
  }
}

// =========================
// LCD
// =========================
void updateLCD() {
  lcd.clear();

  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Fill:");
    lcd.print(fillPercent);
    lcd.print("% ");
    lcd.print(systemStatus);

    lcd.setCursor(0, 1);
    lcd.print("Gas:");
    lcd.print(gasRaw);
    lcd.print(" DO:");
    lcd.print(gasDigitalAlarm ? "ALM" : "OK ");
  } else if (lcdPage == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Dist:");
    if (distanceCm > 0) {
      lcd.print(distanceCm, 1);
      lcd.print("cm");
    } else {
      lcd.print("ERR");
    }

    lcd.setCursor(0, 1);
    lcd.print("E:");
    lcd.print(emptyDistanceCm, 0);
    lcd.print(" F:");
    lcd.print(fullDistanceCm, 0);
    lcd.print("cm");
  } else {
    lcd.setCursor(0, 0);
    lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi:Connected" : "WiFi:Offline");

    lcd.setCursor(0, 1);
    lcd.print("IP:");
    if (WiFi.status() == WL_CONNECTED) {
      lcd.print(WiFi.localIP());
    } else {
      lcd.print("no network");
    }
  }
}

// =========================
// HTTP upload
// =========================
bool sendDataToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Upload skipped: Wi-Fi offline.");
    return false;
  }

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String payload = "";
  payload += "device_id=" + urlEncode(DEVICE_ID);
  payload += "&fill=" + String(fillPercent);
  payload += "&gas=" + String(gasRaw);
  payload += "&gas_voltage=" + String(gasVoltage, 3);
  payload += "&gas_do=" + String(gasDigitalAlarm ? 1 : 0);
  payload += "&distance=" + String(distanceCm, 2);
  payload += "&status=" + urlEncode(systemStatus);
  payload += "&emptied=" + String(emptiedEvent);
  payload += "&firmware=" + urlEncode(FIRMWARE_VERSION);
  payload += "&rssi=" + String(WiFi.RSSI());
  payload += "&cal_empty=" + String(emptyDistanceCm, 2);
  payload += "&cal_full=" + String(fullDistanceCm, 2);
  payload += "&timestamp=" + String(millis());

  int httpCode = http.POST(payload);
  String response = http.getString();

  Serial.printf("POST %s -> HTTP %d | %s\n", API_URL, httpCode, response.c_str());

  http.end();

  // Prevent repeated emptying flag for the same physical event.
  if (httpCode >= 200 && httpCode < 300) {
    emptiedEvent = 0;
  }

  return httpCode >= 200 && httpCode < 300;
}

// =========================
// Setup / Loop
// =========================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(MQ_AO_PIN, INPUT);
  pinMode(MQ_DO_PIN, INPUT);

  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  loadCalibration();

  showMessage("Smart Trash Bin", "Starting...", 1000);
  connectWiFi();

  readSensors();
  updateLCD();
  sendDataToServer();
}

void loop() {
  ensureWiFi();
  //handleKeypad();

  unsigned long now = millis();

  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    readSensors();
  }

  if (now - lastLcdMs >= LCD_INTERVAL_MS) {
    lastLcdMs = now;
    //updateLCD();
  }

  if (now - lastPostMs >= POST_INTERVAL_MS) {
    lastPostMs = now;
    sendDataToServer();
  }
}