#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_VEML7700.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#define EPD_BUSY 17
#define EPD_RST 16
#define EPD_DC 4
#define EPD_CS 5
#define EPD_SCK 18
#define EPD_MOSI 23
#define VEML_SDA 21
#define VEML_SCL 22

Adafruit_VEML7700 veml;
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

const unsigned long UPDATE_INTERVAL_MS = 10000;
unsigned long lastUpdate = 0;

void drawScreen(float lux) {
    char luxText[24];
    dtostrf(lux, 0, 1, luxText);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold12pt7b);
        display.setCursor(0, 40);
        display.print("Lux");
        display.setCursor(0, 80);
        display.print(luxText);
    } while (display.nextPage());
}

void drawError(const char* line1, const char* line2) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold12pt7b);
        display.setCursor(0, 30);
        display.println(line1);
        display.setCursor(0, 60);
        display.println(line2);
    } while (display.nextPage());
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Wire.begin(VEML_SDA, VEML_SCL);
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    display.init(115200);
    display.setRotation(0);
    if (!veml.begin(&Wire)) {
        Serial.println("VEML7700 sensor not found");
        drawError("Sensor", "not found");
        while (true) {
            delay(1000);
        }
    }
    float lux = veml.readLux();
    Serial.print("Lux: ");
    Serial.println(lux);
    drawScreen(lux);
    lastUpdate = millis();
}

void loop() {
    if (millis() - lastUpdate >= UPDATE_INTERVAL_MS) {
        float lux = veml.readLux();
        Serial.print("Lux: ");
        Serial.println(lux);
        drawScreen(lux);
        lastUpdate = millis();
    }
}

