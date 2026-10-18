// กติกาอยู่ที่ drowsiness.h · เกณฑ์อยู่ที่ drowsy_config.h
#include "drowsiness.h"
#include "drowsy_config.h"
#include <string.h>

static bool isVisible(EyeState e) {
  return e == EyeState::Open || e == EyeState::Closed || e == EyeState::Uncertain;
}

void DrowsinessJudge::reset() {
  memset(&r_, 0, sizeof(r_));
  r_.perclos = -1;
  r_.level = DrowsinessLevel::Normal;
  r_.effective = EyeState::Unknown;
  started_ = false;
  lastMs_ = 0;
  lastSeen_ = EyeState::Unknown;
  lastVisibleMs_ = 0;
  driverArmed_ = false;
  memset(closedBucket_, 0, sizeof(closedBucket_));
  memset(validBucket_, 0, sizeof(validBucket_));
  curSec_ = 0;
  perclosLevel_ = 0;
  closedRun_ = gapRun_ = gapUncertain_ = gapOpen_ = openRun_ = 0;
  seenClosed_ = seenOther_ = 0;
  eyeLevel_ = 0;
  uncertainRun_ = 0;
  qHead_ = qCount_ = 0;
  dropped_ = 0;
}

void DrowsinessJudge::addBucket(uint32_t nowMs, uint32_t dt, EyeState e) {
  uint32_t sec = nowMs / 1000;
  uint32_t adv = sec - curSec_;
  if (adv >= (uint32_t)BUCKETS) {
    memset(closedBucket_, 0, sizeof(closedBucket_));
    memset(validBucket_, 0, sizeof(validBucket_));
  } else {
    for (uint32_t i = 1; i <= adv; i++) {
      int b = (curSec_ + i) % BUCKETS;
      closedBucket_[b] = validBucket_[b] = 0;
    }
  }
  curSec_ = sec;
  // ตัวหารนับเฉพาะเวลาที่ตัดสินได้ว่าเปิดหรือปิด
  if (e != EyeState::Open && e != EyeState::Closed) return;
  int b = sec % BUCKETS;
  validBucket_[b] += dt;
  if (e == EyeState::Closed) closedBucket_[b] += dt;
}

float DrowsinessJudge::perclosNow() const {
  uint32_t closed = 0, valid = 0;
  for (int i = 0; i < BUCKETS; i++) { closed += closedBucket_[i]; valid += validBucket_[i]; }
  if (valid < PERCLOS_MIN_VALID_MS) return -1;
  return (float)closed / valid;
}

void DrowsinessJudge::push(const DrowsyEvent& ev) {
  if (qCount_ == QUEUE) {              // เต็ม: ทิ้งอันเก่าสุด
    qHead_ = (qHead_ + 1) % QUEUE;
    qCount_--;
    dropped_++;
  }
  q_[(qHead_ + qCount_) % QUEUE] = ev;
  qCount_++;
}

bool DrowsinessJudge::popEvent(DrowsyEvent& out) {
  if (!qCount_) return false;
  out = q_[qHead_];
  qHead_ = (qHead_ + 1) % QUEUE;
  qCount_--;
  return true;
}

