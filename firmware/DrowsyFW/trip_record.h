// รูปแบบไฟล์ Trip บน FFat + Trip Summary · ไม่พึ่ง Arduino: ทดสอบบน PC ได้ที่ firmware/tests/trip_record_test.cpp
//
// 1 Trip = 3 ไฟล์ใน /trips/ (n = หมายเลข Trip = เลขบูต):
//   <n>.s  TripHeader แล้วตามด้วย SampleRec ทีละ 1 วินาที (เขียนต่อท้ายอย่างเดียว)
//   <n>.e  EventRec (เขียนต่อท้ายอย่างเดียว) รวม TimeKnown = จุดเทียบ millis() กับเวลาจริง
//   <n>.m  TripMeta: ตำแหน่งที่ Sync แล้ว + Trip Summary ที่คำนวณไว้ (เขียนทับทั้งไฟล์ มี crc ถ้าเสียให้ถือว่ายังไม่ Sync)
// เวลาใน record = millis() นับจากบูต (= เริ่ม Trip) · เวลาจริง = epoch ของ TimeKnown + (ms - ms ของ TimeKnown) / 1000
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "domain_types.h"

#define TRIP_MAGIC        0x31534444u   // "DDS1"
#define TRIP_META_MAGIC   0x314D4444u   // "DDM1"
#define TRIP_FORMAT_VER   1

struct __attribute__((packed)) TripHeader {   // 32 ไบต์ ต้นไฟล์ <n>.s
  uint32_t magic;
  uint16_t version;
  uint16_t sampleSize;      // sizeof(SampleRec) ตอนเขียน
  uint32_t trip;
  uint32_t nonce;           // สุ่มทุกบูต: ถ้า NVS ถูกล้างจนเลข Trip ซ้ำ cloud ยังแยก Trip ได้
  char     fw[16];
};

enum SampleFlag : uint8_t {
  SF_DISTRACTION = 1, SF_UNMONITORED = 2, SF_IR = 4, SF_FACE = 8,
};

struct __attribute__((packed)) SampleRec {     // 16 ไบต์ · 1 ชม. = 57.6 KB
  uint32_t ms;              // millis() ตอนเก็บ
  uint8_t  eye;             // EyeState
  uint8_t  level;           // DrowsinessLevel
  uint8_t  flags;           // SampleFlag
  uint8_t  fps10;           // fps × 10
  uint16_t pClosed;         // P(ตาปิด) × 1000
  uint16_t perclos;         // PERCLOS × 1000 · PERCLOS_NONE = ยังไม่พร้อม
  uint16_t ldr;             // ADC ดิบ (มืด = สูง)
  uint16_t closedMs;        // ตาปิดต่อเนื่องที่ยาวที่สุดที่เห็นในวินาทีนี้ (ตัดที่ 65535)
};
#define PERCLOS_NONE 0xFFFFu

enum class LogEventType : uint8_t {
  LevelChange = 0, Microsleep = 1, Distraction = 2,   // ตรงกับ DrowsyEventType
  TimeKnown = 10,                                     // value = epoch (วินาที) ณ ms
};

struct __attribute__((packed)) EventRec {      // 12 ไบต์
  uint32_t ms;
  uint8_t  type;            // LogEventType
  uint8_t  from, to;        // LevelChange
  uint8_t  pad;
  uint32_t value;           // LevelChange/Microsleep: ตาปิดต่อเนื่อง (ms) · TimeKnown: epoch
};

// Trip Summary: นิยามเดียวกับ view trip_summary บน Supabase (cloud/sql/)
struct TripSummary {
  uint32_t samples;         // = วินาทีที่บันทึกได้
  uint32_t levelS[4];       // วินาทีที่อยู่ในแต่ละ Drowsiness Level
  uint32_t levelEnter[4];   // จำนวนครั้งที่ขึ้นไปถึงระดับนั้น (LevelChange ที่ to > from) · [0] ไม่ใช้
  uint32_t microsleeps;     // จำนวน Event Microsleep
  uint32_t distractions;    // จำนวน Event Distraction
  uint32_t distractionS;    // วินาทีที่อยู่ใน Distraction
  uint32_t unmonitoredS;    // วินาทีที่ Unmonitored
  uint32_t longestClosedMs; // ตาปิดต่อเนื่องนานที่สุด
  uint32_t perclosN;        // จำนวน Sample ที่ PERCLOS พร้อม
  uint64_t perclosSum;      // ผลรวม PERCLOS × 1000
  uint32_t events;          // จำนวน EventRec ที่นับแล้ว (รวม TimeKnown) ใช้เช็คว่า cache ตรงกับไฟล์
};

void  summaryReset(TripSummary& s);
void  summaryAddSample(TripSummary& s, const SampleRec& r);
void  summaryAddEvent(TripSummary& s, const EventRec& e);
float summaryPerclosMean(const TripSummary& s);     // -1 = ไม่มี Sample ที่ PERCLOS พร้อม

struct TripMeta {      // ไฟล์ <n>.m · ไม่ packed: summary ถูกส่งต่อเป็น reference (Xtensa อ่าน uint32 ไม่ตรง alignment ไม่ได้)
  uint32_t    magic;
  uint32_t    samplesSynced;   // Sample ลำดับ [0, samplesSynced) ขึ้น cloud แล้ว
  uint32_t    eventsSynced;
  uint8_t     rowTimeKnown;    // แถว trips บน cloud ส่งไปแล้วพร้อมเวลาจริงหรือยัง
  uint8_t     rowSent;         // เคยส่งแถว trips แล้ว
  uint8_t     pad[2];
  uint32_t    timeMs, timeEpoch; // จาก Event TimeKnown · timeEpoch = 0 = ไม่ทราบเวลา
  TripSummary summary;
  uint32_t    crc;             // crc32 ของทุกไบต์ก่อนหน้า
};

// epoch ของ millis() = 0 (= เริ่ม Trip) · 0 = ไม่ทราบเวลา
inline uint32_t tripStartEpoch(const TripMeta& m) {
  return m.timeEpoch ? m.timeEpoch - m.timeMs / 1000 : 0;
}

uint32_t crc32Of(const void* data, size_t len);
void     metaSeal(TripMeta& m);                 // ใส่ magic + crc
bool     metaValid(const TripMeta& m);

// ---- JSON (ใช้ทั้ง Device Dashboard และ Sync) · คืนจำนวนตัวอักษร หรือ -1 ถ้า buffer ไม่พอ ----
// Sample = [i,ms,eye,level,flags,p_closed‰,perclos‰|null,fps×10,ldr,closed_ms]
int sampleJson(char* buf, size_t n, uint32_t idx, const SampleRec& r);
// Event = [i,ms,type,from,to,value]
int eventJson(char* buf, size_t n, uint32_t idx, const EventRec& e);
int summaryJson(char* buf, size_t n, const TripSummary& s);

// เติม SampleRec จากค่าสด (แยกออกมาให้ทดสอบการปัด/ตัดค่าได้)
SampleRec sampleFrom(uint32_t ms, EyeState eye, DrowsinessLevel level, float pClosed, float perclos,
                     bool distraction, bool unmonitored, bool ir, bool face, float fps, uint16_t ldr,
                     uint32_t closedPeakMs);
