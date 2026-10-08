#pragma once

// GOOUUU ESP32-S3-CAM N16R8 pin map
// Never use GPIO 26-37 (flash + octal PSRAM). GPIO 2 = onboard LED, 48 = WS2812.

// Camera DVP (OV5640). PWDN/RESET are not wired to the header.
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD    4  // SCCB SDA
#define CAM_PIN_SIOC    5  // SCCB SCL
#define CAM_PIN_D7     16
#define CAM_PIN_D6     17
#define CAM_PIN_D5     18
#define CAM_PIN_D4     12
#define CAM_PIN_D3     10
#define CAM_PIN_D2      8
#define CAM_PIN_D1      9
#define CAM_PIN_D0     11
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK   13

// Not connected yet, reserved for later steps
#define OLED_PIN_SDA    1
#define OLED_PIN_SCL   42
#define LDR_PIN         3
#define BUZZER_PIN     14
#define IR_LED_PIN     21
#define LED_GREEN_PIN  47
#define LED_RED_PIN    40

// Model input: grayscale eye crop
#define EYE_INPUT_SIZE 96
