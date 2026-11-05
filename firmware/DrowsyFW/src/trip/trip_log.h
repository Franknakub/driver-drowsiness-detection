// ที่เก็บ Trip บน FFat ใช้ร่วมกันระหว่าง log task (เขียน), sync task และหน้าเว็บ (อ่าน)
// ทุกฟังก์ชันล็อก mutex ของระบบไฟล์เอง · รูปแบบไฟล์อยู่ที่ trip_record.h
#pragma once
#include "trip_record.h"

#define MAX_TRIPS 1000   // จำนวน Trip สูงสุดที่เก็บบน FFat พร้อมกัน

struct TripInfo {
  uint32_t trip, nonce;
  uint32_t samples, events;   // จำนวน record ในไฟล์
  bool     current;           // Trip ที่กำลังบันทึกอยู่ (บูตนี้)
  char     fw[16];            // FW_VERSION ตอนบันทึก Trip นี้
  TripMeta meta;              // ตำแหน่งที่ Sync แล้ว + เวลา + Trip Summary
};

struct LogStatus {
  bool     fsOk;
  bool     samplesPaused;     // FFat เต็มด้วยข้อมูลที่ยังไม่ Sync: หยุดเก็บ Sample แต่ยังเก็บ Event
  uint32_t totalKB, usedKB;
  uint32_t trips;
  uint32_t deletedTrips;      // ลบไปแล้วในบูตนี้ (Sync แล้ว + FFat ถึง 80%)
};

bool tripSynced(const TripInfo& t);                          // ขึ้น cloud ครบทุกอย่างแล้ว
int  tripList(TripInfo* out, int max);                       // เรียงจาก Trip เก่าไปใหม่
bool tripGet(uint32_t trip, TripInfo& out);
int  tripReadSamples(uint32_t trip, uint32_t from, SampleRec* out, int max);
int  tripReadEvents(uint32_t trip, uint32_t from, EventRec* out, int max);
void tripMarkSynced(uint32_t trip, uint32_t samplesSynced, uint32_t eventsSynced, bool rowTimeKnown);
LogStatus logStatus();
