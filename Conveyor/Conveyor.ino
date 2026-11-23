#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <string.h>
#include "HX711.h"

#define KELEMBAPAN A0
#define CONFIG_FILE "/database.json"
#define CURRENT_VERSION "1.0.0"
#define VERSION_URL "http://iot.poradaan.com/versions.txt"
#define UPDATE_URL "http://iot.poradaan.com/update.bin"
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4 // Sesuaikan jumlah kotak 8x8 kamu

#define DATA_PIN  D1
#define CLK_PIN   D2
#define CS_PIN    D4
#define COUNTER_SENSOR_PIN D5

StaticJsonDocument<1024> config;
MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
uint32_t counterBarangMasuk = 0;
bool counterStorageReady = false;
bool counterUpdatePending = false;
bool restartPending = false;
unsigned long restartAt = 0;
String wifiSSID;
String wifiPassword;
String staticToken;
int lastCounterReading = HIGH;
int stableCounterReading = HIGH;
unsigned long lastCounterDebounce = 0;
unsigned long lastCounterUpdateAttempt = 0;

// Pin sesuai susunan ESP8266 kamu
const int LOADCELL_DOUT_PIN = D7; // GPIO13 (DT)
const int LOADCELL_SCK_PIN  = D6; // GPIO12 (SCK)
HX711 scale;
const uint8_t STABLE_WEIGHT_SAMPLE_COUNT = 5;
const float STABLE_WEIGHT_TOLERANCE_GRAM = 10.0;
const float MINIMUM_WEIGHT_CHANGE_GRAM = 20.0;
const unsigned long WEIGHT_SAMPLE_INTERVAL_MS = 100;
const unsigned long WEIGHT_TIMEOUT_MS = 30000;
bool weighingPending = false;
float weightBaselineGram = 0.0;
float stableWeightSamples[STABLE_WEIGHT_SAMPLE_COUNT];
uint8_t stableWeightSampleCount = 0;
uint8_t stableWeightSampleIndex = 0;
unsigned long weighingStartedAt = 0;
unsigned long lastWeightSampleAt = 0;
float pendingWeightKg = 0.0;

// Nilai awal kalibrasi untuk load cell 20kg (wajib disesuaikan nanti)
float calibration_factor = 105.0; 

// ==========================================
// KONFIGURASI WIFI & SERVER API
// ==========================================
// Host & Port Backend Go (Plain HTTP / WS non-SSL)
// const char* ws_host  = "ws.poradaan.com"; // HANYA IP / HOSTNAME SAJA, JANGAN pakai "http://"
// const int   ws_port  = 80;
// const char* static_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyX2lkIjoxLCJkZXZpY2VfaWQiOjEsInVzZXJfbmFtZSI6IkVTUDgyNjZfVGltYmFuZ2FuIiwibG9rYXNpX2lkIjoxLCJwZXJ1c2FoYWFuX2lkIjoiMSIsIm5hbWFfcGVydXNhaGFhbiI6Ik5heWxhVG9vbHMiLCJlbWFpbCI6ImhlcnUucHJhc2V0aUBnbWFpbC5jb20iLCJuYW1hX2RhdGFiYXNlIjoiZW5jX1p4Y3RtOXlBdFZEYmN5eEZnTTJBNmJNTER4UWU3bmFxek13eHV6RXliU0tkQzl3Ylo5Zm5VRHI4IiwiaWF0IjoxNzk0OTY0NDY3fQ.Eas_s8eA_bSIwCa4i4hqZrL8VZ__88OIV5vruWjmQRg";

const char* ws_host  = "192.168.1.8"; // HANYA IP / HOSTNAME SAJA, JANGAN pakai "http://"
const int   ws_port  = 3003;

WebSocketsClient webSocket;
String displayMessage;
bool counterDisplayActive = false;

void showDisplayMessage(const String& message) {
  counterDisplayActive = false;
  displayMessage = message;
  display.displayClear();
  display.displayText(
    displayMessage.c_str(),
    PA_CENTER,
    50,
    0,
    PA_SCROLL_LEFT,
    PA_SCROLL_LEFT
  );
}

void showCounter() {
  counterDisplayActive = true;
  displayMessage = String(counterBarangMasuk);
  display.displayClear();
  display.displayText(
    displayMessage.c_str(),
    PA_CENTER,
    0,
    0,
    PA_PRINT,
    PA_NO_EFFECT
  );
}

