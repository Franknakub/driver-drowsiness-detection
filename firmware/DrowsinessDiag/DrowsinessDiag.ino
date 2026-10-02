// DrowsinessDiag — ตรวจบอร์ด + กล้อง + IR filter ในสเก็ตช์เดียว
// บอร์ด: GOOUUU ESP32-S3-CAM (ESP32-S3-WROOM-1 N16R8)
//
// ตอบ 3 คำถาม:
//   1) Flash / PSRAM ที่ได้มาจริงเท่าไหร่
//   2) กล้องที่มาในกล่องเป็นเซนเซอร์ตัวไหน (OV2640 / OV3660 / OV5640)
//   3) กล้องมี IR-cut filter ไหม  <-- ชี้รีโมททีวีเข้าเลนส์แล้วดูค่า brightness

#include "esp_camera.h"
#include "esp_heap_caps.h"

// ---- ขากล้อง: GOOUUU ESP32-S3-CAM (จาก PROJECT_STATUS.md) ----
// PWDN/RESET ไม่ได้ต่อออกมาที่ header -> ใช้ -1
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM   4   // SCCB SDA
#define SIOC_GPIO_NUM   5   // SCCB SCL
#define Y9_GPIO_NUM    16   // D7
#define Y8_GPIO_NUM    17   // D6
#define Y7_GPIO_NUM    18   // D5
#define Y6_GPIO_NUM    12   // D4
#define Y5_GPIO_NUM    10   // D3
#define Y4_GPIO_NUM     8   // D2
#define Y3_GPIO_NUM     9   // D1
#define Y2_GPIO_NUM    11   // D0
#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM  13

static bool cameraOk = false;

static const char* sensorName(uint16_t pid) {
  switch (pid) {
    case 0x26:   return "OV2640";
    case 0x3660: return "OV3660";
    case 0x5640: return "OV5640";
    case 0x7670: return "OV7670";
    case 0x7725: return "OV7725";
    case 0x0031: return "SC031GS";
    default:     return "ไม่รู้จัก";
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);                 // รอ USB CDC ขึ้น
  Serial.println();
  Serial.println("================ DrowsinessDiag ================");

  // ---------- 1) บอร์ด ----------
  Serial.println("\n--- [1] บอร์ด ---");
  Serial.printf("Chip        : %s rev%d, %d core(s) @ %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), getCpuFrequencyMhz());
  Serial.printf("Flash       : %u MB\n", ESP.getFlashChipSize() / (1024U * 1024U));

  if (psramFound()) {
    Serial.printf("PSRAM       : %u MB  <-- ต้องมี ไม่งั้น ML ทำไม่ได้\n",
                  ESP.getPsramSize() / (1024U * 1024U));
    Serial.printf("PSRAM free  : %u KB\n", ESP.getFreePsram() / 1024U);
  } else {
    Serial.println("PSRAM       : *** ไม่พบ ***");
    Serial.println("  -> ถ้าเป็นบอร์ด N16R8 แปลว่าลืมตั้ง Tools > PSRAM = OPI PSRAM");
  }
  Serial.printf("Heap free   : %u KB\n", ESP.getFreeHeap() / 1024U);

  // ---------- 2) กล้อง ----------
  Serial.println("\n--- [2] กล้อง ---");
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;   c.pin_pclk  = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href  = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn  = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_GRAYSCALE;   // ขาวดำ: อ่าน brightness ได้ตรงๆ
  c.frame_size   = FRAMESIZE_QVGA;        // เล็ก เร็ว พอสำหรับทดสอบ
  c.fb_count     = 1;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("init ล้มเหลว: 0x%x\n", err);
    Serial.println("  เช็ค: สาย FPC เสียบแน่น/ถูกด้านไหม · ขากล้องตรงกับบอร์ดรุ่นนี้ไหม");
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  Serial.printf("Sensor      : %s  (PID 0x%04X)\n", sensorName(s->id.PID), s->id.PID);
  cameraOk = true;

  // *** ล็อก exposure/gain ***
  // ถ้าปล่อย AGC/AEC ไว้ พอ IR ส่องเข้ามากล้องจะลด gain ชดเชยทันที
  // -> ค่าที่อ่านได้ไม่ขยับ ทดสอบไม่ได้เลย ต้องล็อกให้คงที่ก่อน
  s->set_gain_ctrl(s, 0);        // ปิด AGC
  s->set_exposure_ctrl(s, 0);    // ปิด AEC
  s->set_agc_gain(s, 10);        // gain คงที่ (0-30)
  s->set_aec_value(s, 400);      // exposure คงที่ (0-1200)
  s->set_whitebal(s, 0);
  s->set_awb_gain(s, 0);
  s->set_bpc(s, 1);              // ตัด hot pixel
  s->set_wpc(s, 1);

  // ---------- 3) ทดสอบ IR filter ----------
  Serial.println("\n--- [3] ทดสอบ IR-cut filter ---");
  Serial.println("ปิดไฟห้องให้มืด แล้วชี้รีโมททีวี/แอร์เข้าเลนส์ ห่าง ~5cm กดปุ่มค้าง");
  Serial.println("ดูค่า avg ด้านล่าง:");
  Serial.println("  กดแล้ว avg เพิ่มชัดเจน  = ไม่มี IR filter  -> IR LED ใช้ได้ ");
  Serial.println("  กดแล้ว avg นิ่งเท่าเดิม = มี IR filter     -> IR LED ไม่มีผล");
  Serial.println();
}

void loop() {
  if (!cameraOk) { delay(2000); return; }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("จับภาพไม่ได้"); delay(500); return; }

  // GRAYSCALE = 1 ไบต์ต่อพิกเซล
  // นับ "จำนวนพิกเซลสว่าง" แทนการดู peak ของพิกเซลเดียว
  // เพราะ peak ไวต่อ noise มาก (hot pixel จุดเดียวก็ดันขึ้นเต็ม)
  // แหล่ง IR จริงจะสว่างเป็นกลุ่ม -> นับได้หลายสิบถึงหลายร้อยพิกเซล
  uint32_t sum = 0;
  uint8_t  peak = 0;
  uint32_t n150 = 0, n200 = 0, n240 = 0;
  for (size_t i = 0; i < fb->len; i++) {
    uint8_t v = fb->buf[i];
    sum += v;
    if (v > peak) peak = v;
    if (v >= 150) { n150++; if (v >= 200) { n200++; if (v >= 240) n240++; } }
  }
  uint32_t avg = sum / fb->len;

  Serial.printf("avg=%3u peak=%3u | px>=150:%5u  >=200:%5u  >=240:%5u\n",
                avg, peak, n150, n200, n240);

  esp_camera_fb_return(fb);
  delay(250);
}
