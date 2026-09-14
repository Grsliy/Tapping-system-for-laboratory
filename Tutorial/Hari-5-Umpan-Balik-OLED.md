# Hari 5 — Umpan Balik OLED

Langkah kerja Hari 5 dari rencana kerja di [Sistem-Logbook-Pengunjung-RFID](../Dokumen/Sistem-Logbook-Pengunjung-RFID.md#status-dan-rencana-kerja). Board: ESP32-C3 SuperMini + MFRC522 + OLED SSD1306.

## Goals hari ini

- [x] Pasang OLED SSD1306 128x64 melalui I2C, tanpa mengganggu pin SPI yang digunakan MFRC522.
- [x] Pindahkan seluruh status dari Serial Monitor ke layar, supaya perangkat dapat berdiri
      sendiri di pintu masuk tanpa komputer.
- [x] Tampilkan animasi bongo cat saat menunggu kartu dan saat menunggu balasan Apps Script.

## Wiring

| OLED (JMD0.96C-1) | ESP32-C3 SuperMini |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO0 |
| SCL | GPIO1 |

GPIO8 dan GPIO9 sengaja dihindari walaupun keduanya adalah pin I2C default ESP32-C3. Di board
SuperMini, GPIO8 tersambung ke LED onboard dan GPIO9 ke tombol BOOT, dan keduanya pin strapping
yang ikut menentukan mode boot. GPIO0 dan GPIO1 bebas dari fungsi tersebut, jadi lebih aman
untuk perangkat yang dipasang permanen.

MFRC522 dan OLED sama-sama mengambil 3.3V dan GND. Gunakan jalur rail pada breadboard, jangan
ditumpuk pada satu lubang pin.

## Uji layar secara terpisah lebih dahulu

Sebelum digabung ke firmware utama, OLED diuji sendirian melalui environment PlatformIO
terpisah. Tujuannya memisahkan kegagalan layar dari kegagalan alur tap, sehingga saat ada
masalah jelas mana yang perlu diperiksa.

Environment tersebut sudah dihapus setelah pengujian selesai, tapi polanya layak diulang untuk
peripheral berikutnya: satu file sumber terpisah, `build_src_filter` pada kedua environment,
lalu scan alamat I2C sebelum menggambar apa pun. Modul ini terdeteksi pada alamat `0x3C`.

## Keadaan layar

| Kondisi | Tampilan |
|---|---|
| Menunggu kartu | Animasi bongo cat, teks `Please tap` |
| Menunggu balasan Apps Script | Bongo cat menggebuk meja, teks `Checking` |
| UID terdaftar | `Welcome` dan nama pengunjung |
| UID tidak terdaftar | UID kartu dan teks `please register` |
| Modul RFID tidak membalas SPI | `RFID` dan `check wiring`, lalu perangkat berhenti |
| Kegagalan jaringan | Lihat bagian diagnosis di bawah |

Nama yang lebih dari sepuluh huruf otomatis dikecilkan ukurannya agar tidak terpotong pada
layar selebar 128 piksel.

## Animasi bongo cat

Frame animasi diambil dari [OLED-BongoCat-Revision](https://github.com/pedker/OLED-BongoCat-Revision),
firmware QMK untuk keyboard Satisfaction75. Data aslinya tidak dapat digunakan langsung karena
dua hal: QMK menyimpan bitmap per kolom sedangkan `drawBitmap()` Adafruit membacanya per baris,
dan layar tujuannya 128x32 sedangkan layar di sini 128x64.

Penyelesaiannya mentranspose data tersebut satu kali di komputer, lalu menyimpan hasilnya
sebagai array biasa di [logbook-firmware/src/bongo.h](../logbook-firmware/src/bongo.h). Lima
frame idle dan dua frame tap, total 3584 byte. Kucing menempati baris 0 sampai 31, teks di
baris 44 sampai 60.

Pemetaannya mengikuti keadaan sistem: frame idle berputar saat menunggu kartu, dan frame tap
bergantian selama request HTTPS berlangsung. Jadi kucing benar-benar menggebuk meja hanya
selama perangkat sedang bekerja.

Animasi tersebut dijalankan sebagai task FreeRTOS terpisah, karena request HTTPS memblokir
`loop()` selama beberapa detik. Task itu menghapus dirinya sendiri setelah frame terakhir
selesai. Menghapusnya dari luar dengan `vTaskDelete()` berisiko memotong transfer I2C di
tengah jalan dan menggantungkan bus sampai perangkat di-reset.

## Kendala 1 — WiFi menyambung ulang terus-menerus

**Gejala:** setelah satu tap gagal, perangkat masuk siklus menyambung ulang WiFi yang tidak
berhenti, dan tap berikutnya ikut gagal.

**Penyebab:** bug yang diperkenalkan saat menangani kegagalan DNS. Cabang error menurunkan
flag `wifiReady`, yang membuat `loop()` memanggil `connectWiFi()`. Fungsi itu memanggil
`WiFi.begin()` lagi, yang memutus asosiasi yang sedang sehat lalu menyambung dari nol. Satu
kegagalan kecil karena itu merobohkan koneksi, dan jeda selama penyambungan ulang membuat tap
berikutnya gagal pula.

**Fix:** baris tersebut dihapus. Putusnya WiFi tetap terdeteksi dengan benar karena `loop()`
sudah memeriksa `WiFi.status() != WL_CONNECTED` pada setiap putaran, dan itu sinyal yang
sebenarnya. Kegagalan pada satu request tidak lagi menyentuh koneksi.

## Kendala 2 — Hasil akhir berupa kode HTTP 302

**Gejala:** layar menampilkan `Error HTTP 302`, padahal 302 adalah redirect yang memang selalu
dikirim Apps Script dan sudah ditangani sejak Hari 3.

**Penyebab:** loop percobaan ulang selalu meminta alamat yang sama. Ketika GET ke URL redirect
ternyata dijawab 302 lagi, alamat baru memang tersimpan, tapi loop tetap mengetuk alamat lama
sampai kesempatannya habis lalu menyerah dengan kode 302.

**Fix:** ikuti rantai redirect dengan memakai alamat terbaru pada setiap hop. Request POST
tetap tidak pernah diulang, karena sekali kirim sudah menjalankan `doPost()` dan menulis ke
Sheet, sehingga mengulangnya akan menghasilkan baris ganda. Yang diulang hanya GET pengambil
hasilnya.

## Kendala 3 — Kegagalan tanpa petunjuk setelah Serial dihapus

**Gejala:** tap gagal dan layar hanya menampilkan pesan error generik. Tanpa Serial Monitor,
tidak ada cara membedakan masalah jaringan dari masalah perangkat.

**Penyebab:** memindahkan seluruh keluaran ke layar menghilangkan diagnostik yang sebelumnya
tersedia, dan pesan penggantinya terlalu umum untuk menunjukkan apa pun.

**Fix:** perangkat menguji dirinya sendiri saat gagal, lalu menampilkan kesimpulannya. Dua uji
independen dijalankan — resolusi nama melalui UDP, dan koneksi TCP ke alamat IP yang sudah
diketahui — sehingga empat kemungkinan dapat dipisahkan:

| Tampilan | DNS | TCP | Artinya |
|---|---|---|---|
| `No route` | jalan | jalan | Jaringan sehat, tapi Google khusus tidak terjangkau |
| `TCP fail` | jalan | gagal | Tidak dapat membuka koneksi TCP apa pun |
| `DNS block` | gagal | jalan | Ada internet, tapi resolver tidak menjawab |
| `Offline` | gagal | gagal | Tidak ada jalur keluar sama sekali |

Baris kedua menyesuaikan kasusnya: kode error HTTPClient beserta sisa RAM ketika DNS berhasil,
alamat resolver ketika DNS yang bermasalah.

Diagnosis inilah yang menyelesaikan kegagalan tap yang sempat berlangsung lama. Layar
menampilkan `DNS block`, yang berarti jalur internet ada tapi kueri DNS tidak pernah dijawab.
Setelah perangkat dipindahkan ke jaringan lain, tap langsung berfungsi kembali tanpa satu pun
perubahan kode. Tanpa pemisahan tersebut, penelusuran akan terus mengarah ke firmware.

## Hasil akhir

Perangkat berjalan tanpa komputer yang tersambung. Layar menyapa pengunjung terdaftar dengan
namanya, menolak kartu yang belum terdaftar sambil menampilkan UID-nya, dan menyebutkan sendiri
penyebabnya ketika jaringan bermasalah.

Pemakaian sumber daya setelah semua fitur di atas masuk: RAM 12,6 persen dan Flash 68,6 persen.
