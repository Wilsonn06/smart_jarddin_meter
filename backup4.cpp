#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>
#include <SD.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ========================== Konstanta & Pinout =============================
constexpr int kFlowMeterPin = 25;
constexpr float kCarFactorFlow = 7.5f;
constexpr int kRxPin = 16;
constexpr int kTxPin = 17;
constexpr int kLcdAddress = 0x27;
constexpr int kLcdColumns = 16;
constexpr int kLcdRows = 2;
constexpr int kCsSdPin = 14;
constexpr int kSVPin = 26;
constexpr int kSSRPin = 27;
constexpr uint32_t kSwitchStageTime = 3000;
constexpr uint32_t kSaveDataPeriod = 10000;
constexpr char ssid[] = "wilson";
constexpr char password[] = "23456789"; 
String apiKey = "";

// ========================== Endpoint =============================
constexpr char authEndpoint[] = "https://sbd.srusun.id/control-agent/auth";
constexpr char controlEndpoint[] = "https://sbd.srusun.id/control-agent/receive";
constexpr char unitId[] = "01JQ1KR5JZ68X1DGAK0QRM1FWT-UNT";
constexpr char flatId[] = "01JTZ69A2BCCYCBJXS68S5Y9JG-flt";

// ========================== Data & Struct =============================
struct MeasurementData {
  float flowRate = 0, voltage = 0, current = 0, frequency = 0, pf = 0, power = 0;
  double energy = 0, volume = 0;
} data;

String deviceToken = "";
bool aktuator_state = false;
bool isSDReady = false;
struct tm timeinfo;

volatile int pulseCount = 0;
uint32_t previousTime = 0;
uint32_t lcdTime1 = 0;
int stage = 0;

// ========================== Flowmeter =============================
void IRAM_ATTR pulseCounter() { pulseCount++; }

void collectFlowMeterData() {
  detachInterrupt(digitalPinToInterrupt(kFlowMeterPin));
  data.flowRate = static_cast<float>(pulseCount) / kCarFactorFlow;
  data.volume += data.flowRate / 60.0;
  pulseCount = 0;
  attachInterrupt(digitalPinToInterrupt(kFlowMeterPin), pulseCounter, FALLING);
}

// ========================== KWh Meter =============================
PZEM004Tv30 pzem(Serial2, kRxPin, kTxPin);

void collectKwhMeterData() {
  if (!isnan(pzem.voltage()) && !isnan(pzem.current()) && !isnan(pzem.frequency()) && !isnan(pzem.pf())) {
    data.voltage = pzem.voltage();
    data.current = pzem.current();
    data.frequency = pzem.frequency();
    data.pf = pzem.pf();
    data.power = data.pf * data.voltage * data.current;
    data.energy += data.power > 0 ? data.power / 3600.0 : 0;
  }
}

// ========================== RTC =============================
const char* ntpServer = "id.pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;

bool collectRTCData() {
  return getLocalTime(&timeinfo);
}

// ========================== LCD =============================
LiquidCrystal_I2C lcd(kLcdAddress, kLcdColumns, kLcdRows);

