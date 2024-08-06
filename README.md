# ESP32-CAM-Overwatch
A flexible, reliable, one-time setup ESP32-CAM web-streaming server.

## Current Outline
- Based on FreeRTOS
- MJPEG Streaming framework from arkhipenko's [MJPEG single-client streaming server](https://github.com/arkhipenko/esp32-cam-mjpeg/)
- OTA update capable (based on [Espressif's OTAWebUpdater sketch](https://docs.espressif.com/projects/arduino-esp32/en/latest/ota_web_update.html))
- Supports configurations for:
  - AI Thinker ESP32-CAM
  - ESP32-WROVER CAM
  - M5Stack ESP32-CAM
- Tested on:
  - AI Thinker ESP32-CAM
- Template interrupts for HTTP POST notifications and hardware buttons

## Planned Additions
- On-the-fly resolution and quality control using predefined presets
- Hardware additions
  - 128x64 SSD1306 support
  - 3-button control scheme
  - 3D-printable housing for the camera
- Software additions
  - Complete web dashboard
  - Support for multi-client MJPEG streaming
  - Support for chunked HTTP streaming (like in the default streaming server example)
