// สถานะกลางที่ task ทุกตัวอ่าน/เขียน ผ่าน mutex
// กฎ: ห้ามถือ lock นาน (ห้ามทำ I/O ระหว่างถือ) · ห้ามส่ง camera fb ข้าม core ผ่าน struct นี้
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class EyeState : uint8_t { Unknown, Open, Closed, Uncertain, NotVisible };
enum class DrowsinessLevel : uint8_t { Normal = 0, Fatigue = 1, Drowsy = 2, Microsleep = 3 };

struct SharedState {
  // vision (Core 1)
  float    fps;
  float    pClosed;            // P(ตาปิด) หลัง EMA
  EyeState eyeState;
  bool     faceFound;
  uint32_t visionFrames;

  // drowsiness
  float           perclos;     // 0..1 ในหน้าต่าง PERCLOS_WINDOW_MS
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