void lcdDisplayData() {
  lcd.clear();
  lcd.setCursor(0, 0);
  switch (stage) {
    case 0:
      lcd.printf("Flow: %.1f L/m", data.flowRate);
      lcd.setCursor(0, 1);
      lcd.printf("Vol: %.2f m3", data.volume);
      break;
    case 1:
      lcd.printf("Volt: %.1f V", data.voltage);
      lcd.setCursor(0, 1);
      lcd.printf("Curr: %.2f A", data.current);
      break;
    case 2:
      lcd.printf("Power: %.1f W", data.power);
      lcd.setCursor(0, 1);
      lcd.printf("Energy: %.1f Wh", data.energy);
      break;
    case 3:
      lcd.printf("Freq: %.1f Hz", data.frequency);
      lcd.setCursor(0, 1);
      lcd.printf("PF: %.2f", data.pf);
      break;
    case 4:
      lcd.printf("Time: %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      lcd.setCursor(0, 1);
      lcd.printf("Date: %04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }
  stage = (stage + 1) % 5;
  lcdTime1 = millis();
}

// ========================== WiFi & Auth =============================
void connectToWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
}

bool getApikeyDevice() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String endpoint = "https://sbd.srusun.id/control-agent/apikey";

  HTTPClient http;
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["device_mac_address"] = WiFi.macAddress();

  String body;
  serializeJson(doc, body);

  Serial.println("Sending API Key Request Payload:");
  Serial.println(body);

  int code = http.POST(body);

  if (code == 200) {
    String response = http.getString();
    Serial.println("API Key Response:");
    Serial.println(response);

    StaticJsonDocument<512> resDoc;
    DeserializationError err = deserializeJson(resDoc, response);
    if (err) {
      Serial.print("JSON parse error: ");
      Serial.println(err.c_str());
      http.end();
      return false;
    }

    if (!resDoc["data"]["device_api_key"].isNull()) {
      apiKey = resDoc["data"]["device_api_key"].as<String>();
      Serial.print("Fetched API Key: ");
      Serial.println(apiKey);
      http.end();
      return true;
    } else {
      Serial.println("API key field missing in JSON response");
    }
  } else {
    Serial.printf("Failed to get API key, status code: %d\n", code);
    String errResp = http.getString();
    Serial.println("Error response:");
    Serial.println(errResp);
  }
  http.end();
  return false;
}

bool authenticateDevice() {
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("wifi tidak tersambung");
    return false;
  }
  if(apiKey.isEmpty()){
    Serial.println("api key tidak ada");
    return false;
  }

  HTTPClient http;
  http.begin(authEndpoint);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["device_mac_address"] = WiFi.macAddress();
  doc["device_api_key"] = apiKey;

  String body;
  serializeJson(doc, body);

  Serial.println("Sending Auth Payload:");
  Serial.println(body);

  int code = http.POST(body);

  if (code == 200) {
    String response = http.getString();
    Serial.println("Auth Response:");
    Serial.println(response);

    StaticJsonDocument<512> resDoc;
    DeserializationError err = deserializeJson(resDoc, response);
    if (err) {
      Serial.print("Failed to parse auth response JSON: ");
      Serial.println(err.c_str());
      http.end();
      return false;
    }

    if (!resDoc["data"]["token"].isNull()) {
      deviceToken = resDoc["data"]["token"].as<String>();
      Serial.print("Received Token: ");
      Serial.println(deviceToken);
      http.end();
      return true;
    } else {
      Serial.println("No 'token' field in response");
    }
  } else {
    Serial.printf("Auth failed, HTTP status: %d\n", code);
    String errResp = http.getString();
    Serial.println("Auth error response:");
    Serial.println(errResp);
  }

  http.end();
  return false;
}


// ========================== Aktuator =============================
void toggleActuator() {
  aktuator_state = !aktuator_state;
  digitalWrite(kSVPin, aktuator_state);
  digitalWrite(kSSRPin, aktuator_state);
}

// ========================== SD Card =============================
bool initSD() {
  pinMode(kCsSdPin, OUTPUT);
  digitalWrite(kCsSdPin, LOW);
  isSDReady = SD.begin(kCsSdPin);
  digitalWrite(kCsSdPin, HIGH);
  return isSDReady;
}

