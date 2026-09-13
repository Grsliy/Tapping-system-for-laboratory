# Hari 1 — Setup Google Sheet + Apps Script

Langkah kerja detail untuk Hari 1 dari milestone di [Sistem-Logbook-Pengunjung-RFID](../Dokumen/Sistem-Logbook-Pengunjung-RFID.md#milestone-5-hari). Tidak butuh hardware sama sekali, tidak perlu menunggu apa pun.

## Goals hari ini

- [x] Google Sheet dengan 2 tab siap (`Pengunjung`, `Log_Kunjungan`).
- [x] Apps Script `doPost` ditulis dan tersimpan.
- [x] Web App sudah di-deploy, URL-nya tercatat.
- [x] Tes dengan UID dummy berhasil, termasuk verifikasi baris baru benar-benar muncul di tab `Log_Kunjungan`.

## Langkah 1 — Buat Google Sheet

1. Buka [sheets.google.com](https://sheets.google.com), buat spreadsheet baru.
2. Beri nama, misal "Database Logbook Pengunjung Lab".
3. Rename tab pertama (default "Sheet1") jadi `Pengunjung`.
4. Isi baris header (baris 1) di tab `Pengunjung`:
   ```
   uid_kartu | Nama | Institusi | Tujuan | Tanggal_daftar | Status
   ```
5. Buat tab baru (klik tanda tambah di pojok kiri bawah), beri nama `Log_Kunjungan`.
6. Isi baris header di tab `Log_Kunjungan` (Nama dan Tujuan ikut disalin ke tiap baris log,
   supaya riwayat langsung kebaca tanpa perlu cross-reference manual ke tab `Pengunjung`):
   ```
   uid_kartu | Nama | Tujuan | Waktu_Tap
   ```

## Langkah 2 — Setting kolom sebelum isi data

- **Format kolom `uid_kartu` jadi Plain Text**, di kedua tab (`Pengunjung` dan
  `Log_Kunjungan`) — pilih kolomnya, **Format → Number → Plain text**. Mencegah Sheets
  menghilangkan angka nol di depan atau mengubah ke notasi ilmiah kalau UID berupa angka
  panjang.
- **Data validation dropdown buat kolom `Status`** di tab `Pengunjung` — select kolom dari
  baris 2 ke bawah, **Data → Data validation**, criteria **Dropdown**, isi opsi `aktif` dan
  `nonaktif`. Mencegah typo yang bikin kartu valid ditolak diam-diam (skrip mencocokkan
  string `"aktif"` secara persis).
- **Freeze baris header** (opsional) — klik kanan angka baris 1 → **View → Freeze → 1 row**.

## Langkah 3 — Isi data uji di tab Pengunjung

Isi satu baris data uji supaya bisa langsung dites:
```
DEADBEEF | Budi Testing | UGM | Uji coba sistem | 2026-09-13 | aktif
```

## Langkah 4 — Buka Apps Script Editor

Di menu Sheet: **Extensions → Apps Script** (atau **Ekstensi → Apps Script**). Editor ini otomatis terhubung ke Sheet yang sedang dibuka, tidak perlu ID spreadsheet manual.

## Langkah 5 — Tulis kode doPost

Hapus isi default `Code.gs`, ganti dengan isi [logbook-appsscript/Code.gs](../logbook-appsscript/Code.gs) di repo ini (salinan lokal kode yang sama):

```javascript
function doPost(e) {
  var uid = "";

  if (e.postData && e.postData.type === "application/json") {
    var body = JSON.parse(e.postData.contents);
    uid = body.uid;
  } else {
    uid = e.parameter.uid;
  }

  if (!uid) {
    return ContentService.createTextOutput(JSON.stringify({status: "ERROR", message: "UID tidak ada"}))
      .setMimeType(ContentService.MimeType.JSON);
  }

  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sheetPengunjung = ss.getSheetByName("Pengunjung");
  var data = sheetPengunjung.getDataRange().getValues();

  var found = null;
  for (var i = 1; i < data.length; i++) {
    if (String(data[i][0]) === String(uid) && data[i][5] === "aktif") {
      found = data[i];
      break;
    }
  }

  if (!found) {
    return ContentService.createTextOutput(JSON.stringify({status: "REJECTED", message: "UID tidak terdaftar"}))
      .setMimeType(ContentService.MimeType.JSON);
  }

  var sheetLog = ss.getSheetByName("Log_Kunjungan");
  var waktu = new Date();
  sheetLog.appendRow([uid, found[1], found[3], waktu]);

  return ContentService.createTextOutput(JSON.stringify({status: "OK", nama: found[1], waktu: waktu.toString()}))
    .setMimeType(ContentService.MimeType.JSON);
}
```

`found[1]` itu Nama, `found[3]` itu Tujuan — sesuai urutan kolom di tab `Pengunjung`.

Simpan dengan ikon disket atau `Ctrl+S`.

## Langkah 6 — Deploy sebagai Web App

1. Klik **Deploy → New deployment** (pojok kanan atas).
2. Klik ikon gerigi di samping "Select type", pilih **Web app**.
3. Isi:
   - **Execute as:** Me
   - **Who has access:** Anyone (wajib, supaya ESP32 bisa akses tanpa login)
4. Klik **Deploy**.
5. Saat muncul prompt izin, klik **Authorize access**, pilih akun Google, lalu **Advanced → Go to (nama project) (unsafe)** kalau muncul peringatan (wajar untuk script yang belum dipublikasikan resmi ke Google), lalu **Allow**.
6. Setelah deploy selesai, copy **Web app URL**-nya (format `https://script.google.com/macros/s/xxxxx/exec`). Ini yang dipakai ESP32 nanti.

**Kalau URL-nya kelewat/tidak sempat di-copy:** buka lagi **Deploy → Manage deployments**,
klik deployment yang ada, URL-nya muncul lagi di situ.

**Kalau kode diedit setelah deploy pertama:** perubahan tidak otomatis live — harus
**Deploy → Manage deployments → ikon pensil (Edit) → New version** dulu.

## Langkah 7 — Tes dengan PowerShell

```powershell
$url = "PASTE_URL_WEB_APP_DI_SINI"

# Tes 1: UID yang terdaftar (harus balas OK)
Invoke-RestMethod -Uri $url -Method Post -Body (@{uid="DEADBEEF"} | ConvertTo-Json) -ContentType "application/json"

# Tes 2: UID yang tidak ada (harus balas REJECTED)
Invoke-RestMethod -Uri $url -Method Post -Body (@{uid="UNKNOWN123"} | ConvertTo-Json) -ContentType "application/json"
```

Hasil yang diharapkan:

```
status nama         waktu
------ ----         -----
OK     Budi Testing ...
```

```
status   message
------   -------
REJECTED UID tidak terdaftar
```

**Sudah diverifikasi jalan (13 Sept 2026)** — kedua tes membalas persis seperti di atas.

## Langkah 8 — Verifikasi tercatat di Sheet

Buka tab `Log_Kunjungan` secara langsung di Sheet, pastikan baris baru dengan `uid_kartu`,
Nama, Tujuan, dan `Waktu_Tap` benar-benar muncul di sana. Balasan JSON `OK` cuma membuktikan
kodenya jalan tanpa error, langkah ini yang memastikan datanya benar-benar tersimpan.

**Hari 1 selesai** — lanjut ke Hari 2 (Google Form pendaftaran).
