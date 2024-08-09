// Common libraries
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
// Camera libraries
#include <OV2640.h>
#include <MJPEG_Streaming.h>
// #include "soc/soc.h" //disable brownout problems
// #include "soc/rtc_cntl_reg.h"  //disable brownout problems
// OTA update libraries
#include <Update.h>
#include <OTA.h> // default login is admin:admin
// SSD1306 Libraries
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
// Custom OLED Helper library
#include <OLED.h>

// SSD1306 OLED Setup
#define SCREEN_WIDTH 128 // OLED display width
#define SCREEN_HEIGHT 64 // OLED display height
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define MAX_CHARS     21 // Max characters to render before running a scrolling text function on displayed text
// SDA/SCL pins can be pre-defined for some boards by the Wire library
#define SDA           15 // SDA Pin
#define SCL           14 // SCL Pin

// I2C connection with SSD1306
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Uncomment to turn serial output into a yapper
#define DEBUG

// Camera model definition
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WITHOUT_PSRAM
//#define CAMERA_MODEL_WROVER_KIT
#include <pins.h> // (includes basic pin definitions + GPIO pin assignments for LEDs, push buttons and sensors)

// Secret header file
#include <secret.h>

// Reset-surviving definition for camera resolution, potentially to be shifted to an SD card/EEPROM instead
// TODO: Add function to control this using a dropdown on a web page
RTC_NOINIT_ATTR uint8_t qualityPreset = 8; // default = VGA
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


// Uptime counter (TODO: Upgrade to a web synced clock)
uint8_t uptimeHours = 0;
uint16_t uptimeDays = 0;
unsigned long prevMillis;

// Client counter
uint8_t clientCount = 0;

// Wifi status code
// uint8_t wifiStatus = 0;

// Task running on same core as system tasks for future additions
TaskHandle_t Task0;

// Hostname for MDNS responder
const char* host = "esp32cam";

// Camera object
OV2640 cam;

// Common webserver for both OTA updates and camera access
WebServer server(80);

//////////////////////////
// Function definitions //
//////////////////////////

// Function to enable the use of the onboard LED as status indicator
void LED_indicate(int code);

// Push button function(s)
// void IRAM_ATTR softRestart(); // (simple test function, can be used for more reasonable actions)

// Sensor trigger function(s)
// void IRAM_ATTR postNotification(); // Current posts to a discord webhook, can do anything though

// Web Server handler/render functions
void handleNotFound();

// OTA Updates

void render_login_page(void);
void render_update_page(void);
void perform_update(void);
void finish_update(void);

// MJPEG Streaming

camera_config_t camera_config_helper(uint8_t qualityPreset); // Mode variable: 0 = photo, 1 = video
void handle_jpg(void);
void handle_jpg_stream(void);

// Skeleton code for on-the-fly quality and resolution tweaking
// void render_dashboard(void);
// void modify_config(void);

// Hand smaller tasks to the system core
void Task0Code(void * pvParameters);

//////////////////////////
//         Setup        //
//////////////////////////

