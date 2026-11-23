#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

// ---------------------------------------------------------
// ESP32-C3 Super Mini (Kabel Kuning -> 20, Kabel Hitam -> 21)
// ---------------------------------------------------------
#define R558_RX_PIN 20
#define R558_TX_PIN 21

HardwareSerial mySerial(1); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void tampilkanMenu();
int bacaNomorID();
void menuDaftarJari();
void menuBacaJari();
void menuHapusJari();
void menuFormatDatabase();

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
  
  tampilkanMenu();
}

void loop() {
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
    while (!Serial.available()) { delay(10); }
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