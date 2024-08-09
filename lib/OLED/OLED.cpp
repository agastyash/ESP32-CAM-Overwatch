#include <OLED.h>
Adafruit_SSD1306 oled;
uint8_t SCREEN_WIDTH, SCREEN_HEIGHT, MAX_CHARS;
uint8_t dynamicUpdateRegion[2];

void initializeDisplay(uint8_t width, uint8_t height, uint8_t maxchars)
{
    SCREEN_WIDTH = width; SCREEN_HEIGHT = height; MAX_CHARS = maxchars;
}

// Overloading * operator to multiply strings, similar to Python
String operator * (String a, unsigned int b) {
    String output = "";
    while (b--) {
        output += a;
    }
    return output;
}

// Draw a horizontal bar with 2px vertical padding and appropriately re-align the cursor
void hBar(Adafruit_SSD1306 oled)
{
  int prevX = oled.getCursorX();
  int prevY = oled.getCursorY();
  oled.drawLine(0, prevY + 2, SCREEN_WIDTH, prevY + 2, WHITE);
  oled.setCursor(prevX, prevY + 5);
}

// Return text spaced apart in the 21-character horizontal range of the 128x64 SSD1306
String splitAlign(String text1, String text2)
{
  if (text1.length()+text2.length() >= 20)
    return "";
  return String(text1+String(" ")*(unsigned int)(21-(text1.length()+text2.length()))+text2);
}

// Print text horizontally center-aligned, optionally print it vertically center aligned between the cursor position at call and the edge of the screen
void printCenteredText(Adafruit_SSD1306 oled, String text, bool middle)
{
  int16_t centercursorx, centercursory; uint16_t centerwidth, centerheight;
  oled.getTextBounds(text, 0, 0, &centercursorx, &centercursory, &centerwidth, &centerheight);
  oled.setCursor((SCREEN_WIDTH-centerwidth)/2, (middle) ? ((oled.getCursorY()+SCREEN_HEIGHT-centerheight)/2) : oled.getCursorY());
  oled.println(text);
}

// Render static properties and return the cursor position to an array
void renderStaticProperties(Adafruit_SSD1306 oled, uint8_t qualityPreset, const char* mdnsname)
{
  oled.clearDisplay();
  oled.setCursor(0,0);
  oled.setTextSize(1);
  printCenteredText(oled, splitAlign(resToText(qualityPreset), mdnsname), false);
  hBar(oled);
  dynamicUpdateRegion[0] = oled.getCursorX();
  dynamicUpdateRegion[1] = oled.getCursorY();
  oled.display();
}

// Update dynamic properties
void updateStats(Adafruit_SSD1306 oled, uint8_t clientCount, uint8_t uptimeHours, uint16_t uptimeDays, uint8_t wifiStatus)
{
  // CLear dynamic update region
  oled.fillRect(0, dynamicUpdateRegion[1], SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
  oled.setCursor(dynamicUpdateRegion[0],dynamicUpdateRegion[1]);
  // Print client count and uptime
  oled.setTextSize(1);
  printCenteredText(oled, splitAlign("Clients:",String(clientCount)), false);
  hBar(oled);
  printCenteredText(oled, splitAlign("Uptime:",(uptimeDays) ? String(uptimeDays)+" days" : String(uptimeHours)+" hours"), false);
  // Print large status
  oled.setTextSize(2);
  printCenteredText(oled, (wifiStatus) ? "ONLINE" : "OFFLINE", true);
  oled.display();
}

String resToText(uint8_t qualityPreset)
{
  /*
    Possible values (lowest to highest resolution possible, corresponding to the enum framesize_t:
      5  FRAMESIZE_QVGA,     // 320x240
      8  FRAMESIZE_VGA,      // 640x480
      9  FRAMESIZE_SVGA,     // 800x600
      10 FRAMESIZE_XGA,      // 1024x768
      11 FRAMESIZE_HD,       // 1280x720
      12 FRAMESIZE_SXGA,     // 1280x1024
      13 FRAMESIZE_UXGA,     // 1600x1200
  */
  switch(qualityPreset)
  {
    case 5:
      return "320x240";
    case 8:
      return "640x480";
    case 9:
      return "800x600";
    case 10:
      return "1024x768";
    case 11:
      return "1280x720";
    case 12:
      return "1280x1024";
    case 13:
      return "1600x1200";
    default:
      return "HUH";
  }
}