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
command), diganti jadi satu chip WiFi+mikon yang urus SPI ke MFRC522 sekaligus WiFi+HTTP-nya
sendiri (percobaan pertama pakai Wemos D1 Mini/ESP8266).

**Revisi (8 Sept 2026):** Wemos D1 Mini (ESP8266) ternyata **gagal konsisten connect ke
eduroam** meski kredensial sudah terbukti benar (berhasil di Windows) — dugaan kuat karena
SDK WPA2-Enterprise ESP8266 cuma support TLS 1.0, sedangkan RADIUS server eduroam kemungkinan
sudah menonaktifkan versi TLS itu. Diganti ke **ESP32-C3**, yang punya dukungan
WPA2-Enterprise jauh lebih matang (terverifikasi dari header SDK resmi: eksplisit mendukung
PEAP-MSCHAPv2, metode yang sama dipakai eduroam kampus ini). Detail teknis ada di bagian
"Keputusan arsitektur" di bawah.

```
[Kartu RFID pengunjung]
        |  (tap)
        v
[MFRC522] --SPI--> [ESP32-C3]
                        |  (WiFi eduroam, HTTP)
                        v
                [Server + Database]
                        ^
                        |
              [Form online pendaftaran]
```

- **MFRC522** — baca UID kartu, dihubungkan ke ESP32-C3 lewat SPI. Logika baca register/UID
  (`Request`/`Anticoll`) sudah pernah ditulis & diuji versi STM32-nya, tinggal diporting ke
  Arduino (SPI API beda, logika protokolnya sama).
- **ESP32-C3** — satu chip yang urus semuanya: baca UID dari MFRC522, connect WiFi eduroam
  (WPA2-Enterprise), kirim HTTP POST langsung ke server tiap ada tap. Tidak ada perantara AT
  command/UART ke modul WiFi terpisah.
- **Server + Database** — terima data dari ESP32-C3, simpan sebagai log kunjungan. Juga jadi
  tempat data hasil form online pendaftaran disimpan (untuk dicocokkan dengan UID). Bagian
  ini **tidak berubah sama sekali** dari rancangan awal — sepenuhnya platform-agnostic.
- **Form online pendaftaran** — tempat data pengunjung (nama, institusi, tujuan) diinput
  sebelum kartu bisa dipakai tap.

## Keputusan arsitektur yang sudah diambil

