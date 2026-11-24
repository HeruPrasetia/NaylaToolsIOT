#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>
#include <LittleFS.h>
#include <esp_system.h>

// ---------------------------------------------------------
// ESP32-C3 Super Mini (Kabel Kuning -> 20, Kabel Hitam -> 21)
// ---------------------------------------------------------
#define R558_RX_PIN 20
#define R558_TX_PIN 21
#define CONFIG_FILE "/database.json"

HardwareSerial mySerial(1); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
WebSocketsClient webSocket;

const char* ws_host = "192.168.1.5";
const int ws_port = 3003;

// Ubah token ini sesuai token device yang terdaftar di menu Master Device.
const char* defaultStaticToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyX2lkIjoxLCJkZXZpY2VfaWQiOjIsInVzZXJfbmFtZSI6IkFCU0VOU0kiLCJsb2thc2lfaWQiOjEsInBlcnVzYWhhYW5faWQiOiIxIiwibmFtYV9wZXJ1c2FoYWFuIjoiTmF5bGFUb29scyIsImVtYWlsIjoiaGVydS5wcmFzZXRpQGdtYWlsLmNvbSIsIm5hbWFfZGF0YWJhc2UiOiJlbmNfZmlmTEZrR2JkU0s5T0hYTzBQTGhMZjRFa2lNcVIzNmM0MHJKNEdpMllrc3VjcnZ5NDVYX1EzSHUiLCJpYXQiOjE3OTU0ODAxNTZ9.8X5avdx14BEoCcsXbCZ258_vx7TZxTZ2MoCT_DcPpJs";

StaticJsonDocument<1024> config;
String wifiSSID = "Anerira";
String wifiPassword = "naylatools.com";
String staticToken = defaultStaticToken;
bool configStorageReady = false;
bool restartPending = false;
unsigned long restartAt = 0;

void tampilkanMenu();
int bacaNomorID();
void menuDaftarJari();
void menuBacaJari();
void menuHapusJari();
void menuFormatDatabase();
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void handleWebSocketText(uint8_t* payload, size_t length);

void setDefaultConfig() {
  config.clear();
  config["token"] = defaultStaticToken;
  config["wifissid"] = "Anerira";
  config["wifipwd"] = "naylatools.com";
}

bool saveConfig() {
  if (!configStorageReady) {
    return false;
  }

  config["token"] = staticToken;
  config["wifissid"] = wifiSSID;
  config["wifipwd"] = wifiPassword;

  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("[FS] Gagal membuka file konfigurasi untuk ditulis");
    return false;
  }

  size_t bytesWritten = serializeJson(config, configFile);
  configFile.println();
  configFile.close();
  return bytesWritten > 0;
}

bool loadConfig() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] Gagal mount LittleFS");
    configStorageReady = false;
    return false;
  }
  configStorageReady = true;

  bool shouldSave = false;
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    setDefaultConfig();
    shouldSave = true;
  } else {
    DeserializationError error = deserializeJson(config, configFile);
    configFile.close();
    if (error || !config.is<JsonObject>()) {
      Serial.printf("[FS] Konfigurasi tidak valid: %s\n", error.c_str());
      setDefaultConfig();
      shouldSave = true;
    }
  }

  if (!config["token"].is<const char*>()) {
    config["token"] = defaultStaticToken;
    shouldSave = true;
  }
  if (!config["wifissid"].is<const char*>()) {
    config["wifissid"] = "Anerira";
    shouldSave = true;
  }
  if (!config["wifipwd"].is<const char*>()) {
    config["wifipwd"] = "naylatools.com";
    shouldSave = true;
  }

  staticToken = config["token"].as<String>();
  wifiSSID = config["wifissid"].as<String>();
  wifiPassword = config["wifipwd"].as<String>();

  if (shouldSave) {
    saveConfig();
  }
  Serial.printf("[CONFIG] SSID: %s\n", wifiSSID.c_str());
  return true;
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

  char jsonBuffer[256];
  if (serializeJson(result, jsonBuffer, sizeof(jsonBuffer)) > 0) {
    webSocket.sendTXT(jsonBuffer);
  }
}

