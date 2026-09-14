# Hari 4 — Integrasi RFID

Langkah kerja Hari 4 dari milestone di [Sistem-Logbook-Pengunjung-RFID](../Dokumen/Sistem-Logbook-Pengunjung-RFID.md#milestone-5-hari). Board: ESP32-C3 SuperMini + MFRC522.

## Goals hari ini

- [x] Port kode `MFRC522_Request`/`MFRC522_Anticoll` dari STM32 HAL ke Arduino SPI.
- [x] Gabungkan dengan kode WiFi+HTTPS dari Hari 3.
- [x] Tes tap kartu asli — UID tidak terdaftar dibalas `REJECTED` tanpa tercatat, UID
      terdaftar dibalas `OK` dan tercatat di `Log_Kunjungan`.

## Wiring

| MFRC522 | ESP32-C3 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCK | GPIO4 |
| MOSI | GPIO6 |
| MISO | GPIO5 |
| SDA (CS) | GPIO7 |
| RST | GPIO10 |
| IRQ | tidak disambung |

## Kode

Driver MFRC522 (`ReadRegister`, `WriteRegister`, `Init`, `Request`, `Anticoll`) di-porting
dari versi STM32 HAL yang sudah diuji sebelumnya — logika protokolnya identik, cuma API SPI
diganti dari `HAL_SPI_Transmit/Receive` ke `SPI.transfer()` bawaan Arduino. Lihat implementasi
lengkap di [logbook-firmware/src/main.cpp](../logbook-firmware/src/main.cpp).

`loop()` sekarang polling `MFRC522_Request()` tiap 100ms — begitu ada kartu, UID
dikonversi ke string hex dan langsung dikirim lewat `sendTap()` (menggantikan tes UID dummy
`DEADBEEF` di Hari 3).

## Kendala — Serial Monitor kosong lagi setelah re-upload

**Gejala:** setelah upload ulang firmware (menambahkan kode retry di `sendTap`), Serial
Monitor kembali kosong total — mirip kendala 1 di Hari 3, padahal flag
`ARDUINO_USB_CDC_ON_BOOT` sudah benar ada di `platformio.ini`. Soft-reset (tombol RST/EN)
sambil Monitor terbuka **tidak membantu** kali ini.

**Penyebab:** ESP32-C3 yang pakai USB native untuk upload sekaligus Serial kadang tidak
clean re-enumerate ke OS setelah proses flashing — soft-reset via tombol tidak cukup
memaksa Windows mengenali ulang device USB-nya.

**Fix:** **cabut kabel USB sepenuhnya** (bukan cuma tombol reset), tunggu beberapa detik,
colok lagi, baru buka ulang Monitor. Ini beda dari kendala 1 di Hari 3 (yang soal flag
build), murni soal re-enumerasi USB setelah upload.

## Hasil akhir

Serial Monitor, kartu belum terdaftar:
```
Kartu terdeteksi, UID: 22DBC84C
POST ke Apps Script...
Kode HTTP pertama: 302
Redirect ke: https://script.googleusercontent.com/macros/echo?...
Kode HTTP setelah redirect (GET), percobaan 1: 200
{"status":"REJECTED","message":"UID tidak terdaftar"}
```

Setelah UID yang sama didaftarkan ke tab `Pengunjung`, tap ulang membalas `status: OK` dan
baris baru muncul di `Log_Kunjungan` — sistem tap-in lengkap dari kartu fisik sampai ke
Sheet sudah berfungsi end-to-end.
