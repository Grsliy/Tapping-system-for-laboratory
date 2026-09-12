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
3. Admin (atau pengunjung, lihat "Hal yang belum diputuskan") mengisi form pendaftaran:
   nama, institusi/asal, tujuan kunjungan.
4. Data dari form + UID kartu RFID pengunjung didaftarkan ke database oleh admin.
5. Pengunjung tap kartu RFID di reader saat masuk lab.
6. Reader mencatat waktu tap, dicocokkan ke UID yang sudah terdaftar, tersimpan sebagai
   log kunjungan.
```

Bagian 1–4 di atas adalah proses pendaftaran (sekali per pengunjung/per kunjungan berlaku),
bagian 5–6 adalah proses harian yang berulang tiap pengunjung datang.

## Arsitektur sistem

**Revisi (12 Sept 2026):** setelah eduroam terbukti tidak stabil di ESP32 Arduino core
(lihat "Riwayat keputusan mikon" di bawah) dan PC lab ternyata sulit disatukan jaringan
dengan device (Ethernet vs WiFi), server self-hosted di PC lab **diganti Google Sheets +
Google Apps Script**. Ini menghilangkan dua masalah sekaligus: tidak perlu lagi PC yang
selalu nyala di jaringan yang sama dengan device, dan tidak perlu lagi mekanisme discovery
IP dinamis — Apps Script Web App punya URL tetap yang bisa diakses dari jaringan mana pun
asal ada internet.

```
[Kartu RFID pengunjung]
        |  (tap)
        v
[MFRC522] --SPI--> [ESP32 WROOM]
                        |  (WiFi + internet, HTTPS)
                        v
              [Google Apps Script (Web App)]
                        |
                        v
              [Google Sheet: Pengunjung + Log_Kunjungan]
                        ^
                        |
              [Google Form: pendaftaran pengunjung]
