#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4 // Sesuaikan jumlah kotak 8x8 kamu

#define DATA_PIN  D1
#define CLK_PIN   D2
#define CS_PIN    D4

MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Struktur untuk menyimpan teks beserta efek masuk & keluarnya
struct PesanAnimasi {
  const char* teks;
  textEffect_t efekMasuk;
  textEffect_t efekKeluar;
  uint16_t jeda; // Waktu diam di tengah (ms)
};

// Daftar semua tulisan yang mau ditampilkan bergantian
PesanAnimasi daftarPesan[] = {
  { "NAMA KU NAYLA...",  PA_SCROLL_LEFT,  PA_SCROLL_LEFT,  0 },
  { "NAMA KU NEIRA...",  PA_SCROLL_RIGHT, PA_SCROLL_RIGHT, 0 },
  { "KAMI HERU Familly", PA_SCROLL_UP,    PA_SCROLL_UP,    1500 } // Diam 1.5 detik di tengah sebelum naik keluar
};

const uint8_t JUMLAH_PESAN = sizeof(daftarPesan) / sizeof(daftarPesan[0]);
uint8_t indeksSekarang = 0;

void setup() {
  display.begin();
  display.setIntensity(3); // Kecerahan 0 - 15
  display.displayClear();

  // Mulai animasi pesan pertama (indeks 0)
  display.displayText(
    daftarPesan[0].teks,
    PA_CENTER,
    50,
    daftarPesan[0].jeda,
    daftarPesan[0].efekMasuk,
    daftarPesan[0].efekKeluar
  );
}

void loop() {
  // Jika animasi teks yang sekarang sudah selesai dijalankan:
  if (display.displayAnimate()) {
    // Pindah ke teks berikutnya, jika sudah mentok balik lagi ke 0
    indeksSekarang = (indeksSekarang + 1) % JUMLAH_PESAN;

    // Jalankan teks berikutnya
    display.displayText(
      daftarPesan[indeksSekarang].teks,
      PA_CENTER,
      50,
      daftarPesan[indeksSekarang].jeda,
      daftarPesan[indeksSekarang].efekMasuk,
      daftarPesan[indeksSekarang].efekKeluar
    );
  }
}