#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_VEML7700.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <math.h>
#include "SafeSerial.h"

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
const unsigned long MINUTE_INTERVAL_MS = 60000UL;
const unsigned long SUBSAMPLE_INTERVAL_MS = 5000UL;
float sampleBuffer[WINDOW_SAMPLES];
size_t sampleIndex = 0;
size_t sampleCount = 0;
float sampleSum = 0.0f;
float subSampleSum = 0.0f;
size_t subSampleCount = 0;
unsigned long lastSubSample = 0;
unsigned long lastMinute = 0;

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
    int luxInt = (int)roundf(lux);
    int avgInt = (int)roundf(oneHourAverage);
    char luxText[16];
    char avgText[16];
    if (luxInt > 9999) {
        snprintf(luxText, sizeof(luxText), "max");
    } else {
        snprintf(luxText, sizeof(luxText), "%d", luxInt);
    }
    snprintf(avgText, sizeof(avgText), "%d", avgInt);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        int screenW = display.width();
        int halfW = screenW / 2;
        display.setFont(&FreeMonoBold12pt7b);
        const char* label1 = "Lux";
        display.getTextBounds(label1, 0, 0, &tbx, &tby, &tbw, &tbh);
        int x1 = (halfW - tbw) / 2;
        int yLabel = 28;
        display.setCursor(x1, yLabel);
        display.print(label1);
        const char* label2 = "Std";
        display.getTextBounds(label2, 0, 0, &tbx, &tby, &tbw, &tbh);
        int x2 = halfW + (halfW - tbw) / 2;
        display.setCursor(x2, yLabel);
        display.print(label2);
        display.setFont(&FreeMonoBold18pt7b);
        int yLux = 90;
        int yAvg = 120;
        display.getTextBounds(luxText, 0, 0, &tbx, &tby, &tbw, &tbh);
        x1 = (halfW - tbw) / 2;
        display.setCursor(x1, yLux);
        display.print(luxText);
        display.setFont(&FreeMonoBold24pt7b);
        display.getTextBounds(avgText, 0, 0, &tbx, &tby, &tbw, &tbh);
        x2 = halfW + (halfW - tbw) / 2;
        display.setCursor(x2, yAvg);
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

void collectSubSample() {
    float lux = veml.readLux();
    subSampleSum += lux;
    subSampleCount++;
    SAFE_SERIAL.print("Sub-sample lux: ");
    SAFE_SERIAL.println(lux, 1);
}

void finalizeMinute() {
    float avgLux = (subSampleCount > 0) ? (subSampleSum / (float)subSampleCount) : 0.0f;
    float minuteScore = luxToDaylightScore(avgLux);
    subSampleSum = 0.0f;
    subSampleCount = 0;
    addSample(minuteScore);
    float avg = rollingAverage();
    SAFE_SERIAL.print("Minute avg lux: ");
    SAFE_SERIAL.print(avgLux, 1);
    SAFE_SERIAL.print("  score: ");
    SAFE_SERIAL.print(minuteScore, 1);
    SAFE_SERIAL.print("  1h avg: ");
    SAFE_SERIAL.println(avg, 1);
    drawScreen(avgLux, avg);
}

void debugBuffer() {
    SAFE_SERIAL.print("Sample count: ");
    SAFE_SERIAL.println(sampleCount);
    SAFE_SERIAL.print("Next write index: ");
    SAFE_SERIAL.println(sampleIndex);
    size_t oldest = (sampleIndex - sampleCount + WINDOW_SAMPLES) % WINDOW_SAMPLES;
    for (size_t i = 0; i < sampleCount; i++) {
        size_t idx = (oldest + i) % WINDOW_SAMPLES;
        SAFE_SERIAL.print("Buffer[");
        SAFE_SERIAL.print(idx);
        SAFE_SERIAL.print("] = ");
        SAFE_SERIAL.println(sampleBuffer[idx], 2);
    }
    SAFE_SERIAL.print("Computed rolling average: ");
    SAFE_SERIAL.println(rollingAverage(), 2);
}

void setup() {
    SAFE_SERIAL.begin(115200);
    delay(500);
    Wire.begin(VEML_SDA, VEML_SCL);
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    display.init(115200);
    display.setRotation(0);
    if (!veml.begin(&Wire)) {
        SAFE_SERIAL.println("VEML7700 sensor not found");
        drawError("Sensor", "not found");
        while (true) {
            delay(1000);
        }
    }
    veml.setIntegrationTime(VEML7700_IT_800MS);
    veml.setGain(VEML7700_GAIN_1_4);
    collectSubSample();
    lastSubSample = millis();
    lastMinute = millis();
}

void loop() {
    unsigned long now = millis();
    if (now - lastSubSample >= SUBSAMPLE_INTERVAL_MS) {
        lastSubSample += SUBSAMPLE_INTERVAL_MS;
        collectSubSample();
    }
    if (now - lastMinute >= MINUTE_INTERVAL_MS) {
        lastMinute += MINUTE_INTERVAL_MS;
        finalizeMinute();
        debugBuffer();
    }
}