void showLiveWeight(float beratGram) {
  float beratKg = beratGram / 1000.0;
  String weightText = String(beratKg, 1);
  if (weightText.length() > 3) {
    weightText = String(beratKg, 0);
  }
  weightText += "kg";
  if (counterDisplayActive && displayMessage == weightText) {
    return;
  }

  counterDisplayActive = true;
  displayMessage = weightText;
  display.displayClear();
  display.displayText(
    displayMessage.c_str(),
    PA_CENTER,
    0,
    0,
    PA_PRINT,
    PA_NO_EFFECT
  );
}

void appendPayload(String& message, const uint8_t* payload, size_t length) {
  message.reserve(message.length() + length);
  for (size_t i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }
}

void setDefaultConfig() {
  config.clear();
  config["token"] = "";
  config["wifissid"] = "Anerira";
  config["wifipwd"] = "naylatools.com";
  config["conter"] = 0;
}

bool saveConfig() {
  if (!counterStorageReady) {
    return false;
  }

  config["token"] = staticToken;
  config["wifissid"] = wifiSSID;
  config["wifipwd"] = wifiPassword;
  config["conter"] = counterBarangMasuk;

  if (config.overflowed()) {
    Serial.println("[FS] Konfigurasi terlalu besar untuk disimpan");
    return false;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("[FS] Gagal membuka file konfigurasi untuk ditulis");
    return false;
  }

  size_t expectedBytes = measureJson(config);
  size_t bytesWritten = serializeJson(config, configFile);
  size_t newlineBytes = configFile.println();
  configFile.close();
  if (bytesWritten != expectedBytes || newlineBytes == 0) {
    Serial.println("[FS] Gagal menyimpan konfigurasi");
    return false;
  }
  return true;
}

bool loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("[FS] Gagal mount LittleFS; konfigurasi tidak dapat dimuat");
    counterStorageReady = false;
    return false;
  }
  counterStorageReady = true;

  bool shouldSaveConfig = false;
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    if (LittleFS.exists(CONFIG_FILE)) {
      Serial.println("[FS] File konfigurasi ada tetapi gagal dibuka");
      return false;
    }
    setDefaultConfig();
    shouldSaveConfig = true;
  } else {
    DeserializationError error = deserializeJson(config, configFile);
    configFile.close();
    if (error || !config.is<JsonObject>()) {
      if (error) {
        Serial.printf("[FS] JSON konfigurasi tidak valid: %s\n", error.c_str());
      } else {
        Serial.println("[FS] Akar JSON konfigurasi harus berupa object");
      }
      setDefaultConfig();
      shouldSaveConfig = true;
    }
  }

  if (!config["token"].is<const char*>()) {
    config["token"] = "";
    shouldSaveConfig = true;
  }
  if (!config["wifissid"].is<const char*>()) {
    config["wifissid"] = "Anerira";
    shouldSaveConfig = true;
  }
  if (!config["wifipwd"].is<const char*>()) {
    config["wifipwd"] = "naylatools.com";
    shouldSaveConfig = true;
  }
  if (!config["conter"].is<uint32_t>()) {
    config["conter"] = 0;
    shouldSaveConfig = true;
  }

  staticToken = config["token"].as<String>();
  wifiSSID = config["wifissid"].as<String>();
  wifiPassword = config["wifipwd"].as<String>();
  counterBarangMasuk = config["conter"].as<uint32_t>();

  if (shouldSaveConfig && !saveConfig()) {
    Serial.println("[FS] Gagal membuat atau memperbaiki file konfigurasi");
    return false;
  }

  Serial.printf("[CONFIG] SSID: %s | Counter: %lu\n",
    wifiSSID.c_str(), static_cast<unsigned long>(counterBarangMasuk));
  Serial.printf("[COUNTER] Barang masuk tersimpan: %lu\n", static_cast<unsigned long>(counterBarangMasuk));
  return true;
}

void sendCounterUpdate() {
  if (!counterUpdatePending) {
    return;
  }

  lastCounterUpdateAttempt = millis();
  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<256> update;
  update["type"] = "counter_update";
  update["act"] = "add counter";
  update["counter_barang"] = counterBarangMasuk;
  update["berat_kg"] = pendingWeightKg;

  char jsonBuffer[256];
  size_t length = serializeJson(update, jsonBuffer, sizeof(jsonBuffer));
  if (length == 0) {
    Serial.println("[WS] Gagal membuat update counter");
    return;
  }
  if (webSocket.sendTXT(jsonBuffer)) {
    counterUpdatePending = false;
  } else {
    Serial.println("[WS] Gagal mengirim update counter");
  }
}

