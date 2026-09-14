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

// Kirim POST JSON ke url, balikin kode HTTP-nya lewat responseOut. Dipakai buat request
// pertama ke Apps Script (yang benar-benar menjalankan doPost di sisi Google).
int postJson(const String &url, const String &payload, String &responseOut) {
    WiFiClientSecure client;
    client.setInsecure(); // Apps Script sudah HTTPS domain Google yang terpercaya

    HTTPClient http;
    http.setTimeout(15000);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        responseOut = http.getString();
    }

    String location = http.getLocation();
    http.end();

    if (httpCode == 302) {
        responseOut = location; // sengaja dipakai ulang buat bawa URL redirect keluar
    }

    return httpCode;
}

// Ambil isi respons lewat GET biasa -- dipakai buat "menjemput" hasil dari URL redirect
// yang dikasih Apps Script. URL itu cuma nyimpen hasil yang SUDAH dieksekusi di request
// POST sebelumnya, jadi wajar cuma nerima GET (POST ke situ balas 405 Method Not Allowed).
int getContent(const String &url, String &responseOut) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(15000);
    http.begin(client, url);

    int httpCode = http.GET();
    if (httpCode > 0) {
        responseOut = http.getString();
    }

    http.end();
    return httpCode;
}

// Kirim UID (dummy dulu buat tes Hari 3, UID asli setelah Fase 4/RFID terpasang)
// ke Google Apps Script.
bool sendTap(const String &uid) {
    if (!wifiReady) {
        Serial.println("WiFi belum siap, tap dibatalkan.");
        return false;
    }

    String payload = "{\"uid\":\"" + uid + "\"}";
    String response;

    Serial.println("POST ke Apps Script...");
    int httpCode = postJson(APPS_SCRIPT_URL, payload, response);
    Serial.print("Kode HTTP pertama: ");
    Serial.println(httpCode);

    // Apps Script Web App SELALU balas 302 dulu (redirect ke domain googleusercontent.com
    // tempat script-nya benar-benar jalan). HTTPClient ESP32 punya bug kalau redirect
    // di-follow otomatis (salah kirim Content-Length, bikin Google balas 400) -- makanya
    // redirect-nya kita tangani manual: request baru bersih ke URL hasil redirect.
    if (httpCode == 302) {
        String redirectUrl = response;
        Serial.print("Redirect ke: ");
        Serial.println(redirectUrl);
        httpCode = getContent(redirectUrl, response);
        Serial.print("Kode HTTP setelah redirect (GET): ");
        Serial.println(httpCode);
    }

    if (httpCode == 200) {
        Serial.println(response);
    } else {
        Serial.println("Gagal, respons:");
        Serial.println(response);
    }

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
