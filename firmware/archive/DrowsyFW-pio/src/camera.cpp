#include "camera.h"
#include "config.h"
#include "jpeg_decoder.h"

static uint16_t *rgbBuf = nullptr;  // decoded RGB565 frame in PSRAM
static size_t rgbBufSize = 0;

bool cameraInit() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;
  c.pin_d1 = CAM_PIN_D1;
  c.pin_d2 = CAM_PIN_D2;
  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;
  c.pin_d5 = CAM_PIN_D5;
  c.pin_d6 = CAM_PIN_D6;
  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
#ifndef CAM_FORMAT
#define CAM_FORMAT PIXFORMAT_GRAYSCALE
#endif
#ifndef CAM_FRAMESIZE
#define CAM_FRAMESIZE FRAMESIZE_SVGA  // 800x600, OV5640 without AF firmware
#endif
  c.pixel_format = CAM_FORMAT;
  c.frame_size = CAM_FRAMESIZE;
  c.jpeg_quality = 10;            // 0-63, lower = better quality
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x (check FPC cable)\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  Serial.printf("Camera OK, sensor PID 0x%04X\n", s->id.PID);
  s->set_bpc(s, 1);  // remove hot pixels
  s->set_wpc(s, 1);
  return true;
}

bool cameraGrabGray(GrayImage &img) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) return false;

  const size_t pixels = (size_t)fb->width * fb->height;
  if (fb->format == PIXFORMAT_GRAYSCALE) {
    if (img.buf == nullptr) img.buf = (uint8_t *)ps_malloc(pixels);
    memcpy(img.buf, fb->buf, pixels);
    img.width = fb->width;
    img.height = fb->height;
    esp_camera_fb_return(fb);
    return true;
  }
  if (rgbBuf == nullptr || rgbBufSize < pixels * 2) {
    free(rgbBuf);
    free(img.buf);
    rgbBuf = (uint16_t *)ps_malloc(pixels * 2);
    img.buf = (uint8_t *)ps_malloc(pixels);
    rgbBufSize = pixels * 2;
    if (rgbBuf == nullptr || img.buf == nullptr) {
      esp_camera_fb_return(fb);
      return false;
    }
  }

  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = fb->buf;
  cfg.indata_size = fb->len;
  cfg.outbuf = (uint8_t *)rgbBuf;
  cfg.outbuf_size = rgbBufSize;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t out = {};
  esp_err_t err = esp_jpeg_decode(&cfg, &out);
  esp_camera_fb_return(fb);
  if (err != ESP_OK) return false;

  img.width = out.width;
  img.height = out.height;
  const size_t n = (size_t)out.width * out.height;
  for (size_t i = 0; i < n; i++) {
    uint16_t p = rgbBuf[i];
    uint32_t r = (p >> 11) << 3;
    uint32_t g = ((p >> 5) & 0x3F) << 2;
    uint32_t b = (p & 0x1F) << 3;
    img.buf[i] = (r * 77 + g * 150 + b * 29) >> 8;
  }
  return true;
}

void cropResizeGray(const GrayImage &img, const EyeRoi &roi, uint8_t *out, int outW, int outH) {
  // Clamp roi to the frame
  int x0 = constrain(roi.x, 0, img.width - 1);
  int y0 = constrain(roi.y, 0, img.height - 1);
  int w = constrain(roi.w, 1, img.width - x0);
  int h = constrain(roi.h, 1, img.height - y0);

  for (int oy = 0; oy < outH; oy++) {
    int sy0 = y0 + oy * h / outH;
    int sy1 = max(sy0 + 1, y0 + (oy + 1) * h / outH);
    for (int ox = 0; ox < outW; ox++) {
      int sx0 = x0 + ox * w / outW;
      int sx1 = max(sx0 + 1, x0 + (ox + 1) * w / outW);
      uint32_t sum = 0;
      for (int sy = sy0; sy < sy1; sy++) {
        const uint8_t *row = img.buf + sy * img.width;
        for (int sx = sx0; sx < sx1; sx++) sum += row[sx];
      }
      out[oy * outW + ox] = sum / ((sy1 - sy0) * (sx1 - sx0));
    }
  }
}

uint8_t meanBrightness(const uint8_t *img, size_t len) {
  uint64_t sum = 0;
  for (size_t i = 0; i < len; i++) sum += img[i];
  return len ? sum / len : 0;
}
