// ---------------------------------------------------------
// ESP32
// ---------------------------------------------------------
#define R558_RX_PIN 5 
#define R558_TX_PIN 4

HardwareSerial mySerial(1); 

void setup() {
  Serial.begin(115200);
  while (!Serial);
  delay(100);

  // Baud rate default R558 adalah 57600
  mySerial.begin(57600, SERIAL_8N1, R558_RX_PIN, R558_TX_PIN);
  delay(100);

  tampilkanMenu();
}

void loop() {
  // Mengecek apakah ada input dari Serial Monitor
  if (Serial.available() > 0) {
    // Membaca seluruh teks yang diketik sampai tombol Enter ditekan
    String input = Serial.readStringUntil('\n');
    input.trim(); // Membersihkan spasi kosong atau karakter /r tersembunyi
    
    if (input.length() == 0) return; // Abaikan jika kosong

    // KONDISI SPESIAL: LAMPU MATI TOTAL
    if (input == "0-0") {
      kontrolLED(4, 0); // Mode 4 adalah OFF
      Serial.println("\n[0-0] Mematikan semua lampu...");
      return;
    }

    // MENCARI TANDA STRIP (-) SEBAGAI PEMISAH
    int dashIndex = input.indexOf('-');
    
    if (dashIndex > 0) {
      // Memecah teks menjadi angka Warna dan Mode
      int warna = input.substring(0, dashIndex).toInt();
      int mode = input.substring(dashIndex + 1).toInt();

      // Validasi apakah angkanya masuk akal (1 sampai 7)
      if (warna >= 1 && warna <= 7 && mode >= 1 && mode <= 7) {
        kontrolLED(mode, warna);
      } else {
        Serial.println("\n-> ERROR: Angka tidak valid! Pastikan Warna (1-7) dan Mode (1-7).");
      }
    } else {
      Serial.println("\n-> ERROR: Format salah! Gunakan tanda strip, contoh: 1-1");
    }
  }
}

// ---------------------------------------------------------
// FUNGSI TAMPILAN MENU
// ---------------------------------------------------------
void tampilkanMenu() {
  Serial.println("\n=====================================");
  Serial.println("     PLAYGROUND LED GROW R558        ");
  Serial.println("=====================================\n");
  
  Serial.println("--- DAFTAR WARNA ---");
  Serial.println("[1] Biru       [5] Ungu");
  Serial.println("[2] Hijau      [6] Kuning");
  Serial.println("[3] Sian       [7] Putih");
  Serial.println("[4] Merah");

  Serial.println("\n--- DAFTAR MODE ---");
  Serial.println("[1] Breathing    (Meredup-Terang Perlahan)");
  Serial.println("[2] Flashing     (Berkedip)");
  Serial.println("[3] Selalu ON    (Menyala Statis)");
  Serial.println("[4] Selalu OFF   (Dimatikan Paksa)");
  Serial.println("[5] Gradually ON (Menyala Bertahap)");
  Serial.println("[6] Gradually OFF(Mati Bertahap)");
  Serial.println("[7] Colorful     (Mode Disko Warna-warni!)");

  Serial.println("\n=====================================");
  Serial.println("CARA PENGGUNAAN:");
  Serial.println("Ketik: [WARNA]-[MODE] lalu Enter.");
  Serial.println("Contoh: 1-1 (Untuk Biru Breathing)");
  Serial.println("Ketik:  0-0 (Untuk Mematikan Lampu)");
  Serial.println("=====================================\n");
}

// ---------------------------------------------------------
// FUNGSI INTI PENGIRIMAN PAKET DATA LAMPU
// ---------------------------------------------------------
void kontrolLED(uint8_t mode, uint8_t color) {
  // ARRAY UNTUK TAMPILAN TEKS SAJA
  String namaWarna[] = {"", "Biru", "Hijau", "Sian", "Merah", "Ungu", "Kuning", "Putih"};
  String namaMode[] = {"", "Breathing", "Flashing", "Selalu ON", "OFF", "Gradually ON", "Gradually OFF", "Colorful"};

  Serial.print("\nMenjalankan -> Warna: ["); 
  Serial.print(namaWarna[color]); 
  Serial.print("] | Mode: ["); 
  Serial.print(namaMode[mode]); 
  Serial.println("]");

  // JIKA MODE 1 SAMPAI 6 (Format Paket Standar)
  if (mode >= 1 && mode <= 6) {
    uint16_t sum = 0x01 + 0x00 + 0x07 + 0x3C + mode + color + color + 0x00;
    
    uint8_t packet[] = {
      0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x07,
      0x3C, mode, color, color, 0x00,
      (uint8_t)(sum >> 8), (uint8_t)(sum & 0xFF)
    };
    
    mySerial.write(packet, sizeof(packet));
  } 
  
  // JIKA MODE 7: COLORFUL PROGRAMMED (Format Paket Khusus)
  else if (mode == 7) {
    // Pada mode ini, input warna dasar akan diabaikan oleh hardware karena sudah diprogram kombinasi pelangi
    uint16_t sum = 0x01 + 0x00 + 0x0B + 0x3C + 0x07 + 0x24 + 0xCA + 0x9D + 0xB0 + 0x00 + 0x00 + 0x00;
    
    uint8_t packet[] = {
      0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x0B,
      0x3C, 0x07, 0x24, 0xCA, 0x9D, 0xB0, 0x00, 0x00, 0x00,
      (uint8_t)(sum >> 8), (uint8_t)(sum & 0xFF)
    };
    
    mySerial.write(packet, sizeof(packet));
  }
}
