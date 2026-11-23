#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

// ---------------------------------------------------------
// ARDUINO UNO (MEMORY OPTIMIZED)
// ---------------------------------------------------------
#define R558_RX_PIN 2  // Dihubungkan ke TX Sensor
#define R558_TX_PIN 3  // Dihubungkan ke RX Sensor

SoftwareSerial mySerial(R558_RX_PIN, R558_TX_PIN); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  delay(100);
  
  Serial.println(F("\n\n====================================="));
  Serial.println(F("  SISTEM SIDIK JARI GROW R558        "));
  Serial.println(F("====================================="));

  // Inisialisasi SoftwareSerial (Baud rate default sensor 57600)
  mySerial.begin(57600);
  delay(50);

  if (finger.verifyPassword()) {
    Serial.println(F("[V] Sensor R558 Terhubung!"));
  } else {
    Serial.println(F("[X] Sensor TIDAK DITEMUKAN. Cek kabel TX/RX."));
    while (1) { delay(1); }
  }

  finger.getTemplateCount();
  Serial.print(F("Jumlah sidik jari tersimpan: "));
  Serial.println(finger.templateCount);
  
  tampilkanMenu();
}

void loop() {
  if (Serial.available() > 0) {
    char pilihan = Serial.read();
    while(Serial.available() > 0) { Serial.read(); }

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
  Serial.println(F("\n====================================="));
  Serial.println(F("Ketik angka di Serial Monitor & Enter:"));
  Serial.println(F("[1] DAFTAR (Menggunakan Mode Auto-Enroll Pabrik)"));
  Serial.println(F("[2] BACA / Identifikasi Sidik Jari"));
  Serial.println(F("[3] HAPUS Sidik Jari Spesifik"));
  Serial.println(F("[4] FORMAT / Kosongkan Seluruh Memori"));
  Serial.println(F("=====================================\n"));
}

int bacaNomorID() {
  int id = 0;
  while (true) {
    while (!Serial.available()) { delay(10); }
    id = Serial.parseInt();
    while(Serial.available() > 0) { Serial.read(); } 
    
    // Validasi ketat maksimal ID adalah 63
    if (id > 0 && id <= 63) { 
      return id;
    } else {
      Serial.println(F("-> ID tidak valid! Maksimal nomor ID adalah 63. Masukkan angka 1 sampai 63:"));
    }
  }
}

// ---------------------------------------------------------
// 1. FUNGSI DAFTAR (AUTO-ENROLL)
// ---------------------------------------------------------
void menuDaftarJari() {
  Serial.println(F("\n--- MODE PENDAFTARAN OTOMATIS (AUTO-ENROLL) ---"));
  Serial.println(F("Masukkan nomor ID (1-63):"));
  int id = bacaNomorID();

  Serial.print(F("\nMendaftarkan ID #")); Serial.println(id);
  Serial.println(F("=> Beralih ke protokol komando mentah Pabrik."));

  // Bersihkan buffer RX
  while(mySerial.available()) mySerial.read();

  // Membangun paket data Auto-Enroll (31H) sesuai spesifikasi GROW R558
  uint8_t id_h = (id >> 8) & 0xFF;
  uint8_t id_l = id & 0xFF;
  uint8_t times = 0x04; // Set 4 kali penempelan untuk kualitas maksimal

  uint16_t sum = 0x01 + 0x00 + 0x08 + 0x31 + id_h + id_l + times + 0x00 + 0x00;
  uint8_t packet[] = {
    0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x08,
    0x31, id_h, id_l, times, 0x00, 0x00,
    (uint8_t)(sum >> 8), (uint8_t)(sum & 0xFF)
  };

  // Tembakkan perintah langsung ke hardware
  mySerial.write(packet, sizeof(packet));

  unsigned long startTime = millis();
  bool isFinished = false;

  // Membaca respon beruntun dari sensor (Timeout 40 detik)
  while(millis() - startTime < 40000 && !isFinished) {
    if (mySerial.available()) {
      if (mySerial.read() == 0xEF) {
        delay(5);
        if (mySerial.read() == 0x01) {
          uint8_t buf[12];
          int bytesRead = mySerial.readBytes(buf, 12);
          if (bytesRead == 12) {
            uint8_t confCode = buf[7];
            uint8_t p1 = buf[8];

            if (p1 == 0x00) { // Validasi instruksi
               if (confCode == 0x00) Serial.println(F("-> Sensor siap. Mulai TEMPELKAN jari Anda (Total butuh 4x Tap)..."));
               else { Serial.println(F("-> Gagal memulai prosedur.")); isFinished = true; }
            }
            else if (p1 == 0x01 && confCode == 0x00) { // Berhasil ambil gambar
               Serial.println(F("-> Gambar berhasil direkam! SEKARANG ANGKAT JARI ANDA."));
            }
            else if (p1 == 0x03 && confCode == 0x00) { // Pemindaian ulang setelah jari terangkat
               Serial.println(F("-> TEMPELKAN KEMBALI jari Anda untuk tahap selanjutnya..."));
            }
            else if (p1 == 0x06) { // Final Storage ke Flash
               if (confCode == 0x00) {
                 Serial.println(F("\n SUKSES! Sidik jari telah disimpan permanen!"));
               } else {
                 Serial.print(F("\n GAGAL MENYIMPAN. Kode: 0x"));
                 Serial.println(confCode, HEX);
               }
               isFinished = true;
            }
            else if (confCode != 0x00 && p1 != 0x06) {
               // Intersepsi khusus untuk kode error 0x26
               if (confCode == 0x26) {
                 Serial.println(F("\n GAGAL: Sensor meminta jari ditekan lebih jelas (Kode 0x26)."));
                 Serial.println(F("=> Proses pendaftaran dibatalkan otomatis. Kembali ke menu."));
                 isFinished = true; // Langsung patahkan loop dan keluar
               } else {
                 Serial.print(F("   (Info: sensor mengebalikan respons. Kode 0x"));
                 Serial.print(confCode, HEX); Serial.println(F(")"));
               }
            }
            startTime = millis(); // Reset timer setiap ada progress agar tidak timeout
          }
        }
      }
    }
  }
  if (!isFinished) {
    Serial.println(F("\n Waktu habis. Pendaftaran dibatalkan."));
  }
}

// ---------------------------------------------------------
// 2. FUNGSI BACA
// ---------------------------------------------------------
void menuBacaJari() {
  Serial.println(F("\n--- MODE MEMBACA ---"));
  Serial.println(F("Sistem siaga. Tempelkan jari kapan saja."));
  Serial.println(F("Ketik '0' dan Enter untuk KEMBALI."));

  while (true) {
    if (Serial.available() > 0) {
      if (Serial.read() == '0') {
        while(Serial.available() > 0) { Serial.read(); } 
        Serial.println(F("Keluar dari Mode Membaca...")); 
        break;
      }
    }

    uint8_t status = finger.getImage();
    if (status == FINGERPRINT_OK) {
      Serial.println(F("\n[!] Jari terdeteksi. Memproses..."));
      status = finger.image2Tz();
      if (status != FINGERPRINT_OK) {
        Serial.println(F(" -> Gambar kurang jelas. Coba lagi.")); 
        delay(1500);
        continue;
      }
      status = finger.fingerSearch();
      if (status == FINGERPRINT_OK) {
        Serial.println(F(" ->  AKSES DITERIMA! Sidik jari COCOK."));
        Serial.print(F("    - ID Pengguna : ")); Serial.println(finger.fingerID);
      } else {
        Serial.println(F(" ->  AKSES DITOLAK! Sidik jari tidak terdaftar."));
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
  Serial.println(F("\n--- MODE HAPUS ---"));
  Serial.println(F("Masukkan nomor ID (1-63) yang ingin DIHAPUS:"));
  int id = bacaNomorID();
  if (finger.deleteModel(id) == FINGERPRINT_OK) {
    Serial.print(F(" SUKSES! Sidik jari ID #")); Serial.print(id); Serial.println(F(" telah dihapus."));
  } else {
    Serial.println(F(" GAGAL menghapus."));
  }
}

// ---------------------------------------------------------
// 4. FUNGSI FORMAT DATABASE
// ---------------------------------------------------------
void menuFormatDatabase() {
  Serial.println(F("\n--- MODE FORMAT DATABASE ---"));
  Serial.println(F("PERINGATAN: Ini akan mereset dan menghapus SELURUH sidik jari!"));
  Serial.println(F("Ketik 'Y' lalu Enter untuk melanjutkan, atau 'N' untuk batal."));
  while (true) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      while(Serial.available() > 0) { Serial.read(); }

      if (c == 'Y' || c == 'y') {
        Serial.println(F("Memformat ulang memori Flash sensor..."));
        uint8_t p = finger.emptyDatabase();
        if (p == FINGERPRINT_OK) {
          Serial.println(F(" SUKSES! Tabel indeks memori sensor telah direset total."));
        } else {
          Serial.println(F(" GAGAL memformat memori."));
        }
        break;
      } else if (c == 'N' || c == 'n') {
        Serial.println(F("Format dibatalkan."));
        break;
      }
    }
  }
}
