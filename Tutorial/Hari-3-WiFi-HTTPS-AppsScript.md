# Hari 3 — ESP32-C3 connect WiFi + HTTPS ke Apps Script

Langkah kerja Hari 3 dari rencana kerja di [Sistem-Logbook-Pengunjung-RFID](../Dokumen/Sistem-Logbook-Pengunjung-RFID.md#status-dan-rencana-kerja). Board: ESP32-C3 SuperMini.

## Goals hari ini

- [x] `main.cpp` bersih dari kode UDP discovery & eduroam, pakai WiFi WPA2-PSK biasa.
- [x] Kirim HTTPS POST ke Apps Script, dapat balasan JSON yang benar.
- [x] Tes UID dummy tercatat di Sheet, tanpa duplikat.

## Ringkasan hasil

Berhasil end-to-end, tapi butuh memecahkan **3 kendala teknis berbeda**, semuanya spesifik ke kombinasi ESP32-C3 SuperMini + Google Apps Script. Dicatat detail di bawah supaya tidak perlu didebug ulang dari nol kalau nanti ganti board atau ada yang melanjutkan project ini.

## Kendala 1 — Serial Monitor kosong total

**Gejala:** setelah upload, Serial Monitor benar-benar tidak menampilkan apa pun, bahkan pesan boot ROM chip (`ets Jul 29 2019...`) yang biasanya selalu muncul di board manapun.

**Penyebab:** ESP32-C3 SuperMini pakai **USB native bawaan chip**, bukan chip USB-to-UART terpisah seperti board resmi DevKitM-1. Tanpa konfigurasi khusus, output `Serial` tidak ter-routing ke port USB itu sama sekali.

**Fix** — tambahkan di `platformio.ini`:
```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
```

Sources: [PlatformIO Community — ESP32-C3 Super mini serial problem](https://community.platformio.org/t/esp32-c3-super-mini-serial-problem/53165), [PlatformIO Community — Enabling USB CDC on Boot on ESP32-C3 Devkit](https://community.platformio.org/t/enabling-usb-cdc-on-boot-on-esp32-c3-devkit/33346)

## Kendala 2 — WiFi tidak pernah connect ke hotspot

**Gejala:** `WiFi.begin()` tidak pernah berhasil, status tetap `WL_DISCONNECTED`, walau SSID/password sudah benar.

**Penyebab:** hotspot HP yang dipakai untuk tes defaultnya di band **5GHz**. ESP32 (semua varian, termasuk C3) **cuma support 2.4GHz**, tidak punya radio buat 5GHz sama sekali — bukan soal auth, chip-nya tidak bisa "melihat" SSID itu dari awal.

**Fix:** paksa hotspot HP ke band 2.4GHz (cari opsi "AP Band"/"Pilihan Frekuensi" di setting hotspot, biasanya di menu Advanced).

## Kendala 3 — HTTP 400/405 dari Google, padahal WiFi sudah connect

**Gejala bertahap:**
1. Awalnya `POST` ke Apps Script balas kode **302**, dan balasan JSON asli tidak pernah kelihatan (walau data ternyata tetap masuk ke Sheet).
2. Setelah coba `http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)`, malah dapat **400 Bad Request** dari `google.com` (bukan dari script kita).
3. Setelah redirect ditangani manual dengan `POST` lagi ke URL hasil redirect, dapat **405 Method Not Allowed**.

**Penyebab:**
- Google Apps Script Web App **selalu balas 302 dulu** — request `POST` yang pertama itu **sudah benar-benar menjalankan** `doPost()` di sisi Google (makanya data sempat masuk ke Sheet duluan sebelum redirect-nya beres ditangani). Redirect-nya cuma buat **mengambil hasil/balasan** dari eksekusi itu.
- `HTTPClient` bawaan ESP32 **punya bug** kalau redirect di-follow otomatis untuk request `POST`: dia salah kirim header `Content-Length` yang tidak sesuai (masih bawa nilai lama tanpa body yang cocok), bikin Google balas 400. Bug ini tercatat di [espressif/arduino-esp32#8300](https://github.com/espressif/arduino-esp32/issues/8300), belum ada fix resmi dari maintainer.
- URL hasil redirect (`script.googleusercontent.com/macros/echo?...`) itu cuma nyimpen **hasil yang sudah dieksekusi**, jadi cuma nerima **GET**, bukan `POST` lagi — makanya `POST` ke situ balas 405.

**Fix:** tangani redirect manual, dua request terpisah:
1. `POST` pertama ke `APPS_SCRIPT_URL` (ini yang benar-benar menjalankan script), `follow redirects` **dimatikan** (default).
2. Kalau kode HTTP-nya `302`, ambil URL dari `http.getLocation()`, lalu **`GET`** (bukan `POST` lagi) ke URL itu buat ambil balasan JSON yang sebenarnya.

Lihat implementasinya di [logbook-firmware/src/main.cpp](../logbook-firmware/src/main.cpp) (fungsi `postJson` dan `getContent`).

Sources: [GitHub espressif/arduino-esp32#8300](https://github.com/espressif/arduino-esp32/issues/8300), [Arduino Forum — Post Request on ESP32 to Google Apps Script](https://forum.arduino.cc/t/post-request-on-esp32-to-google-apps-script/1435194)

## Kendala 4 — Data tercatat dobel di Sheet

**Gejala:** satu kali tes tap, muncul **dua baris** di `Log_Kunjungan` dengan UID dan waktu yang beda tipis (selisih ~10 detik).

**Penyebab:** dikonfirmasi lewat Serial Monitor — `main.cpp` cuma kirim **satu** request `POST` (bukan bug di device). Ini murni **Google yang mengeksekusi ulang request yang sama di level infrastrukturnya sendiri** (retry internal), di luar kendali kode ESP32.

**Fix** — tambahkan proteksi duplikat di Apps Script (`doPost`): sebelum `appendRow`, cek baris terakhir di `Log_Kunjungan` yang UID-nya sama — kalau selisih waktunya kurang dari 10 detik, anggap duplikat, balas `OK` tapi tidak dicatat ulang. Lihat implementasi di [logbook-appsscript/Code.gs](../logbook-appsscript/Code.gs).

**Catatan penting:** dedup ini cuma buat jendela 10 detik (nangkep retry infrastruktur), **bukan** buat mencegah tap yang sama di jam berbeda hari yang sama — itu memang harus tetap tercatat sebagai baris terpisah, karena itu kunjungan yang benar-benar berbeda, bukan duplikat.

## Hasil akhir yang diharapkan

Serial Monitor:
```
Menyambungkan ke WiFi...
Menyambungkan.
Terhubung! IP device: 192.168.x.x
Tes kirim UID dummy...
POST ke Apps Script...
Kode HTTP pertama: 302
Redirect ke: https://script.googleusercontent.com/macros/echo?...
Kode HTTP setelah redirect (GET): 200
{"status":"OK","nama":"...","waktu":"..."}
```

Satu baris baru (bukan dua) muncul di tab `Log_Kunjungan` di Sheet.