- **Mikon utama: ESP32-C3**, menggantikan rencana awal STM32F401 (Black Pill) + ESP-01
  terpisah, dan juga menggantikan percobaan pertama Wemos D1 Mini (ESP8266). Alasan awal
  pindah dari STM32+ESP-01: WiFi lab yang tersedia ada yang **eduroam (WPA2-Enterprise/
  802.1X)** dan ada yang **captive portal (login browser)** — dua-duanya tidak didukung ESP-01
  dengan firmware AT bawaan (AT+CWJAP cuma support WPA2-PSK biasa), jadi modul WiFi-nya harus
  jalanin logika sendiri (bukan cuma "modem AT"), makanya sekalian dijadikan satu chip.
  Alasan pindah dari ESP8266 ke ESP32-C3: **ESP8266 gagal konsisten connect ke eduroam**
  walau kredensial sudah terbukti benar (berhasil di Windows dengan identity/username/password
  yang sama persis) — dugaan kuat karena SDK WPA2-Enterprise ESP8266 (`wpa2_enterprise.h`)
  cuma support TLS 1.0, dan beberapa laporan komunitas ([esp8266/Arduino#3842](https://github.com/esp8266/Arduino/issues/3842))
  menunjukkan implementasinya memang dikenal tidak stabil buat eduroam. ESP32-C3 pakai
  `esp_wpa2.h`, SDK yang jauh lebih baru — header resminya eksplisit menyebut dukungan
  **PEAP-MSCHAPv2** (metode yang sama dipakai eduroam kampus ini, dikonfirmasi dari dialog
  autentikasi WiFi Windows).
  Captive portal tetap tidak bisa diotomatisasi di mikon manapun (butuh MAC whitelist dari
  IT lab kalau itu yang dipakai).
  ESP32-C3 tidak pernah tahu siapa yang terdaftar — dia cuma kurir, seluruh logika pencocokan
  UID ada di server.
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
  keputusan pindah ke ESP32-C3. Tetap disimpan di repo sebagai referensi.
- ❌ Modul MFRC522 pertama ternyata rusak di bagian antena (bagian digital/SPI-nya masih
  hidup, terbukti dari pembacaan Version Register yang konsisten, tapi tidak bisa
  mendeteksi kartu sama sekali). Sedang menunggu modul pengganti.
- ⚠️ **Percobaan pertama pakai Wemos D1 Mini (ESP8266)** — build sukses, tapi **gagal
  konsisten connect ke eduroam** walau kredensial terbukti benar. Diganti ke ESP32-C3
  (lihat "Keputusan arsitektur"). Kode lama tidak disimpan sebagai environment terpisah,
  cukup dicatat di sini dan riwayat git.
- ✅ Project PlatformIO (`logbook-firmware/`) sudah disetup target **ESP32-C3**, skeleton kode
  WiFi eduroam (WPA2-Enterprise via `esp_wpa2.h`) + UDP discovery + HTTP POST sudah ditulis
  dan **build-nya tervalidasi sukses**. Kredensial eduroam sudah diisi di `secrets.h`
  (gitignored). Board fisik sudah ada, **masih dalam proses debug koneksi eduroam** —
  langkah upload+monitor sudah dicoba, belum berhasil connect penuh di percobaan terakhir.
- ✅ Broadcaster UDP di sisi server (`app.py`) — ditulis dan **teruji** lewat
  `test_discovery_listener.py` (script simulasi), pesan diterima benar & konsisten tiap
  ~5 detik.
- ✅ Form online pendaftaran (`/daftar`) — ditulis dan **teruji** (daftar baru, UID duplikat
  ditolak, hasil pendaftaran langsung bisa dipakai tap).

## Hal yang belum diputuskan

Beberapa keputusan ini masih perlu ditentukan sebelum lanjut ke implementasi:

- **Siapa yang isi form online pendaftaran** — admin lab yang input manual setelah
  memeriksa surat izin, atau pengunjung sendiri yang isi (lalu admin tinggal approve)?
- **Satu tap atau dua tap (masuk-keluar)** — saat ini diasumsikan cukup satu kali tap per
  kunjungan (cuma catat kehadiran), belum ada kebutuhan hitung durasi kunjungan. Bisa
  direvisi kalau ternyata dibutuhkan.
- ~~**Jaringan WiFi mana yang dipakai**~~ — sudah diputuskan: **eduroam**, sudah ada
  kredensial (username+password akun pribadi pengelola project). Sisa tantangan sekarang
  murni teknis (debug koneksi di ESP32-C3), bukan lagi keputusan.
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
berkala ke jaringan lokal, ESP32-C3 dengerin buat tahu IP terkini, jadi tidak perlu reflash
device kalau IP PC lab berubah. Ini tetap relevan walau mikon-nya sudah ganti (dari Wemos D1
ke ESP32-C3) — soal IP dinamis ini murni karena kebijakan jaringan lab, bukan soal hardware
yang dipakai. **Broadcaster di sisi server sudah ditulis & teruji** (lihat status di atas).

**Cara kerja:**
```
1. PC lab (server) kirim paket UDP broadcast tiap ~5 detik ke 255.255.255.255:5001,
   isi pesan: "LOGBOOK_SERVER|<ip>|<port>"
2. ESP32-C3 buka UDP listen (WiFiUDP di Arduino), tangkap broadcast itu
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

### Fase 1 — Backend server ✅ SELESAI

Tidak butuh STM32/ESP-01/RFID sama sekali, murni dikerjakan di PC lab.

- [x] Setup project Flask sederhana.
- [x] Buat skema database SQLite — tabel `pengunjung` dan `log_kunjungan` (lihat di atas).
- [x] Buat endpoint API, misal `POST /tap`, yang menerima UID + waktu, mencocokkan ke tabel
      `pengunjung`, mencatat ke `log_kunjungan` kalau cocok, membalas status `OK`/`REJECTED`.
- [x] Tes endpoint ini pakai `curl`/`Invoke-RestMethod` — sudah diverifikasi jalan, termasuk
      diakses dari perangkat lain di jaringan lab (bukan cuma localhost).

### Fase 2 — ESP32-C3 connect WiFi + HTTP ke server lokal ⏳ SEBAGIAN

Butuh ESP32-C3 + kabel USB buat langkah yang butuh hardware fisik.

- [x] Setup **PlatformIO** (bukan Arduino IDE — diganti karena sudah kerja di VS Code/
      Antigravity) — project `logbook-firmware/`, board `esp32-c3-devkitm-1`, framework
      Arduino.
- [x] Tulis skeleton kode (`main.cpp`): WiFi connect, UDP discovery listener, HTTP POST
      `/tap` — **tervalidasi build sukses**.
- [x] Tambahkan broadcaster UDP di `app.py` (thread terpisah, kirim
      `LOGBOOK_SERVER|<ip>|<port>` tiap ~5 detik ke `255.255.255.255:5001`) — **teruji**
      lewat `test_discovery_listener.py` (script simulasi tanpa perlu board fisik), pesan
      diterima benar & konsisten tiap ~5 detik.
- [x] ~~Tes koneksi WiFi dasar pakai hotspot HP~~ — dilewati, langsung eksperimen eduroam
      karena sudah di lokasi lab.
- [ ] Tes **eduroam** — **percobaan pertama di Wemos D1/ESP8266 gagal** (dugaan batasan
      TLS 1.0 SDK lama), pindah ke **ESP32-C3** pakai `esp_wpa2.h` (API terverifikasi
      dari header resmi, eksplisit dukung PEAP-MSCHAPv2). Build sukses, kredensial sudah
      diisi di `secrets.h`, **upload & tes koneksi masih berjalan/belum berhasil penuh**.
- [ ] Tes kirim HTTP POST UID dummy ke `/tap` pakai IP hasil discovery — **butuh WiFi
      eduroam berhasil connect dulu**.

### Fase 3 — (digabung ke Fase 2)

Karena sekarang cuma satu chip (bukan STM32 + ESP-01 terpisah), tidak ada lagi tahap
"integrasi dua device" yang berdiri sendiri — semua logika WiFi+HTTP+discovery sudah
menyatu di kode Fase 2. Fase ini dilewati.

### Fase 4 — Integrasi RFID (setelah modul pengganti datang) ⏳ BELUM MULAI

- [ ] Port kode `MFRC522_Request`/`MFRC522_Anticoll` dari versi STM32 HAL ke Arduino
      (`SPI.h` bawaan Arduino, logika protokolnya sama, cuma API SPI-nya beda).
- [ ] Gabungkan dengan kode WiFi+HTTP dari Fase 2.
- [ ] Tiap tap kartu sukses → UID asli (bukan dummy lagi) dikirim ke server.

### Fase 5 — Fitur pendukung admin ⏳ SEBAGIAN

- [x] Form pendaftaran online — endpoint `GET/POST /daftar`, template `daftar.html`.
      Teruji: daftar baru berhasil, UID duplikat ditolak dengan pesan yang jelas, hasil
      pendaftaran langsung bisa dipakai tap di `/tap`.
- [ ] Halaman sederhana buat admin lihat/cari riwayat `log_kunjungan` — endpoint `/log`
      sudah ada (balikin JSON mentah), tapi belum ada tampilan HTML yang enak dibaca.

### Fase 6 — Keandalan jangka panjang ⏳ BELUM MULAI

- [ ] Set Flask auto-start di PC lab (Task Scheduler Windows).
- [x] ~~Reservasi IP statis PC lab di router lab~~ — tidak memungkinkan (jaringan lab tidak
      mengizinkan), sudah digantikan mekanisme UDP Broadcast Discovery di Fase 2 (rencana
      sudah final, implementasi broadcaster-nya sendiri masih di Fase 2 yang belum selesai).

## Timeline 4 hari (target santai, ~1 jam/hari)

Catatan jujur di depan: **Hari 2-4 sebagian bergantung kapan modul MFRC522 pengganti
sampai** — itu di luar kendali (soal pengiriman). Supaya 4 hari ini tetap produktif walau
barang belum datang, tiap hari punya kerjaan cadangan yang tidak butuh hardware. (Update:
ternyata debug koneksi eduroam — termasuk ganti mikon dari Wemos D1 ke ESP32-C3 — memakan
waktu lebih banyak dari estimasi awal, timeline ini jadi kurang akurat dibanding kenyataan.
Dibiarkan apa adanya sebagai catatan, bukan diedit ulang seolah sudah pas dari awal.)

**Hari 1 — Kerjaan software, tidak perlu hardware sama sekali (~1 jam)**
- Tulis broadcaster UDP di `app.py` (~20 menit) — sisa satu-satunya item Fase 2 yang bisa
  dikerjakan tanpa board fisik.
- Tes broadcaster jalan pakai script Python simulasi sederhana (~15 menit).
- Kalau belum dipesan, pesan modul WiFi+mikon dan modul MFRC522 pengganti sekarang juga
  (~5 menit) — supaya jam pengiriman mulai berjalan dari hari ini.
- Mulai draft halaman form pendaftaran online: route Flask + template HTML dasar (~20 menit).

**Hari 2 (~1 jam)**
- *Kalau board sudah sampai:* upload skeleton `main.cpp` ke board, tes koneksi WiFi.
- *Kalau belum sampai:* lanjut selesaikan form pendaftaran online + halaman admin lihat
  riwayat `log_kunjungan` (Fase 5).

**Hari 3 (~1 jam)**
- *Kalau tes WiFi berhasil:* lanjut coba **eduroam**, lalu tes UDP discovery
  + kirim HTTP POST UID dummy ke server — validasi jalur end-to-end tanpa RFID dulu.
- *Kalau masih nunggu barang:* setup Task Scheduler auto-start Flask di PC lab (Fase 6),
  atau rapikan dokumentasi/README repo.

**Hari 4 — Wrap-up (~1 jam)**
- *Kalau modul MFRC522 pengganti sudah sampai dan ESP32-C3 sudah tervalidasi:* port kode
  `MFRC522_Request`/`MFRC522_Anticoll` ke Arduino, tes baca kartu asli sampai ke server.
- *Kalau salah satu barang belum sampai:* itu wajar, bukan kegagalan timeline — tandai sisa
  pekerjaan itu sebagai lanjutan di luar 4 hari ini, dan pastikan semua yang sudah dikerjakan
  (broadcaster, form admin, auto-start) sudah ter-commit & ter-push rapi ke GitHub.

Target di akhir hari ke-4: **sistem tap-in jalan end-to-end pakai UID dummy minimal**
(bahkan kalau RFID fisik belum terintegrasi), plus fitur admin (form pendaftaran + lihat
log) sudah ada. Integrasi RFID fisik boleh menyusul kapan pun modulnya siap, tanpa
menghalangi bagian lain sistem untuk selesai lebih dulu.
