// ตัดสินความง่วงจาก Eye State ทีละเฟรม -> PERCLOS, ตาปิดต่อเนื่อง, Drowsiness Level, Distraction, Unmonitored
// ไม่พึ่ง Arduino/FreeRTOS: ทดสอบบน PC ได้ที่ firmware/tests/drowsiness_test.cpp
// ทุกอย่างนับเป็นเวลา (dt ระหว่างเฟรม) ไม่นับเป็นเฟรม เพราะ fps แกว่ง 5–7 · ใช้จาก task เดียว (vision) ไม่มี lock
#pragma once
#include <stdint.h>
#include "domain_types.h"

struct DrowsyResult {
  float           perclos;      // 0..1 · -1 = ยังไม่พร้อม (เห็นตาเปิด/ปิดรวมไม่ถึง PERCLOS_MIN_VALID_MS)
  uint32_t        closedMs;     // ตาปิดต่อเนื่องขณะนี้ (รวมช่องว่างที่เชื่อมไว้) · 0 = ไม่ได้ปิด หรือหลักฐานตาปิดจริงยังไม่พอ
  DrowsinessLevel level;        // max(ระดับจากตาปิดต่อเนื่อง, ระดับจาก PERCLOS)
  bool            distraction;  // มองไม่เห็นตา ≥ DISTRACTION_MS (หลังเคยเห็นตา และไม่เกิน DRIVER_ABSENT_MS)
  bool            unmonitored;  // เห็นหน้าแต่ "ไม่แน่ใจ" ต่อเนื่อง ≥ UNMONITORED_MS
  EyeState        effective;    // Eye State หลังถือค่าเดิมช่วงหาหน้าไม่เจอสั้นๆ (EYE_HOLD_MS)
};

enum class DrowsyEventType : uint8_t { LevelChange, Microsleep, Distraction };

struct DrowsyEvent {
  uint32_t        ms;           // เวลา (nowMs ที่ส่งเข้า update) ที่เกิด
  DrowsyEventType type;
  DrowsinessLevel from, to;     // LevelChange
  uint32_t        closedMs;     // Microsleep: ตาปิดต่อเนื่องตอนถึงเกณฑ์
};

class DrowsinessJudge {
public:
  DrowsinessJudge() { reset(); }
  void reset();
  const DrowsyResult& update(uint32_t nowMs, EyeState raw);
  const DrowsyResult& result() const { return r_; }
  bool popEvent(DrowsyEvent& out);   // คืน false เมื่อคิวว่าง
  uint32_t droppedEvents() const { return dropped_; }

private:
  static const int BUCKETS = 60;     // PERCLOS_WINDOW_MS / 1000
  static const int QUEUE = 16;

  void addBucket(uint32_t nowMs, uint32_t dt, EyeState e);
  float perclosNow() const;
  void push(const DrowsyEvent& ev);

  DrowsyResult r_;
  bool     started_;
  uint32_t lastMs_;

  // ถือ Eye State เดิม
  EyeState lastSeen_;               // Open/Closed/Uncertain ล่าสุดที่เห็นจริง
  uint32_t lastVisibleMs_;          // เวลาเฟรมล่าสุดที่เห็นตา (หรือเวลาเริ่ม)
  bool     driverArmed_;            // เคยเห็นตาแล้ว และยังไม่หายไปนานเกิน DRIVER_ABSENT_MS

  // PERCLOS: ถังละ 1 วินาที
  uint32_t closedBucket_[BUCKETS], validBucket_[BUCKETS];
  uint32_t curSec_;
  uint8_t  perclosLevel_;

  // ตาปิดต่อเนื่อง
  uint32_t closedRun_, gapRun_;     // gapRun_ = ช่วงไม่ใช่ Closed ที่กำลังเชื่อมอยู่
  uint32_t gapUncertain_, gapOpen_; // ส่วนของช่องว่างที่เป็น Uncertain+Open / Open ต่อเนื่อง
  uint32_t seenClosed_, seenOther_; // ในช่วงตาปิด: เวลาที่เห็นตาปิดจริง / เห็นตาเปิดหรือไม่แน่ใจ (raw)
  uint32_t openRun_;
  uint8_t  eyeLevel_;               // latch จนกว่าลืมตาต่อเนื่อง EYES_OPEN_CLEAR_MS

  uint32_t uncertainRun_;

  DrowsyEvent q_[QUEUE];
  uint8_t  qHead_, qCount_;
  uint32_t dropped_;
};
