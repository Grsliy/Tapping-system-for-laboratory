# Sistem Logbook Pengunjung Lab berbasis RFID

Catatan teknis rancangan sistem pencatatan kunjungan lab, dari proses surat izin sampai
pengunjung tinggal tap kartu RFID di pintu masuk.

## Latar belakang

Saat ini proses pencatatan pengunjung lab (siapa saja yang masuk, kapan, dan untuk keperluan
apa) masih manual. Tujuannya membuat proses ini lebih cepat untuk pengunjung yang sudah
terdaftar — cukup tap kartu RFID, tanpa perlu isi buku tamu fisik tiap kali datang. Proses
administratif (surat izin) tetap manual seperti biasa, cuma bagian pencatatan kehadirannya
yang dibuat otomatis.

## Alur proses

```
1. Pengunjung mengajukan surat izin kunjungan (fisik, ke lab/kampus) — di luar sistem ini.
2. Admin lab menyetujui surat izin.
3. Admin (atau pengunjung, lihat "Hal yang belum diputuskan") mengisi form online:
   nama, institusi/asal, tujuan kunjungan.
4. Data dari form + UID kartu RFID pengunjung didaftarkan ke database oleh admin.
5. Pengunjung tap kartu RFID di reader saat masuk lab.
6. Reader mencatat waktu tap, dicocokkan ke UID yang sudah terdaftar, tersimpan sebagai
   log kunjungan di database.
```

Bagian 1–4 di atas adalah proses pendaftaran (sekali per pengunjung/per kunjungan berlaku),
bagian 5–6 adalah proses harian yang berulang tiap pengunjung datang.

## Arsitektur sistem

**Revisi (7 Sept 2026):** awalnya dirancang dua chip terpisah (STM32 + ESP-01 dikontrol AT
command), diganti jadi **satu chip Wemos D1 Mini (ESP8266)** yang urus SPI ke MFRC522
sekaligus WiFi+HTTP-nya sendiri. Alasan penggantian ada di bagian "Keputusan arsitektur"
di bawah.

```
[Kartu RFID pengunjung]
        |  (tap)
        v
[MFRC522] --SPI--> [Wemos D1 Mini (ESP8266)]
                            |  (WiFi, HTTP)
                            v
                    [Server + Database]
                            ^
                            |
                  [Form online pendaftaran]
```

- **MFRC522** — baca UID kartu, dihubungkan ke Wemos D1 lewat SPI. Logika baca register/UID
  (`Request`/`Anticoll`) sudah pernah ditulis & diuji versi STM32-nya, tinggal diporting ke
  Arduino (SPI API beda, logika protokolnya sama).
- **Wemos D1 Mini** — satu chip yang urus semuanya: baca UID dari MFRC522, connect WiFi
  (termasuk WPA2-Enterprise/eduroam kalau itu jaringan yang dipakai), kirim HTTP POST
  langsung ke server tiap ada tap. Tidak ada lagi perantara AT command/UART ke modul WiFi
  terpisah.
- **Server + Database** — terima data dari Wemos D1, simpan sebagai log kunjungan. Juga jadi
  tempat data hasil form online pendaftaran disimpan (untuk dicocokkan dengan UID). Bagian
  ini **tidak berubah sama sekali** dari rancangan awal — sepenuhnya platform-agnostic.
- **Form online pendaftaran** — tempat data pengunjung (nama, institusi, tujuan) diinput
  sebelum kartu bisa dipakai tap.

## Keputusan arsitektur yang sudah diambil

