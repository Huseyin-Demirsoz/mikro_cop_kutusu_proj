/*
Smart Trash Bin Fill + Odor Monitoring System
ESP32 firmware
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Preferences.h>
#include <WiFiClientSecure.h> // TELEGRAM KISMI


// Network configuration
const char* WIFI_SSID = "Galaxy S22 Ultra B12D";
const char* WIFI_PASSWORD = "bob12345a";
//const char* WIFI_SSID = "readme note on iki puro";
//const char* WIFI_PASSWORD = "jonganma";

// Change the IP address to the PC/server running server.cpp
const char* API_URL = "http://10.99.101.218:8080/api/trash";

// Device configuration
const char* DEVICE_ID = "trash_bin_01";
const char* FIRMWARE_VERSION = "2.0.0";

//USED PINS
//5,18,34,32,25,26,27,33,21,22,23,19,17,16,12,13

// Pin configuration
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
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte rowPins[ROWS] = {23, 19, 17, 16};
byte colPins[COLS] = {12, 14, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Thresholds
const int FILL_WARNING = 70;
const int FILL_ALARM = 90;
const int GAS_WARNING = 400;
const int GAS_ALARM = 800;

// Timing
const unsigned long SENSOR_INTERVAL_MS = 1000;
const unsigned long POST_INTERVAL_MS = 5000;
const unsigned long LCD_INTERVAL_MS = 1000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;

// Calibration defaults
// emptyDistanceCm: distance from sensor to trash when bin is empty
// fullDistanceCm : distance from sensor to trash when bin is considered full
float emptyDistanceCm = 22.303548387097;
float fullDistanceCm = 3.781282051282;
int cleanGasBaseline = 0;

// Runtime values
Preferences prefs;

float distanceCm = -1.0;
int fillPercent = 0;
int gasRaw = 0;
float gasVoltage = 0.0;
bool gasDigitalAlarm = false;
String systemStatus = "NORMAL";
String lastStatusForTelegram = "NORMAL"; // Durum değişimini takip etmek için ekledik

int previousFillPercent = 0;
bool forceEmptiedEvent = false;
int emptiedEvent = 0;

unsigned long lastSensorMs = 0;
unsigned long lastPostMs = 0;
unsigned long lastLcdMs = 0;
unsigned long lastWifiRetryMs = 0;

byte lcdPage = 0;


// Prototip
bool sendDataToServer();


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

// TELEGRAM KISMI: Mesaj gönderme fonksiyonu
void sendTelegramMessage(String message) {
  WiFiClientSecure client;
  client.setInsecure(); // Windows/ESP32 sertifika hatalarını önler
  
  String token = "8607846827:AAHP3oKxtuAtw5RJJ9KYOY-PvFoOvp2vCWE";
  String chat_id = "1142788030";

  if (client.connect("api.telegram.org", 443)) {
    // Mesajdaki boşlukları ve özel karakterleri düzeltmek için urlEncode kullanıyoruz
    String url = "/bot" + token + "/sendMessage?chat_id=" + chat_id + "&text=" + urlEncode(message);
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: api.telegram.org\r\n" +
                 "Connection: close\r\n\r\n");
    Serial.println("Telegram: Mesaj gonderildi!");
  }
}


// Wi-Fi
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi: ");
  //Serial.println(WIFI_SSID);

  //WiFi.mode(WIFI_STA);
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
  
}

// Sensor reading
float readUltrasonicDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(18);
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
    total += d;
    valid++;

    delay(40);
  }

  if (valid == 0) {
    return -1.0;
  }

  return total / valid;
}

int readGasAverage(byte sampleCount = 10) {
  long total = 0;

  for (byte i = 0; i < sampleCount; i++) {
    total += analogRead(MQ_AO_PIN);
    delay(5);
  }

  return total / sampleCount;
}


void readSensors() {
  distanceCm = readUltrasonicDistanceAverage();
  fillPercent = (1-((distanceCm-fullDistanceCm)/(emptyDistanceCm-fullDistanceCm)))*100;
  Serial.printf("Full: %f\n",fullDistanceCm);
  Serial.printf("Empty:%f\n",emptyDistanceCm);
  Serial.printf("Dist: %f\n",distanceCm);


  gasRaw = readGasAverage();
  gasVoltage = (gasRaw / 4095.0) * 3.3;
  {
    bool value = (bool)digitalRead(MQ_DO_PIN);
    gasDigitalAlarm = value^MQ_DO_ACTIVE_LOW;
  }
  systemStatus = "NORMAL";
  if (fillPercent >= FILL_WARNING || gasRaw >= GAS_WARNING) {
    systemStatus = "WARNING";
  }
  if (fillPercent >= FILL_ALARM || gasRaw >= GAS_ALARM || gasDigitalAlarm) {
    systemStatus = "ALARM";
  }
  

  // TELEGRAM KISMI: Alarm durumuna geçişte bir kez mesaj atar
  if (systemStatus == "ALARM" && lastStatusForTelegram != "ALARM") {
    sendTelegramMessage("Sistem Uyarisi: dikkat! Cop kutusu doldu veya kotu koku tespit edildi!");
  }
  lastStatusForTelegram = systemStatus;

  // Emptying detection:
  // If the bin was high and suddenly becomes low, count it as an emptying event.
  emptiedEvent = 0;
  if ((previousFillPercent >= 70 && fillPercent <= 20) || forceEmptiedEvent) {
    emptiedEvent = 1;
    forceEmptiedEvent = false;
    sendTelegramMessage("Cop kutusu bosaltildi. Islem tamam!"); // Opsiyonel bilgi mesajı
  }

  previousFillPercent = fillPercent;

  digitalWrite(LED_GREEN_PIN, systemStatus == "NORMAL" ? HIGH : LOW);
  digitalWrite(LED_YELLOW_PIN, systemStatus == "WARNING" ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, systemStatus == "ALARM" ? HIGH : LOW);

  static unsigned long lastBeep = 0;
  unsigned long now = millis();

  if (systemStatus == "ALARM" && now - lastBeep > 2000) {
    tone(BUZZER_PIN, 2200, 250);
    lastBeep = now;
  } else if (systemStatus == "WARNING" && now - lastBeep > 5000) {
    tone(BUZZER_PIN, 1800, 120);
    lastBeep = now;
  }

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


void showMessage(const String& line1, const String& line2, unsigned long holdMs = 1200) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
  delay(holdMs);
}

// Keypad ve Kalibrasyon
void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  tone(BUZZER_PIN, 1800, 120);

  switch (key) {
    case '1':
      emptyDistanceCm = readUltrasonicDistanceAverage(10);
      break;

    case '2':
      fullDistanceCm = readUltrasonicDistanceAverage(10);
      break;

    case '3':
      showMessage("Calibrating", "Clean air gas", 900);
      cleanGasBaseline = readGasAverage(25);
      showMessage("Gas baseline", String(cleanGasBaseline), 1500);
      
      showMessage("Manual upload", "Request sent", 1000);
      break;

    case '4':
      forceEmptiedEvent = true;
      showMessage("Manual event", "Emptied=1", 1200);
      break;

    case '#':
      lcdPage = (lcdPage + 1) % 3;
      break;

    case '*':
      sendDataToServer();
      break;

    default:
      break;
  }
}

// LCD
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
    // lcd.print(fullDistanceCm, 0); // Orijinal koda sadık kalınmıştır
    lcd.print("cm");
  } else {
    lcd.setCursor(0, 0);
    lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi:Connected" : "WiFi:Offline");

    lcd.setCursor(0, 1);
    lcd.print("IP:");
    if (WiFi.status() == WL_CONNECTED) {
      lcd.print(WiFi.localIP());
    } else {
      // lcd.print("no network"); // Orijinal koda sadık kalınmıştır
    }
  }
}

// HTTP upload
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

// Setup / Loop
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
  
  showMessage("Smart Trash Bin", "Starting...", 1000);

  connectWiFi();

  readSensors();
  updateLCD();
  sendDataToServer();
}

void loop() {
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED){
    if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiRetryMs = now;
      connectWiFi();
    }
  }
  handleKeypad();

  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    readSensors();
  }

  if (now - lastLcdMs >= LCD_INTERVAL_MS) {
    lastLcdMs = now;
    updateLCD();
  }

  if (now - lastPostMs >= POST_INTERVAL_MS) {
    lastPostMs = now;
    sendDataToServer();
  }
}
