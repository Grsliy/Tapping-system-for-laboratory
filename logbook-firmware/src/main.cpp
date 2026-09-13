#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "secrets.h"
// File secrets.h tidak ada di repo (gitignored) -- salin dari secrets.h.example
// dan isi kredensial WiFi + URL Apps Script asli di situ sebelum build.

// ===== Konfigurasi MFRC522 (diisi Fase 4, setelah modul RFID pengganti datang) =====
// TODO Fase 4: port kode MFRC522_Request/MFRC522_Anticoll dari versi STM32 HAL ke sini,
// pakai SPI.h bawaan Arduino (logika protokolnya sama, cuma API SPI-nya beda).

bool wifiReady = false;

void connectWiFi() {
    Serial.println("Menyambungkan ke WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // hindari WiFi "tidur" sebentar-sebentar, jaga koneksi tetap stabil
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Menyambungkan");
    int timeoutCount = 0;
    while (WiFi.status() != WL_CONNECTED && timeoutCount < 40) {
        delay(500);
        Serial.print(".");
        timeoutCount++;
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("GAGAL connect WiFi. Kode status: ");
        Serial.println(WiFi.status());
        return;
    }

    Serial.print("Terhubung! IP device: ");
    Serial.println(WiFi.localIP());
    wifiReady = true;
}

// Kirim UID (dummy dulu buat tes Hari 3, UID asli setelah Fase 4/RFID terpasang)
// ke Google Apps Script.
bool sendTap(const String &uid) {
    if (!wifiReady) {
        Serial.println("WiFi belum siap, tap dibatalkan.");
        return false;
    }

    WiFiClientSecure client;
    // Lewati validasi sertifikat -- Apps Script sudah HTTPS lewat domain Google yang
    // terpercaya, setInsecure() cukup buat kebutuhan ini (bukan aplikasi finansial/sensitif).
    client.setInsecure();

    HTTPClient http;
    http.begin(client, APPS_SCRIPT_URL);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"uid\":\"" + uid + "\"}";
    int httpCode = http.POST(payload);

    Serial.print("POST -> kode HTTP: ");
    Serial.println(httpCode);

    if (httpCode > 0) {
        Serial.println(http.getString());
    } else {
        Serial.print("Request gagal: ");
        Serial.println(http.errorToString(httpCode));
    }

    http.end();
    return httpCode == 200;
}

void setup() {
    Serial.begin(115200);
    delay(100);

    connectWiFi();

    if (wifiReady) {
        // Tes Hari 3: kirim UID dummy sekali begitu WiFi siap, pastikan tercatat di Sheet.
        Serial.println("Tes kirim UID dummy...");
        sendTap("DEADBEEF");
    }
}

void loop() {
    if (!wifiReady) {
        connectWiFi();
        delay(2000);
    }

    // TODO Fase 4: ganti trigger ini jadi hasil baca kartu MFRC522 (bukan dummy lagi).

    delay(100);
}
