// Tes OLED sendirian, di luar alur tap kartu. Scan alamat I2C lalu tampilkan teks.
// Build cuma lewat env "oled-test" (lihat build_src_filter di platformio.ini).

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define OLED_SDA_PIN 0
#define OLED_SCL_PIN 1
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_ADDR    0x3C

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

void setup() {
    Serial.begin(115200);
    delay(2000); // beri waktu USB CDC enumerate sebelum print pertama

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    Serial.println("Scan alamat I2C...");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("  perangkat ditemukan di 0x");
            Serial.println(addr, HEX);
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  tidak ada perangkat I2C terdeteksi.");
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED gagal init.");
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.println("Please");
    display.println("tap");
    display.display();

    Serial.println("OLED OK.");
}

void loop() {
}
