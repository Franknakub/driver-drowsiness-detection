// ทดสอบ DrowsinessJudge บน PC ด้วยลำดับเฟรมจำลอง (เฟรมละ 150 ms) · ไม่ต้องมีบอร์ด
// อยู่นอกโฟลเดอร์สเก็ตช์ เพราะ Arduino compile ทุก .cpp ในโฟลเดอร์นั้น
//
//   g++ -std=c++17 -Wall -I../DrowsyFW drowsiness_test.cpp ../DrowsyFW/drowsiness.cpp -o drowsiness_test && ./drowsiness_test
#include <cmath>
#include <cstdio>
#include <functional>
#include "drowsiness.h"
#include "drowsy_config.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
  printf("  FAIL %s:%d  %s  -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const uint32_t FRAME = 150;
using S = EyeState;
using L = DrowsinessLevel;

// ขับ judge ทีละเฟรม พร้อมเก็บสถิติที่ทดสอบต้องใช้
struct Sim {
  DrowsinessJudge j;
  uint32_t t = 10000;                // เริ่มที่ไม่ใช่ 0 กันบั๊กที่ซ่อนอยู่หลังค่าเริ่มต้น
  int maxLevel = 0;
  uint32_t maxClosed = 0;
  int microsleeps = 0, distractions = 0, levelChanges = 0;
  bool everDistraction = false, everUnmonitored = false;

  void frame(S s) {
    t += FRAME;
    const DrowsyResult& r = j.update(t, s);
    if ((int)r.level > maxLevel) maxLevel = (int)r.level;
    if (r.closedMs > maxClosed) maxClosed = r.closedMs;
    everDistraction |= r.distraction;
    everUnmonitored |= r.unmonitored;
    DrowsyEvent e;
    while (j.popEvent(e)) {
      if (e.type == DrowsyEventType::Microsleep) microsleeps++;
      else if (e.type == DrowsyEventType::Distraction) distractions++;
      else levelChanges++;
    }
  }
  // เล่นสถานะเดียวนาน ms (ปัดลงเป็นจำนวนเฟรม) · each ถูกเรียกหลังทุกเฟรม
  void run(S s, uint32_t ms, const std::function<void(const DrowsyResult&, uint32_t)>& each = nullptr) {
    for (uint32_t el = FRAME; el <= ms; el += FRAME) { frame(s); if (each) each(j.result(), el); }
  }
  // สัดส่วนตาปิด p กระจายสม่ำเสมอ (แบบ Bresenham) ช่วงตาปิดแต่ละครั้งสั้น ไม่ถึงเกณฑ์ตาปิดต่อเนื่อง
  long k = 0;
  void perclos(double p, uint32_t ms, const std::function<void(const DrowsyResult&, uint32_t)>& each = nullptr) {
    for (uint32_t el = FRAME; el <= ms; el += FRAME, k++) {
      bool closed = std::floor((k + 1) * p) > std::floor(k * p);
      frame(closed ? S::Closed : S::Open);
      if (each) each(j.result(), el);
    }
  }
  const DrowsyResult& r() const { return j.result(); }
};

static void testOpenOnly() {
  puts("open only 70 s");
  Sim s;
  bool earlyReady = false;
  s.run(S::Open, 70000, [&](const DrowsyResult& r, uint32_t el) {
    if (el < PERCLOS_MIN_VALID_MS - FRAME && r.perclos >= 0) earlyReady = true;
  });
  CHECK(!earlyReady, "PERCLOS พร้อมก่อน 30 s");
  CHECK(s.r().perclos == 0.0f, "perclos=%.3f", s.r().perclos);
  CHECK(s.maxLevel == 0, "maxLevel=%d", s.maxLevel);
  CHECK(!s.everDistraction && !s.everUnmonitored, "distraction/unmonitored ติด");
  CHECK(s.levelChanges == 0 && s.microsleeps == 0 && s.distractions == 0, "มี event");
}

static void testClosed2s() {
  puts("closed 2 s");
  Sim s;
  s.run(S::Open, 5000);
  int levelAt1200 = -1;
  s.run(S::Closed, 2000, [&](const DrowsyResult& r, uint32_t el) { if (el == 1200) levelAt1200 = (int)r.level; });
  CHECK(levelAt1200 == 0, "ระดับที่ 1.2 s = %d (ต้องยังไม่ถึง 2)", levelAt1200);
  CHECK(s.r().level == L::Drowsy, "level=%d", (int)s.r().level);
  CHECK(s.maxLevel == 2, "maxLevel=%d", s.maxLevel);
  CHECK(s.microsleeps == 0, "microsleep=%d", s.microsleeps);
  // latch: ลืมตาไม่ถึง 1 s ยังค้าง · ครบแล้วหาย
  s.run(S::Open, 750);
  CHECK(s.r().level == L::Drowsy, "หลังลืมตา 0.75 s level=%d", (int)s.r().level);
  CHECK(s.r().closedMs == 0, "closedMs=%u", s.r().closedMs);
  s.run(S::Open, 450);
  CHECK(s.r().level == L::Normal, "หลังลืมตา 1.2 s level=%d", (int)s.r().level);
  CHECK(s.levelChanges == 2, "levelChanges=%d", s.levelChanges);
}

static void testClosed35() {
  puts("closed 3.5 s");
  Sim s;
  s.run(S::Open, 5000);
  s.run(S::Closed, 3500);
  CHECK(s.r().level == L::Microsleep, "level=%d", (int)s.r().level);
  CHECK(s.microsleeps == 1, "microsleep=%d", s.microsleeps);
  CHECK(s.maxClosed >= 3300, "maxClosed=%u", s.maxClosed);
  CHECK(!s.everDistraction, "distraction ติด");
}

static void testClosed35Flicker() {
  puts("closed 3.5 s, every other frame NotVisible");
  Sim s;
  s.run(S::Open, 5000);
  for (uint32_t el = 0; el < 3500; el += 2 * FRAME) { s.frame(S::Closed); s.frame(S::NotVisible); }
  CHECK(s.r().level == L::Microsleep, "level=%d", (int)s.r().level);
  CHECK(s.microsleeps == 1, "microsleep=%d", s.microsleeps);
  CHECK(!s.everDistraction, "distraction ติด");
}

static void testNotVisible4() {
  puts("NotVisible 4 s");
  Sim s;
  s.run(S::Open, 5000);
  bool early = false;
  s.run(S::NotVisible, 4000, [&](const DrowsyResult& r, uint32_t el) {
    if (el + FRAME <= DISTRACTION_MS && r.distraction) early = true;
  });
  CHECK(!early, "distraction ก่อน 3 s");
  CHECK(s.r().distraction, "distraction ไม่ติด");
  CHECK(s.distractions == 1, "distraction event=%d", s.distractions);
  CHECK(s.maxLevel == 0, "maxLevel=%d", s.maxLevel);
  s.run(S::Open, 300);
  CHECK(!s.r().distraction, "เห็นตาแล้ว distraction ยังค้าง");
}

static void testUncertain6() {
  puts("Uncertain 6 s");
  Sim s;
  s.run(S::Open, 5000);
  bool early = false;
  s.run(S::Uncertain, 6000, [&](const DrowsyResult& r, uint32_t el) {
    if (el + FRAME <= UNMONITORED_MS && r.unmonitored) early = true;
  });
  CHECK(!early, "unmonitored ก่อน 5 s");
  CHECK(s.r().unmonitored, "unmonitored ไม่ติด");
  CHECK(!s.everDistraction, "distraction ติด");
  CHECK(s.maxLevel == 0, "maxLevel=%d", s.maxLevel);
  s.run(S::Open, 300);
  CHECK(!s.r().unmonitored, "unmonitored ยังค้าง");
}

static void testBridge() {
  puts("Uncertain bridge");
  {
    Sim s;  // ปิด 1.05 + ไม่แน่ใจ 0.6 + ปิด 0.6 = ต่อเนื่อง ≥ 1.5 s
    s.run(S::Open, 3000); s.run(S::Closed, 1050); s.run(S::Uncertain, 600);
    CHECK(s.r().level == L::Normal, "ระหว่างเชื่อม level=%d", (int)s.r().level);
    s.run(S::Closed, 600);
    CHECK(s.r().level == L::Drowsy, "level=%d closed=%u", (int)s.r().level, s.r().closedMs);
  }
  {
    Sim s;  // ไม่แน่ใจนานเกิน 1 s ตัดช่วง
    s.run(S::Open, 3000); s.run(S::Closed, 1050); s.run(S::Uncertain, 1200); s.run(S::Closed, 1050);
    CHECK(s.maxLevel == 0, "maxLevel=%d closed=%u", s.maxLevel, s.maxClosed);
  }
  {
    Sim s;  // ไม่แน่ใจ + ลืมตาสั้นๆ รวมเกิน 1 s ตัดช่วง
    s.run(S::Open, 3000); s.run(S::Closed, 1050);
    s.run(S::Uncertain, 600); s.frame(S::Open); s.run(S::Uncertain, 450); s.run(S::Closed, 1050);
    CHECK(s.maxLevel == 0, "maxLevel=%d closed=%u", s.maxLevel, s.maxClosed);
  }
}

static void testOpenConfirm() {
  puts("Open must last 0.3 s to break a closed run");
  {
    Sim s;  // ลืมตาเฟรมเดียว (0.15 s) = โมเดลตอบผิด ไม่ตัดช่วง
    s.run(S::Open, 3000); s.run(S::Closed, 1050); s.frame(S::Open); s.run(S::Closed, 600);
    CHECK(s.r().level == L::Drowsy, "level=%d closed=%u", (int)s.r().level, s.r().closedMs);
  }
  {
    Sim s;  // ลืมตา 2 เฟรม (0.3 s) ตัดช่วง
    s.run(S::Open, 3000); s.run(S::Closed, 1050); s.run(S::Open, 300); s.run(S::Closed, 1050);
    CHECK(s.maxLevel == 0, "maxLevel=%d closed=%u", s.maxLevel, s.maxClosed);
  }
  {
    Sim s;  // หลับ 3.5 s แต่โมเดลตอบ "เปิด" ทุกเฟรมที่ 5 -> ยังถึง Microsleep
    s.run(S::Open, 3000);
    for (int i = 0; i < 24; i++) s.frame(i % 5 == 4 ? S::Open : S::Closed);
    CHECK(s.r().level == L::Microsleep, "level=%d closed=%u", (int)s.r().level, s.r().closedMs);
    CHECK(s.microsleeps == 1, "microsleep=%d", s.microsleeps);
  }
}

static void testNotVisibleBridge() {
  puts("face lost while eyes closed: bridge up to 2 s");
  {
    Sim s;  // ปิด 0.6 + หาหน้าไม่เจอ 1.8 + ปิด 0.6 = ต่อเนื่อง 3 s
    s.run(S::Open, 3000); s.run(S::Closed, 600); s.run(S::NotVisible, 1800);
    CHECK(s.r().level == L::Normal, "ระหว่างเชื่อม level=%d", (int)s.r().level);
    s.run(S::Closed, 600);
    CHECK(s.r().level == L::Microsleep, "level=%d closed=%u", (int)s.r().level, s.r().closedMs);
    CHECK(!s.everDistraction, "distraction ติด");
  }
  {
    Sim s;  // หาหน้าไม่เจอ 2.7 s (ถือค่าเดิม 0.45 + ช่องว่าง 2.25 > 2) ตัดช่วง
    s.run(S::Open, 3000); s.run(S::Closed, 600); s.run(S::NotVisible, 2700); s.run(S::Closed, 600);
    CHECK(s.maxLevel == 0, "maxLevel=%d closed=%u", s.maxLevel, s.maxClosed);
  }
}

static void testPerclos() {
  puts("PERCLOS 20% / 30% + hysteresis");
  Sim s;
  int lo = 9, hi = -1;
  uint32_t from = PERCLOS_MIN_VALID_MS + 1000;  // ช่วงแรก: PERCLOS ยังไม่พร้อม · ช่วงต่อไป: เผื่อหน้าต่างเลื่อน
  auto track = [&](const DrowsyResult& r, uint32_t el) {
    if (el > from) { lo = std::min(lo, (int)r.level); hi = std::max(hi, (int)r.level); }
  };
  auto reset = [&] { lo = 9; hi = -1; };

  s.perclos(0.20, 70000, track);
  CHECK(lo == 1 && hi == 1, "20%%: level %d..%d perclos=%.3f", lo, hi, s.r().perclos);
  CHECK(std::fabs(s.r().perclos - 0.20f) < 0.01f, "perclos=%.3f", s.r().perclos);
  CHECK(s.r().closedMs < CLOSED_LEVEL2_MS, "ตาปิดต่อเนื่องต้องสั้น");

  from = 15000;
  reset(); s.perclos(0.30, 70000, track);
  CHECK(hi == 2 && s.r().level == L::Drowsy, "30%%: level %d..%d perclos=%.3f", lo, hi, s.r().perclos);

  reset(); s.perclos(0.24, 70000, track);   // ต่ำกว่า 25% แต่ไม่ต่ำกว่า 23%: ค้างที่ 2
  CHECK(lo == 2 && hi == 2, "24%%: level %d..%d perclos=%.3f", lo, hi, s.r().perclos);

  reset(); s.perclos(0.21, 70000, track);   // ต่ำกว่า 23%: ลงเป็น 1
  CHECK(s.r().level == L::Fatigue && hi <= 2, "21%%: level=%d perclos=%.3f", (int)s.r().level, s.r().perclos);

  reset(); s.perclos(0.14, 70000, track);   // ต่ำกว่า 15% แต่ไม่ต่ำกว่า 13%: ค้างที่ 1
  CHECK(lo == 1 && hi == 1, "14%%: level %d..%d perclos=%.3f", lo, hi, s.r().perclos);

  reset(); s.perclos(0.11, 70000, track);   // ต่ำกว่า 13%: ลงเป็น 0
  CHECK(s.r().level == L::Normal, "11%%: level=%d perclos=%.3f", (int)s.r().level, s.r().perclos);
  CHECK(s.microsleeps == 0, "microsleep=%d", s.microsleeps);
}

static void testPerclosIgnoresInvisible() {
  puts("PERCLOS denominator = Open+Closed only");
  Sim s;
  // ครึ่งหนึ่งของเวลาไม่แน่ใจ: เวลาที่ตัดสินได้รวม < 30 s ในหน้าต่าง -> ยังไม่พร้อม
  for (int i = 0; i < 400; i++) s.frame(i % 2 ? S::Uncertain : S::Open);   // 60 s
  CHECK(s.r().perclos < 0, "perclos=%.3f ต้อง -1", s.r().perclos);
}

static void testMicrosleepHeadDrop() {
  puts("microsleep then head drop");
  Sim s;
  s.run(S::Open, 5000);
  s.run(S::Closed, 3500);
  CHECK(s.r().level == L::Microsleep, "level=%d", (int)s.r().level);
  bool dropped = false;
  s.run(S::NotVisible, 6000, [&](const DrowsyResult& r, uint32_t) { if (r.level != L::Microsleep) dropped = true; });
  CHECK(!dropped, "ระดับ 3 หลุดตอนหาหน้าไม่เจอ");
  CHECK(s.r().distraction, "distraction ไม่ติด");
  s.run(S::Uncertain, 1500);                 // เห็นหน้าแต่ไม่แน่ใจ ก็ยังไม่ปลด
  CHECK(s.r().level == L::Microsleep, "หลัง uncertain level=%d", (int)s.r().level);
  s.run(S::Open, 600);
  CHECK(s.r().level == L::Microsleep, "ลืมตา 0.6 s level=%d", (int)s.r().level);
  s.run(S::Open, 600);
  CHECK(s.r().level == L::Normal, "ลืมตา 1.2 s level=%d", (int)s.r().level);
  CHECK(s.microsleeps == 1, "microsleep=%d", s.microsleeps);
}

static void testBootNoFace() {
  puts("boot with no face: no Distraction until eyes seen");
  Sim s;
  s.run(S::NotVisible, 10000);
  CHECK(!s.everDistraction && s.distractions == 0, "distraction ตอนบูตไม่มีคน");
  s.run(S::Open, 2000);                       // คนขับนั่งแล้ว
  s.run(S::NotVisible, 4000);
  CHECK(s.r().distraction && s.distractions == 1, "หลังเห็นหน้าแล้วหันออก 4 s ต้องเตือน (n=%d)", s.distractions);
}

static void testDriverAbsent() {
  puts("away 70 s: Distraction stops after 60 s, re-arms on next eyes");
  Sim s;
  s.run(S::Open, 5000);
  bool at10 = false, at65 = false;
  s.run(S::NotVisible, 70000, [&](const DrowsyResult& r, uint32_t el) {
    if (el == 9900) at10 = r.distraction;     // el เป็นจำนวนเท่าของ 150
    if (el == 64950) at65 = r.distraction;
  });
  CHECK(at10, "ที่ 10 s distraction ต้องติด");
  CHECK(!at65 && !s.r().distraction, "เกิน 60 s ต้องเลิกเตือน");
  CHECK(s.distractions == 1, "distraction event=%d", s.distractions);
  s.run(S::Open, 1000);
  s.run(S::NotVisible, 4000);
  CHECK(s.r().distraction && s.distractions == 2, "เห็นตาอีกครั้งแล้วหันออก ต้องเตือนใหม่ (n=%d)", s.distractions);
}

static void testMicrosleepHeadDropLong() {
  puts("microsleep then head drop 70 s: level 3 stays");
  Sim s;
  s.run(S::Open, 5000);
  s.run(S::Closed, 3500);
  CHECK(s.r().level == L::Microsleep, "level=%d", (int)s.r().level);
  bool dropped = false;
  s.run(S::NotVisible, 70000, [&](const DrowsyResult& r, uint32_t) { if (r.level != L::Microsleep) dropped = true; });
  CHECK(!dropped, "ระดับ 3 หลุดระหว่างหาหน้าไม่เจอ 70 s");
  CHECK(!s.r().distraction, "เกิน 60 s distraction ต้องดับ (ระดับ 3 ยังดังอยู่)");
  s.run(S::Open, 1200);
  CHECK(s.r().level == L::Normal, "ลืมตา 1.2 s level=%d", (int)s.r().level);
}

static void testStallKeepsLatch() {
  puts("vision stall 3 s then open: latch must not clear on one frame");
  Sim s;   // เจอบนบอร์ด 0.2.6: vision ค้าง ~3 s เฟรมแรกหลังค้างได้ dt ใหญ่จนปลดระดับทันที
  s.run(S::Open, 3000);
  s.run(S::Closed, 2000);
  CHECK(s.r().level == L::Drowsy, "level=%d", (int)s.r().level);
  s.t += 3000; s.frame(S::Open);
  CHECK(s.r().level == L::Drowsy, "หลังค้าง level=%d", (int)s.r().level);
  s.run(S::Open, 1200);
  CHECK(s.r().level == L::Normal, "ลืมตาต่อ 1.2 s level=%d", (int)s.r().level);
}

static void testEventQueue() {
  puts("event queue overflow keeps newest");
  Sim s;
  for (int i = 0; i < 20; i++) {             // Distraction 20 ครั้ง ไม่ pop
    s.t += 5000; s.j.update(s.t, S::Open);
    for (int k = 0; k < 25; k++) { s.t += FRAME; s.j.update(s.t, S::NotVisible); }
  }
  CHECK(s.j.droppedEvents() == 4, "dropped=%u", s.j.droppedEvents());
  int n = 0; DrowsyEvent e, last{};
  while (s.j.popEvent(e)) { n++; last = e; }
  CHECK(n == 16 && last.ms == s.t - 5 * FRAME, "n=%d last=%u t=%u", n, last.ms, s.t);
}

int main() {
  testOpenOnly();
  testClosed2s();
  testClosed35();
  testClosed35Flicker();
  testNotVisible4();
  testUncertain6();
  testBridge();
  testOpenConfirm();
  testNotVisibleBridge();
  testPerclos();
  testPerclosIgnoresInvisible();
  testMicrosleepHeadDrop();
  testBootNoFace();
  testDriverAbsent();
  testMicrosleepHeadDropLong();
  testStallKeepsLatch();
  testEventQueue();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