- **Mikon utama: Wemos D1 Mini (ESP8266)**, menggantikan rencana awal STM32F401 (Black Pill)
  + ESP-01 terpisah. Alasan penggantian: WiFi lab yang tersedia ada yang **eduroam
  (WPA2-Enterprise/802.1X)** dan ada yang **captive portal (login browser)** — dua-duanya
  tidak didukung ESP-01 dengan firmware AT bawaan (AT+CWJAP cuma support WPA2-PSK biasa).
  WPA2-Enterprise **bisa** ditangani lewat kode custom (library `ESP8266_WPA2_Enterprise`
  atau native di ESP32), tapi itu artinya modul WiFi-nya harus jalanin logika sendiri, bukan
  lagi cuma "modem AT" yang diperintah STM32 — jadi lebih masuk akal sekalian jadikan satu
  chip. Captive portal tetap tidak bisa diotomatisasi di mikon manapun (butuh MAC whitelist
  dari IT lab kalau itu yang dipakai).
  Wemos D1 dan STM32/ESP-01 tidak pernah tahu siapa yang terdaftar — mereka cuma kurir,
  seluruh logika pencocokan UID ada di server.
- **Server: self-hosted di komputer lab yang selalu nyala**, bukan Google Sheets/cloud.
  Alasannya: menghindari kebutuhan HTTPS/SSL yang berat buat RAM kecil ESP-01 (endpoint
  Google Apps Script wajib HTTPS, sedangkan server lokal bisa diakses HTTP biasa di jaringan
  lokal), dan tidak bergantung ke koneksi internet keluar sama sekali — kalau internet lab
  mati, sistem tap-in tetap jalan karena semua trafik cuma di LAN.
- **Stack server: Flask (Python) + SQLite** — ringan, tidak perlu install database server
  terpisah, dan Python sudah familiar dari pengalaman sebelumnya (Percobaan 4 modul
  praktikum IoT, pakai `paho-mqtt`).
- **Biaya: Rp0** untuk seluruh bagian software dan server (semua open source / pakai PC yang
  sudah ada). Satu-satunya biaya yang tersisa adalah modul MFRC522 pengganti yang memang
  sudah direncanakan dibeli sejak awal.

## Rancangan skema data

Perlu minimal dua tabel:

**Tabel `pengunjung`** — data hasil pendaftaran (satu baris per pengunjung/kunjungan yang disetujui)

| Kolom | Keterangan |
|---|---|
| `id` | Primary key |
| `uid_kartu` | UID RFID yang didaftarkan ke pengunjung ini |
| `nama` | Nama pengunjung |
| `institusi` | Asal institusi/kampus/perusahaan |
| `tujuan` | Keperluan kunjungan |
| `tanggal_daftar` | Kapan data ini dimasukkan ke sistem |
| `status` | Aktif / nonaktif (buat nonaktifkan kartu setelah kunjungan selesai, kalau perlu) |

**Tabel `log_kunjungan`** — catatan tiap kali tap kartu

| Kolom | Keterangan |
|---|---|
| `id` | Primary key |
| `uid_kartu` | Foreign key ke `pengunjung.uid_kartu` |
| `waktu_tap` | Timestamp saat tap terjadi |

## Status pengerjaan saat ini

- ✅ **Fase 1 (server Flask + SQLite) selesai dan teruji sepenuhnya** — endpoint `/tap` dan
  `/log` jalan, diverifikasi bisa diakses dari perangkat lain di jaringan lab (firewall bukan
  penghalang). Bagian ini tidak terpengaruh penggantian mikon di atas.
- ✅ Kode baca UID kartu (`MFRC522_Request`/`MFRC522_Anticoll`) sudah pernah ditulis & teruji
  strukturnya versi STM32 HAL — jadi referensi logika, perlu diporting ke Arduino/ESP8266.
- ⚠️ **Setup STM32F401CCU6 (Black Pill) + STM32CubeIDE** (clock 84 MHz, SPI1, UART debug lewat
  ST-Link) — sudah jalan dan teruji, tapi **tidak dipakai lagi untuk versi final** setelah
  keputusan pindah ke Wemos D1 Mini. Tetap disimpan di repo sebagai referensi.
