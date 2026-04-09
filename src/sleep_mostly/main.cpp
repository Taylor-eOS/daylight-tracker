#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <Adafruit_VEML7700.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>

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

const size_t WINDOW_SAMPLES = 15;
const unsigned long SUBSAMPLE_INTERVAL_MS = 6000UL;
const unsigned long MEASURE_DURATION_MS = 60000UL;
const uint64_t SLEEP_DURATION_US = 3ULL * 60ULL * 1000000ULL;
const uint8_t NIGHT_THRESHOLD = 5;
const float minLux = 50.0f;
const float maxLux = 10000.0f;
RTC_DATA_ATTR uint8_t sampleBuffer[WINDOW_SAMPLES];
RTC_DATA_ATTR size_t sampleIndex = 0;
RTC_DATA_ATTR size_t sampleCount = 0;
RTC_DATA_ATTR bool hasSleptThisNight = false;
uint32_t subSampleSum = 0;
size_t subSampleCount = 0;

uint8_t luxToDaylightScore(float lux) {
    if (lux <= minLux) return 0;
    if (lux > maxLux) lux = maxLux;
    float compressed = log10f(lux - minLux + 1.0f);
    float maxCompressed = log10f(maxLux - minLux + 1.0f);
    compressed = compressed / maxCompressed;
    float midpoint = 0.78f;
    float steepness = 8.0f;
    float normalized = 1.0f / (1.0f + expf(-steepness * (compressed - midpoint)));
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    Serial.print("compressed: ");
    Serial.println(compressed, 3);
    return (uint8_t)(normalized * 100.0f);
}

uint8_t rollingAverage() {
    if (sampleCount == 0) return 0;
    uint32_t sum = 0;
    for (size_t i = 0; i < sampleCount; i++) {
        sum += sampleBuffer[i];
    }
    return (uint8_t)(sum / sampleCount);
}

void drawScreen(uint32_t lux, uint8_t oneHourAverage) {
    char luxText[16];
    char avgText[16];
    if (lux > 9999) {
        snprintf(luxText, sizeof(luxText), "max");
    } else {
        snprintf(luxText, sizeof(luxText), "%lu", lux);
    }
    snprintf(avgText, sizeof(avgText), "%u", oneHourAverage);
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
        const char* label2 = "%";
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
        display.setCursor(x2 - 2, yAvg);
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

void addSample(uint8_t score) {
    sampleBuffer[sampleIndex] = score;
    sampleIndex = (sampleIndex + 1) % WINDOW_SAMPLES;
    if (sampleCount < WINDOW_SAMPLES) sampleCount++;
}

void collectSubSample() {
    float lux = veml.readLux();
    if (lux < 0.0f) lux = 0.0f;
    if (lux > maxLux) lux = maxLux;
    subSampleSum += (uint32_t)roundf(lux);
    subSampleCount++;
    Serial.print("lux: ");
    Serial.println(lux, 1);
}

void drawSleep() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold18pt7b);
        display.setCursor(10, 80);
        display.print("sleep");
    } while (display.nextPage());
}

void finalizeMinute() {
    uint32_t avgLux = (subSampleCount > 0) ? (subSampleSum / subSampleCount) : 0;
    uint8_t minuteScore = luxToDaylightScore((float)avgLux);
    subSampleSum = 0;
    subSampleCount = 0;
    addSample(minuteScore);
    uint8_t avg = rollingAverage();
    Serial.print("Minute avg lux: ");
    Serial.print(avgLux);
    Serial.print("  score: ");
    Serial.print(minuteScore);
    Serial.print("  1h avg: ");
    Serial.println(avg);
    drawScreen(avgLux, avg);
    if (sampleCount == WINDOW_SAMPLES) {
        if (avg < NIGHT_THRESHOLD && !hasSleptThisNight) {
            Serial.println("Entering sleep mode");
            hasSleptThisNight = true;
            drawSleep();
            delay(1000);
            esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
            esp_deep_sleep_start();
        }
        if (avg > (NIGHT_THRESHOLD + 10)) {
            hasSleptThisNight = false;
        }
    }
}

void debugBuffer() {
    Serial.flush();
    Serial.print("Sample count: ");
    Serial.println(sampleCount);
    Serial.print("Next write index: ");
    Serial.println(sampleIndex);
    size_t oldest = (sampleIndex - sampleCount + WINDOW_SAMPLES) % WINDOW_SAMPLES;
    for (size_t i = 0; i < sampleCount; i++) {
        size_t idx = (oldest + i) % WINDOW_SAMPLES;
        Serial.print("Buffer[");
        Serial.print(idx);
        Serial.print("] = ");
        Serial.println(sampleBuffer[idx]);
    }
    Serial.print("Computed rolling average: ");
    Serial.println(rollingAverage());
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
    veml.setIntegrationTime(VEML7700_IT_100MS);
    veml.setGain(VEML7700_GAIN_1_8);
    delay(150);
    veml.readLux();
    delay(150);
    unsigned long measureStart = millis();
    unsigned long lastSubSample = measureStart;
    collectSubSample();
    while (millis() - measureStart < MEASURE_DURATION_MS) {
        unsigned long now = millis();
        if (now - lastSubSample >= SUBSAMPLE_INTERVAL_MS) {
            lastSubSample += SUBSAMPLE_INTERVAL_MS;
            collectSubSample();
        }
    }
    finalizeMinute();
    debugBuffer();
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}

void loop() {}

