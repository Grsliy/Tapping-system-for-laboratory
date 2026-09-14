#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_SSD1306.h>
#include <esp_netif.h>

#include "bongo.h"

#include "secrets.h"
// File secrets.h tidak ada di repo (gitignored) -- salin dari secrets.h.example
// dan isi kredensial WiFi + URL Apps Script asli di situ sebelum build.

// ===== Pin MFRC522 (SPI custom pin, ESP32-C3 GPIO) =====
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

#define MI_OK   0
#define MI_ERR  2

// MFRC522_ToCard bisa menyalin sampai 16 byte ke buffer balasan, jadi buffer pemanggil
// harus sebesar ini walau respons normalnya cuma 2-5 byte.
#define MFRC522_BUF_SIZE 16

// ===== OLED SSD1306 (I2C) =====
#define OLED_SDA_PIN 0
#define OLED_SCL_PIN 1
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_ADDR    0x3C

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

IPAddress dhcpDns;
bool wifiReady = false;
bool oledReady = false;

void oledInit() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
}

void oledShow(const String &line1, uint8_t size1, const String &line2, uint8_t size2) {
    if (!oledReady) return;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, (OLED_HEIGHT - (size1 + size2) * 8) / 2);
    display.setTextSize(size1);
    display.println(line1);
    display.setTextSize(size2);
    display.println(line2);
    display.display();
}

// Satu frame kucing di paruh atas layar, teks di bawahnya.
void oledCat(const unsigned char *frame, const char *caption, int16_t captionX) {
    if (!oledReady) return;

    display.clearDisplay();
    display.drawBitmap(0, 0, frame, BONGO_WIDTH, BONGO_HEIGHT, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(captionX, 44);
    display.print(caption);
    display.display();
}

void oledIdle(uint8_t frame = 0) {
    oledCat(bongoIdle[frame % BONGO_IDLE_FRAMES], "Please tap", 4);
}

// Dipanggil tiap putaran loop() saat tidak ada kartu; frame baru digambar tiap 400 ms.
void oledIdleAnimate() {
    static uint32_t lastFrame = 0;
    static uint8_t frame = 0;

    if (millis() - lastFrame < 220) return;

    lastFrame = millis();
    frame = (frame + 1) % BONGO_IDLE_FRAMES;
    oledIdle(frame);
}

// Animasi titik saat menunggu balasan Apps Script, dijalankan sebagai task terpisah
// karena request HTTPS memblokir loop() selama beberapa detik.
volatile bool loadingRun = false;
volatile bool loadingDone = true;

void loadingTask(void *param) {
    uint8_t frame = 0;

    while (loadingRun) {
        oledCat(bongoTap[frame], "Checking", 16);
        frame = (frame + 1) % BONGO_TAP_FRAMES;
        for (int i = 0; i < 3 && loadingRun; i++) {
            vTaskDelay(pdMS_TO_TICKS(50)); // dipecah kecil supaya loadingStop() cepat
        }
    }

    loadingDone = true;
    vTaskDelete(NULL);
}

void loadingStart() {
    if (!loadingDone) return;
    loadingRun = true;
    loadingDone = false;
    xTaskCreate(loadingTask, "loading", 4096, NULL, 1, NULL);
}

// Task menghapus dirinya sendiri setelah frame terakhir selesai. Menghapusnya dari luar
// berisiko memotong transfer I2C di tengah jalan dan menggantungkan bus.
void loadingStop() {
    loadingRun = false;
    while (!loadingDone) delay(10);
}

// Cadangan kalau DHCP tidak memberi alamat DNS sama sekali. Jaringan yang memblokir DNS
// ke luar tetap harus memakai resolver sendiri, jadi ini tidak dipasang kalau DHCP
// sudah memberi alamat. Lewat esp_netif, bukan WiFi.config(), supaya IP tetap dari DHCP.
void setPublicDns() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) return;

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;

    dns.ip.u_addr.ip4.addr = (uint32_t)IPAddress(8, 8, 8, 8);
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);

    dns.ip.u_addr.ip4.addr = (uint32_t)IPAddress(1, 1, 1, 1);
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int timeoutCount = 0;
    while (WiFi.status() != WL_CONNECTED && timeoutCount < 40) {
        delay(500);
        timeoutCount++;
    }

    wifiReady = (WiFi.status() == WL_CONNECTED);
    if (wifiReady) {
        dhcpDns = WiFi.dnsIP();
        if (dhcpDns == IPAddress(0, 0, 0, 0)) setPublicDns();
    }
}

// payload kosong berarti GET, selain itu POST JSON. Balasan 302 mengisi responseOut
// dengan URL redirect-nya, bukan body.
int httpRequest(const String &url, const String &payload, String &responseOut) {
    WiFiClientSecure client;
    client.setInsecure(); // Apps Script sudah HTTPS domain Google

    HTTPClient http;
    http.setTimeout(15000);
    http.begin(client, url);

    int httpCode;
    if (payload.isEmpty()) {
        httpCode = http.GET();
    } else {
        http.addHeader("Content-Type", "application/json");
        httpCode = http.POST(payload);
    }

    if (httpCode == 302) {
        responseOut = http.getLocation();
    } else if (httpCode > 0) {
        responseOut = http.getString();
    }

    http.end();
    return httpCode;
}

// Ambil nilai string satu field dari balasan JSON Apps Script, mis. "nama" atau "status".
String jsonField(const String &json, const String &key) {
    int k = json.indexOf("\"" + key + "\"");
    if (k < 0) return "";

    int colon = json.indexOf(':', k);
    if (colon < 0) return "";

    int q1 = json.indexOf('"', colon);
    int q2 = (q1 < 0) ? -1 : json.indexOf('"', q1 + 1);
    if (q1 < 0 || q2 < 0) return "";

    return json.substring(q1 + 1, q2);
}

