// oled_task: จอ SSD1306 128x64 สำหรับคนขับ (driver เขียนเอง ไม่ใช้ไลบรารี เหมือน BenchTest)
//   แสดงทีละหน้า ตัวใหญ่ 1 คำ + บรรทัดเล็ก 1 บรรทัด · ไม่กะพริบ · หรี่จอเมื่อมืด (เกณฑ์ LDR เดียวกับ IR)
//   เฟส 1 (บูต): CONNECTING -> WIFI OK + ชื่อ WiFi / NO WIFI + ชื่อ AP ค้าง OLED_WIFI_SHOW_MS
//   เฟส 2 (ใช้งาน): ลำดับ WAKE UP! > DROWSY > EYES ON THE ROAD > ข้อความแทรก > NO READ > TIRED > WAITING > AWAKE
//   ข้อความแทรก OLED_NOTICE_MS: MUTED (กด BOOT / ปิดเสียงจากเว็บ) · WIFI LOST / WIFI OK ระหว่างใช้งาน
//   ค่าดีบัก (P ตาปิด, fps, LDR, IP, Sync) ดูที่หน้าเว็บของ Device แทน
// อยู่ Core 0 priority ต่ำ: I2C ส่งทั้งจอ ~25 ms ไม่แย่งเวลา vision
#include <Wire.h>
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

static const uint8_t FONT5X7[95][5] = {   // ASCII 0x20..0x7E · คอลัมน์ละไบต์ บิต 0 = บนสุด
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

static uint8_t s_addr = 0;
static uint8_t s_fb[8][128];   // 8 page x 128 คอลัมน์ · บิต 0 ของแต่ละไบต์ = แถวบนสุดของ page

static void cmd(uint8_t c) {
  Wire.beginTransmission(s_addr); Wire.write(0x00); Wire.write(c); Wire.endTransmission();
}

static bool probe() {
  for (uint8_t a : {(uint8_t)0x3C, (uint8_t)0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { s_addr = a; return true; }
  }
  s_addr = 0;
  return false;
}

static void initDisplay() {
  // ชุดเดียวกับ BenchTest ที่ทดสอบผ่านบนบอร์ด v2 (23 ต.ค.)
  const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
                          0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
                          0xDB, 0x40, 0xA4, 0xA6, 0xAF};
  for (uint8_t c : init) cmd(c);
}

// ส่งทั้งจอ · คืน false ถ้า I2C ล้ม (จอหลุด)
static bool flush() {
  cmd(0x21); cmd(0); cmd(127);   // column 0-127
  cmd(0x22); cmd(0); cmd(7);     // page 0-7
  for (int p = 0; p < 8; p++) {
    for (int c = 0; c < 128; c += 32) {
      Wire.beginTransmission(s_addr);
      Wire.write(0x40);
      Wire.write(&s_fb[p][c], 32);
      if (Wire.endTransmission() != 0) return false;
    }
  }
  return true;
}

static const uint8_t* glyph(char ch) {
  if (ch < 0x20 || ch > 0x7E) ch = '?';
  return FONT5X7[ch - 0x20];
}

static void pixel(int x, int y) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64) s_fb[y >> 3][x] |= 1 << (y & 7);
}

// ตัว 5x7 ขยาย scale เท่า มุมซ้ายบนที่ (x, y) · ช่องไฟ 1 คอลัมน์ (x scale) ระหว่างตัว
static void textScaled(int x, int y, const char* s, int scale) {
  for (; *s; s++, x += 6 * scale) {
    const uint8_t* g = glyph(*s);
    for (int i = 0; i < 5; i++)
      for (int b = 0; b < 7; b++)
        if (g[i] & (1 << b))
          for (int dx = 0; dx < scale; dx++)
            for (int dy = 0; dy < scale; dy++) pixel(x + i * scale + dx, y + b * scale + dy);
  }
}

static int textWidth(const char* s, int scale) {
  int n = strlen(s);
  return n ? (6 * n - 1) * scale : 0;
}