const char* updateDeviceSettings(JsonVariantConst rawSettings) {
  if (!rawSettings.is<JsonObjectConst>() || !rawSettings.as<JsonObjectConst>().containsKey("token")) {
    return "Token wajib disertakan";
  }

  JsonObjectConst settings = rawSettings.as<JsonObjectConst>();
  String nextSSID = wifiSSID;
  String nextPassword = wifiPassword;
  String nextToken = staticToken;

  if (settings.containsKey("wifissid")) {
    if (!settings["wifissid"].is<const char*>()) {
      return "SSID WiFi tidak valid";
    }
    const char* value = settings["wifissid"].as<const char*>();
    if (strlen(value) == 0) {
      return "SSID WiFi tidak valid";
    }
    nextSSID = value;
  }
  if (settings.containsKey("wifipwd")) {
    if (!settings["wifipwd"].is<const char*>()) {
      return "Password WiFi tidak valid";
    }
    nextPassword = settings["wifipwd"].as<const char*>();
  }
  if (!settings["token"].is<const char*>() || strlen(settings["token"]) == 0) {
    return "Token device tidak valid";
  }
  nextToken = settings["token"].as<const char*>();

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

void handleWebSocketText(uint8_t* payload, size_t length) {
  StaticJsonDocument<1024> command;
  DeserializationError error = deserializeJson(command, payload, length);
  if (error || strcmp(command["type"] | "", "device_command") != 0) {
    return;
  }

  const char* commandName = command["command"] | "";
  if (strcmp(commandName, "update_settings") == 0) {
    const char* errorMessage = updateDeviceSettings(command["settings"]);
    if (errorMessage) {
      sendCommandResult(commandName, false, errorMessage);
      return;
    }
    sendCommandResult(commandName, true, "Pengaturan tersimpan; device akan restart");
    restartPending = true;
    restartAt = millis() + 1000;
  } else {
    sendCommandResult(commandName, false, "Perintah perangkat tidak dikenal");
  }
}

void setup() {
  Serial.begin(115200);
  // Tunggu maksimal 3 detik agar tidak macet saat jalan standalone tanpa PC
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }
  delay(200);
  
  Serial.println("\n\n=====================================");
  Serial.println("  SISTEM SIDIK JARI GROW R558        ");
  Serial.println("=====================================");

  mySerial.begin(57600, SERIAL_8N1, R558_RX_PIN, R558_TX_PIN);
  delay(50);

  if (finger.verifyPassword()) {
    Serial.println("[V] Sensor R558 Terhubung!");
  } else {
    Serial.println("[X] Sensor TIDAK DITEMUKAN. Cek kabel TX/RX (20 & 21).");
    while (1) { delay(100); }
  }

  finger.getTemplateCount();
  Serial.print("Jumlah sidik jari tersimpan: ");
  Serial.println(finger.templateCount);

  if (!loadConfig()) {
    Serial.println("[CONFIG] Menggunakan konfigurasi bawaan");
  }

  WiFi.mode(WIFI_STA);
  // WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  Serial.print("Menghubungkan ke WiFi");

  unsigned long wifiWaitStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiWaitStart < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi terhubung! IP ESP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Belum terhubung. Menunggu koneksi di loop.");
  }

  String url = "/ws?token=" + staticToken;
  webSocket.begin(ws_host, ws_port, url);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  tampilkanMenu();
}

void loop() {
  webSocket.loop();

  if (restartPending && static_cast<int32_t>(millis() - restartAt) >= 0) {
    ESP.restart();
  }

  if (Serial.available() > 0) {
    char pilihan = Serial.read();
    while (Serial.available() > 0) { Serial.read(); }

    switch (pilihan) {
      case '1':
        menuDaftarJari();
        tampilkanMenu();
        break;
      case '2':
        menuBacaJari();
        tampilkanMenu();
        break;
      case '3':
        menuHapusJari();
        tampilkanMenu();
        break;
      case '4': 
        menuFormatDatabase();
        tampilkanMenu();
        break;
      default:
        break;
    }
  }

  static unsigned long lastSent = 0;
  if (millis() - lastSent >= 5000) {
    lastSent = millis();

    finger.getTemplateCount();
    if (webSocket.isConnected()) {
      StaticJsonDocument<512> telemetry;
      telemetry["type"] = "device_telemetry";
      telemetry["chip_id"] = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX) +
                             String((uint32_t)ESP.getEfuseMac(), HEX);
      telemetry["free_heap_kb"] = ESP.getFreeHeap() / 1024.0;
      telemetry["heap_fragmentation"] = 0;
      telemetry["flash_chip_size_kb"] = ESP.getFlashChipSize() / 1024.0;
      telemetry["free_sketch_space_kb"] = ESP.getFreeSketchSpace() / 1024.0;
      telemetry["local_ip"] = WiFi.localIP().toString();
      telemetry["mac_address"] = WiFi.macAddress();
      telemetry["ssid"] = WiFi.SSID();
      telemetry["bssid"] = WiFi.BSSIDstr();
      telemetry["rssi"] = WiFi.RSSI();
      telemetry["reset_reason"] = String((uint32_t)esp_reset_reason());
      telemetry["template_count"] = finger.templateCount;
      telemetry["uptime"] = millis() / 1000;

      char jsonBuffer[512];
      if (serializeJson(telemetry, jsonBuffer, sizeof(jsonBuffer)) > 0) {
        webSocket.sendTXT(jsonBuffer);
      }
    }
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Terputus dari server!");
      break;

    case WStype_CONNECTED:
      Serial.printf("[WS] Terhubung ke server: %s\n", payload);
      webSocket.sendTXT(
        "{\"type\":\"device_ready\",\"device\":\"Fingerprint_ESP32C3\",\"status\":\"online\"}"
      );
      break;

    case WStype_TEXT:
      Serial.printf("[WS] Pesan diterima (%u bytes)\n", length);
      handleWebSocketText(payload, length);
      break;

    case WStype_BIN:
      Serial.printf("[WS] Data biner diterima (%u bytes)\n", length);
      break;

    case WStype_ERROR:
      Serial.println("[WS] Error terjadi!");
      break;

    default:
      break;
  }
}

