// สถานะกลางที่ task ทุกตัวอ่าน/เขียน ผ่าน mutex
// กฎ: ห้ามถือ lock นาน (ห้ามทำ I/O ระหว่างถือ) · ห้ามส่ง camera fb ข้าม core ผ่าน struct นี้
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "domain_types.h"

// ผลเทียบ A/B ทางเดิม (หมุนทั้งเฟรม) กับทางใหม่ (หยิบพิกเซลผ่านแผนที่) บนเฟรมเดียวกัน · เวลาเป็นค่าเฉลี่ย ms
struct AbStats {
  uint32_t frames, foundNew, foundOld, both;
  float    eyeErrMean; int32_t eyeErrMax;       // ระยะจุดตาที่ห่างกันมากสุดในสองข้าง (px) เฉพาะเฟรมที่เจอทั้งคู่
  uint32_t input0, input0Bad; int32_t input0DiffMax;   // model input ขั้น MSR ที่มุมชดเชย 0: ควรต่าง 0 byte
  uint32_t inputTilt; float inputTiltDiffMean;  // มุมชดเชยไม่ใช่ 0: ต่างได้เล็กน้อยจากการปัดเศษ
  uint32_t crops, cropBad;                      // Eye Region ที่ไม่ตรงกันทุก byte
  float    oldRotateMs, oldDetectMs, newRotateMs, newDetectMs, newPreMs;
  float    msrPreOldMs, msrPreNewMs;                 // preprocess ขั้น MSR อย่างเดียว ทางเดิม/ทางใหม่ บนเฟรมเดียวกัน
};

struct SharedState {
  // vision (Core 1)
  float    fps;
  float    pClosed;            // P(ตาปิด) หลัง EMA
  EyeState eyeState;
  bool     faceFound;
  float    faceScore;
  float    pLeft, pRight;      // P(ตาปิด) ของตาซ้าย/ขวาในภาพ · -1 = ใช้ไม่ได้
  int16_t  eyeDistPx;          // ระยะระหว่างจุดตาในภาพที่หมุนแล้ว
  int16_t  tiltCompDeg;        // มุมที่หมุนชดเชยหน้าเอียงในเฟรมล่าสุด
  float    rotateMs, detectMs, classifyMs;
  float    preMs;              // ส่วนของ detectMs ที่ esp-dl หยิบพิกเซล (หมุน + ย่อ) เข้าโมเดล
  bool     abOn, abReset;      // web ตั้ง · vision อ่าน
  AbStats  ab;
  uint32_t visionFrames;
  uint32_t lastFrameMs;        // millis() ของเฟรมล่าสุดที่ vision ประมวลผลเสร็จ

  // drowsiness
  float           perclos;     // 0..1 ในหน้าต่าง PERCLOS_WINDOW_MS · -1 = ยังไม่พร้อม
  bool            perclosReady;
  uint32_t        closedMs;    // ตาปิดต่อเนื่องขณะนี้
  uint32_t        closedPeakMs; // ตาปิดต่อเนื่องสูงสุดตั้งแต่ log task เก็บ Sample ครั้งก่อน (log task รีเซ็ต)
  DrowsinessLevel level;
  bool            distraction;
  bool            unmonitored;

  // alert
  uint32_t mutedUntilMs;       // millis() ที่การปิดเสียงหมดอายุ · 0 = ไม่ได้ปิด

  // sensor
  uint16_t ldrRaw;
  bool     irOn;

  // trip / time
  uint32_t bootCount;          // = หมายเลข Trip บน Device นี้
  bool     timeKnown;          // ได้เวลาจริงจาก NTP หรือ browser แล้ว
  time_t   tripStartEpoch;     // 0 ถ้ายังไม่รู้เวลาจริง
  uint32_t tripStartMs;        // millis() ตอนเริ่ม Trip (= ตอนบูต)

  // net
  bool     staConnected;
  char     staSsid[33];
  char     staIp[16];
  char     apSsid[33];
};

void stateInit();
void stateLock();
void stateUnlock();
SharedState stateSnapshot();   // คัดลอกทั้งก้อนออกมาอ่านนอก lock

extern SharedState g_state;    // เข้าถึงตรงได้เฉพาะระหว่าง stateLock()/stateUnlock()

// ใช้แบบ: { StateGuard g; g_state.fps = x; }
struct StateGuard {
  StateGuard()  { stateLock(); }
  ~StateGuard() { stateUnlock(); }
};
