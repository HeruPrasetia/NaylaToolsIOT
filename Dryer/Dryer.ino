#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <DHT11.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#define KELEMBAPAN A0
#define CONFIG_FILE "/database.json"
#define CURRENT_VERSION "1.0.0"
#define VERSION_URL "http://iot.poradaan.com/versions.txt"
#define UPDATE_URL "http://iot.poradaan.com/update.bin"

StaticJsonDocument<1024> config;
DHT11 dht11(4);

#define MOISTURE_RAW_MIN 200
#define MOISTURE_RAW_MAX 900

// ==========================================
// KONFIGURASI WIFI & SERVER API
// ==========================================
const char* ssid     = "Anerira";
const char* password = "naylatools.com";

// Host & Port Backend Go (Plain HTTP / WS non-SSL)
const char* ws_host  = "ws.poradaan.com"; // HANYA IP / HOSTNAME SAJA, JANGAN pakai "http://"
const int   ws_port  = 80;
const char* static_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyX2lkIjoxLCJ1c2VyX25hbWUiOiJFU1A4MjY2X1RpbWJhbmdhbiIsImxva2FzaV9pZCI6MSwicGVydXNhaGFhbl9pZCI6IjEiLCJuYW1hX3BlcnVzYWhhYW4iOiJOYXlsYVRvb2xzIiwiZW1haWwiOiJoZXJ1LnByYXNldGlAZ21haWwuY29tIiwibmFtYV9kYXRhYmFzZSI6ImVuY19MNXp0eTZ4ZXJHSW5wQzJfcS1HS1YzSFZjaVBXZXpNYnlkY0RGWHdUcy1RRlR3b3EteFdCTTBwVCIsImlhdCI6MTc5NDA4Njg4MH0.eKFxhThlvxkXClczMLzgLTvVJnDAAYEZpEJwYtPIynQ";

// const char* ws_host  = "192.168.1.13"; // HANYA IP / HOSTNAME SAJA, JANGAN pakai "http://"
// const int   ws_port  = 3003;

// Static Token yang digenerate dari Menu Tools (ESP8266 / IoT Token)
// const char* static_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyX2lkIjoxLCJ1c2VyX25hbWUiOiJFU1A4MjY2X1RpbWJhbmdhbiIsImxva2FzaV9pZCI6MSwicGVydXNhaGFhbl9pZCI6IjEiLCJuYW1hX3BlcnVzYWhhYW4iOiJOYXlsYVRvb2xzIiwiZW1haWwiOiJoZXJ1LnByYXNldGlAZ21haWwuY29tIiwibmFtYV9kYXRhYmFzZSI6ImVuY19vVXFEcG1iWjl2NnZqUG05VjhWVVRrTU9Qc0F5TkNxTFgzSWtENUtydE5ud2xUckh6R0kzYUNQeCIsImlhdCI6MTc5NDAxMzI3Mn0.P900IPGy8l-uATHYa3qBMxujgNKn8U03bM3jl4TzNrw";

WebSocketsClient webSocket;

// ==========================================
// SETUP
// ==========================================
void setup() {
  pinMode(KELEMBAPAN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n--- Memulai Inisialisasi ESP8266 Conveyor ---");

  // 1. Hubungkan ke WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung! IP ESP: " + WiFi.localIP().toString());

  // 2. Konfigurasi WebSocket Client dengan Static Token
  String url = "/ws?token=" + String(static_token);
  webSocket.begin(ws_host, ws_port, url);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // Reconnect otomatis setiap 5 detik jika terputus
}

// ==========================================
// CALLBACK EVENT WEBSOCKET
// ==========================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Terputus dari server!");
      digitalWrite(LED_BUILTIN, LOW);
      break;

    case WStype_CONNECTED:
      Serial.printf("[WS] Terhubung ke server: %s\n", payload);
      digitalWrite(LED_BUILTIN, HIGH);
      // Kirim event awal saat terkoneksi
      webSocket.sendTXT("{\"type\":\"device_ready\",\"device\":\"Conveyor_ESP8266\",\"status\":\"online\"}");
      break;

    case WStype_TEXT:
      Serial.printf("[WS] Pesan diterima: %s\n", payload);
      // Contoh tangkap perintah dari frontend atau server:
      // if (strstr((char*)payload, "\"cmd\":\"START\"")) {
      //   // Nyalakan relay / motor conveyor
      // } else if (strstr((char*)payload, "\"cmd\":\"STOP\"")) {
      //   // Matikan relay / motor conveyor
      // }
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

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // Wajib dipanggil terus menerus untuk memproses frame WS
  webSocket.loop();

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
    int suhu = dht11.readTemperature();

    static int moistureSamples[5];
    static int moistureIndex = 0;
    static int moistureSum = 0;

    int rawKelembapan = analogRead(KELEMBAPAN);
    moistureSum -= moistureSamples[moistureIndex];
    moistureSamples[moistureIndex] = rawKelembapan;
    moistureSum += rawKelembapan;
    moistureIndex = (moistureIndex + 1) % 5;

    int filteredRawKelembapan = moistureSum / 5;
    int kelembapan = constrain(map(filteredRawKelembapan, MOISTURE_RAW_MIN, MOISTURE_RAW_MAX, 100, 0), 0, 100);

    // Cetak ke Serial Monitor
    Serial.printf("[TELEMETRY] ChipID: %08X | Free RAM: %.2f KB (%u%% frag) | Flash: %.2f KB | Free Sketch: %.2f KB | RSSI: %d dBm | IP: %s | MAC: %s | SSID: %s | BSSID: %s | Reset: %s | RH: %d%% (raw: %d)\n",
                  chipId, freeHeapKb, heapFrag, flashSizeKb, freeSketchKb, rssi, localIp.c_str(), macAddr.c_str(), ssid.c_str(), bssid.c_str(), resetRsn.c_str(), kelembapan, filteredRawKelembapan);

    // Kirim data ke WebSocket server dalam format JSON
    if (webSocket.isConnected()) {
      StaticJsonDocument<512> telemetry;
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
      telemetry["suhu"] = suhu;
      telemetry["dht11"] = suhu;
      telemetry["kelembapan_raw"] = filteredRawKelembapan;
      telemetry["kelembapan"] = kelembapan;
      telemetry["uptime"] = millis() / 1000;

      char jsonBuffer[512];
      serializeJson(telemetry, jsonBuffer, sizeof(jsonBuffer));
      webSocket.sendTXT(jsonBuffer);
    }
  }
}
