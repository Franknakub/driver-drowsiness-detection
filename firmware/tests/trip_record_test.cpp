// ทดสอบ trip_record.{h,cpp} บน PC: ขนาด record, การปัด/ตัดค่า, Trip Summary, crc ของ <n>.m, JSON
// และเขียน trip_fixture.json (Trip จำลอง) ให้ cloud/test ใช้เทียบ Trip Summary ของ SQL กับของ Device
//   cd firmware/tests && g++ -std=c++17 -Wall -I../DrowsyFW trip_record_test.cpp ../DrowsyFW/trip_record.cpp -o trip_record_test && ./trip_record_test
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "trip_record.h"

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static SampleRec S(uint32_t ms, DrowsinessLevel lv, float perclos, bool dis, bool unmon, uint32_t closed) {
  return sampleFrom(ms, EyeState::Open, lv, 0.1f, perclos, dis, unmon, false, true, 6.4f, 1800, closed);
}

int main() {
  // ---- ขนาดไฟล์ต้องคงที่ (ไฟล์บน FFat อ่านข้ามเวอร์ชัน) ----
  CHECK(sizeof(TripHeader) == 32);
  CHECK(sizeof(SampleRec) == 16);
  CHECK(sizeof(EventRec) == 12);

  // ---- sampleFrom: ปัดและตัดค่า ----
  SampleRec r = sampleFrom(1234, EyeState::Closed, DrowsinessLevel::Drowsy, 0.8765f, -1, true, false, true, false,
                           6.37f, 3000, 70000);
  CHECK(r.ms == 1234 && r.eye == (uint8_t)EyeState::Closed && r.level == 2);
  CHECK(r.pClosed == 877);
  CHECK(r.perclos == PERCLOS_NONE);
  CHECK(r.flags == (SF_DISTRACTION | SF_IR));
  CHECK(r.fps10 == 64);
  CHECK(r.closedMs == 65535);                       // ตัดที่ uint16
  r = sampleFrom(0, EyeState::Open, DrowsinessLevel::Normal, NAN, 1.7f, false, true, false, true, 300, 0, 0);
  CHECK(r.pClosed == 0 && r.perclos == 1000 && r.fps10 == 255 && r.flags == (SF_UNMONITORED | SF_FACE));
  r = sampleFrom(0, EyeState::Open, DrowsinessLevel::Normal, -0.2f, 0.0f, false, false, false, false, -1, 0, 0);
  CHECK(r.pClosed == 0 && r.perclos == 0 && r.fps10 == 0);

  // ---- Trip จำลอง 600 วิ ----
  std::vector<SampleRec> samples;
  std::vector<EventRec> events;
  for (uint32_t i = 0; i < 600; i++) {
    uint32_t ms = 2000 + i * 1000;
    DrowsinessLevel lv = i < 100 ? DrowsinessLevel::Normal : i < 200 ? DrowsinessLevel::Fatigue
                       : i < 230 ? DrowsinessLevel::Drowsy : i < 240 ? DrowsinessLevel::Microsleep : DrowsinessLevel::Normal;
    float perclos = i < 30 ? -1 : (i % 50) / 100.0f;             // 0.00..0.49 · 30 วิแรกยังไม่พร้อม
    uint32_t closed = (i >= 228 && i < 236) ? (i - 227) * 1000 : (i == 400 ? 1800 : 0);
    samples.push_back(S(ms, lv, perclos, i >= 300 && i < 310, i >= 500 && i < 560, closed));
  }
  auto ev = [&](uint32_t ms, LogEventType t, uint8_t from, uint8_t to, uint32_t v) {
    EventRec e = {}; e.ms = ms; e.type = (uint8_t)t; e.from = from; e.to = to; e.value = v; events.push_back(e);
  };
  ev(50000, LogEventType::TimeKnown, 0, 0, 1792300000);
  ev(102000, LogEventType::LevelChange, 0, 1, 0);
  ev(202000, LogEventType::LevelChange, 1, 2, 1600);
  ev(232000, LogEventType::LevelChange, 2, 3, 3000);
  ev(232000, LogEventType::Microsleep, 0, 0, 3000);
  ev(242000, LogEventType::LevelChange, 3, 0, 0);
  ev(305000, LogEventType::Distraction, 0, 0, 0);
  ev(402000, LogEventType::LevelChange, 0, 2, 1500);              // ข้ามระดับ 1: นับเฉพาะระดับ 2
  ev(404000, LogEventType::LevelChange, 2, 0, 0);

  TripSummary s;
  summaryReset(s);
  for (auto& x : samples) summaryAddSample(s, x);
  for (auto& x : events) summaryAddEvent(s, x);
  CHECK(s.samples == 600);
  CHECK(s.levelS[0] == 460 && s.levelS[1] == 100 && s.levelS[2] == 30 && s.levelS[3] == 10);
  CHECK(s.levelEnter[1] == 1 && s.levelEnter[2] == 2 && s.levelEnter[3] == 1);
  CHECK(s.microsleeps == 1 && s.distractions == 1);
  CHECK(s.distractionS == 10 && s.unmonitoredS == 60);
  CHECK(s.longestClosedMs == 8000);
  CHECK(s.events == events.size());
  CHECK(s.perclosN == 570);
  double expect = 0;
  for (uint32_t i = 30; i < 600; i++) expect += (i % 50) * 10;      // permille
  expect = expect / 570 / 1000;
  CHECK(fabs(summaryPerclosMean(s) - expect) < 1e-6);
  TripSummary empty;
  summaryReset(empty);
  CHECK(summaryPerclosMean(empty) < 0);

  // ---- TripMeta crc ----
  TripMeta m = {};
  m.samplesSynced = 300;
  m.timeMs = 50000;
  m.timeEpoch = 1792300000;
  m.summary = s;
  metaSeal(m);
  CHECK(metaValid(m));
  CHECK(tripStartEpoch(m) == 1792300000 - 50);
  TripMeta bad = m;
  bad.samplesSynced++;
  CHECK(!metaValid(bad));
  TripMeta zero = {};
  CHECK(!metaValid(zero));
  CHECK(crc32Of("123456789", 9) == 0xCBF43926u);                   // ค่ามาตรฐานของ CRC-32

  // ---- JSON ----
  char buf[400];
  CHECK(sampleJson(buf, sizeof(buf), 7, samples[0]) > 0);
  CHECK(strcmp(buf, "[7,2000,1,0,8,100,null,64,1800,0]") == 0);
  CHECK(sampleJson(buf, 10, 7, samples[0]) == -1);                  // buffer ไม่พอ
  CHECK(eventJson(buf, sizeof(buf), 3, events[4]) > 0);
  CHECK(strcmp(buf, "[3,232000,1,0,0,3000]") == 0);
  CHECK(summaryJson(buf, sizeof(buf), s) > 0);
  printf("summary %s\n", buf);
  CHECK(strstr(buf, "\"level_enter\":[1,2,1]") != nullptr);
  CHECK(summaryJson(buf, sizeof(buf), empty) > 0 && strstr(buf, "\"perclos_mean\":null"));

  // ---- fixture ให้ cloud/test: payload ที่ Device ส่ง + Trip Summary ที่ Device คำนวณ ----
  FILE* f = fopen("trip_fixture.json", "w");
  if (f) {
    std::string out = "{\"trip\":{\"boot\":42,\"nonce\":3735928559,\"fw\":\"test\",\"started_at\":";
    out += std::to_string(tripStartEpoch(m)) + ",\"samples\":600,\"events\":" + std::to_string(events.size()) + "},\"samples\":[";
    for (size_t i = 0; i < samples.size(); i++) { sampleJson(buf, sizeof(buf), i, samples[i]); out += (i ? "," : ""); out += buf; }
    out += "],\"events\":[";
    for (size_t i = 0; i < events.size(); i++) { eventJson(buf, sizeof(buf), i, events[i]); out += (i ? "," : ""); out += buf; }
    summaryJson(buf, sizeof(buf), s);
    out += "],\"device_summary\":";
    out += buf;
    out += "}\n";
    fputs(out.c_str(), f);
    fclose(f);
  }

  printf("%d/%d checks passed\n", checks - fails, checks);
  return fails ? 1 : 0;
}
