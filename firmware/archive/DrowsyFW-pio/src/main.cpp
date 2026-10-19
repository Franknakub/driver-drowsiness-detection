#include <Arduino.h>
#include "config.h"
#include "camera.h"
#include "eye_model.h"

// Camera -> fixed eye box -> 96x96 crop -> INT8 model -> P(eye closed).
//
// Serial commands (115200, newline terminated):
//   roi x y w h   set the fixed eye box in camera frame coordinates
//   stream 1|0    send every frame to the PC viewer (ml/eye_viewer.py):
//                 "FRM <pw> <ph> <crop> <pClosed> <frameW> <frameH>" line + preview bytes + crop bytes

static bool cameraOk = false;
static bool modelOk = false;
static EyeRoi eyeRoi = {300, 200, 200, 200};  // default: middle of an SVGA frame
static uint8_t *eyeCrop = nullptr;            // EYE_INPUT_SIZE^2 bytes in PSRAM
static bool streaming = false;

// Full-frame preview for the PC viewer
static constexpr int PREVIEW_W = 200;
static constexpr int PREVIEW_H = 150;
static uint8_t *preview = nullptr;

static void printBoardInfo() {
  Serial.println(F("=== DrowsyFW camera pipeline ==="));
  Serial.printf("Chip: %s rev %d @ %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu MB, PSRAM: %lu KB free / %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned long)(ESP.getFreePsram() / 1024), (unsigned long)(ESP.getPsramSize() / 1024));
  if (!psramFound()) Serial.println(F("PSRAM NOT FOUND (check memory_type = qio_opi)"));
}

static void sendStreamFrame(float pClosed, int frameW, int frameH) {
  Serial.printf("FRM %d %d %d %.3f %d %d\n", PREVIEW_W, PREVIEW_H, EYE_INPUT_SIZE, pClosed, frameW, frameH);
  Serial.write(preview, PREVIEW_W * PREVIEW_H);
  Serial.write(eyeCrop, EYE_INPUT_SIZE * EYE_INPUT_SIZE);
}

static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  EyeRoi r;
  if (sscanf(line.c_str(), "roi %d %d %d %d", &r.x, &r.y, &r.w, &r.h) == 4) {
    eyeRoi = r;
    Serial.printf("ROI set to x=%d y=%d w=%d h=%d\n", r.x, r.y, r.w, r.h);
  } else if (line == "stream 1" || line == "stream 0") {
    streaming = line.endsWith("1");
  } else if (line.startsWith("fs ")) {  // debug: frame size enum (e.g. 5 QVGA, 8 VGA, 9 SVGA)
    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("set_framesize -> %d\n", s->set_framesize(s, (framesize_t)line.substring(3).toInt()));
  } else if (line.startsWith("xclk ")) {  // debug: XCLK in MHz
    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("set_xclk -> %d\n", s->set_xclk(s, LEDC_TIMER_0, line.substring(5).toInt()));
  } else if (line.startsWith("sharp ")) {  // debug: sharpness -3..3
    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("set_sharpness -> %d\n", s->set_sharpness(s, line.substring(6).toInt()));
  } else if (line.startsWith("contrast ")) {  // debug: contrast -2..2
    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("set_contrast -> %d\n", s->set_contrast(s, line.substring(9).toInt()));
  } else if (line.startsWith("reg ")) {  // debug: reg <hex addr> <hex value>, or reg <hex addr> to read
    sensor_t *s = esp_camera_sensor_get();
    unsigned addr = 0, val = 0;
    int n = sscanf(line.c_str(), "reg %x %x", &addr, &val);
    if (n == 2) Serial.printf("set_reg 0x%04X=0x%02X -> %d\n", addr, val, s->set_reg(s, addr, 0xFF, val));
    if (n >= 1) Serial.printf("get_reg 0x%04X = 0x%02X\n", addr, s->get_reg(s, addr, 0xFF));
  } else if (line.startsWith("pull ")) {  // debug: pull <gpio> up|down|none
    int pin = 0;
    char mode[8] = {0};
    if (sscanf(line.c_str(), "pull %d %7s", &pin, mode) == 2) {
      gpio_set_pull_mode((gpio_num_t)pin, strcmp(mode, "up") == 0     ? GPIO_PULLUP_ONLY
                                          : strcmp(mode, "down") == 0 ? GPIO_PULLDOWN_ONLY
                                                                      : GPIO_FLOATING);
      Serial.printf("GPIO%d pull %s\n", pin, mode);
    }
  } else if (line == "scan") {  // debug: count level changes on each GPIO while the camera runs
    const int freePins[] = {1, 2, 3, 14, 21, 38, 39, 40, 41, 42, 47};
    for (int p : freePins) pinMode(p, INPUT);
    static uint32_t toggles[49];
    memset(toggles, 0, sizeof(toggles));
    uint64_t prev = ((uint64_t)REG_READ(GPIO_IN1_REG) << 32) | REG_READ(GPIO_IN_REG);
    for (int i = 0; i < 400000; i++) {
      uint64_t cur = ((uint64_t)REG_READ(GPIO_IN1_REG) << 32) | REG_READ(GPIO_IN_REG);
      uint64_t diff = cur ^ prev;
      while (diff) {
        int b = __builtin_ctzll(diff);
        if (b < 49) toggles[b]++;
        diff &= diff - 1;
      }
      prev = cur;
    }
    for (int p = 0; p < 49; p++) {
      if (toggles[p]) Serial.printf("GPIO%d toggles %lu\n", p, (unsigned long)toggles[p]);
    }
    Serial.println(F("scan done"));
  } else if (line.length() > 0) {
    Serial.println(F("Commands: roi x y w h | stream 1|0"));
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

  printBoardInfo();
  eyeCrop = (uint8_t *)ps_malloc(EYE_INPUT_SIZE * EYE_INPUT_SIZE);
  preview = (uint8_t *)ps_malloc(PREVIEW_W * PREVIEW_H);
  cameraOk = eyeCrop != nullptr && preview != nullptr && cameraInit();
  modelOk = eyeModelInit();
}

void loop() {
  if (!cameraOk) {
    delay(2000);
    Serial.println(F("Camera not ready, reset the board after checking the cable"));
    return;
  }

  handleSerial();

  static GrayImage frame = {};
  uint32_t tGrab = millis();
  if (!cameraGrabGray(frame)) {
    Serial.println(F("Frame grab failed"));
    delay(100);
    return;
  }
  tGrab = millis() - tGrab;
  cropResizeGray(frame, eyeRoi, eyeCrop, EYE_INPUT_SIZE, EYE_INPUT_SIZE);
  EyeRoi full = {0, 0, frame.width, frame.height};
  cropResizeGray(frame, full, preview, PREVIEW_W, PREVIEW_H);
  uint8_t frameMean = meanBrightness(preview, PREVIEW_W * PREVIEW_H);

  float pClosed = -1.0f;
  uint32_t inferMs = 0;
  if (modelOk) {
    uint32_t t0 = millis();
    pClosed = eyeModelPredictClosed(eyeCrop);
    inferMs = millis() - t0;
  }
  if (streaming) sendStreamFrame(pClosed, frame.width, frame.height);

  static unsigned long lastReport = 0;
  static uint32_t frames = 0;
  frames++;
  if (millis() - lastReport >= 1000) {
    Serial.printf("fps %lu | frame mean %u | eye crop mean %u | P(closed) %.2f in %lu ms | grab+decode %lu ms\n",
                  (unsigned long)frames, frameMean, meanBrightness(eyeCrop, EYE_INPUT_SIZE * EYE_INPUT_SIZE),
                  pClosed, (unsigned long)inferMs, (unsigned long)tGrab);
    frames = 0;
    lastReport = millis();
  }
}
