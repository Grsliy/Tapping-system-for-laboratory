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

```
[Kartu RFID pengunjung]
        |  (tap)
        v
[MFRC522] --SPI--> [STM32F401CCU6 (Black Pill)] --UART--> [ESP8266/ESP32]
                                                                  |  (WiFi, HTTP)
                                                                  v
                                                          [Server + Database]
                                                                  ^
                                                                  |
                                                        [Form online pendaftaran]
```

- **MFRC522** — baca UID kartu, dihubungkan ke STM32 lewat SPI (setup ini sudah dikerjakan
  dan diuji, lihat catatan status di bawah).
- **STM32F401CCU6** — baca UID dari MFRC522, kirim ke ESP lewat UART setiap ada tap.
- **ESP8266/ESP32** — terima UID dari STM32 lewat UART, teruskan ke server lewat WiFi
  (HTTP POST ke endpoint API, payload JSON berisi UID + timestamp).
- **Server + Database** — terima data dari ESP, simpan sebagai log kunjungan. Juga jadi
  tempat data hasil form online pendaftaran disimpan (untuk dicocokkan dengan UID).
- **Form online pendaftaran** — tempat data pengunjung (nama, institusi, tujuan) diinput
  sebelum kartu bisa dipakai tap.

## Keputusan arsitektur yang sudah diambil

- **Modul WiFi: ESP-01**, dipakai dengan **firmware AT bawaan** (bukan flash firmware custom).
  STM32 tetap jadi "otak" utama, ESP-01 cuma diperintah lewat AT command via UART untuk
  connect WiFi dan kirim HTTP request. STM32 dan ESP-01 sendiri tidak pernah tahu siapa yang
  terdaftar — mereka cuma kurir, seluruh logika pencocokan UID ada di server.
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

- ✅ Setup STM32F401CCU6 (Black Pill) + STM32CubeIDE, clock 84 MHz, SPI1 ke MFRC522, UART
  buat debug lewat ST-Link — sudah jalan dan teruji.
- ✅ Kode baca UID kartu (`MFRC522_Request` + `MFRC522_Anticoll`) sudah ditulis dan
  strukturnya teruji lewat modul MFRC522 pertama.
- ❌ Modul MFRC522 pertama ternyata rusak di bagian antena (bagian digital/SPI-nya masih
  hidup, terbukti dari pembacaan Version Register yang konsisten, tapi tidak bisa
  mendeteksi kartu sama sekali). Sedang menunggu modul pengganti.
- ⏳ Modul WiFi (ESP8266/ESP32) — belum mulai, nunggu modul RFID pengganti datang dulu
  supaya bisa tes ujung ke ujung sekalian.
- ⏳ Server, database, dan form online pendaftaran — belum mulai, masih tahap rancangan.

## Hal yang belum diputuskan

Beberapa keputusan ini masih perlu ditentukan sebelum lanjut ke implementasi:

- **Siapa yang isi form online pendaftaran** — admin lab yang input manual setelah
  memeriksa surat izin, atau pengunjung sendiri yang isi (lalu admin tinggal approve)?
- **Satu tap atau dua tap (masuk-keluar)** — saat ini diasumsikan cukup satu kali tap per
  kunjungan (cuma catat kehadiran), belum ada kebutuhan hitung durasi kunjungan. Bisa
  direvisi kalau ternyata dibutuhkan.
- **Keamanan jaringan WiFi lab** — kredensial WiFi buat modul ESP perlu dipikirkan, apakah
  pakai jaringan lab yang sudah ada atau jaringan terpisah khusus alat ini.
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
berkala ke jaringan lokal, ESP-01 dengerin buat tahu IP terkini, jadi tidak perlu reflash
STM32 kalau IP PC lab berubah.

**Cara kerja:**
```
1. PC lab (server) kirim paket UDP broadcast tiap ~5 detik ke 255.255.255.255:5001,
   isi pesan: "LOGBOOK_SERVER|<ip>|<port>"
2. ESP-01 buka UDP listen mode, tangkap broadcast itu
3. STM32 parsing pesan, simpan IP terbaru di memori
4. Tiap ada tap kartu, STM32 pakai IP tersimpan itu buat HTTP POST ke server
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

### Fase 2 — ESP-01 connect ke server lokal

Butuh ESP-01 + USB-TTL, belum butuh STM32/RFID.

1. Tes `AT`, `AT+CWJAP` (connect WiFi).
2. Tes `AT+CIPSTART="TCP","<IP PC lab>",<port>` + `AT+CIPSEND` kirim HTTP POST manual ke
   endpoint Flask dari Fase 1 (pakai IP yang diketahui manual dulu, buat validasi jalur
   HTTP-nya sendiri).
3. Tambahkan broadcaster UDP di `app.py` (thread terpisah, kirim `LOGBOOK_SERVER|<ip>|<port>`
   tiap ~5 detik ke `255.255.255.255:5001`).
4. Tes ESP-01 buka UDP listen (`AT+CIPSTART="UDP","0.0.0.0",5001,...`) dan berhasil menangkap
   pesan broadcast itu lewat `+IPD`.
5. Pastikan data dummy sampai dan tercatat di database lewat IP yang didapat dari discovery
   (bukan IP hardcoded lagi) — isolasi masalah jaringan/HTTP/discovery dari masalah kode
   STM32 nanti.

### Fase 3 — Integrasi STM32 + ESP-01

Butuh STM32 Black Pill + ESP-01 disambung bareng.

1. Sambungkan ESP-01 ke UART kedua STM32 (USART2, PA2/PA3 — supaya tidak rebutan sama
   UART1 yang dipakai debug/serial monitor).
2. Port kode AT command dari modul praktikum IoT lama, ganti bagian MQTT jadi HTTP POST
   ke server lokal.
3. Implementasikan logika discovery di STM32: dengarkan broadcast UDP dari Fase 2 saat boot
   (dan idealnya di-refresh berkala), simpan IP server terbaru di variabel, pakai variabel
   itu (bukan string IP tetap) tiap bikin `AT+CIPSTART` buat kirim tap.
4. Tes kirim UID dummy yang di-hardcode dulu di kode STM32 (belum pakai RFID beneran) —
   pastikan data sampai ke server lewat jalur lengkap STM32 → ESP-01 (discovery + HTTP) →
   server.

### Fase 4 — Integrasi RFID (setelah modul pengganti datang)

1. Gabungkan kode `MFRC522_Request`/`MFRC522_Anticoll` yang sudah dibuat & teruji sebelumnya
   dengan pipeline dari Fase 3.
2. Tiap tap kartu sukses → UID asli (bukan dummy lagi) dikirim ke server.

### Fase 5 — Fitur pendukung admin

1. Form pendaftaran online (cara admin lab masukkan pengunjung baru + UID kartunya ke
   tabel `pengunjung`).
2. Halaman sederhana buat admin lihat/cari riwayat `log_kunjungan`.

### Fase 6 — Keandalan jangka panjang

1. Set Flask auto-start di PC lab (Task Scheduler Windows).
2. Reservasi IP statis PC lab di router lab.