void recordIncomingItem(float beratGram) {
  if (counterBarangMasuk != 0xFFFFFFFFUL) {
    counterBarangMasuk++;
  }
  pendingWeightKg = beratGram / 1000.0;
  counterUpdatePending = true;
  Serial.printf("[COUNTER] Barang masuk: %lu | Berat: %.3f kg\n",
    static_cast<unsigned long>(counterBarangMasuk), pendingWeightKg);
  showCounter();
  if (!saveConfig()) {
    Serial.println("[COUNTER] Gagal menyimpan counter ke konfigurasi LittleFS");
  }
  sendCounterUpdate();
}

bool resetCounter() {
  if (!counterStorageReady) {
    return false;
  }

  uint32_t previousCounter = counterBarangMasuk;
  counterBarangMasuk = 0;
  if (!saveConfig()) {
    counterBarangMasuk = previousCounter;
    return false;
  }
  return true;
}

const char* updateDeviceSettings(JsonVariantConst rawSettings) {
  if (!rawSettings.is<JsonObjectConst>()) {
    return "Format pengaturan device tidak valid";
  }

  JsonObjectConst settings = rawSettings.as<JsonObjectConst>();
  if (!settings.containsKey("token") || settings.size() > 3) {
    return "Token wajib disertakan dan field pengaturan harus valid";
  }

  String nextSSID = wifiSSID;
  String nextPassword = wifiPassword;
  String nextToken = staticToken;

  if (settings.containsKey("wifissid")) {
    if (!settings["wifissid"].is<const char*>()) {
      return "SSID WiFi tidak valid";
    }
    const char* value = settings["wifissid"].as<const char*>();
    size_t length = strlen(value);
    if (length == 0 || length > 32) {
      return "SSID WiFi harus berisi 1-32 byte";
    }
    nextSSID = value;
  }

  if (settings.containsKey("wifipwd")) {
    if (!settings["wifipwd"].is<const char*>()) {
      return "Password WiFi tidak valid";
    }
    const char* value = settings["wifipwd"].as<const char*>();
    size_t length = strlen(value);
    if (length != 0 && (length < 8 || length > 63)) {
      return "Password WiFi harus berisi 8-63 byte";
    }
    nextPassword = value;
  }

  if (!settings["token"].is<const char*>()) {
    return "Token device tidak valid";
  }
  const char* token = settings["token"].as<const char*>();
  size_t tokenLength = strlen(token);
  if (tokenLength == 0 || tokenLength > 768) {
    return "Token device tidak valid";
  }
  nextToken = token;

  String previousSSID = wifiSSID;
  String previousPassword = wifiPassword;
  String previousToken = staticToken;
  wifiSSID = nextSSID;
  wifiPassword = nextPassword;
  staticToken = nextToken;
  if (!saveConfig()) {
    wifiSSID = previousSSID;
    wifiPassword = previousPassword;
    staticToken = previousToken;
    return "Gagal menyimpan konfigurasi ke LittleFS";
  }

  return nullptr;
}

void sendCommandResult(const char* command, bool success, const char* message) {
  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<256> result;
  result["type"] = "device_command_result";
  result["command"] = command;
  result["status"] = success ? "success" : "error";
  result["message"] = message;
  if (strcmp(command, "reset_counter") == 0) {
    result["counter_barang"] = counterBarangMasuk;
  }

  char jsonBuffer[256];
  if (serializeJson(result, jsonBuffer, sizeof(jsonBuffer)) == 0) {
    Serial.println("[WS] Gagal membuat hasil perintah");
    return;
  }
  webSocket.sendTXT(jsonBuffer);
}

void handleWebSocketText(uint8_t* payload, size_t length) {
  StaticJsonDocument<1024> command;
  DeserializationError error = deserializeJson(command, payload, length);
  if (error || strcmp(command["type"] | "", "device_command") != 0) {
    String message = "[WS] ";
    appendPayload(message, payload, length);
    showDisplayMessage(message);
    return;
  }

  const char* commandName = command["command"] | "";
  if (strcmp(commandName, "display_message") == 0) {
    String message = command["message"] | "";
    if (message.length() == 0) {
      sendCommandResult(commandName, false, "Pesan LED kosong");
      return;
    }
    showDisplayMessage(message);
    sendCommandResult(commandName, true, "Pesan ditampilkan di LED matrix");
  } else if (strcmp(commandName, "reset_counter") == 0) {
    bool success = resetCounter();
    const char* resultMessage = success ? "Counter barang berhasil direset" : "Gagal menyimpan reset counter ke LittleFS";
    showDisplayMessage(success ? "Counter direset" : "Gagal reset counter");
    sendCommandResult(commandName, success, resultMessage);
  } else if (strcmp(commandName, "update_settings") == 0) {
    const char* errorMessage = updateDeviceSettings(command["settings"]);
    if (errorMessage) {
      showDisplayMessage("Gagal simpan pengaturan");
      sendCommandResult(commandName, false, errorMessage);
      return;
    }
    showDisplayMessage("Setting tersimpan, restart");
    sendCommandResult(commandName, true, "Pengaturan tersimpan; device akan restart");
    restartPending = true;
    restartAt = millis() + 1000;
  } else {
    showDisplayMessage("[WS] Perintah tidak dikenal");
    sendCommandResult(commandName, false, "Perintah perangkat tidak dikenal");
  }
}

