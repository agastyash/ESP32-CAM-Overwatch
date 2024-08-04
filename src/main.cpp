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

// Uncomment to turn serial output into a yapper
// Currently useless, planned addition to essentially delete serial output junk from the 'production' binaries
// #define DEBUG
// TEMPLATE: 
// #ifdef DEBUG
// BLABLA
// #endif

// Camera model definition
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WITHOUT_PSRAM
//#define CAMERA_MODEL_WROVER_KIT
#include <pins.h> // (includes basic pin definitions + GPIO pin assignments for LEDs, push buttons and sensors)

// Secret header file
#include <secret.h>

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

// Function to enable the use of the onboard LED as an indicator for OTA flashing status
void LED_indicate(uint8_t code);

// Push button function(s)
void IRAM_ATTR softRestart(); // (simple test function, can be used for more reasonable actions)

// Sensor trigger function(s)
void IRAM_ATTR postNotification(); // Current posts to a discord webhook, can do anything though

// Web Server handler/render functions
void handleNotFound();

// OTA Updates

void render_login_page(void);
void render_update_page(void);
void render_update_status(void);
void upload_update(void);

// MJPEG Streaming

void handle_jpg(void);
void handle_jpg_stream(void);

// Hand smaller tasks to the system core
void Task0Code(void * pvParameters) {
  delay(1);
}

void setup(void) {
  // Set onboard LED pin mode and turn it off (LOW is on, HIGH is off)
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH);
  // Define push button pin mode(s) and assign an interrupt function to them
  pinMode(PUSHBUTTON1, INPUT_PULLDOWN);
  attachInterrupt(PUSHBUTTON1, softRestart, RISING);
  // Define pin mode(s) for sensors and assign an interrupt function to them
  pinMode(SENSOR1, INPUT);
  attachInterrupt(SENSOR1, postNotification, CHANGE);

  // Serial for debugging
  Serial.begin(115200);

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
  // Quality
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  cam.init(config);

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("Connected to "); Serial.println(ssid);
  Serial.print("IP address: "); Serial.println(WiFi.localIP());

  if (!MDNS.begin(host)) // http://esp32cam.local
  { Serial.println("Error setting up MDNS responder!"); while (1) { delay(1000); } }
  Serial.println("mDNS responder started");

  server.onNotFound(handleNotFound);
  // OTA Update pages (login on index, upload page upon login, update page upon upload)
  server.on("/", HTTP_GET, render_login_page);
  server.on("/serverIndex", HTTP_GET, render_update_page);
  server.on("/update", HTTP_POST, upload_update, render_update_status);
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

  server.begin();
}

void loop(void) {
  server.handleClient();
  delay(1);
}

void render_login_page(void)
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", loginIndex);
}

void render_update_page(void)
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", serverIndex);
}

void upload_update(void)
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
}

void render_update_status(void)
{
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    Serial.printf("Update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      LED_indicate(1);
      //start with max available size
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    /* flashing firmware to ESP*/
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      LED_indicate(1);
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true))
    {
      LED_indicate(0);
      // true to set the size to the current progress
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    }
    else
    {
      LED_indicate(1);
      Update.printError(Serial);
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

  while (true)
  {
    if (!client.connected()) break;
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
  cam.run();
  client.write(JHEADER, jhdLen);
  client.write((char *)cam.getfb(), cam.getSize());
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
}

void LED_indicate(uint8_t code)
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
  // Use common Wifi client
  WiFiClient client = server.client();
  // Generate a temporary HTTP client
  HTTPClient http;
  http.begin(client, webhookURL);
  http.addHeader("Content-Type", "application/json");
  String message = "{ \"content\": \"This is where message content goes\", \"embeds\": null, \"username\": \"usernamedisplayed\", \"attachments\": [] }";
  // Perform POST request, grab response code, discard HTTP client
  int httpResponseCode = http.POST(message);
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
  http.end();
  
}