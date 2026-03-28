#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_VEML7700.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <math.h>

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

const size_t WINDOW_SAMPLES = 60;
const unsigned long SAMPLE_INTERVAL_MS = WINDOW_SAMPLES * 1000UL;

float sampleBuffer[WINDOW_SAMPLES];
size_t sampleIndex = 0;
size_t sampleCount = 0;
float sampleSum = 0.0f;
unsigned long lastSample = 0;

float luxToDaylightScore(float lux) {
    const float maxLux = 10000.0f;
    if (lux <= 0.0f) return 0.0f;
    if (lux > maxLux) lux = maxLux;
    float normalized = log10f(lux + 1.0f) / log10f(maxLux + 1.0f);
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return normalized * 100.0f;
}

float rollingAverage() {
    if (sampleCount == 0) return 0.0f;
    return sampleSum / (float)sampleCount;
}

void formatFloat(float value, char* out, size_t outSize, uint8_t decimals) {
    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%.%uf", decimals);
    snprintf(out, outSize, fmt, value);
}

void drawScreen(float lux, float oneHourAverage) {
    char luxText[24];
    char avgText[24];
    dtostrf(lux, 0, 1, luxText);
    formatFloat(oneHourAverage, avgText, sizeof(avgText), 1);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold12pt7b);
        display.setCursor(0, 26);
        display.print("Lux");
        display.setCursor(0, 54);
        display.print(luxText);
        display.setCursor(0, 82);
        display.print("1h avg");
        display.setCursor(0, 110);
        display.print(avgText);
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

void addSample(float score) {
    if (sampleCount < WINDOW_SAMPLES) {
        sampleBuffer[sampleIndex] = score;
        sampleSum += score;
        sampleIndex = (sampleIndex + 1) % WINDOW_SAMPLES;
        sampleCount++;
        return;
    }
    sampleSum -= sampleBuffer[sampleIndex];
    sampleBuffer[sampleIndex] = score;
    sampleSum += score;
    sampleIndex = (sampleIndex + 1) % WINDOW_SAMPLES;
}

void sampleAndDisplay() {
    float lux = veml.readLux();
    float score = luxToDaylightScore(lux);
    addSample(score);
    float avg = rollingAverage();
    Serial.print("Lux: ");
    Serial.print(lux, 1);
    Serial.print("  Score: ");
    Serial.print(score, 1);
    Serial.print("  1h avg: ");
    Serial.println(avg, 1);
    drawScreen(lux, avg);
}

void debugBuffer() {
    Serial.print("Sample count: "); Serial.println(sampleCount);
    Serial.print("Next write index: "); Serial.println(sampleIndex);
    size_t oldest = (sampleIndex - sampleCount + WINDOW_SAMPLES) % WINDOW_SAMPLES;
    for (size_t i = 0; i < sampleCount; i++) {
        size_t idx = (oldest + i) % WINDOW_SAMPLES;
        Serial.print("Buffer["); Serial.print(idx); Serial.print("] = ");
        Serial.println(sampleBuffer[idx], 2);
    }
    Serial.print("Computed rolling average: "); Serial.println(rollingAverage(), 2);
    Serial.println("");
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
    sampleAndDisplay();
    lastSample = millis();
}

void loop() {
    while (millis() - lastSample >= SAMPLE_INTERVAL_MS) {
        lastSample += SAMPLE_INTERVAL_MS;
        sampleAndDisplay();
        debugBuffer();
    }
}

