// BenchTest — ทดสอบ LED / LDR / OLED / buzzer / IR บนบอร์ด v1 โดยไม่เสียบโมดูล
// ผังต่อสาย: driver-drowsiness-detection/docs/bench-wiring-v1.md
//
// พิมพ์คำสั่งใน Serial Monitor (115200) แล้ว Enter:
//   l = LED   d = LDR   o = OLED   b = buzzer   i = IR + กล้อง   a = ทุกข้อ
// ข้อความ serial เป็นภาษาอังกฤษ เพราะตัวอ่าน PowerShell ไม่ decode UTF-8

#include <Wire.h>
#include "esp_camera.h"

// ---- ขาตามแบบ (GPIO map ใน CLAUDE.md) ----
#define PIN_LED_GREEN 47
#define PIN_LED_RED   40
#define PIN_LDR        3   // ADC1 · R1 10k ขึ้น 3V3, LDR GL5528 ลง GND -> มืด = ค่าสูง
#define PIN_BUZZER    14   // โมดูล MH-FMD active-LOW: HIGH = เงียบ
#define PIN_IR        21   // R3 1k -> base S8050 (Q3): HIGH = IR ติด
#define PIN_OLED_SDA  42
#define PIN_OLED_SCL   1

// ---- ขากล้อง GOOUUU ESP32-S3-CAM (เหมือน DrowsinessDiag) ----
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM   4
#define SIOC_GPIO_NUM   5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM     8
#define Y3_GPIO_NUM     9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM  13

static bool cameraOk = false;
static uint8_t oledAddr = 0;

// ================= LED =================
static void testLed() {
  Serial.println("\n[LED] green 1s -> red 1s -> both blink x3");
  digitalWrite(PIN_LED_GREEN, HIGH); delay(1000); digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, HIGH);   delay(1000); digitalWrite(PIN_LED_RED, LOW);
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_GREEN, HIGH); digitalWrite(PIN_LED_RED, HIGH); delay(250);
    digitalWrite(PIN_LED_GREEN, LOW);  digitalWrite(PIN_LED_RED, LOW);  delay(250);
  }
  Serial.println("[LED] done - did you see green, then red, then both blink?");
}

// ================= LDR =================
static void testLdr() {
  Serial.println("\n[LDR] 10 s - cover the LDR with your hand: value should go UP when dark");
  uint32_t t0 = millis();
  int lo = 4095, hi = 0;
  while (millis() - t0 < 10000) {
    int raw = analogRead(PIN_LDR);
    int mv  = analogReadMilliVolts(PIN_LDR);
    if (raw < lo) lo = raw;
    if (raw > hi) hi = raw;
    Serial.printf("[LDR] raw=%4d  mV=%4d\n", raw, mv);
    delay(250);
  }
  Serial.printf("[LDR] min=%d max=%d swing=%d %s\n", lo, hi, hi - lo,
                (hi - lo) > 300 ? "-> OK, LDR responds" : "-> small swing: check wire 5 (GPIO3) and 3V3/GND");
}

// ================= OLED (SSD1306 แบบไม่ใช้ไลบรารี) =================
static void oledCmd(uint8_t c) {
  Wire.beginTransmission(oledAddr); Wire.write(0x00); Wire.write(c); Wire.endTransmission();
}

static void oledFill(uint8_t (*pattern)(int col, int page)) {
  oledCmd(0x21); oledCmd(0); oledCmd(127);   // column 0-127
  oledCmd(0x22); oledCmd(0); oledCmd(7);     // page 0-7
  for (int page = 0; page < 8; page++) {
    for (int col = 0; col < 128; col += 16) {
      Wire.beginTransmission(oledAddr);
      Wire.write(0x40);
      for (int k = 0; k < 16; k++) Wire.write(pattern(col + k, page));
      Wire.endTransmission();
    }
  }
}

static uint8_t patWhite(int, int)        { return 0xFF; }
static uint8_t patBlack(int, int)        { return 0x00; }
static uint8_t patChecker(int c, int)    { return (c & 1) ? 0xAA : 0x55; }
// กรอบรอบจอ: ถ้าขอบซ้าย/ขวาหายหรือเลื่อน 2 พิกเซล แปลว่าเป็น SH1106 ไม่ใช่ SSD1306
static uint8_t patBorder(int c, int p) {
  if (c == 0 || c == 127) return 0xFF;
  uint8_t v = 0;
  if (p == 0) v |= 0x01;
  if (p == 7) v |= 0x80;
  return v;
}

static void testOled() {
  Serial.println("\n[OLED] I2C scan on SDA=42 SCL=1");
  oledAddr = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[OLED] found device at 0x%02X\n", a);
      if (a == 0x3C || a == 0x3D) oledAddr = a;
    }
  }
  if (!oledAddr) {
    Serial.println("[OLED] no display at 0x3C/0x3D - check wires 6,7 and H1 VCC/GND/SCL/SDA order");
    return;
  }
  const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
                          0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
                          0xDB, 0x40, 0xA4, 0xA6, 0xAF};
  for (uint8_t c : init) oledCmd(c);
  Serial.println("[OLED] white 1s -> checkerboard 1s -> border (stays on)");
  oledFill(patWhite);   delay(1000);
  oledFill(patChecker); delay(1000);
  oledFill(patBlack);
  oledFill(patBorder);
  Serial.println("[OLED] done - border must touch all 4 edges. Missing/shifted edge = SH1106, tell Claude");
}

