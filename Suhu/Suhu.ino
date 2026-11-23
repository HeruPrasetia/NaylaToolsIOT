#include <Arduino.h>
#include "max6675.h"

int thermoDO  = D7; // SO
int thermoCS  = D6; // CS
int thermoCLK = D5; // SCK

MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

void setup() {
  Serial.begin(115200);
  delay(500); // Jeda stabilisasi chip MAX6675
}

void loop() {
  float suhuC = thermocouple.readCelsius();

  if (isnan(suhuC)) {
    Serial.println("Sensor thermocouple terputus / tidak terbaca!");
  } else {
    Serial.print("Suhu: ");
    Serial.print(suhuC);
    Serial.println(" °C");
  }

  // MAX6675 butuh jeda minimal ~250ms antar pembacaan agar konversi ADC selesai
  delay(1000);
}