void updateCounterSensor() {
  int reading = digitalRead(COUNTER_SENSOR_PIN);
  if (reading != lastCounterReading) {
    lastCounterDebounce = millis();
    lastCounterReading = reading;
  }

  if (millis() - lastCounterDebounce >= 50 && reading != stableCounterReading) {
    stableCounterReading = reading;
    if (stableCounterReading == LOW) {
      if (weighingPending) {
        Serial.println("[WEIGH] Trigger baru diabaikan; penimbangan sebelumnya belum stabil");
        return;
      }

      weighingPending = true;
      weighingStartedAt = millis();
      lastWeightSampleAt = 0;
      stableWeightSampleCount = 0;
      stableWeightSampleIndex = 0;
      weightBaselineGram = scale.is_ready() ? scale.get_units(1) : 0.0;
      showLiveWeight(weightBaselineGram);
      Serial.println("[WEIGH] Switch terdeteksi; menunggu berat stabil");
    }
  }
}

void updateLoadCell() {
  if (!weighingPending) {
    return;
  }

  if (millis() - weighingStartedAt >= WEIGHT_TIMEOUT_MS) {
    weighingPending = false;
    Serial.println("[WEIGH] Timeout; berat tidak stabil, counter tidak ditambah");
    showCounter();
    return;
  }

  if (millis() - lastWeightSampleAt < WEIGHT_SAMPLE_INTERVAL_MS || !scale.is_ready()) {
    return;
  }
  lastWeightSampleAt = millis();

  float beratGram = scale.get_units(1);
  if (beratGram < 1.0 && beratGram > -1.0) {
    beratGram = 0.0;
  }
  showLiveWeight(beratGram);

  float weightChange = beratGram - weightBaselineGram;
  if (weightChange < 0.0) {
    weightChange = -weightChange;
  }
  if (weightChange < MINIMUM_WEIGHT_CHANGE_GRAM) {
    stableWeightSampleCount = 0;
    stableWeightSampleIndex = 0;
    return;
  }

  if (stableWeightSampleCount < STABLE_WEIGHT_SAMPLE_COUNT) {
    stableWeightSamples[stableWeightSampleCount++] = beratGram;
  } else {
    stableWeightSamples[stableWeightSampleIndex] = beratGram;
    stableWeightSampleIndex = (stableWeightSampleIndex + 1) % STABLE_WEIGHT_SAMPLE_COUNT;
  }
  if (stableWeightSampleCount < STABLE_WEIGHT_SAMPLE_COUNT) {
    return;
  }

  float minimumWeight = stableWeightSamples[0];
  float maximumWeight = stableWeightSamples[0];
  float totalWeight = 0.0;
  for (uint8_t i = 0; i < STABLE_WEIGHT_SAMPLE_COUNT; i++) {
    float sample = stableWeightSamples[i];
    if (sample < minimumWeight) minimumWeight = sample;
    if (sample > maximumWeight) maximumWeight = sample;
    totalWeight += sample;
  }

  if (maximumWeight - minimumWeight <= STABLE_WEIGHT_TOLERANCE_GRAM) {
    weighingPending = false;
    recordIncomingItem(totalWeight / STABLE_WEIGHT_SAMPLE_COUNT);
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(COUNTER_SENSOR_PIN, INPUT_PULLUP);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  
  display.begin();
  display.setIntensity(3); // Kecerahan 0 - 15
  display.displayClear();

  const String startupMessage = "--- Memulai Inisialisasi ESP8266 Conveyor ---";
  showDisplayMessage(startupMessage);
  Serial.println("\n" + startupMessage);
  if (!loadConfig()) {
    showDisplayMessage("Gagal memuat konfigurasi");
    while (true) {
      delay(1000);
    }
  }
  lastCounterReading = digitalRead(COUNTER_SENSOR_PIN);
  stableCounterReading = lastCounterReading;
  lastCounterDebounce = millis();

  // 1. Hubungkan ke WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (display.displayAnimate()) {
      display.displayReset();
    }
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung! IP ESP: " + WiFi.localIP().toString());

  // 2. Konfigurasi WebSocket Client dengan Static Token
  String url = "/ws?token=" + staticToken;
  webSocket.begin(ws_host, ws_port, url);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // Reconnect otomatis setiap 5 detik jika terputus
  
  scale.set_scale(calibration_factor);
  scale.tare(); // Reset angka ke 0
}