// POST menjalankan doPost() di sisi Google, GET ke URL redirect-nya yang mengambil hasil.
int sendTap(const String &uid, String &responseOut) {
    int httpCode = httpRequest(APPS_SCRIPT_URL, "{\"uid\":\"" + uid + "\"}", responseOut);

    // POST-nya tidak pernah diulang: sekali kirim sudah menjalankan doPost() dan menulis
    // ke Sheet, mengulangnya akan menghasilkan baris ganda. Yang diulang hanya GET-nya.
    for (int attempt = 0; attempt < 4 && httpCode != 200; attempt++) {
        String next = responseOut; // diisi Location saat 302, tetap saat gagal
        if (next.isEmpty()) break;

        if (httpCode != 302) delay(500); // bukan redirect, berarti kegagalan sesaat
        httpCode = httpRequest(next, "", responseOut);
    }

    return httpCode;
}

// ===== Driver MFRC522 =====

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
    MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) | mask);
}

void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) & (~mask));
}

void MFRC522_AntennaOn() {
    if (!(MFRC522_ReadRegister(MFRC522_REG_TXCONTROL) & 0x03)) {
        MFRC522_SetBitMask(MFRC522_REG_TXCONTROL, 0x03);
    }
}

bool MFRC522_Init() {
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

    // 0x00 atau 0xFF berarti modul tidak membalas SPI sama sekali
    uint8_t version = MFRC522_ReadRegister(MFRC522_REG_VERSION);
    return version != 0x00 && version != 0xFF;
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

    if (i != 0 && !(MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B)) {
        status = (n & irqEn & 0x01) ? MI_ERR : MI_OK;

        if (command == PCD_TRANSCEIVE) {
            n = MFRC522_ReadRegister(MFRC522_REG_FIFOLEVEL);
            lastBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;
            *backLen = lastBits ? (n - 1) * 8 + lastBits : n * 8;

            if (n == 0) n = 1;
            if (n > MFRC522_BUF_SIZE) n = MFRC522_BUF_SIZE;

            for (i = 0; i < n; i++) {
                backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFODATA);
            }
        }
    }

    return status;
}

// Cek apakah ada kartu di area antena (REQA)
uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType) {
    uint16_t backBits;

    MFRC522_WriteRegister(MFRC522_REG_BITFRAMING, 0x07);

    TagType[0] = reqMode;
    uint8_t status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

    return (status == MI_OK && backBits == 0x10) ? MI_OK : MI_ERR;
}

// Ambil UID kartu (anticollision cascade level 1)
uint8_t MFRC522_Anticoll(uint8_t *serNum) {
    uint8_t serNumCheck = 0;
    uint16_t unLen;

    MFRC522_WriteRegister(MFRC522_REG_BITFRAMING, 0x00);

    serNum[0] = PICC_ANTICOLL;
    serNum[1] = 0x20;
    uint8_t status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

    if (status == MI_OK) {
        for (uint8_t i = 0; i < 4; i++) {
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
    oledInit();
    oledShow("WiFi", 2, "connecting...", 1);
    connectWiFi();

    if (!MFRC522_Init()) {
        oledShow("RFID", 2, "check wiring", 1);
        while (true) delay(1000);
    }

    oledIdle();
}

void loop() {
    if (!wifiReady || WiFi.status() != WL_CONNECTED) {
        oledShow("No WiFi", 2, "reconnecting...", 1);
        connectWiFi();
        delay(2000);
        if (wifiReady) oledIdle();
        return;
    }

    uint8_t TagType[MFRC522_BUF_SIZE];
    uint8_t serNum[MFRC522_BUF_SIZE];

    if (MFRC522_Request(PICC_REQIDL, TagType) == MI_OK && MFRC522_Anticoll(serNum) == MI_OK) {
        String uid = uidToString(serNum);
        String response;

        loadingStart();
        int httpCode = sendTap(uid, response);
        loadingStop();

        String status = (httpCode == 200) ? jsonField(response, "status") : "";

        if (status == "OK") {
            String nama = jsonField(response, "nama");
            oledShow("Welcome", 2, nama, nama.length() > 10 ? 1 : 2);
        } else if (status == "REJECTED") {
            oledShow(uid, 2, "please register", 1);
        } else if (httpCode <= 0) {
            // pisahkan tiga sebab: nama gagal diterjemahkan, DNS diblokir walau internet
            // jalan, atau memang tidak ada jalur internet sama sekali
            IPAddress resolved;
            bool dnsOk = WiFi.hostByName("script.google.com", resolved);

            WiFiClient probe;
            bool netOk = probe.connect(IPAddress(1, 1, 1, 1), 443, 3000);
            probe.stop();

            // baris kedua menyesuaikan: kode error kalau DNS beres, alamat resolver
            // kalau DNS-nya yang bermasalah
            // dnsOk lewat UDP, netOk lewat TCP -- keduanya gagal bersamaan berarti jaringan,
            // hanya TCP yang gagal berarti socket habis atau TCP keluar diblokir
            const char *reason;
            if (!dnsOk) reason = netOk ? "DNS block" : "Offline";
            else        reason = netOk ? "No route" : "TCP fail";
            String detail = dnsOk ? "code " + String(httpCode) + " ram " +
                                        String(ESP.getFreeHeap() / 1024) + "k"
                                  : "dns " + dhcpDns.toString();
            oledShow(reason, 2, detail, 1);
        } else {
            oledShow("Error", 2, "HTTP " + String(httpCode), 1);
        }

        delay(7000); // tahan pesan di layar, sekaligus jeda kalau kartu masih nempel
        oledIdle();
    } else {
        oledIdleAnimate();
    }

    delay(100);
}
