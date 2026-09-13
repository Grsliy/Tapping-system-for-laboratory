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
[MFRC522] --SPI--> [ESP32-C3 SuperMini]
                        |  (WiFi + internet, HTTPS)
                        v
              [Google Apps Script (Web App)]
                        |
                        v
              [Google Sheet: Pengunjung + Log_Kunjungan]
```

Pendaftaran pengunjung tidak lewat Form (lihat "Hal yang belum diputuskan" — sudah diganti
isi manual langsung ke Sheet oleh admin).

- **MFRC522** — baca UID kartu, dihubungkan ke ESP32 lewat SPI. Logika baca register/UID
  (`Request`/`Anticoll`) sudah pernah ditulis & diuji versi STM32-nya, tinggal diporting ke
  Arduino (SPI API beda, logika protokolnya sama).
- **ESP32-C3 SuperMini** — satu chip yang urus semuanya: baca UID dari MFRC522, connect WiFi
  (rencana pakai **UGM-IoT**, WPA2-PSK biasa — lihat "Hal yang belum diputuskan"), kirim
  HTTPS POST ke Apps Script tiap ada tap.
- **Google Apps Script** — terima UID dari ESP32, cocokkan ke Sheet `Pengunjung`, catat ke
  Sheet `Log_Kunjungan` kalau valid, balas status. Menggantikan peran server Flask yang
  sebelumnya dipakai.
- **Google Sheet** — dua tab, `Pengunjung` (diisi manual oleh admin) dan `Log_Kunjungan`
  (riwayat tap). Admin bisa lihat/edit langsung lewat Sheet, tidak perlu halaman admin
  terpisah.

## Riwayat keputusan mikon

Perjalanan sampai ke ESP32-C3 SuperMini, disimpan supaya tidak mengulang percobaan yang sama:

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
5. **Kembali ke ESP32-C3 SuperMini** (13 Sept 2026) — setelah keputusan berhenti mengejar
   eduroam dan pindah ke WPA2-PSK biasa (UGM-IoT), alasan awal pindah ke WROOM (butuh
   WPA2-Enterprise yang stabil) sudah tidak relevan. Brownout yang dulu terjadi di
   SuperMini murni soal power supply saat pengujian, bukan cacat bawaan chip C3 — WPA2-PSK
   tidak punya masalah stabilitas seperti eduroam di board manapun. Board lebih kompak dan
   sudah dimiliki, dipakai lagi dengan syarat power supply yang memadai kali ini.

**Keputusan:** berhenti mengejar eduroam, pindah ke jaringan **UGM-IoT** (WPA2-PSK biasa,
kalau permohonan izin disetujui — lihat "Hal yang belum diputuskan"). Mikon final:
**ESP32-C3 SuperMini**.

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

**Tab `Log_Kunjungan`** — catatan tiap kali tap kartu. Nama dan tujuan ikut disalin dari
`Pengunjung` di tiap baris (denormalisasi) supaya riwayat langsung kebaca tanpa perlu
cross-reference manual ke tab lain, dan tetap merekam kondisi pengunjung saat tap itu terjadi
walau datanya di `Pengunjung` diedit belakangan.

| Kolom | Keterangan |
|---|---|
| `uid_kartu` | Merujuk ke `Pengunjung.uid_kartu` |
| `nama` | Disalin dari `Pengunjung` saat tap terjadi |
| `tujuan` | Disalin dari `Pengunjung` saat tap terjadi |
| `waktu_tap` | Timestamp saat tap terjadi |

## Status pengerjaan saat ini

- ⚠️ **Kembali ke ESP32-C3 SuperMini** (dari ESP32 WROOM yang sempat tervalidasi sehat) —
  alasan pindah ke WROOM (WPA2-Enterprise) sudah tidak relevan setelah eduroam ditinggalkan.
  `platformio.ini` dan `main.cpp` sudah disiapkan ulang, **build tervalidasi sukses**, tapi
  **belum diuji di board fisik** — perlu diperhatikan lagi soal power supply yang memadai
  (pelajaran dari brownout sebelumnya).
- ❌ **eduroam ditinggalkan** — sudah dicoba maksimal di 2 mikon (ESP8266, ESP32-C3/WROOM)
  dengan berbagai pendekatan, tetap tidak stabil. Lihat "Riwayat keputusan mikon" di atas.
- ⏳ **Permohonan akses WiFi UGM-IoT** — sudah dikirim email ke pengelola jaringan
  departemen, menunggu balasan (termasuk konfirmasi apakah UGM-IoT satu subnet dengan
  Ethernet lab, relevan untuk arsitektur lama; dengan Apps Script ini sudah tidak masalah
  karena tidak perlu satu jaringan dengan server).
- ❌ **`logbook-server/` (Flask + SQLite) dihapus** dari repo — arsitektur pindah ke Google
  Sheets + Apps Script, PC lab tidak lagi berperan sebagai server.
- ✅ **Google Sheet + Apps Script selesai dan teruji** — Hari 1 milestone tuntas, lihat
  detail di bagian Milestone di bawah.
- ⏳ **Modul MFRC522 pengganti** — status kedatangan perlu dicek ulang (terakhir tercatat
  masih menunggu pengiriman).
- ✅ Kode baca UID kartu (`MFRC522_Request`/`MFRC522_Anticoll`) sudah pernah ditulis & teruji
  strukturnya versi STM32 HAL — jadi referensi logika, masih perlu diporting ke Arduino.

## Hal yang belum diputuskan

- **Balasan izin UGM-IoT dari departemen** — belum ada kepastian, dan mempengaruhi apakah
  Hari 3 di milestone bisa jalan sesuai rencana atau perlu jaringan sementara (hotspot HP).
- ~~**Siapa yang isi form pendaftaran**~~ — sudah diputuskan: **tidak pakai Form sama
  sekali**. Admin isi manual langsung ke tab `Pengunjung` di Sheet setelah surat izin
  disetujui. Google Form (Hari 2 di milestone lama) di-skip.
- **Satu tap atau dua tap (masuk-keluar)** — saat ini diasumsikan cukup satu kali tap per
  kunjungan (cuma catat kehadiran), belum ada kebutuhan hitung durasi kunjungan.
- **Penanganan kartu tidak terdaftar** — apa yang terjadi kalau ada kartu di-tap tapi UID-nya
  tidak ada di tab `Pengunjung` (misal ditolak dengan indikator LED/buzzer, atau tetap
  dicatat sebagai "UID tidak dikenal" untuk ditindaklanjuti admin).

## Milestone 5 hari

Disusun 12 Sept 2026, setelah pivot ke Google Sheets + Apps Script. Beberapa hari
bergantung hal di luar kendali (approval UGM-IoT, kedatangan modul RFID) — ditandai jelas.

**Hari 1 — Backend: Google Sheet + Apps Script** ✅ SELESAI
*(Tidak perlu hardware, tidak perlu tunggu approval UGM-IoT)*
- [x] Buat Google Sheet, 2 tab: `Pengunjung` dan `Log_Kunjungan` (skema di atas).
- [x] Tulis Apps Script `doPost(e)`: terima UID, cocokkan ke tab `Pengunjung`, catat ke
      `Log_Kunjungan` kalau valid, balas JSON status. Kode tersimpan di
      [logbook-appsscript/Code.gs](../logbook-appsscript/Code.gs).
- [x] Deploy sebagai Web App, catat URL-nya.
- [x] Tes pakai PowerShell (`Invoke-RestMethod`) dengan UID dummy — terverifikasi 13 Sept
      2026, `OK` untuk UID terdaftar, `REJECTED` untuk UID tidak dikenal, dan baris baru
      terkonfirmasi muncul di tab `Log_Kunjungan`. Detail di
      [Tutorial/Hari-1-Setup-Sheet-AppsScript.md](../Tutorial/Hari-1-Setup-Sheet-AppsScript.md).

**Hari 2 — Pendaftaran & lihat riwayat** ⏭️ DI-SKIP
*(Keputusan 13 Sept 2026: tidak pakai Google Form. Admin isi manual langsung ke tab
`Pengunjung` di Sheet — lebih simpel buat skala pengunjung yang tidak terlalu banyak.
Riwayat kunjungan juga cukup dilihat langsung dari tab `Log_Kunjungan`, tidak perlu
halaman terpisah. Langsung lanjut ke Hari 3.)*

**Hari 3 — ESP32 connect WiFi + HTTPS ke Apps Script**
*(Kalau UGM-IoT belum di-approve, pakai hotspot HP dulu buat validasi kode)*
- [x] Bersihkan `main.cpp`: hapus kode UDP discovery & eduroam (sudah tidak relevan), ganti
      WiFi ke WPA2-PSK biasa (`WiFi.begin(ssid, password)`) — kredensial diisi saat upload
      sesuai jaringan yang dipakai (UGM-IoT atau hotspot sementara).
- [x] Tambah kode HTTPS POST ke URL Apps Script (`HTTPClient` + `WiFiClientSecure`,
      `setInsecure()`). Build tervalidasi sukses di ESP32-C3.
- [ ] Tes kirim UID dummy dari ESP32 fisik, pastikan tercatat di Sheet — **belum dijalankan
      di board sungguhan**, kode sudah otomatis kirim UID dummy `DEADBEEF` sekali begitu
      WiFi connect saat boot.

**Hari 4 — Integrasi RFID**
*(Butuh modul MFRC522 pengganti sudah di tangan)*
- [ ] Port kode `MFRC522_Request`/`MFRC522_Anticoll` ke project ini (SPI Arduino).
- [ ] Gabungkan dengan kode WiFi+HTTPS dari Hari 3.
- [ ] Tes tap kartu asli sampai tercatat di Sheet.

**Hari 5 — Wrap-up**
- [ ] Uji kasus tidak biasa: kartu tidak terdaftar, WiFi putus sesaat.
- [ ] Update dokumen ini ke kondisi final.
- [ ] Commit & push terakhir, rapikan repo.
