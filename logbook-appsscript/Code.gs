/**
 * Backend Google Apps Script untuk Sistem Logbook Pengunjung Lab.
 *
 * Ditulis di Apps Script Editor (Extensions -> Apps Script) dari Google Sheet
 * "Database Logbook Pengunjung Lab". File ini salinan lokal untuk riwayat/backup --
 * kalau ada perubahan, salin balik ke Apps Script Editor lalu buat versi deployment baru
 * (Deploy -> Manage deployments -> Edit -> New version).
 *
 * Skema tab "Pengunjung": uid_kartu | Nama | Institusi | Tujuan | Tanggal_daftar | Status
 * Skema tab "Log_Kunjungan": uid_kartu | Nama | Tujuan | Waktu_Tap
 */

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

  // Cegah duplikat -- Google kadang eksekusi ulang request yang sama di level
  // infrastrukturnya sendiri (retry internal), bukan berarti device kirim dua kali.
  // Kalau UID yang sama sudah tercatat dalam 10 detik terakhir, anggap request ini
  // duplikat dari eksekusi yang sama, jangan dicatat ulang.
  var DEDUP_WINDOW_MS = 10000;
  var logData = sheetLog.getDataRange().getValues();
  for (var j = logData.length - 1; j >= 1; j--) {
    if (String(logData[j][0]) !== String(uid)) continue;
    var lastWaktu = new Date(logData[j][3]);
    if (waktu.getTime() - lastWaktu.getTime() < DEDUP_WINDOW_MS) {
      return ContentService.createTextOutput(JSON.stringify({status: "OK", nama: found[1], waktu: waktu.toString(), note: "duplikat diabaikan"}))
        .setMimeType(ContentService.MimeType.JSON);
    }
    break; // cuma cek kemunculan UID ini yang paling akhir, tidak perlu scan semua baris
  }

  sheetLog.appendRow([uid, found[1], found[3], waktu]);

  return ContentService.createTextOutput(JSON.stringify({status: "OK", nama: found[1], waktu: waktu.toString()}))
    .setMimeType(ContentService.MimeType.JSON);
}
