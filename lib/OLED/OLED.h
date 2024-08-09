#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <String.h>

// External
void initializeDisplay(uint8_t width, uint8_t height, uint8_t maxchars);
void renderStaticProperties(Adafruit_SSD1306 oled, uint8_t qualityPreset, const char* mdnsname);
void updateStats(Adafruit_SSD1306 oled, uint8_t clientCount, uint8_t uptimeHours, uint16_t uptimeDays, uint8_t wifiStatus);

// Internal
void hBar(Adafruit_SSD1306 oled);
String splitAlign(String text1, String);
void printCenteredText(Adafruit_SSD1306 oled, String text, bool middle);
String resToText(uint8_t qualityPreset);