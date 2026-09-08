#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include "esp_wpa2.h"

#include "secrets.h"
// File secrets.h tidak ada di repo (gitignored) -- salin dari secrets.h.example
// dan isi kredensial eduroam asli di situ sebelum build.

// ===== Konfigurasi UDP Discovery =====
const uint16_t DISCOVERY_PORT = 5001;
const char *DISCOVERY_PREFIX = "LOGBOOK_SERVER|";

WiFiUDP udp;
String serverIP = "";
uint16_t serverPort = 5000;

// ===== Konfigurasi MFRC522 (diisi Fase 4, setelah modul RFID pengganti datang) =====
// TODO Fase 4: port kode MFRC522_Request/MFRC522_Anticoll dari versi STM32 HAL ke sini,
// pakai SPI.h bawaan Arduino (logika protokolnya sama, cuma API SPI-nya beda).

void connectWiFi() {
    Serial.println("Menyambungkan ke eduroam (WPA2-Enterprise)...");

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);

    // Bersihkan dulu kredensial enterprise lama sebelum set yang baru
    esp_wifi_sta_wpa2_ent_clear_identity();
    esp_wifi_sta_wpa2_ent_clear_username();
    esp_wifi_sta_wpa2_ent_clear_password();
    esp_wifi_sta_wpa2_ent_clear_ca_cert();

    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EDUROAM_IDENTITY, strlen(EDUROAM_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EDUROAM_USERNAME, strlen(EDUROAM_USERNAME));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EDUROAM_PASSWORD, strlen(EDUROAM_PASSWORD));

    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(EDUROAM_SSID);

    Serial.print("Menyambungkan");
    int timeoutCount = 0;
    while (WiFi.status() != WL_CONNECTED && timeoutCount < 60) {
        delay(500);
        Serial.print(".");
        timeoutCount++;
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("GAGAL connect ke eduroam (timeout 30 detik). Kode status WiFi.status(): ");
        Serial.println(WiFi.status());
        Serial.println("Arti kode: 0=IDLE 1=NO_SSID 3=CONNECTED 4=CONNECT_FAILED 5=CONNECTION_LOST 6=DISCONNECTED");
        return;
    }

    Serial.print("Terhubung, IP device: ");
    Serial.println(WiFi.localIP());

    udp.begin(DISCOVERY_PORT);
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
    connectWiFi();
}

void loop() {
    checkServerDiscovery();

    // TODO Fase 2: ganti trigger ini jadi tombol/interval tes manual dulu,
    // TODO Fase 4: ganti jadi hasil baca kartu MFRC522 (bukan dummy lagi).

    delay(100);
}