void tampilkanMenu() {
  Serial.println("\n=====================================");
  Serial.println("Ketik angka di Serial Monitor & Enter:");
  Serial.println("[1] DAFTAR (Menggunakan Mode Auto-Enroll Pabrik)");
  Serial.println("[2] BACA / Identifikasi Sidik Jari");
  Serial.println("[3] HAPUS Sidik Jari Spesifik");
  Serial.println("[4] FORMAT / Kosongkan Seluruh Memori");
  Serial.println("=====================================\n");
}

int bacaNomorID() {
  int id = 0;
  while (true) {
    while (!Serial.available()) {
      webSocket.loop();
      delay(10);
    }
    id = Serial.parseInt();
    while (Serial.available() > 0) { Serial.read(); } 
    
    if (id > 0 && id <= 63) { 
      return id;
    } else {
      Serial.println("-> ID tidak valid! Maksimal nomor ID adalah 63. Masukkan angka 1 sampai 63:");
    }
  }
}

// ---------------------------------------------------------
// 1. FUNGSI DAFTAR (RAW AUTO-ENROLL 0x31 - 4x TAP)
// ---------------------------------------------------------
void menuDaftarJari() {
  Serial.println("\n--- MODE PENDAFTARAN OTOMATIS (AUTO-ENROLL) ---");
  Serial.println("Masukkan nomor ID (1-63):");
  int id = bacaNomorID();

  Serial.print("\nMendaftarkan ID #"); Serial.println(id);

  // Bersihkan buffer RX
  while (mySerial.available()) mySerial.read();

  // Paket Auto-Enroll (31H) sesuai spesifikasi firmware GROW R558
  uint8_t id_h = (id >> 8) & 0xFF;
  uint8_t id_l = id & 0xFF;
  uint8_t times = 0x04; // 4 kali penempelan jari

  uint16_t sum = 0x01 + 0x00 + 0x08 + 0x31 + id_h + id_l + times + 0x00 + 0x00;
  uint8_t packet[] = {
    0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x08,
    0x31, id_h, id_l, times, 0x00, 0x00,
    (uint8_t)(sum >> 8), (uint8_t)(sum & 0xFF)
  };

  mySerial.write(packet, sizeof(packet));

  unsigned long startTime = millis();
  bool isFinished = false;

  while (millis() - startTime < 40000 && !isFinished) {
    webSocket.loop();
    if (mySerial.available()) {
      if (mySerial.read() == 0xEF) {
        delay(5);
        if (mySerial.read() == 0x01) {
          uint8_t buf[12];
          int bytesRead = mySerial.readBytes(buf, 12);
          if (bytesRead == 12) {
            uint8_t confCode = buf[7];
            uint8_t p1 = buf[8];

            if (p1 == 0x00) {
               if (confCode == 0x00) Serial.println("-> Sensor siap. TEMPELKAN jari Anda (Butuh 4x Tap)...");
               else { Serial.println("-> Gagal memulai prosedur."); isFinished = true; }
            }
            else if (p1 == 0x01 && confCode == 0x00) {
               Serial.println("-> Gambar direkam! ANGKAT jari Anda sebentar.");
            }
            else if (p1 == 0x03 && confCode == 0x00) {
               Serial.println("-> TEMPELKAN KEMBALI jari Anda...");
            }
            else if (p1 == 0x06) {
               if (confCode == 0x00) {
                 Serial.println("\n[SUKSES] Sidik jari telah disimpan permanen di ID #" + String(id));
               } else {
                 Serial.print("\n[GAGAL MENYIMPAN] Kode: 0x");
                 Serial.println(confCode, HEX);
               }
               isFinished = true;
            }
            else if (confCode != 0x00 && p1 != 0x06) {
               if (confCode == 0x26) {
                 Serial.println("\n[GAGAL] Tekan jari lebih jelas (Kode 0x26). Pendaftaran dibatalkan.");
                 isFinished = true;
               } else {
                 Serial.print("   (Info respons sensor: 0x");
                 Serial.print(confCode, HEX); Serial.println(")");
               }
            }
            startTime = millis();
          }
        }
      }
    }
  }
  if (!isFinished) {
    Serial.println("\n[TIMEOUT] Waktu habis. Pendaftaran dibatalkan.");
  }
}