- ❌ Modul MFRC522 pertama ternyata rusak di bagian antena (bagian digital/SPI-nya masih
  hidup, terbukti dari pembacaan Version Register yang konsisten, tapi tidak bisa
  mendeteksi kartu sama sekali). Sedang menunggu modul pengganti.
- ⏳ Wemos D1 Mini — belum dibeli, belum mulai setup Arduino IDE.
- ⏳ Form online pendaftaran — belum mulai, masih tahap rancangan.

## Hal yang belum diputuskan

Beberapa keputusan ini masih perlu ditentukan sebelum lanjut ke implementasi:

- **Siapa yang isi form online pendaftaran** — admin lab yang input manual setelah
  memeriksa surat izin, atau pengunjung sendiri yang isi (lalu admin tinggal approve)?
- **Satu tap atau dua tap (masuk-keluar)** — saat ini diasumsikan cukup satu kali tap per
  kunjungan (cuma catat kehadiran), belum ada kebutuhan hitung durasi kunjungan. Bisa
  direvisi kalau ternyata dibutuhkan.
- **Jaringan WiFi mana yang dipakai** — lab punya pilihan eduroam (WPA2-Enterprise) dan
  captive portal (login browser). Rencananya pakai **eduroam** karena itu yang bisa
  diotomatisasi lewat kode di Wemos D1 (captive portal tidak bisa tanpa bantuan IT). Perlu
  kredensial eduroam yang dipakai device ini (username+password khusus device, atau punya
  admin lab).
- **Penanganan kartu tidak terdaftar** — apa yang terjadi kalau ada kartu di-tap tapi UID-nya
  tidak ada di tabel `pengunjung` (misal ditolak dengan indikator LED/buzzer, atau tetap
  dicatat sebagai "UID tidak dikenal" untuk ditindaklanjuti admin).
- **Keandalan server lokal** — aplikasi Flask-nya perlu auto-start kalau PC itu pernah
  restart (lihat Fase 6 di roadmap).

## Catatan soal IP server yang tidak statis

Jaringan lab **tidak mengizinkan IP statis** (baik reservasi DHCP di router maupun setting
manual di PC) dan **tidak auto-register hostname ke DNS internal** (sudah dicoba, hasil
`Test-NetConnection -ComputerName "DESKTOP-REC5G8C"` gagal resolve). Windows Firewall PC lab
**sudah mengizinkan port 5000** (terverifikasi jalan dari perangkat lain di jaringan yang sama),
jadi bukan itu masalahnya — murni soal IP-nya sendiri yang bisa berubah sewaktu-waktu.

Solusi yang dipakai: **UDP Broadcast Discovery** — server broadcast IP-nya sendiri secara
berkala ke jaringan lokal, Wemos D1 dengerin buat tahu IP terkini, jadi tidak perlu reflash
device kalau IP PC lab berubah. Ini tetap relevan walau mikon-nya sudah ganti ke Wemos D1 —
soal IP dinamis ini murni karena kebijakan jaringan lab, bukan soal hardware yang dipakai.

**Cara kerja:**
```
1. PC lab (server) kirim paket UDP broadcast tiap ~5 detik ke 255.255.255.255:5001,
   isi pesan: "LOGBOOK_SERVER|<ip>|<port>"
2. Wemos D1 buka UDP listen (WiFiUDP di Arduino), tangkap broadcast itu
3. Parsing pesan, simpan IP terbaru di variabel
4. Tiap ada tap kartu, pakai IP tersimpan itu buat HTTP POST ke server
```

Detail teknis (buat referensi implementasi Fase 2 & 3):
- Port UDP broadcast: **5001** (beda dari port HTTP 5000, supaya tidak tercampur)
- Interval broadcast: **~5 detik**
- Format pesan: teks polos `LOGBOOK_SERVER|<ip>|<port>` — IP didapat server lewat trik socket
  (`connect` ke alamat luar tanpa kirim data, baca `getsockname()`), bukan dari
  `hostname`/`gethostbyname` yang bisa tidak akurat di PC dengan banyak network adapter.