// ==========================================
// CALLBACK EVENT WEBSOCKET
// ==========================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Terputus dari server!");
      showDisplayMessage("[WS] Terputus dari server!");
      break;

    case WStype_CONNECTED:
      Serial.printf("[WS] Terhubung ke server: %s\n", payload);
      showDisplayMessage("[WS] Terhubung ke server!");
      sendCounterUpdate();
      // Kirim event awal saat terkoneksi
      webSocket.sendTXT("{\"type\":\"device_ready\",\"device\":\"Conveyor_ESP8266\",\"status\":\"online\"}");
      break;

    case WStype_TEXT:
      Serial.printf("[WS] Pesan diterima (%u bytes)\n", length);
      handleWebSocketText(payload, length);
      // Contoh tangkap perintah dari frontend atau server:
      // if (strstr((char*)payload, "\"cmd\":\"START\"")) {
      //   // Nyalakan relay / motor conveyor
      // } else if (strstr((char*)payload, "\"cmd\":\"STOP\"")) {
      //   // Matikan relay / motor conveyor
      // }
      break;

    case WStype_BIN:
      Serial.printf("[WS] Data biner diterima (%u bytes)\n", length);
      showDisplayMessage("[WS] Data biner diterima (" + String(length) + " bytes)");
      break;

    case WStype_ERROR:
      Serial.println("[WS] Error terjadi!");
      showDisplayMessage("[WS] Error terjadi!");
      break;

    default:
      break;
  }
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // Wajib dipanggil terus menerus untuk memproses frame WS
  webSocket.loop();
  if (restartPending && static_cast<int32_t>(millis() - restartAt) >= 0) {
    ESP.restart();
  }
  if (counterUpdatePending && millis() - lastCounterUpdateAttempt >= 1000) {
    sendCounterUpdate();
  }
  updateCounterSensor();
  if (display.displayAnimate()) {
    if (!counterDisplayActive) {
      display.displayReset();
    }
  }
  updateLoadCell();

  // Kirim data diagnostik & telemetri perangkat secara berkala ke server (setiap 5 detik)
  static unsigned long lastSent = 0;
  if (millis() - lastSent > 5000) {
    lastSent = millis();

    uint32_t chipId       = ESP.getChipId();
    float    freeHeapKb   = ESP.getFreeHeap() / 1024.0;
    uint8_t  heapFrag     = ESP.getHeapFragmentation();
    float    flashSizeKb  = ESP.getFlashChipSize() / 1024.0;
    float    freeSketchKb = ESP.getFreeSketchSpace() / 1024.0;
    int32_t  rssi         = WiFi.RSSI();
    String   resetRsn     = ESP.getResetReason();
    String   localIp      = WiFi.localIP().toString();
    String   macAddr      = WiFi.macAddress();
    String   ssid         = WiFi.SSID();
    String   bssid        = WiFi.BSSIDstr();

    // Kirim data ke WebSocket server dalam format JSON
    if (webSocket.isConnected()) {
      StaticJsonDocument<640> telemetry;
      telemetry["type"] = "device_telemetry";
      telemetry["chip_id"] = chipId;
      telemetry["free_heap_kb"] = freeHeapKb;
      telemetry["heap_fragmentation"] = heapFrag;
      telemetry["flash_chip_size_kb"] = flashSizeKb;
      telemetry["free_sketch_space_kb"] = freeSketchKb;
      telemetry["local_ip"] = localIp;
      telemetry["mac_address"] = macAddr;
      telemetry["ssid"] = ssid;
      telemetry["bssid"] = bssid;
      telemetry["rssi"] = rssi;
      telemetry["reset_reason"] = resetRsn;
      telemetry["counter_barang"] = counterBarangMasuk;
      telemetry["uptime"] = millis() / 1000;

      char jsonBuffer[640];
      serializeJson(telemetry, jsonBuffer, sizeof(jsonBuffer));
      webSocket.sendTXT(jsonBuffer);
    }
  }
}