// ---------------------------------------------------------
// 2. FUNGSI BACA / CHECK LOGIN
// ---------------------------------------------------------
void menuBacaJari() {
  Serial.println("\n--- MODE MEMBACA (CHECK LOGIN) ---");
  Serial.println("Sistem siaga. Tempelkan jari kapan saja.");
  Serial.println("Ketik '0' dan Enter untuk KEMBALI.");

  while (true) {
    webSocket.loop();
    if (Serial.available() > 0) {
      if (Serial.read() == '0') {
        while (Serial.available() > 0) { Serial.read(); } 
        Serial.println("Keluar dari Mode Membaca..."); 
        break;
      }
    }

    uint8_t status = finger.getImage();
    if (status == FINGERPRINT_OK) {
      Serial.println("\n[!] Jari terdeteksi. Memproses...");
      status = finger.image2Tz();
      if (status != FINGERPRINT_OK) {
        Serial.println(" -> Gambar kurang jelas. Coba lagi."); 
        delay(1500);
        continue;
      }
      status = finger.fingerSearch();
      if (status == FINGERPRINT_OK) {
        Serial.println(" -> [AKSES DITERIMA] Sidik jari COCOK!");
        Serial.print("    - ID Pengguna : "); Serial.println(finger.fingerID);
        Serial.print("    - Akurasi     : "); Serial.println(finger.confidence);
        if (webSocket.isConnected()) {
          StaticJsonDocument<256> attendance;
          attendance["type"] = "fingerprint_match";
          attendance["finger_id"] = finger.fingerID;
          attendance["confidence"] = finger.confidence;
          attendance["uptime"] = millis() / 1000;

          char jsonBuffer[256];
          if (serializeJson(attendance, jsonBuffer, sizeof(jsonBuffer)) > 0) {
            webSocket.sendTXT(jsonBuffer);
          }
        }
      } else {
        Serial.println(" -> [AKSES DITOLAK] Sidik jari tidak terdaftar.");
      }
      delay(1500);
    }
    delay(50);
  }
}

// ---------------------------------------------------------
// 3. FUNGSI HAPUS SPESIFIK
// ---------------------------------------------------------
void menuHapusJari() {
  Serial.println("\n--- MODE HAPUS ---");
  Serial.println("Masukkan nomor ID (1-63) yang ingin DIHAPUS:");
  int id = bacaNomorID();
  if (finger.deleteModel(id) == FINGERPRINT_OK) {
    Serial.print("[SUKSES] Sidik jari ID #"); Serial.print(id); Serial.println(" telah dihapus.");
  } else {
    Serial.println("[GAGAL] Tidak dapat menghapus ID tersebut.");
  }
}

// ---------------------------------------------------------
// 4. FUNGSI FORMAT DATABASE
// ---------------------------------------------------------
void menuFormatDatabase() {
  Serial.println("\n--- MODE FORMAT DATABASE ---");
  Serial.println("PERINGATAN: Ini akan menghapus SELURUH sidik jari!");
  Serial.println("Ketik 'Y' lalu Enter untuk lanjut, atau 'N' untuk batal.");
  while (true) {
    webSocket.loop();
    if (Serial.available() > 0) {
      char c = Serial.read();
      while (Serial.available() > 0) { Serial.read(); }

      if (c == 'Y' || c == 'y') {
        Serial.println("Memformat ulang memori Flash sensor...");
        uint8_t p = finger.emptyDatabase();
        if (p == FINGERPRINT_OK) {
          Serial.println("[SUKSES] Memori sensor telah direset total.");
        } else {
          Serial.println("[GAGAL] Gagal memformat memori.");
        }
        break;
      } else if (c == 'N' || c == 'n') {
        Serial.println("Format dibatalkan.");
        break;
      }
    }
  }
}