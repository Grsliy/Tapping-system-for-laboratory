# Sistem Logbook Pengunjung Lab berbasis RFID

Sistem pencatatan kunjungan otomatis untuk Laboratorium Power Electronics. Pengunjung yang
sudah terdaftar tinggal tap kartu RFID di pintu masuk, tanpa perlu isi buku tamu fisik.

## Arsitektur

ESP32 membaca UID kartu lewat modul MFRC522, connect ke jaringan WiFi kampus, lalu
mengirim data tap ke Google Apps Script yang mencocokkan dan mencatatnya ke Google Sheet.
Detail lengkap arsitektur, varian ESP32 yang dipakai, keputusan teknis, dan skema data ada
di [Dokumen/Sistem-Logbook-Pengunjung-RFID](Dokumen/Sistem-Logbook-Pengunjung-RFID.md).

## Struktur folder

- [Dokumen/](Dokumen/) — rancangan sistem, keputusan arsitektur, dan milestone pengerjaan.
- [Tutorial/](Tutorial/) — panduan langkah kerja per milestone, urut sesuai hari pengerjaan.
- [Referensi/](Referensi/) — materi pendukung (modul praktikum IoT, datasheet, gambar pinout).
- [logbook-firmware/](logbook-firmware/) — kode firmware ESP32 (PlatformIO, framework Arduino).
- [logbook-appsscript/](logbook-appsscript/) — salinan lokal kode Google Apps Script (backend
  yang jalan di Google, dipakai untuk riwayat/backup di luar Apps Script Editor).

## Status pengerjaan

Status terkini, keputusan yang sudah diambil, dan milestone yang sedang berjalan ada di
bagian [Status pengerjaan saat ini](Dokumen/Sistem-Logbook-Pengunjung-RFID.md#status-pengerjaan-saat-ini)
dan [Milestone 5 hari](Dokumen/Sistem-Logbook-Pengunjung-RFID.md#milestone-5-hari) di
dokumen utama.
