// vision_task: ดึงภาพจากกล้อง · ตอนนี้วัด fps อย่างเดียว
// ขั้นต่อไป: หมุนภาพ (ภาพจากกล้องตะแคง 90°) -> HumanFaceDetect (โค้ดจาก FaceSpike)
//            -> ตัด Eye Region -> eye_classifier -> drowsiness
#include "esp_camera.h"
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

static bool cameraInit() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;  c.pin_d1 = CAM_PIN_D1;
  c.pin_d2 = CAM_PIN_D2;  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;  c.pin_d5 = CAM_PIN_D5;
  c.pin_d6 = CAM_PIN_D6;  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;    c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC;  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn  = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;   // ผล FaceSpike: RGB565 QVGA เร็วที่สุดสำหรับหาหน้า
  c.frame_size   = FRAMESIZE_QVGA;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;   // ไม่เปิด AF: fixed-focus ตั้งใจไว้
}

static void visionTask(void*) {
  if (!cameraInit()) {
    Serial.println("[vision] camera init FAILED");
    vTaskDelete(nullptr);
  }
  Serial.println("[vision] camera OK (RGB565 QVGA)");

  uint32_t windowStart = millis(), frames = 0;
  for (;;) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    // TODO: หาหน้า + Eye State จาก fb แล้วคัดลอกเฉพาะผลลัพธ์ลง g_state (ห้ามส่ง fb ข้าม core)
    esp_camera_fb_return(fb);

    frames++;
    uint32_t now = millis();
    if (now - windowStart >= 1000) {
      StateGuard g;
      g_state.fps = frames * 1000.0f / (now - windowStart);
      g_state.visionFrames += frames;
      frames = 0;
      windowStart = now;
    }
  }
}

void startVisionTask() {
  xTaskCreatePinnedToCore(visionTask, "vision", 8192, nullptr, 5, nullptr, CORE_VISION);
}
