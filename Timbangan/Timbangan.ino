#include <Arduino.h>
#include "HX711.h"

// Pin sesuai susunan ESP8266 kamu
const int LOADCELL_DOUT_PIN = D7; // GPIO13 (DT)
const int LOADCELL_SCK_PIN  = D6; // GPIO12 (SCK)

HX711 scale;

// Nilai awal kalibrasi untuk load cell 20kg (wajib disesuaikan nanti)
float calibration_factor = 105.0; 

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nInisialisasi Timbangan HX711...");

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  
  Serial.println("Jangan taruh beban apapun! Melakukan Tare (Nol-kan timbangan)...");
  delay(1000);
  scale.tare(); // Reset angka ke 0
  Serial.println("Timbangan Siap!");
}

void loop() {
  if (scale.is_ready()) {
    // Ambil rata-rata dari 10 kali pembacaan agar angka stabil tidak lompat-lompat
    float beratGram = scale.get_units(10);

    // Hilangkan noise minus kecil saat kosong (misal -0.2g dianggap 0)
    if (beratGram < 1.0 && beratGram > -1.0) {
      beratGram = 0.0;
    }

    float beratKg = beratGram / 1000.0;

    Serial.print("Berat: ");
    Serial.print(beratGram, 1);
    Serial.print(" g  |  ");
    Serial.print(beratKg, 3);
    Serial.println(" kg");
  } else {
    Serial.println("HX711 tidak terdeteksi, cek kabel DT/SCK!");
  }

  delay(300);
}