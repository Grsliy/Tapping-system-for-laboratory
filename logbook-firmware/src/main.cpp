#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "secrets.h"
// File secrets.h tidak ada di repo (gitignored) -- salin dari secrets.h.example
// dan isi kredensial WiFi + URL Apps Script asli di situ sebelum build.

// ===== Pin MFRC522 (SPI custom pin, ESP32-C3 GPIO matrix fleksibel) =====
#define RFID_SCK_PIN  4
#define RFID_MISO_PIN 5
#define RFID_MOSI_PIN 6
#define RFID_CS_PIN   7
#define RFID_RST_PIN  10

// ===== Register & command MFRC522 =====
#define MFRC522_REG_COMMAND      0x01
#define MFRC522_REG_COMIEN       0x02
#define MFRC522_REG_COMIRQ       0x04
#define MFRC522_REG_ERROR        0x06
#define MFRC522_REG_FIFODATA     0x09
#define MFRC522_REG_FIFOLEVEL    0x0A
#define MFRC522_REG_CONTROL      0x0C
#define MFRC522_REG_BITFRAMING   0x0D
#define MFRC522_REG_MODE         0x11
#define MFRC522_REG_TXCONTROL    0x14
#define MFRC522_REG_TXASK        0x15
#define MFRC522_REG_TMODE        0x2A
#define MFRC522_REG_TPRESCALER   0x2B
#define MFRC522_REG_TRELOADL     0x2D
#define MFRC522_REG_TRELOADH     0x2C
#define MFRC522_REG_VERSION      0x37

#define PCD_IDLE       0x00
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F

#define PICC_REQIDL    0x26
#define PICC_ANTICOLL  0x93

#define MI_OK        0
#define MI_NOTAGERR  1
#define MI_ERR       2

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
    //
    // Catatan: tap sudah TERCATAT di Sheet begitu request POST pertama berhasil (302
    // diterima) -- bagian di bawah ini cuma buat AMBIL BALASANNYA, bukan mengulang aksinya.
    // Kalau ini gagal (mis. hiccup TLS sesaat), coba beberapa kali sebelum menyerah, supaya
    // device tidak salah lapor "gagal" padahal datanya sudah masuk.
    if (httpCode == 302) {
        String redirectUrl = response;
        Serial.print("Redirect ke: ");
        Serial.println(redirectUrl);

        const int MAX_RETRY = 3;
        for (int attempt = 1; attempt <= MAX_RETRY; attempt++) {
            httpCode = getContent(redirectUrl, response);
            Serial.print("Kode HTTP setelah redirect (GET), percobaan ");
            Serial.print(attempt);
            Serial.print(": ");
            Serial.println(httpCode);
            if (httpCode == 200) break;
            delay(500);
        }
    }

    if (httpCode == 200) {
        Serial.println(response);
    } else {
        Serial.println("Gagal, respons:");
        Serial.println(response);
    }

    return httpCode == 200;
}

// ===== Driver MFRC522 (porting dari versi STM32 HAL yang sudah diuji sebelumnya) =====

uint8_t MFRC522_ReadRegister(uint8_t reg) {
    uint8_t addr = ((reg << 1) & 0x7E) | 0x80;
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(RFID_CS_PIN, LOW);
    SPI.transfer(addr);
    uint8_t value = SPI.transfer(0x00);
    digitalWrite(RFID_CS_PIN, HIGH);
    SPI.endTransaction();
    return value;
}

void MFRC522_WriteRegister(uint8_t reg, uint8_t value) {
    uint8_t addr = (reg << 1) & 0x7E;
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(RFID_CS_PIN, LOW);
    SPI.transfer(addr);
    SPI.transfer(value);
    digitalWrite(RFID_CS_PIN, HIGH);
    SPI.endTransaction();
}

void MFRC522_SetBitMask(uint8_t reg, uint8_t mask) {
    uint8_t tmp = MFRC522_ReadRegister(reg);
    MFRC522_WriteRegister(reg, tmp | mask);
}

void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    uint8_t tmp = MFRC522_ReadRegister(reg);
    MFRC522_WriteRegister(reg, tmp & (~mask));
}

void MFRC522_AntennaOn() {
    uint8_t temp = MFRC522_ReadRegister(MFRC522_REG_TXCONTROL);
    if (!(temp & 0x03)) {
        MFRC522_SetBitMask(MFRC522_REG_TXCONTROL, 0x03);
    }
}

