// ดู trip_record.h
#include "trip_record.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

void summaryReset(TripSummary& s) { memset(&s, 0, sizeof(s)); }

void summaryAddSample(TripSummary& s, const SampleRec& r) {
  s.samples++;
  if (r.level < 4) s.levelS[r.level]++;
  if (r.flags & SF_DISTRACTION) s.distractionS++;
  if (r.flags & SF_UNMONITORED) s.unmonitoredS++;
  if (r.closedMs > s.longestClosedMs) s.longestClosedMs = r.closedMs;
  if (r.perclos != PERCLOS_NONE) { s.perclosN++; s.perclosSum += r.perclos; }
}

void summaryAddEvent(TripSummary& s, const EventRec& e) {
  s.events++;
  switch ((LogEventType)e.type) {
    case LogEventType::LevelChange:
      if (e.to > e.from && e.to < 4) s.levelEnter[e.to]++;
      break;
    case LogEventType::Microsleep:
      s.microsleeps++;
      if (e.value > s.longestClosedMs) s.longestClosedMs = e.value;
      break;
    case LogEventType::Distraction:
      s.distractions++;
      break;
    default:
      break;
  }
}

float summaryPerclosMean(const TripSummary& s) {
  return s.perclosN ? (float)((double)s.perclosSum / s.perclosN / 1000.0) : -1.0f;
}

uint32_t crc32Of(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  uint32_t c = 0xFFFFFFFFu;
  while (len--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

void metaSeal(TripMeta& m) {
  m.magic = TRIP_META_MAGIC;
  m.crc = crc32Of(&m, offsetof(TripMeta, crc));
}

bool metaValid(const TripMeta& m) {
  return m.magic == TRIP_META_MAGIC && m.crc == crc32Of(&m, offsetof(TripMeta, crc));
}

static int fit(int n, size_t cap) { return (n < 0 || (size_t)n >= cap) ? -1 : n; }

int sampleJson(char* buf, size_t n, uint32_t idx, const SampleRec& r) {
  char perclos[8];
  if (r.perclos == PERCLOS_NONE) strcpy(perclos, "null");
  else snprintf(perclos, sizeof(perclos), "%u", (unsigned)r.perclos);
  return fit(snprintf(buf, n, "[%lu,%lu,%u,%u,%u,%u,%s,%u,%u,%u]",
                      (unsigned long)idx, (unsigned long)r.ms, r.eye, r.level, r.flags,
                      (unsigned)r.pClosed, perclos, r.fps10, (unsigned)r.ldr, (unsigned)r.closedMs), n);
}

int eventJson(char* buf, size_t n, uint32_t idx, const EventRec& e) {
  return fit(snprintf(buf, n, "[%lu,%lu,%u,%u,%u,%lu]", (unsigned long)idx, (unsigned long)e.ms,
                      e.type, e.from, e.to, (unsigned long)e.value), n);
}

int summaryJson(char* buf, size_t n, const TripSummary& s) {
  float pm = summaryPerclosMean(s);
  char perclos[12];
  if (pm < 0) strcpy(perclos, "null");
  else snprintf(perclos, sizeof(perclos), "%.3f", pm);
  return fit(snprintf(buf, n,
    "{\"duration_s\":%lu,\"level_s\":[%lu,%lu,%lu,%lu],\"level_enter\":[%lu,%lu,%lu],"
    "\"microsleeps\":%lu,\"distractions\":%lu,\"distraction_s\":%lu,\"unmonitored_s\":%lu,"
    "\"longest_closed_ms\":%lu,\"perclos_mean\":%s}",
    (unsigned long)s.samples,
    (unsigned long)s.levelS[0], (unsigned long)s.levelS[1], (unsigned long)s.levelS[2], (unsigned long)s.levelS[3],
    (unsigned long)s.levelEnter[1], (unsigned long)s.levelEnter[2], (unsigned long)s.levelEnter[3],
    (unsigned long)s.microsleeps, (unsigned long)s.distractions, (unsigned long)s.distractionS,
    (unsigned long)s.unmonitoredS, (unsigned long)s.longestClosedMs, perclos), n);
}

static uint16_t permille(float v) {
  if (!(v > 0)) return 0;                 // รวม NaN
  if (v >= 1) return 1000;
  return (uint16_t)lroundf(v * 1000);
}

SampleRec sampleFrom(uint32_t ms, EyeState eye, DrowsinessLevel level, float pClosed, float perclos,
                     bool distraction, bool unmonitored, bool ir, bool face, float fps, uint16_t ldr,
                     uint32_t closedPeakMs) {
  SampleRec r;
  r.ms = ms;
  r.eye = (uint8_t)eye;
  r.level = (uint8_t)level;
  r.flags = (distraction ? SF_DISTRACTION : 0) | (unmonitored ? SF_UNMONITORED : 0) |
            (ir ? SF_IR : 0) | (face ? SF_FACE : 0);
  float f = fps * 10;
  r.fps10 = !(f > 0) ? 0 : f >= 255 ? 255 : (uint8_t)lroundf(f);
  r.pClosed = permille(pClosed);
  r.perclos = perclos < 0 ? PERCLOS_NONE : permille(perclos);
  r.ldr = ldr;
  r.closedMs = closedPeakMs > 65535 ? 65535 : (uint16_t)closedPeakMs;
  return r;
}
