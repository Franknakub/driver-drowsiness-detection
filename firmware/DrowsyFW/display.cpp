// display_task: จอ OLED 128x64 (SSD1306) ที่ H1 · Core 0 priority ต่ำ อัปเดตทุก DISPLAY_PERIOD_MS
// ข้อความภาษาอังกฤษ · Alert แสดงเต็มจอ พื้นดำ ไม่กะพริบ · หรี่จอเมื่อมืด (เกณฑ์ LDR เดียวกับ IR)
// หาจอไม่เจอ = จบ task ไป ระบบส่วนอื่นทำงานต่อ
#include <Wire.h>
#include <U8g2lib.h>
#include "config.h"
#include "shared_state.h"
#include "sync.h"
#include "tasks.h"

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_oled(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

struct Screen {
  const char* big;
  const char* small;      // nullptr = ไม่มีบรรทัดเล็ก
  bool        statusBar;  // แถบ WIFI/SYNC/IR/MUTE ด้านบน (เฉพาะจอที่ไม่ใช่ Alert)
};

// ลำดับ: Microsleep > ง่วง > Distraction > Unmonitored > เริ่มล้า > รอ Driver > ปกติ
static Screen pickScreen(const SharedState& s, char* perclosLine, size_t n) {
  switch (s.level) {
    case DrowsinessLevel::Microsleep: return {"WAKE UP!", nullptr, false};
    case DrowsinessLevel::Drowsy:     return {"DROWSY", "PULL OVER", false};
    default: break;
  }
  if (s.distraction)                          return {"EYES ON", "THE ROAD", false};
  if (s.unmonitored)                          return {"NO READ", "CHECK CAMERA", false};
  if (s.level == DrowsinessLevel::Fatigue)    return {"TIRED", "TAKE A BREAK", false};
  if (!s.driverPresent)                       return {"WAITING", "LOOK AT CAMERA", true};
  if (s.perclosReady) snprintf(perclosLine, n, "PERCLOS %d%%", (int)lroundf(s.perclos * 100));
  else                snprintf(perclosLine, n, "PERCLOS --");
  return {"OK", perclosLine, true};
}

// ใช้ฟอนต์ใหญ่สุดที่ข้อความยังกว้างไม่เกินจอ
static void drawCentered(const char* txt, int baseline, const uint8_t* const* fonts, int nFonts) {
  for (int i = 0; i < nFonts; i++) {
    s_oled.setFont(fonts[i]);
    if (s_oled.getStrWidth(txt) <= 124) break;
  }
  s_oled.drawStr((128 - s_oled.getStrWidth(txt)) / 2, baseline, txt);
}

static const uint8_t* const kBigFonts[]   = {u8g2_font_logisoso28_tr, u8g2_font_logisoso22_tr,
                                             u8g2_font_helvB18_tr, u8g2_font_helvB14_tr};
static const uint8_t* const kSmallFonts[] = {u8g2_font_6x13B_tr, u8g2_font_5x7_tr};

static void drawStatusBar(const SharedState& s, uint32_t now) {
  SyncStatus sy = syncStatus();
  char left[24];
  const char* sync = "";
  if (sy.configured && s.staConnected) {
    if (sy.lastHttp < 0 || sy.lastHttp >= 300)      sync = " SYNC ERR";
    else if (sy.pendingSamples + sy.pendingEvents)  sync = " SYNCING";
    else                                            sync = " SYNCED";
  }
  snprintf(left, sizeof(left), "%s%s", s.staConnected ? "WIFI" : "NO WIFI", sync);
  bool muted = s.mutedUntilMs && (int32_t)(s.mutedUntilMs - now) > 0;
  char right[12];
  snprintf(right, sizeof(right), "%s%s%s", s.irOn ? "IR" : "", s.irOn && muted ? " " : "", muted ? "MUTE" : "");
  s_oled.setFont(u8g2_font_5x7_tr);
  s_oled.drawStr(0, 7, left);
  s_oled.drawStr(128 - s_oled.getStrWidth(right), 7, right);
}

static void displayTask(void*) {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  uint8_t addr = 0;
  for (uint8_t a : {0x3C, 0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { addr = a; break; }
  }
  if (!addr) {
    Serial.println("[oled] no display at 0x3C/0x3D - running without OLED");
    vTaskDelete(nullptr);
  }
  s_oled.setI2CAddress(addr << 1);
  s_oled.setBusClock(400000);
  s_oled.begin();
  Serial.printf("[oled] SSD1306 at 0x%02X\n", addr);

  bool dark = false;
  int contrast = -1;
  char perclosLine[20];
  for (;;) {
    SharedState s = stateSnapshot();
    uint32_t now = millis();

    if (!dark && s.ldrRaw >= LDR_DARK_ON_RAW)  dark = true;
    if (dark  && s.ldrRaw <= LDR_DARK_OFF_RAW) dark = false;
    int want = dark ? OLED_CONTRAST_DARK : OLED_CONTRAST_DAY;
    if (want != contrast) { s_oled.setContrast(want); contrast = want; }

    Screen sc = pickScreen(s, perclosLine, sizeof(perclosLine));
    s_oled.clearBuffer();
    if (sc.statusBar) drawStatusBar(s, now);
    int bigBase = sc.small ? (sc.statusBar ? 42 : 38) : 46;
    drawCentered(sc.big, bigBase, kBigFonts, 4);
    if (sc.small) drawCentered(sc.small, 60, kSmallFonts, 2);
    s_oled.sendBuffer();

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

void startDisplayTask() {
  xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 1, nullptr, CORE_NET);
}
