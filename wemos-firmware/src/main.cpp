#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

// ===== Konfigurasi jaringan =====
// TODO Fase 2: isi kalau pakai WiFi hotspot/PSK biasa buat tes awal
const char *WIFI_SSID = "";
const char *WIFI_PASSWORD = "";

// TODO Fase 2: implementasi WPA2-Enterprise (eduroam) kalau tes hotspot sudah berhasil.
// ESP8266 Arduino core punya akses ke fungsi SDK wifi_station_set_wpa2_enterprise_auth()
// dkk lewat <wpa2_enterprise.h> -- perlu diverifikasi & diisi detailnya nanti pas sudah
// pegang board fisik, supaya bisa langsung dites, bukan cuma nebak dari dokumentasi.

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
    Serial.print("Menyambungkan ke WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
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