```

- **MFRC522** — baca UID kartu, dihubungkan ke ESP32 lewat SPI. Logika baca register/UID
  (`Request`/`Anticoll`) sudah pernah ditulis & diuji versi STM32-nya, tinggal diporting ke
  Arduino (SPI API beda, logika protokolnya sama).
- **ESP32 WROOM** — satu chip yang urus semuanya: baca UID dari MFRC522, connect WiFi
  (rencana pakai **UGM-IoT**, WPA2-PSK biasa — lihat "Hal yang belum diputuskan"), kirim
  HTTPS POST ke Apps Script tiap ada tap.
- **Google Apps Script** — terima UID dari ESP32, cocokkan ke Sheet `Pengunjung`, catat ke
  Sheet `Log_Kunjungan` kalau valid, balas status. Menggantikan peran server Flask yang
  sebelumnya dipakai.
- **Google Sheet** — dua tab, `Pengunjung` (hasil pendaftaran) dan `Log_Kunjungan` (riwayat
  tap). Admin bisa lihat/edit langsung lewat Sheet, tidak perlu halaman admin terpisah.
- **Google Form** — tempat data pengunjung (nama, institusi, tujuan) diinput sebelum kartu
  bisa dipakai tap, response otomatis masuk ke Sheet `Pengunjung`.

## Riwayat keputusan mikon

Perjalanan sampai ke ESP32 WROOM, disimpan supaya tidak mengulang percobaan yang sama:

1. **STM32F401 (Black Pill) + ESP-01 terpisah** (rancangan awal) — ESP-01 dengan firmware
   AT bawaan cuma support WPA2-PSK biasa, sedangkan WiFi lab ada yang eduroam
   (WPA2-Enterprise) dan ada yang captive portal. Diganti jadi satu chip WiFi+mikon supaya
   bisa jalanin logika WPA2-Enterprise sendiri.
2. **Wemos D1 Mini (ESP8266)** — build sukses, tapi **gagal konsisten connect ke eduroam**
   meski kredensial terbukti benar (berhasil di Windows). Dugaan kuat: SDK WPA2-Enterprise
   ESP8266 (`wpa2_enterprise.h`) cuma support TLS 1.0, RADIUS eduroam kemungkinan sudah
   menonaktifkan versi itu. Match dengan laporan komunitas
   ([esp8266/Arduino#3842](https://github.com/esp8266/Arduino/issues/3842)) yang menunjukkan
   implementasi ini memang dikenal tidak stabil buat eduroam.
3. **ESP32-C3 (SuperMini/clone)** — API WPA2-Enterprise-nya jauh lebih matang (`esp_wpa2.h`,
   terverifikasi dari header SDK resmi mendukung PEAP-MSCHAPv2), tapi board ini brownout
   berulang pas WiFi transmit ("wifi:Set status to INIT" berulang cepat) — regulator daya
   board SuperMini dikenal pas-pasan (arus WiFi transmit bisa sampai 500mA sesaat).
4. **ESP32 WROOM (DevKit)** — regulator lebih baik, brownout hilang. Tapi eduroam **tetap
   gagal** dengan berbagai kode alasan yang berganti-ganti (`BEACON_TIMEOUT`, `ASSOC_FAIL`,
   `HANDSHAKE_TIMEOUT`, `AUTH_EXPIRE`), bahkan ke access point fisik yang berbeda-beda
   (BSSID beda). Sudah dicoba: API resmi Espressif (`WiFi.begin(ssid, WPA2_AUTH_PEAP, ...)`),
   matikan WiFi power-save, pantau auto-retry sampai beberapa menit — tetap gagal. Kesimpulan:
   ini bukan bug di kode kita, tapi ketidakstabilan WPA2-Enterprise Arduino-ESP32 core
   terhadap infrastruktur RADIUS eduroam kampus ini spesifik — didukung laporan serupa di
   [issue #5027](https://github.com/espressif/arduino-esp32/issues/5027) (berhasil di
   ESP-IDF murni, gagal di Arduino core).

**Keputusan:** berhenti mengejar eduroam, pindah ke jaringan **UGM-IoT** (WPA2-PSK biasa,
kalau permohonan izin disetujui — lihat "Hal yang belum diputuskan"). ESP32 WROOM tetap
dipakai sebagai mikon final — board-nya sudah terbukti sehat, cuma jaringannya yang diganti.

## Rancangan skema data (Google Sheet)

**Tab `Pengunjung`** — data hasil pendaftaran (satu baris per pengunjung/kunjungan yang disetujui)

| Kolom | Keterangan |
|---|---|
| `uid_kartu` | UID RFID yang didaftarkan ke pengunjung ini |
| `nama` | Nama pengunjung |
| `institusi` | Asal institusi/kampus/perusahaan |
| `tujuan` | Keperluan kunjungan |
| `tanggal_daftar` | Kapan data ini dimasukkan ke sistem |
| `status` | Aktif / nonaktif (buat nonaktifkan kartu setelah kunjungan selesai, kalau perlu) |

**Tab `Log_Kunjungan`** — catatan tiap kali tap kartu

| Kolom | Keterangan |
|---|---|
| `uid_kartu` | Merujuk ke `Pengunjung.uid_kartu` |
| `waktu_tap` | Timestamp saat tap terjadi |

## Status pengerjaan saat ini

- ✅ **ESP32 WROOM tervalidasi sehat** — tidak ada lagi brownout/crash (masalah yang sempat
  terjadi di ESP32-C3 SuperMini), build dan upload lewat PlatformIO berhasil.
- ❌ **eduroam ditinggalkan** — sudah dicoba maksimal di 2 mikon (ESP8266, ESP32-C3/WROOM)
  dengan berbagai pendekatan, tetap tidak stabil. Lihat "Riwayat keputusan mikon" di atas.
- ⏳ **Permohonan akses WiFi UGM-IoT** — sudah dikirim email ke pengelola jaringan
  departemen, menunggu balasan (termasuk konfirmasi apakah UGM-IoT satu subnet dengan
  Ethernet lab, relevan untuk arsitektur lama; dengan Apps Script ini sudah tidak masalah
  karena tidak perlu satu jaringan dengan server).
- ❌ **`logbook-server/` (Flask + SQLite) dihapus** dari repo — arsitektur pindah ke Google
  Sheets + Apps Script, PC lab tidak lagi berperan sebagai server.
- ⏳ **Google Sheet + Apps Script** — belum mulai dibuat.
- ⏳ **Modul MFRC522 pengganti** — status kedatangan perlu dicek ulang (terakhir tercatat
  masih menunggu pengiriman).
- ✅ Kode baca UID kartu (`MFRC522_Request`/`MFRC522_Anticoll`) sudah pernah ditulis & teruji
  strukturnya versi STM32 HAL — jadi referensi logika, masih perlu diporting ke Arduino.

## Hal yang belum diputuskan

- **Balasan izin UGM-IoT dari departemen** — belum ada kepastian, dan mempengaruhi apakah
  Hari 3 di milestone bisa jalan sesuai rencana atau perlu jaringan sementara (hotspot HP).
- **Siapa yang isi form pendaftaran** — admin lab yang input manual setelah memeriksa surat
  izin, atau pengunjung sendiri yang isi (lalu admin tinggal approve)?
- **Satu tap atau dua tap (masuk-keluar)** — saat ini diasumsikan cukup satu kali tap per
  kunjungan (cuma catat kehadiran), belum ada kebutuhan hitung durasi kunjungan.
- **Penanganan kartu tidak terdaftar** — apa yang terjadi kalau ada kartu di-tap tapi UID-nya
  tidak ada di tab `Pengunjung` (misal ditolak dengan indikator LED/buzzer, atau tetap
  dicatat sebagai "UID tidak dikenal" untuk ditindaklanjuti admin).

## Milestone 5 hari

Disusun 12 Sept 2026, setelah pivot ke Google Sheets + Apps Script. Beberapa hari
bergantung hal di luar kendali (approval UGM-IoT, kedatangan modul RFID) — ditandai jelas.

**Hari 1 — Backend: Google Sheet + Apps Script**
*(Tidak perlu hardware, tidak perlu tunggu approval UGM-IoT)*
- [ ] Buat Google Sheet, 2 tab: `Pengunjung` dan `Log_Kunjungan` (skema di atas).
- [ ] Tulis Apps Script `doPost(e)`: terima UID, cocokkan ke tab `Pengunjung`, catat ke
      `Log_Kunjungan` kalau valid, balas JSON status.
- [ ] Deploy sebagai Web App, catat URL-nya.
- [ ] Tes pakai `curl`/Postman dengan UID dummy sebelum ada hardware terlibat.

**Hari 2 — Pendaftaran & lihat riwayat**
*(Lanjutan backend, masih tidak perlu hardware)*
- [ ] Setup Google Form buat pendaftaran pengunjung, response otomatis masuk tab
      `Pengunjung`.
- [ ] Uji alur: isi Form → cek data masuk Sheet → tes tap dummy lagi lewat Apps Script.

**Hari 3 — ESP32 connect WiFi + HTTPS ke Apps Script**
*(Kalau UGM-IoT belum di-approve, pakai hotspot HP dulu buat validasi kode)*
- [ ] Bersihkan `main.cpp`: hapus kode UDP discovery & eduroam (sudah tidak relevan), ganti
      kredensial WiFi ke UGM-IoT atau hotspot sementara.
- [ ] Tambah kode HTTPS POST ke URL Apps Script (`HTTPClient`/`WiFiClientSecure`).
- [ ] Tes kirim UID dummy dari ESP32, pastikan tercatat di Sheet.

**Hari 4 — Integrasi RFID**
*(Butuh modul MFRC522 pengganti sudah di tangan)*
- [ ] Port kode `MFRC522_Request`/`MFRC522_Anticoll` ke project ini (SPI Arduino).
- [ ] Gabungkan dengan kode WiFi+HTTPS dari Hari 3.
- [ ] Tes tap kartu asli sampai tercatat di Sheet.

**Hari 5 — Wrap-up**
- [ ] Uji kasus tidak biasa: kartu tidak terdaftar, WiFi putus sesaat.
- [ ] Update dokumen ini ke kondisi final.
- [ ] Commit & push terakhir, rapikan repo.
