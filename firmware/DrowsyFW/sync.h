// สถานะการ Sync ขึ้น Supabase ให้หน้าเว็บแสดง
#pragma once
#include <stdint.h>

struct SyncStatus {
  bool     configured;        // มี DEVICE_SECRET จริงใน secrets.h
  bool     busy;
  uint32_t okCalls, failCalls;
  uint32_t lastOkMs;          // millis() ที่ส่งสำเร็จล่าสุด · 0 = ยังไม่เคย
  int      lastHttp;          // HTTP status ล่าสุด · < 0 = ต่อไม่ติด
  char     lastErr[96];
  uint32_t pendingSamples, pendingEvents;   // ที่ยังไม่ได้ส่ง (ทุก Trip)
};

SyncStatus syncStatus();
const char* deviceId();       // MAC ของ Device เช่น "28:84:85:65:63:44" = device_id บน cloud