- IP server saat pengujian awal (7 Sept 2026): `10.42.17.248`, hostname PC: `DESKTOP-REC5G8C`
  — dicatat buat referensi, tapi tidak lagi jadi satu-satunya sumber kebenaran karena sudah
  ada mekanisme discovery otomatis.

## Roadmap implementasi

Diurutkan dari yang tidak butuh hardware sama sekali sampai yang butuh modul RFID pengganti.
Fase 1 bisa mulai dikerjakan sekarang juga, tidak perlu menunggu apa-apa.

### Fase 1 — Backend server

Tidak butuh STM32/ESP-01/RFID sama sekali, murni dikerjakan di PC lab.

1. Setup project Flask sederhana.
2. Buat skema database SQLite — tabel `pengunjung` dan `log_kunjungan` (lihat di atas).
3. Buat endpoint API, misal `POST /tap`, yang menerima UID + waktu, mencocokkan ke tabel
   `pengunjung`, mencatat ke `log_kunjungan` kalau cocok, membalas status `OK`/`REJECTED`.
4. Tes endpoint ini pakai Postman/`curl` dulu — kirim UID palsu manual, pastikan logikanya
   benar sebelum ada hardware yang terlibat sama sekali.

### Fase 2 — Wemos D1 connect WiFi + HTTP ke server lokal

Butuh Wemos D1 Mini + kabel USB, belum butuh RFID.

1. Setup Arduino IDE + board package ESP8266 (`Additional Board Manager URLs`, install
   "esp8266 by ESP8266 Community").
2. Tes koneksi WiFi dasar dulu pakai hotspot HP (WPA2-PSK biasa) — validasi board sehat
   sebelum coba yang lebih rumit (eduroam).
3. Kalau WiFi hotspot berhasil, lanjut coba **eduroam** pakai library
   `ESP8266_WPA2_Enterprise` (isi identity/username/password eduroam kampus).
4. Tambahkan broadcaster UDP di `app.py` (thread terpisah, kirim `LOGBOOK_SERVER|<ip>|<port>`
   tiap ~5 detik ke `255.255.255.255:5001`).
5. Tulis kode Wemos D1 buat dengerin broadcast itu (`WiFiUDP`), parsing dapat IP server.
6. Tes kirim HTTP POST (`ESP8266HTTPClient`) berisi UID dummy ke endpoint `/tap` pakai IP
   hasil discovery — pastikan data sampai dan tercatat di database.

### Fase 3 — (digabung ke Fase 2)

Karena sekarang cuma satu chip (bukan STM32 + ESP-01 terpisah), tidak ada lagi tahap
"integrasi dua device" yang berdiri sendiri — semua logika WiFi+HTTP+discovery sudah
menyatu di kode Fase 2. Fase ini dilewati.

### Fase 4 — Integrasi RFID (setelah modul pengganti datang)

1. Port kode `MFRC522_Request`/`MFRC522_Anticoll` dari versi STM32 HAL ke Arduino (`SPI.h`
   bawaan Arduino, logika protokolnya sama, cuma API SPI-nya beda).
2. Gabungkan dengan kode WiFi+HTTP dari Fase 2.
3. Tiap tap kartu sukses → UID asli (bukan dummy lagi) dikirim ke server.

### Fase 5 — Fitur pendukung admin

1. Form pendaftaran online (cara admin lab masukkan pengunjung baru + UID kartunya ke
   tabel `pengunjung`).
2. Halaman sederhana buat admin lihat/cari riwayat `log_kunjungan`.

### Fase 6 — Keandalan jangka panjang

1. Set Flask auto-start di PC lab (Task Scheduler Windows).
2. ~~Reservasi IP statis PC lab di router lab~~ — tidak memungkinkan (jaringan lab tidak
   mengizinkan), sudah digantikan mekanisme UDP Broadcast Discovery di Fase 2.
