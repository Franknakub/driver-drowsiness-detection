#pragma once

#include <Arduino.h>
#include "esp_camera.h"

struct EyeRoi {
  int x;
  int y;
  int w;
  int h;
};

struct GrayImage {
  uint8_t *buf;
  int width;
  int height;
};

// Init OV5640 (format/size from CAM_FORMAT / CAM_FRAMESIZE) with frame buffers in PSRAM.
bool cameraInit();

// Grab one frame into img as 8-bit grayscale (JPEG frames are decoded; buffer allocated on first call).
bool cameraGrabGray(GrayImage &img);

// Crop roi from img and resize it to out (outW x outH) by area averaging.
void cropResizeGray(const GrayImage &img, const EyeRoi &roi, uint8_t *out, int outW, int outH);

uint8_t meanBrightness(const uint8_t *img, size_t len);