void MFRC522_Init() {
    pinMode(RFID_CS_PIN, OUTPUT);
    pinMode(RFID_RST_PIN, OUTPUT);
    digitalWrite(RFID_CS_PIN, HIGH);
    digitalWrite(RFID_RST_PIN, HIGH);
    delay(50);

    SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_CS_PIN);

    MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_RESETPHASE);
    delay(50);

    MFRC522_WriteRegister(MFRC522_REG_TMODE, 0x8D);
    MFRC522_WriteRegister(MFRC522_REG_TPRESCALER, 0x3E);
    MFRC522_WriteRegister(MFRC522_REG_TRELOADL, 30);
    MFRC522_WriteRegister(MFRC522_REG_TRELOADH, 0);
    MFRC522_WriteRegister(MFRC522_REG_TXASK, 0x40);
    MFRC522_WriteRegister(MFRC522_REG_MODE, 0x3D);

    MFRC522_AntennaOn();

    Serial.print("MFRC522 Version Reg: 0x");
    Serial.println(MFRC522_ReadRegister(MFRC522_REG_VERSION), HEX);
}

// Kirim command ke kartu lewat FIFO, ambil balasannya. Dipakai Request & Anticoll.
uint8_t MFRC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen) {
    uint8_t status = MI_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint32_t i;

    if (command == PCD_TRANSCEIVE) {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    MFRC522_WriteRegister(MFRC522_REG_COMIEN, irqEn | 0x80);
    MFRC522_ClearBitMask(MFRC522_REG_COMIRQ, 0x80);
    MFRC522_SetBitMask(MFRC522_REG_FIFOLEVEL, 0x80);
    MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_IDLE);

    for (i = 0; i < sendLen; i++) {
        MFRC522_WriteRegister(MFRC522_REG_FIFODATA, sendData[i]);
    }

    MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);
    if (command == PCD_TRANSCEIVE) {
        MFRC522_SetBitMask(MFRC522_REG_BITFRAMING, 0x80);
    }

    i = 2000;
    do {
        n = MFRC522_ReadRegister(MFRC522_REG_COMIRQ);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    MFRC522_ClearBitMask(MFRC522_REG_BITFRAMING, 0x80);

    if (i != 0) {
        if (!(MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B)) {
            status = MI_OK;
            if (n & irqEn & 0x01) {
                status = MI_NOTAGERR;
            }

            if (command == PCD_TRANSCEIVE) {
                n = MFRC522_ReadRegister(MFRC522_REG_FIFOLEVEL);
                lastBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;
                if (lastBits) {
                    *backLen = (n - 1) * 8 + lastBits;
                } else {
                    *backLen = n * 8;
                }

                if (n == 0) n = 1;
                if (n > 16) n = 16;

                for (i = 0; i < n; i++) {
                    backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFODATA);
                }
            }
        } else {
            status = MI_ERR;
        }
    }

    return status;
}

// Cek apakah ada kartu di area antena (REQA)
uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType) {
    uint8_t status;
    uint16_t backBits;

    MFRC522_WriteRegister(MFRC522_REG_BITFRAMING, 0x07);

    TagType[0] = reqMode;
    status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

    if ((status != MI_OK) || (backBits != 0x10)) {
        status = MI_ERR;
    }

    return status;
}

// Ambil UID kartu (anticollision cascade level 1)
uint8_t MFRC522_Anticoll(uint8_t *serNum) {
    uint8_t status;
    uint8_t i;
    uint8_t serNumCheck = 0;
    uint16_t unLen;

    MFRC522_WriteRegister(MFRC522_REG_BITFRAMING, 0x00);

    serNum[0] = PICC_ANTICOLL;
    serNum[1] = 0x20;
    status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

    if (status == MI_OK) {
        for (i = 0; i < 4; i++) {
            serNumCheck ^= serNum[i];
        }
        if (serNumCheck != serNum[4]) {
            status = MI_ERR;
        }
    }

    return status;
}

// Ubah 4 byte UID jadi string hex, mis. {0x5A,0xE4,0xC9,0x55} -> "5AE4C955"
String uidToString(uint8_t *serNum) {
    char buf[9];
    sprintf(buf, "%02X%02X%02X%02X", serNum[0], serNum[1], serNum[2], serNum[3]);
    return String(buf);
}

void setup() {
    Serial.begin(115200);
    delay(100);

    connectWiFi();
    MFRC522_Init();

    Serial.println("Siap. Tempelkan kartu...");
}

void loop() {
    if (!wifiReady) {
        connectWiFi();
        delay(2000);
        return;
    }

    uint8_t TagType[2];
    uint8_t serNum[5];

    if (MFRC522_Request(PICC_REQIDL, TagType) == MI_OK) {
        if (MFRC522_Anticoll(serNum) == MI_OK) {
            String uid = uidToString(serNum);
            Serial.print("Kartu terdeteksi, UID: ");
            Serial.println(uid);
            sendTap(uid);
            delay(1000); // jeda supaya tidak spam kalau kartu masih nempel
        }
    }

    delay(100);
}
