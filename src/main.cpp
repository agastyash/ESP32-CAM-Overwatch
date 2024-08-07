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
// 0-6, lower means higher resolution, defaults to VGA if not defined
// TODO: Add function to control this using a dropdown on a web page
RTC_NOINIT_ATTR uint8_t qualityPreset = 0;

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

  cam.init(camera_config_helper(qualityPreset));

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
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

  server.begin();
}

//////////////////////////
// Function Definitions //
//////////////////////////

void Task0Code(void * pvParameters)
{
  vTaskDelete(NULL);
  // Nothing for now lol
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

  #ifdef DEBUG
    Serial.println("Serving MJPEG stream now.");
  #endif

  while (true)
  {
    if (!client.connected())
    {
      #ifdef DEBUG
        Serial.println("Client disconnected, MJPEG stream killed.");
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
  switch(qualityPreset)
  {
    case 0: // Max quality
      config.frame_size = FRAMESIZE_UXGA;
    case 1:
      config.frame_size = FRAMESIZE_SXGA;
    case 2:
      config.frame_size = FRAMESIZE_XGA;
    case 3:
      config.frame_size = FRAMESIZE_SVGA;
    case 4:
      config.frame_size = FRAMESIZE_VGA;
    case 5:
      config.frame_size = FRAMESIZE_CIF;
    case 6: // Lowest quality
      config.frame_size = FRAMESIZE_QVGA;
    default:
      config.frame_size = FRAMESIZE_VGA;
  }
  // config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12; // Lower JPEG quality (higher corresponding number) for videos
  // config.jpeg_quality = 12;
  config.fb_count = 2;
  return config;
}