void saveToSD() {
  if (!isSDReady) return;

  char filename[32];
  snprintf(filename, sizeof(filename), "/%04d%02d%02d-%02d%02d%02d.json",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  File file = SD.open(filename, FILE_WRITE);
  if (!file) return;

  StaticJsonDocument<256> doc;
  doc["volume"] = data.volume;
  doc["flowRate"] = data.flowRate;
  doc["current"] = data.current;
  doc["voltage"] = data.voltage;
  doc["pf"] = data.pf;
  doc["frequency"] = data.frequency;
  doc["power"] = data.power;
  doc["energy"] = data.energy / 1000.0;

  serializeJson(doc, file);
  file.println();
  file.close();
}

void restoreLastData() {
  File root = SD.open("/");
  File file;
  File latest;
  uint32_t latestTime = 0;

  while ((file = root.openNextFile())) {
    if (!file.isDirectory()) {
      uint32_t mod = file.getLastWrite();
      if (mod > latestTime) {
        latestTime = mod;
        if (latest) latest.close();
        latest = file;
      } else file.close();
    }
  }

  if (latest) {
    String json = latest.readStringUntil('\n');
    StaticJsonDocument<256> doc;
    deserializeJson(doc, json);
    data.volume = doc["volume"];
    data.energy = doc["energy"].as<float>() * 1000.0;
    latest.close();
  }
}

// ========================== Send to Server =============================
void sendDataToServer() {
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("wifi tidak tersambung");
    return;
  }
  if(deviceToken.isEmpty()){
    Serial.println("token tidak ada");
    return;
  }

  HTTPClient https;
  https.begin(controlEndpoint);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + deviceToken); 

  StaticJsonDocument<512> doc;
  doc["device_mac_address"] = WiFi.macAddress();
  doc["device_token"] = deviceToken;  // meskipun bisa opsional, tetap dikirim
  doc["unit_id"] = unitId;
  doc["flat_id"] = flatId;

  JsonArray result = doc.createNestedArray("device_components_result");

  JsonObject sensor = result.createNestedObject();
  sensor["component_type"] = "sensor";
  sensor["component_name"] = "water";
  JsonObject sensorVal = sensor.createNestedObject("value");
  sensorVal["volume"] = data.volume;
  sensorVal["flowRate"] = data.flowRate;

  JsonObject actuator = result.createNestedObject();
  actuator["component_type"] = "actuator";
  actuator["component_name"] = "water";
  actuator["value"] = aktuator_state;

  String jsonBody;
  serializeJsonPretty(doc, jsonBody);  // agar payload terbaca dengan rapi

  Serial.println("\n==== Sending Sensor Data ====");
  Serial.print("POST to: ");
  Serial.println(controlEndpoint);
  Serial.println("Payload:");
  Serial.println(jsonBody);

  int code = https.POST(jsonBody);
  Serial.printf("HTTPS POST status: %d\n", code);

  if (code > 0) {
    String response = https.getString();
    Serial.println("Response body:");
    Serial.println(response);

    // ==== PARSE RESPONSE DAN EKSEKUSI PERINTAH BERDASARKAN RESPONSE SERVER ====
    StaticJsonDocument<512> resDoc;
    DeserializationError err = deserializeJson(resDoc, response);
    if (!err && resDoc["data"].is<JsonObject>()) {
      JsonObject cmd = resDoc["data"];
      if (cmd["type"] == "water" && cmd["to"].is<int>()) {
        bool targetState = cmd["to"].as<int>() == 1;
        if (targetState != aktuator_state) {
          Serial.printf("Mengubah aktuator ke: %s\n", targetState ? "NYALA" : "MATI");
          aktuator_state = targetState;
          digitalWrite(kSVPin, aktuator_state);
          digitalWrite(kSSRPin, aktuator_state);
        } else {
          Serial.println("Perintah sama dengan status saat ini, tidak ada perubahan.");
        }
      } else {
        Serial.println("Tidak ada perintah kontrol yang valid.");
      }
    } else {
      Serial.println("Tidak ada perintah kontrol dalam response.");
    }
  } else {
    Serial.print("Request failed, error: ");
    Serial.println(https.errorToString(code));
  }

  https.end();
}

// ========================== RTOS Task =============================
void taskCollect(void* param) {
  while (true) {
    uint32_t now = millis();
    if (now - previousTime >= 1000) {
      collectFlowMeterData();
      collectKwhMeterData();
      collectRTCData();
      previousTime = now;
    }
    if (now - lcdTime1 >= kSwitchStageTime) lcdDisplayData();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void taskSend(void* param) {
  uint32_t lastSave = millis();
  while (true) {
    uint32_t now = millis();
    if (now - lastSave >= kSaveDataPeriod) {
      saveToSD();
      sendDataToServer();
      lastSave = now;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ========================== Setup =============================
void setup() {
  Serial.begin(115200);
  pinMode(kFlowMeterPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kFlowMeterPin), pulseCounter, FALLING);
  Serial2.begin(9600, SERIAL_8N1, kRxPin, kTxPin);
  lcd.init(); lcd.begin(kLcdColumns, kLcdRows); lcd.backlight();

  connectToWiFi();
  configTime(gmtOffset_sec, 0, ntpServer);
  while (!collectRTCData()) delay(1000);

  pinMode(kSVPin, OUTPUT); pinMode(kSSRPin, OUTPUT);
  digitalWrite(kSVPin, HIGH); digitalWrite(kSSRPin, HIGH);
  aktuator_state = true;

  if (initSD()) restoreLastData();

  if (!getApikeyDevice()) {
    Serial.println("Gagal mendapatkan API Key, reset ESP...");
    ESP.restart();
  }

  if (!authenticateDevice()) {
    Serial.println("Gagal autentikasi, reset ESP...");
    ESP.restart();
  }

  lcdTime1 = millis();
  xTaskCreatePinnedToCore(taskCollect, "CollectTask", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskSend, "SendTask", 8192, NULL, 1, NULL, 0);
  }

void loop() {}
