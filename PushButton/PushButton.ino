#include <Arduino.h>

const int BUTTON_PIN = D5; // GPIO14 (Limit Switch)
int Conter = 0;

int buttonState = HIGH;             // Status stabil saat ini
int lastReading = HIGH;             // Pembacaan mentah loop sebelumnya
unsigned long lastDebounceTime = 0; // Waktu terakhir sinyal berubah
const unsigned long debounceDelay = 50; // Jeda stabilitas 50ms

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);

  // Jika sinyal mentah berubah (baik karena ditekan, dilepas, atau getaran/bouncing)
  // Reset terus timer-nya sampai sinyal benar-benar anteng!
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  // Jika sinyal sudah anteng lebih lama dari debounceDelay (50ms)
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Cek apakah status stabilnya berbeda dari status resmi sebelumnya
    if (reading != buttonState) {
      buttonState = reading;

      // Hitung counter HANYA saat status resmi berubah menjadi LOW (Ditekan)
      if (buttonState == LOW) {
        Conter++;
        Serial.println("Beras Masuk! Total Counter: " + String(Conter));
      }
    }
  }

  lastReading = reading;
}