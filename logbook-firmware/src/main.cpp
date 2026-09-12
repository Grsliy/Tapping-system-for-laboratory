#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>

#include "secrets.h"
// File secrets.h tidak ada di repo (gitignored) -- salin dari secrets.h.example
// dan isi kredensial eduroam asli di situ sebelum build.

// ===== Konfigurasi UDP Discovery =====
const uint16_t DISCOVERY_PORT = 5001;
const char *DISCOVERY_PREFIX = "LOGBOOK_SERVER|";

WiFiUDP udp;
String serverIP = "";
uint16_t serverPort = 5000;
bool wifiReady = false;
unsigned long lastStatusPrint = 0;

// ===== Konfigurasi MFRC522 (diisi Fase 4, setelah modul RFID pengganti datang) =====
// TODO Fase 4: port kode MFRC522_Request/MFRC522_Anticoll dari versi STM32 HAL ke sini,
// pakai SPI.h bawaan Arduino (logika protokolnya sama, cuma API SPI-nya beda).

void startWiFi() {
    Serial.println("Menyambungkan ke eduroam (WPA2-Enterprise)...");

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);

    // Matikan WiFi power-save mode -- kalau aktif, ESP32 sempat "tidur" sebentar-sebentar
    // dan bisa melewatkan paket 4-way handshake dari AP, bikin HANDSHAKE_TIMEOUT (reason 204).
    WiFi.setSleep(false);

    // API tingkat tinggi bawaan WiFiSTA -- bungkus semua setup enterprise (identity,
    // username, password) jadi satu pemanggilan, sesuai contoh resmi Espressif
    // (WiFiClientEnterprise.ino) supaya urutan internal-nya benar.
    WiFi.begin(EDUROAM_SSID, WPA2_AUTH_PEAP, EDUROAM_IDENTITY, EDUROAM_USERNAME, EDUROAM_PASSWORD);

    // Tidak nunggu blocking di sini -- driver WiFi ESP-IDF auto-retry sendiri di
    // background ("WiFi Reconnect Running" di log). Status dipantau terus dari loop(),
    // supaya kalau percobaan pertama gagal tapi percobaan berikutnya (otomatis) berhasil,
    // tetap ketangkep, bukan cuma nyerah setelah satu window 30 detik.
}

// Dipanggil terus tiap loop -- cek status WiFi, print perkembangan tiap 5 detik supaya
// tidak membanjiri Serial Monitor, dan aktifkan UDP begitu pertama kali berhasil connect.
void monitorWiFi() {
    if (wifiReady) {
        return;  // sudah connect sebelumnya, tidak perlu dicek ulang tiap loop
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Terhubung! IP device: ");
        Serial.println(WiFi.localIP());
        udp.begin(DISCOVERY_PORT);
        wifiReady = true;
        return;
    }

    if (millis() - lastStatusPrint >= 5000) {
        lastStatusPrint = millis();
        Serial.print("[");
        Serial.print(millis() / 1000);
        Serial.print("s] Masih menunggu koneksi eduroam... status: ");
        Serial.println(WiFi.status());
    }
}

// Dengarkan broadcast UDP dari server, update serverIP kalau ada pesan baru masuk
void checkServerDiscovery() {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
        return;
    }

    char buffer[64] = {0};
    int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return;
    }
    buffer[len] = '\0';

    String message = String(buffer);
    if (!message.startsWith(DISCOVERY_PREFIX)) {
        return;
    }

    // Format: LOGBOOK_SERVER|<ip>|<port>
    int firstSep = message.indexOf('|');
    int secondSep = message.indexOf('|', firstSep + 1);
    if (firstSep == -1 || secondSep == -1) {
        return;
    }

    String newIP = message.substring(firstSep + 1, secondSep);
    uint16_t newPort = message.substring(secondSep + 1).toInt();

    if (newIP != serverIP) {
        serverIP = newIP;
        serverPort = newPort;
        Serial.print("Server ditemukan: ");
        Serial.print(serverIP);
        Serial.print(":");
        Serial.println(serverPort);
    }
}

// Kirim UID (dummy dulu di Fase 2, UID asli setelah Fase 4) ke endpoint /tap
bool sendTap(const String &uid) {
    if (serverIP == "") {
        Serial.println("Server belum ditemukan lewat discovery, tap dibatalkan.");
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    String url = "http://" + serverIP + ":" + String(serverPort) + "/tap";

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"uid\":\"" + uid + "\"}";
    int httpCode = http.POST(payload);

    Serial.print("POST ");
    Serial.print(url);
    Serial.print(" -> ");
    Serial.println(httpCode);

    if (httpCode > 0) {
        Serial.println(http.getString());
    }

    http.end();
    return httpCode == 200;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    startWiFi();
}

void loop() {
    monitorWiFi();

    if (wifiReady) {
        checkServerDiscovery();
    }

    // TODO Fase 2: ganti trigger ini jadi tombol/interval tes manual dulu,
    // TODO Fase 4: ganti jadi hasil baca kartu MFRC522 (bukan dummy lagi).

    delay(100);
}