const DrowsyResult& DrowsinessJudge::update(uint32_t nowMs, EyeState raw) {
  if (!started_) {
    started_ = true;
    lastMs_ = nowMs;
    lastVisibleMs_ = nowMs;
    curSec_ = nowMs / 1000;
  }
  uint32_t dt = nowMs - lastMs_;
  if (dt > FRAME_DT_MAX_MS) dt = FRAME_DT_MAX_MS;
  lastMs_ = nowMs;

  // 1. ถือ Eye State เดิม: หาหน้าพลาดราวครึ่งหนึ่งของเฟรม ไม่งั้นช่วงตาปิดจะขาดเป็นท่อนๆ
  EyeState e = raw;
  if (isVisible(raw)) {
    lastSeen_ = raw;
    lastVisibleMs_ = nowMs;
  } else {
    e = EyeState::NotVisible;
    if (isVisible(lastSeen_) && nowMs - lastVisibleMs_ <= EYE_HOLD_MS) e = lastSeen_;
  }
  r_.effective = e;

  // 2. PERCLOS
  addBucket(nowMs, dt, e);
  r_.perclos = perclosNow();
  uint8_t pl = 0;
  if (r_.perclos >= 0) {
    float p = r_.perclos;
    if (p >= PERCLOS_LEVEL2 || (perclosLevel_ == 2 && p >= PERCLOS_LEVEL2 - PERCLOS_HYST)) pl = 2;
    else if (p >= PERCLOS_LEVEL1 || (perclosLevel_ >= 1 && p >= PERCLOS_LEVEL1 - PERCLOS_HYST)) pl = 1;
  }
  perclosLevel_ = pl;

  // 3. ตาปิดต่อเนื่อง: ช่องว่างระหว่างตาปิดเชื่อมได้ถ้า
  //    - ลืมตาต่อเนื่องไม่ถึง OPEN_CONFIRM_MS (โมเดลตอบ "เปิด" ผิดบางเฟรมขณะหลับตา · ทดสอบหน้ากล้อง 0.2.7)
  //    - Uncertain + ลืมตาสั้นๆ รวมไม่เกิน UNCERTAIN_BRIDGE_MS
  //    - ช่องว่างทั้งหมด (รวมหาหน้าไม่เจอ ซึ่งเกิดบ่อยตอนหลับตา) ไม่เกิน NOTVISIBLE_BRIDGE_MS
  //    เวลาที่เชื่อมไว้นับรวมเข้าไปตอนกลับมาเห็นตาปิดอีกครั้ง (ไม่นับระหว่างที่ยังไม่แน่ใจ)
  //    ลืมตาสลับกับหาหน้าไม่เจอ (O . O) นับเป็นลืมตาต่อเนื่อง: หาหน้าไม่เจอไม่ใช่หลักฐานว่าตาปิด
  if (e == EyeState::Closed) {
    if (!closedRun_) seenClosed_ = seenOther_ = 0;
    closedRun_ += gapRun_ + dt;
    gapRun_ = gapUncertain_ = gapOpen_ = 0;
  } else if (closedRun_) {
    gapRun_ += dt;
    if (e != EyeState::NotVisible) gapUncertain_ += dt;
    if (e == EyeState::Open) gapOpen_ += dt;
    else if (e == EyeState::Uncertain) gapOpen_ = 0;
    if (gapOpen_ >= OPEN_CONFIRM_MS || gapUncertain_ > UNCERTAIN_BRIDGE_MS || gapRun_ > NOTVISIBLE_BRIDGE_MS)
      closedRun_ = gapRun_ = gapUncertain_ = gapOpen_ = 0;
  }
  // หลักฐานในช่วงนี้นับจากเฟรมที่เห็นตาจริง (raw) ไม่นับเฟรมที่ถือค่าเดิม
  if (closedRun_) {
    if (raw == EyeState::Closed) seenClosed_ += dt;
    else if (raw == EyeState::Open || raw == EyeState::Uncertain) seenOther_ += dt;
  }
  // ช่วงตาปิดจะนับเข้าเกณฑ์ระดับ 2/3 ได้ต่อเมื่อเฟรมตาปิดจริงเป็นส่วนใหญ่ของเฟรมที่เห็นตา (นำอยู่ ≥ CLOSED_SEEN_LEAD_MS
  // กันกรณีปิด/ไม่แน่ใจสลับกันครึ่งต่อครึ่ง ซึ่งจะ "ชนะ" ทุกครั้งหลังเฟรมตาปิดแล้วระดับ latch ทันที)
  // และมีเวลาตาปิดจริงอย่างน้อย CLOSED_SEEN_MIN_PCT % ของช่วง (Trip 411: ช่องว่างหาหน้าไม่เจอ +
  // "ไม่แน่ใจ" p≈0.45 ถูกเชื่อมด้วยเฟรมตาปิด p≈0.5–0.6 ไม่กี่เฟรม จนเกิด Microsleep ทั้งที่ไม่ได้หันมาที่กล้อง)
  bool credible = closedRun_ && seenClosed_ >= seenOther_ + CLOSED_SEEN_LEAD_MS &&
                  seenClosed_ * 100 >= closedRun_ * CLOSED_SEEN_MIN_PCT;
  uint32_t run = credible ? closedRun_ : 0;
  r_.closedMs = run;

  uint8_t el = run >= MICROSLEEP_MS ? 3 : run >= CLOSED_LEVEL2_MS ? 2 : 0;
  if (el == 3 && eyeLevel_ < 3) {
    DrowsyEvent ev = {nowMs, DrowsyEventType::Microsleep, DrowsinessLevel::Normal, DrowsinessLevel::Normal, run};
    push(ev);
  }
  if (el > eyeLevel_) eyeLevel_ = el;

  // latch: ระดับจากตาปิดหายเมื่อเห็นตาเปิดต่อเนื่องเท่านั้น (หัวก้มจนหาหน้าไม่เจอ ระดับต้องค้าง)
  openRun_ = e == EyeState::Open ? openRun_ + dt : 0;
  if (openRun_ >= EYES_OPEN_CLEAR_MS) eyeLevel_ = 0;

  // 4. Distraction: มองไม่เห็นตานับจากเฟรมสุดท้ายที่เห็น
  //    เริ่มนับหลังเห็นตาครั้งแรกเท่านั้น (บูตมาไม่มีคนนั่ง = ไม่เตือน) · มองไม่เห็นนานเกิน DRIVER_ABSENT_MS
  //    ถือว่า Driver ลงจากรถ: เลิกเตือนจนกว่าจะเห็นตาอีกครั้ง (กันเสียงค้างตอนจอดทิ้งไว้) · ไม่แตะระดับที่ latch ไว้
  if (isVisible(raw)) driverArmed_ = true;
  else if (driverArmed_ && nowMs - lastVisibleMs_ > DRIVER_ABSENT_MS) driverArmed_ = false;
  bool dis = driverArmed_ && e == EyeState::NotVisible && nowMs - lastVisibleMs_ >= DISTRACTION_MS;
  if (dis && !r_.distraction) {
    DrowsyEvent ev = {nowMs, DrowsyEventType::Distraction, DrowsinessLevel::Normal, DrowsinessLevel::Normal, 0};
    push(ev);
  }
  r_.distraction = dis;

  // 5. Unmonitored
  uncertainRun_ = e == EyeState::Uncertain ? uncertainRun_ + dt : 0;
  r_.unmonitored = uncertainRun_ >= UNMONITORED_MS;

  // 6. ระดับรวม
  DrowsinessLevel lv = (DrowsinessLevel)(eyeLevel_ > pl ? eyeLevel_ : pl);
  if (lv != r_.level) {
    DrowsyEvent ev = {nowMs, DrowsyEventType::LevelChange, r_.level, lv, r_.closedMs};
    push(ev);
    r_.level = lv;
  }
  return r_;
}