void setup(void)
{
  // Serial for debugging
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  // Set onboard LED pin mode and turn it off (LOW is on, HIGH is off)
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH);
  // Define push button pin mode(s) and assign an interrupt function to them
  #ifdef PUSHBUTTON1
  pinMode(PUSHBUTTON1, INPUT_PULLDOWN);
  attachInterrupt(PUSHBUTTON1, softRestart, RISING);
  #endif
  // Define pin mode(s) for sensors and assign an interrupt function to them
  #ifdef SENSOR1
  pinMode(SENSOR1, INPUT);
  attachInterrupt(SENSOR1, postNotification, CHANGE);
  #endif

  // initialize OLED display with I2C address 0x3C
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("failed to start SSD1306 OLED"));
    LED_indicate(1);
    while (1);
  }

  #ifdef DEBUG
    Serial.println("SSD1306 successfully connected.");
  #endif

  delay(1000); // Breathing room

  if (!cam.init(camera_config_helper(qualityPreset)))
  {
    #ifdef DEBUG
      Serial.println("Camera failed to initialize.");
    #endif
    while (1);
  }
  #ifdef DEBUG
    Serial.println("Camera initialized.");
  #endif

  delay(2000); // Breathing room

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    #ifdef DEBUG
      Serial.print(".");
    #endif
    delay(500);
  }
  // wifiStatus = 1;
  LED_indicate(0);
  #ifdef DEBUG
    Serial.println();
    Serial.print("Connected to "); Serial.println(ssid);
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
  #endif

  // MDNS kinda useless for now
  // if (!MDNS.begin(host)) // http://esp32cam.local
  // { Serial.println("Error setting up MDNS responder!"); while (1) { delay(1000); } }
  // Serial.println("mDNS responder started");

  server.onNotFound(handleNotFound);
  // OTA Update pages (login on index, upload page upon login, update page upon upload)
  server.on("/", HTTP_GET, render_login_page);
  server.on("/serverIndex", HTTP_GET, render_update_page);
  server.on("/update", HTTP_POST, finish_update, perform_update);
  // On-the-fly quality and config updates
  // server.on("/dash", HTTP_GET, render_dashboard);
  // server.on("/reconfig", HTTP_POST, modify_config);
  // MJPEG Streaming Server pages (Stream and Still)
  server.on("/mjpeg", HTTP_GET, handle_jpg_stream);
  server.on("/jpg", HTTP_GET, handle_jpg);

  //create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
                    Task0Code,   /* Task function. */
                    "Task0",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task0,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  delay(500);
  #ifdef DEBUG
    Serial.println("Setup complete.");
  #endif

  // Take a deep breath before diving into the SSD1306 library
  delay(2000);
  
  #ifdef DEBUG
    Serial.println("Pushing SSD1306 updates.");
  #endif
  // Clear screen and set static properties
  display.clearDisplay(); // clear display
  display.setTextColor(WHITE);
  // Push the screen constants to the custom OLED library
  initializeDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, MAX_CHARS);
  // Render the static and dynamic parts of the display
  renderStaticProperties(display, qualityPreset, host);
  updateStats(display, clientCount, uptimeHours, uptimeDays, WiFi.status() == WL_CONNECTED);
  #ifdef DEBUG
    Serial.println("SSD1306 initial rendering complete.");
  #endif

  delay(1000); // Breathing room

  server.begin();
  #ifdef DEBUG
    Serial.println("Server configured and ready for requests.");
  #endif
}

//////////////////////////
// Function Definitions //
//////////////////////////

void Task0Code(void * pvParameters)
{
  // Every 1h update the hours (and days) counters
  unsigned long timeDiff = millis() - prevMillis;
  if (timeDiff >= 3600000)
  {
    prevMillis = millis();
    uptimeHours++;
    if (uptimeHours >= 24)
    {
      uptimeHours = 0;
      uptimeDays++;
    }
  }
  // Update the display with the new stats
  updateStats(display, clientCount, uptimeHours, uptimeDays, WiFi.status() == WL_CONNECTED);
  delay(29000); // Let core 0 breathe
  // vTaskDelete(NULL); // Nuke this task from core 0
}

// Main loop automatically assigned to core 1
void loop(void)
{
  server.handleClient();
  delay(1);
}

void render_login_page(void)
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", loginIndex);
  #ifdef DEBUG
    Serial.println("Login page rendered.");
  #endif
}

void render_update_page(void)
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", serverIndex);
  #ifdef DEBUG
    Serial.println("Update page rendered.");
  #endif
}

void finish_update(void)
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
}

void perform_update(void)
{
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    #ifdef DEBUG
      Serial.printf("Update: %s\n", upload.filename.c_str());
    #endif
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      LED_indicate(1);
      //start with max available size
      #ifdef DEBUG
        Update.printError(Serial);
      #endif
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    /* flashing firmware to ESP*/
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      LED_indicate(1);
      #ifdef DEBUG
        Update.printError(Serial);
      #endif
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true))
    {
      LED_indicate(0);
      // true to set the size to the current progress
      #ifdef DEBUG
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      #endif
    }
    else
    {
      LED_indicate(1);
      #ifdef DEBUG
        Update.printError(Serial);
      #endif
    }
  }
}

