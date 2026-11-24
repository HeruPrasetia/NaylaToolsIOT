#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Memulai scan Wi-Fi...");
}

void loop() {
  int n = WiFi.scanNetworks();
  Serial.println("\n--- Hasil Scan Wi-Fi ---");

  if (n == 0) {
    Serial.println("Tidak ada Wi-Fi ditemukan.");
  } else {
    Serial.print("Ditemukan ");
    Serial.print(n);
    Serial.println(" jaringan:");

    for (int i = 0; i < n; ++i) {
      Serial.print(i + 1);
      Serial.print(". ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm) ");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "[Open]" : "[Terkunci]");
      delay(10);
    }
  }

  WiFi.scanDelete();
  delay(5000); // Scan ulang setiap 5 detik
}