// scale ใหญ่สุดไม่เกิน maxScale ที่ทุกบรรทัดกว้างไม่เกินจอ
static int fitScale(const char* const* lines, int n, int maxScale) {
  for (int sc = maxScale; sc > 1; sc--) {
    bool ok = true;
    for (int i = 0; i < n; i++) if (textWidth(lines[i], sc) > 128) ok = false;
    if (ok) return sc;
  }
  return 1;
}

struct Screen {
  const char* big1;
  const char* big2;    // บรรทัดที่สองของตัวใหญ่ · nullptr = ไม่มี (ใช้กับ WAKE / UP! ให้ตัวใหญ่สุด)
  const char* small;   // nullptr = ไม่มี
};

// ตัวใหญ่สูงสุด x4 (24x28 ต่อตัว) · ตัวเล็ก x2 ถ้าพอ ไม่งั้น x1 · จัดกลางทั้งแนวตั้งและแนวนอน
static void drawScreen(const Screen& sc) {
  const char* big[2] = {sc.big1, sc.big2};
  int nBig = sc.big2 ? 2 : 1;
  int ss = sc.small ? fitScale(&sc.small, 1, 2) : 0;
  int smallH = ss ? 6 + 7 * ss : 0;               // ช่องว่าง 6 + ความสูงตัวเล็ก
  int bs = fitScale(big, nBig, 4);
  while (bs > 1 && nBig * 7 * bs + (nBig - 1) * 2 * bs + smallH > 64) bs--;
  int y = (64 - (nBig * 7 * bs + (nBig - 1) * 2 * bs + smallH)) / 2;
  for (int i = 0; i < nBig; i++) {
    textScaled((128 - textWidth(big[i], bs)) / 2, y, big[i], bs);
    y += 9 * bs;                                    // สูง 7 + ช่องว่างระหว่างบรรทัด 2
  }
  y -= 2 * bs;
  if (ss) textScaled((128 - textWidth(sc.small, ss)) / 2, y + 6, sc.small, ss);
}

// ทดสอบจากหน้าเว็บ: สลับทั้งจอขาว / ตาราง ให้เห็นว่าทุกพิกเซลติดและขอบครบ
static void renderTest(uint32_t now) {
  bool white = (now / 500) % 2 == 0;
  for (int p = 0; p < 8; p++)
    for (int c = 0; c < 128; c++) s_fb[p][c] = white ? 0xFF : ((c & 1) ? 0xAA : 0x55);
}