void handle_jpg_stream(void)
{
  char buf[32];
  int s;

  WiFiClient client = server.client();

  client.write(HEADER, hdrLen);
  client.write(BOUNDARY, bdrLen);

  clientCount = 1;
  #ifdef DEBUG
    Serial.printf("Client count updated to %d\n", clientCount);
    Serial.println("Serving MJPEG stream to a new client now.");
  #endif

  while (true)
  {
    if (!client.connected())
    {
      clientCount = 0;
      #ifdef DEBUG
        Serial.printf("Client count updated to %d\n", clientCount);
        Serial.println("Clients disconnected, MJPEG stream killed.");
      #endif
      break;
    }
    cam.run();
    s = cam.getSize();
    client.write(CTNTTYPE, cntLen);
    sprintf( buf, "%d\r\n\r\n", s );
    client.write(buf, strlen(buf));
    client.write((char *)cam.getfb(), s);
    client.write(BOUNDARY, bdrLen);
  }
}

void handle_jpg(void)
{
  WiFiClient client = server.client();

  if (!client.connected()) return;
  cam.run(); delay(100); cam.run(); // double capture to dump buffer
  client.write(JHEADER, jhdLen);
  client.write((char *)cam.getfb(), cam.getSize());

  #ifdef DEBUG
    Serial.println("JPEG posted.");
  #endif
}

void handleNotFound()
{
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text / plain", message);
  #ifdef DEBUG
    Serial.println("Serving 404 page.");
  #endif
}

void LED_indicate(int code)
{
  switch(code)
  {
    case 0: // Success
      // blink onboard LED thrice
      for (uint8_t i = 0; i < 3; i++)
      {
        delay(300);
        digitalWrite(ONBOARD_LED, LOW); // Switches it on contrary to how it looks
        delay(300);
        digitalWrite(ONBOARD_LED, HIGH); // Switches it off contrary to how it looks
      }
    case 1: // Total failure
      // turn on onboard LED (static)
      digitalWrite(ONBOARD_LED, LOW);
    case 2: // Misc debugging option
      // Flash onboard LED 10 times rapidly
      for (uint8_t i = 0; i < 10; i++)
      {
        delay(200);
        digitalWrite(ONBOARD_LED, LOW); // Switches it on contrary to how it looks
        delay(200);
        digitalWrite(ONBOARD_LED, HIGH); // Switches it off contrary to how it looks
      }
  }
}

void IRAM_ATTR softRestart() {
  LED_indicate(0);
  ESP.restart();
}

void IRAM_ATTR postNotification()
{
  #ifdef DEBUG
    Serial.println("Attempting to perform HTTP POST request to webhook.");
  #endif
  // Use common Wifi client
  WiFiClient client = server.client();
  // Generate a temporary HTTP client
  HTTPClient http;
  http.begin(client, webhookURL);
  http.addHeader("Content-Type", "application/json");
  String message = "{ \"content\": \"This is where message content goes\", \"embeds\": null, \"username\": \"usernamedisplayed\", \"attachments\": [] }";
  // Perform POST request, grab response code, discard HTTP client
  int httpResponseCode = http.POST(message);
  #ifdef DEBUG
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
  #endif
  http.end();
}

/*
void render_dashboard(void)
{
  server.sendHeader("Connection", "close");
  const char* dashboardIndex = "";
  server.send(200, "text/html", dashboardIndex);
  #ifdef DEBUG
    Serial.println("Dashboard page rendered.");
  #endif
}

void modify_config(void)
{
  String postData = server.arg(server.args() - 1);
}
*/

camera_config_t camera_config_helper(uint8_t qualityPreset)
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  /* Reference paste */
  config.frame_size = (framesize_t) qualityPreset;
  // config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  // config.jpeg_quality = 12;
  config.fb_count = 2;
  return config;
}