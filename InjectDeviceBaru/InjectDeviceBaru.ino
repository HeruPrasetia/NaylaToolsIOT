#include <ArduinoJson.h>
#include <LittleFS.h>

const char* CONFIG_FILE = "/database.json";

String readLine(const char* prompt) {
  Serial.println(prompt);

  String value;
  unsigned long lastByteAt = 0;
  while (true) {
    while (Serial.available() > 0) {
      char character = static_cast<char>(Serial.read());
      if (character == '\n') {
        if (value.endsWith("\r")) {
          value.remove(value.length() - 1);
        }
        return value;
      }
      if (character != '\r') {
        value += character;
      }
      lastByteAt = millis();
    }

    if (value.length() > 0 && millis() - lastByteAt >= 1500) {
      return value;
    }
    delay(1);
  }
}

bool mountLittleFS() {
  if (LittleFS.begin()) {
    return true;
  }

  Serial.println("LittleFS gagal di-mount. Ketik FORMAT untuk format filesystem baru.");
  Serial.println("PERINGATAN: format akan menghapus seluruh data LittleFS.");
  String confirmation = readLine("Ketik FORMAT untuk lanjut:");
  confirmation.trim();
  if (confirmation != "FORMAT") {
    Serial.println("Format dibatalkan.");
    return false;
  }

  if (!LittleFS.format() || !LittleFS.begin()) {
    Serial.println("Gagal format atau mount LittleFS.");
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Inject Konfigurasi Device ke LittleFS ===");
  Serial.println("Set Serial Monitor ke 115200 baud dan line ending Newline.");

  if (!mountLittleFS()) {
    return;
  }

  if (LittleFS.exists(CONFIG_FILE)) {
    String confirmation = readLine("/database.json sudah ada. Ketik OVERWRITE untuk menggantinya:");
    confirmation.trim();
    if (confirmation != "OVERWRITE") {
      Serial.println("Penulisan konfigurasi dibatalkan.");
      return;
    }
  }

  String wifiSSID = readLine("Masukkan SSID WiFi:");
  String wifiPassword = readLine("Masukkan password WiFi (kosongkan untuk WiFi tanpa password):");
  String token = readLine("Masukkan token terbaru yang terdaftar untuk device ini:");
  token.trim();
  Serial.printf("Token diterima (%u karakter).\n", static_cast<unsigned int>(token.length()));

  if (wifiSSID.length() == 0 || wifiSSID.length() > 32) {
    Serial.println("SSID harus berisi 1-32 byte. Konfigurasi tidak disimpan.");
    return;
  }
  if (wifiPassword.length() != 0 && (wifiPassword.length() < 8 || wifiPassword.length() > 63)) {
    Serial.println("Password WiFi harus berisi 8-63 byte, atau kosong untuk jaringan terbuka.");
    return;
  }
  if (token.length() == 0 || token.length() > 768) {
    Serial.println("Token wajib diisi dan maksimal 768 byte. Konfigurasi tidak disimpan.");
    return;
  }

  StaticJsonDocument<1024> config;
  config["token"] = token;
  config["wifissid"] = wifiSSID;
  config["wifipwd"] = wifiPassword;
  config["conter"] = 0;
  if (config.overflowed()) {
    Serial.println("Konfigurasi terlalu besar untuk disimpan.");
    return;
  }

  Serial.println("Menyimpan konfigurasi ke LittleFS...");
  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Gagal membuka /database.json untuk ditulis.");
    return;
  }

  size_t expectedBytes = measureJson(config);
  size_t bytesWritten = serializeJson(config, configFile);
  size_t newlineBytes = configFile.println();
  configFile.close();
  if (bytesWritten != expectedBytes || newlineBytes == 0) {
    Serial.println("Gagal menyimpan konfigurasi secara lengkap.");
    return;
  }

  Serial.println("Konfigurasi berhasil disimpan ke /database.json.");
  Serial.println("Sekarang upload Conveyor.ino atau Finger.ino tanpa menghapus atau memformat LittleFS.");
}

void loop() {
  delay(1000);
}