// เลือกหน้าที่จะแสดง · เก็บสถานะเฟสบูตและข้อความแทรกข้ามเฟรม
class ScreenPicker {
 public:
  Screen pick(const SharedState& s, uint32_t now) {
    // ข้อความแทรก: ปิดเสียง (กด BOOT ซ้ำก็แสดงใหม่) · WiFi หลุด/กลับมาหลังจบเฟสบูต
    bool muted = s.mutedUntilMs && (int32_t)(s.mutedUntilMs - now) > 0;
    if (muted && s.mutedUntilMs != prevMuteUntil_) {
      snprintf(noticeLine_, sizeof(noticeLine_), "%lu MIN",
               (unsigned long)((s.mutedUntilMs - now + 59999) / 60000));
      notice_ = {"MUTED", nullptr, noticeLine_};
      noticeEndMs_ = now + OLED_NOTICE_MS;
    }
    prevMuteUntil_ = s.mutedUntilMs;
    if (!boot_ && s.staConnected != prevConnected_) {
      if (s.staConnected) {
        snprintf(noticeLine_, sizeof(noticeLine_), "%.21s", s.staSsid);
        notice_ = {"WIFI OK", nullptr, noticeLine_};
      } else {
        notice_ = {"WIFI LOST", nullptr, "TRYING AGAIN"};
      }
      noticeEndMs_ = now + OLED_NOTICE_MS;
    }
    prevConnected_ = s.staConnected;

    // เฟสบูต: รอลอง WiFi รอบแรกจบ แล้วค้างผลไว้ OLED_WIFI_SHOW_MS
    Screen bootScreen = {};
    if (boot_) {
      if (!s.staTried || s.staTrying[0]) {
        snprintf(bootLine_, sizeof(bootLine_), "%.21s", s.staTrying[0] ? s.staTrying : "WIFI");
        bootScreen = {"CONNECTING", nullptr, bootLine_};
      } else {
        if (!bootEndMs_) bootEndMs_ = now + OLED_WIFI_SHOW_MS;
        if (s.staConnected) {
          snprintf(bootLine_, sizeof(bootLine_), "%.21s", s.staSsid);
          bootScreen = {"WIFI OK", nullptr, bootLine_};
        } else {
          snprintf(bootLine_, sizeof(bootLine_), "AP %.18s", s.apSsid);
          bootScreen = {"NO WIFI", nullptr, bootLine_};
        }
        if ((int32_t)(now - bootEndMs_) >= 0) boot_ = false;
      }
    }

    if (s.level == DrowsinessLevel::Microsleep)  return {"WAKE", "UP!", nullptr};
    if (s.level == DrowsinessLevel::Drowsy)      return {"DROWSY", nullptr, "PULL OVER"};
    if (s.distraction)                           return {"EYES ON", nullptr, "THE ROAD"};
    if (noticeEndMs_ && (int32_t)(noticeEndMs_ - now) > 0) return notice_;
    if (boot_ && bootScreen.big1)                return bootScreen;
    if (s.unmonitored)                           return {"NO READ", nullptr, "CHECK CAMERA"};
    if (s.level == DrowsinessLevel::Fatigue)     return {"TIRED", nullptr, "TAKE A BREAK"};
    if (!s.driverPresent)                        return {"WAITING", nullptr, "LOOK AT CAMERA"};
    return {"AWAKE", nullptr, nullptr};
  }

 private:
  bool     boot_ = true;
  uint32_t bootEndMs_ = 0;
  char     bootLine_[24];
  bool     prevConnected_ = false;
  uint32_t prevMuteUntil_ = 0;
  Screen   notice_ = {};
  uint32_t noticeEndMs_ = 0;
  char     noticeLine_[24];
};

static void oledTask(void*) {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  bool ok = false;
  bool dark = false;
  int contrast = -1;
  ScreenPicker picker;
  for (;;) {
    uint32_t now = millis();
    if (!ok) {
      ok = probe();
      if (ok) { initDisplay(); contrast = -1; Serial.printf("[oled] SSD1306 at 0x%02X\n", s_addr); }
      { StateGuard g; g_state.oledOk = ok; }
      if (!ok) { vTaskDelay(pdMS_TO_TICKS(OLED_RETRY_MS)); continue; }
    }
    SharedState s = stateSnapshot();

    if (!dark && s.ldrRaw >= LDR_DARK_ON_RAW)  dark = true;
    if (dark  && s.ldrRaw <= LDR_DARK_OFF_RAW) dark = false;
    int want = dark ? OLED_CONTRAST_DARK : OLED_CONTRAST_DAY;
    if (want != contrast) { cmd(0x81); cmd(want); contrast = want; }

    Screen sc = picker.pick(s, now);   // เรียกทุกเฟรมแม้กำลังทดสอบ ให้เฟสบูต/ข้อความแทรกเดินตามเวลา
    memset(s_fb, 0, sizeof(s_fb));
    if (s.hwTest == HwTest::Oled && s.hwTestUntilMs && (int32_t)(s.hwTestUntilMs - now) > 0) renderTest(now);
    else drawScreen(sc);
    if (!flush()) {
      ok = false;
      StateGuard g; g_state.oledOk = false;
    }
    vTaskDelay(pdMS_TO_TICKS(OLED_PERIOD_MS));
  }
}

void startOledTask() {
  xTaskCreatePinnedToCore(oledTask, "oled", 4096, nullptr, 1, nullptr, CORE_NET);
}