// ================= Buzzer =================
static void testBuzzer() {
  Serial.println("\n[BUZZER] 3 beeps (LOW = sound)");
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUZZER, LOW);  delay(200);
    digitalWrite(PIN_BUZZER, HIGH); delay(300);
  }
  Serial.println("[BUZZER] done - 3 beeps and silent now? If it never stops, unplug: VCC must be 3V3 not 5V");
}

// ================= IR + กล้อง =================
static bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;   c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn  = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size   = FRAMESIZE_QVGA;
  c.fb_count     = 1;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&c) != ESP_OK) return false;

  // ล็อก exposure/gain ไม่งั้นกล้องจะชดเชยแสง IR จนค่าไม่ขยับ
  sensor_t* s = esp_camera_sensor_get();
  s->set_gain_ctrl(s, 0);
  s->set_exposure_ctrl(s, 0);
  s->set_agc_gain(s, 10);
  s->set_aec_value(s, 400);
  s->set_whitebal(s, 0);
  s->set_awb_gain(s, 0);
  s->set_bpc(s, 1);
  s->set_wpc(s, 1);
  return true;
}

// นับพิกเซลสว่าง (ไม่ใช้ peak เพราะ hot pixel จุดเดียวหลอกได้) เฉลี่ยจาก 4 เฟรม
static uint32_t brightPixels(uint8_t threshold) {
  uint32_t total = 0;
  for (int f = 0; f < 5; f++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) continue;
    if (f > 0) {                         // ทิ้งเฟรมแรกที่อาจถ่ายก่อน IR เปลี่ยนสถานะ
      for (size_t i = 0; i < fb->len; i++) if (fb->buf[i] >= threshold) total++;
    }
    esp_camera_fb_return(fb);
  }
  return total / 4;
}

static void testIr() {
  Serial.println("\n[IR] 1) look at the IR LEDs through a PHONE camera: they should glow purple while ON");
  Serial.println("[IR] 2) point the OV5640 lens at the IR LEDs (~10 cm), keep it still");
  if (!cameraOk) {
    Serial.println("[IR] camera not ready - toggling IR only (3 x 2 s ON)");
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_IR, HIGH); delay(2000); digitalWrite(PIN_IR, LOW); delay(1000);
    }
    return;
  }
  delay(3000);
  uint32_t offSum = 0, onSum = 0;
  for (int cycle = 1; cycle <= 3; cycle++) {
    digitalWrite(PIN_IR, LOW);  delay(500);
    uint32_t off = brightPixels(200);
    digitalWrite(PIN_IR, HIGH); delay(500);
    uint32_t on = brightPixels(200);
    digitalWrite(PIN_IR, LOW);
    Serial.printf("[IR] cycle %d: px>=200  OFF=%6u  ON=%6u  diff=%+d\n",
                  cycle, off, on, (int)on - (int)off);
    offSum += off; onSum += on;
  }
  int diff = (int)(onSum / 3) - (int)(offSum / 3);
  Serial.printf("[IR] average diff = %+d pixels\n", diff);
  if (diff > 200)
    Serial.println("[IR] -> camera SEES 940nm IR (no IR-cut filter). Night mode is possible");
  else
    Serial.println("[IR] -> little/no change. Either IR LEDs are not lit (check with phone camera, wires 8,9)"
                   " or the lens has an IR-cut filter. Report both observations to Claude");
}

// =======================================
static void runAll() { testLed(); testBuzzer(); testOled(); testLdr(); testIr(); }

static void printMenu() {
  Serial.println("\nCommands: l=LED  d=LDR  o=OLED  b=buzzer  i=IR+camera  a=all");
}

void setup() {
  // buzzer เป็น active-LOW: ดึงขึ้นก่อนเป็นอย่างแรก กันดังตอนบูต
  pinMode(PIN_BUZZER, INPUT_PULLUP);
  digitalWrite(PIN_BUZZER, HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH);

  pinMode(PIN_IR, OUTPUT);        digitalWrite(PIN_IR, LOW);
  pinMode(PIN_LED_GREEN, OUTPUT); digitalWrite(PIN_LED_GREEN, LOW);
  pinMode(PIN_LED_RED, OUTPUT);   digitalWrite(PIN_LED_RED, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);   // อ่านได้ถึง ~3.1V

  Serial.begin(115200);
  delay(3000);
  Serial.println("\n================ BenchTest (v1 board, module NOT plugged in) ================");

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);

  cameraOk = initCamera();
  Serial.printf("Camera: %s\n", cameraOk ? "OK (exposure locked)" : "FAILED - IR test will run without camera");

  printMenu();
}

void loop() {
  if (!Serial.available()) { delay(20); return; }
  char c = Serial.read();
  switch (c) {
    case 'l': testLed();    break;
    case 'd': testLdr();    break;
    case 'o': testOled();   break;
    case 'b': testBuzzer(); break;
    case 'i': testIr();     break;
    case 'a': runAll();     break;
    default:  return;       // ข้าม \r \n
  }
  printMenu();
}
