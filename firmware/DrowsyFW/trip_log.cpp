// log_task: เลข Trip (= จำนวนครั้งที่บูต) + เขียน Sample ทุก 1 วิ และ Event ลง FFat (ADR 0002)
// รูปแบบไฟล์: trip_record.h · ทุกไฟล์เขียนต่อท้ายแล้วปิดทันที ไฟดับ (= จบ Trip) เสียได้อย่างมาก record สุดท้าย
// FFat ถึง 80%: ลบ Trip เก่าสุดที่ Sync ครบแล้ว · ถึง 95%: หยุดเก็บ Sample แต่ยังเก็บ Event
#include <Preferences.h>
#include <FFat.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include "config.h"
#include "shared_state.h"
#include "tasks.h"
#include "drowsiness.h"
#include "trip_log.h"

#define TRIP_DIR           "/trips"
#define FS_CLEAN_AT        0.80f   // ลบ Trip ที่ Sync แล้วเมื่อใช้พื้นที่ถึงนี้
#define FS_CLEAN_TO        0.75f   //   ลบจนเหลือต่ำกว่านี้ กันลบทีละนิดทุกนาที
#define FS_PAUSE_AT        0.95f   // ไม่มีอะไรให้ลบแล้วและถึงนี้: หยุดเก็บ Sample
#define META_SAVE_MS       60000   // บันทึก Trip Summary ของ Trip ปัจจุบันลง <n>.m

static SemaphoreHandle_t s_fs = nullptr;   // recursive: ฟังก์ชันใน lock เรียกกันเองได้
static TripInfo* s_trips = nullptr;        // PSRAM เรียงตามเลข Trip
static int       s_n = 0;
static int       s_cur = -1;               // index ของ Trip ปัจจุบัน
static bool      s_fsOk = false, s_paused = false;
static uint32_t  s_deleted = 0;

struct FsGuard {
  FsGuard()  { xSemaphoreTakeRecursive(s_fs, portMAX_DELAY); }
  ~FsGuard() { xSemaphoreGiveRecursive(s_fs); }
};

static void pathOf(char* buf, size_t n, uint32_t trip, char ext) {
  snprintf(buf, n, TRIP_DIR "/%lu.%c", (unsigned long)trip, ext);
}

static size_t fileSize(uint32_t trip, char ext) {
  char p[32];
  pathOf(p, sizeof(p), trip, ext);
  File f = FFat.open(p, FILE_READ);
  if (!f) return 0;
  size_t n = f.size();
  f.close();
  return n;
}

static bool appendRec(uint32_t trip, char ext, const void* rec, size_t len) {
  char p[32];
  pathOf(p, sizeof(p), trip, ext);
  File f = FFat.open(p, FILE_APPEND);
  if (!f) return false;
  bool ok = f.write((const uint8_t*)rec, len) == len;
  f.close();
  return ok;
}

static void metaSave(TripInfo& t) {
  metaSeal(t.meta);
  char p[32];
  pathOf(p, sizeof(p), t.trip, 'm');
  File f = FFat.open(p, FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t*)&t.meta, sizeof(t.meta));
  f.close();
}

static int indexOf(uint32_t trip) {
  for (int i = 0; i < s_n; i++) if (s_trips[i].trip == trip) return i;
  return -1;
}

// อ่านไฟล์ทั้ง Trip ใหม่เมื่อ <n>.m ไม่ตรงกับไฟล์ (ส่วนใหญ่ = Trip ก่อนหน้าที่ไฟดับก่อนบันทึก Summary รอบสุดท้าย)
static void rebuildMeta(TripInfo& t) {
  TripMeta& m = t.meta;
  summaryReset(m.summary);
  m.timeMs = m.timeEpoch = 0;
  char p[32];
  pathOf(p, sizeof(p), t.trip, 's');
  File f = FFat.open(p, FILE_READ);
  if (f) {
    f.seek(sizeof(TripHeader));
    SampleRec buf[32];
    for (uint32_t left = t.samples; left; ) {        // record ที่เขียนไม่ครบตอนไฟดับไม่นับ
      int want = left < 32 ? left : 32;
      int got = f.read((uint8_t*)buf, want * sizeof(SampleRec)) / sizeof(SampleRec);
      if (got <= 0) break;
      for (int i = 0; i < got; i++) summaryAddSample(m.summary, buf[i]);
      left -= got;
    }
    f.close();
  }
  pathOf(p, sizeof(p), t.trip, 'e');
  f = FFat.open(p, FILE_READ);
  if (f) {
    EventRec e;
    for (uint32_t i = 0; i < t.events && f.read((uint8_t*)&e, sizeof(e)) == sizeof(e); i++) {
      summaryAddEvent(m.summary, e);
      if (e.type == (uint8_t)LogEventType::TimeKnown && !m.timeEpoch) { m.timeMs = e.ms; m.timeEpoch = e.value; }
    }
    f.close();
  }
  if (m.samplesSynced > t.samples) m.samplesSynced = t.samples;
  if (m.eventsSynced > t.events) m.eventsSynced = t.events;
  metaSave(t);
  Serial.printf("[log] trip %lu: summary rebuilt (%lu samples, %lu events)\n",
                (unsigned long)t.trip, (unsigned long)t.samples, (unsigned long)t.events);
}

static bool loadTrip(uint32_t trip, TripInfo& t) {
  memset(&t, 0, sizeof(t));
  t.trip = trip;
  char p[32];
  pathOf(p, sizeof(p), trip, 's');
  File f = FFat.open(p, FILE_READ);
  if (!f) return false;
  TripHeader h;
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == TRIP_MAGIC &&
            h.sampleSize == sizeof(SampleRec);
  size_t sz = f.size();
  f.close();
  if (!ok) return false;
  t.nonce = h.nonce;
  memcpy(t.fw, h.fw, sizeof(t.fw));
  t.fw[sizeof(t.fw) - 1] = 0;
  t.samples = (sz - sizeof(TripHeader)) / sizeof(SampleRec);
  t.events = fileSize(trip, 'e') / sizeof(EventRec);

  pathOf(p, sizeof(p), trip, 'm');
  f = FFat.open(p, FILE_READ);
  bool metaOk = f && f.read((uint8_t*)&t.meta, sizeof(t.meta)) == sizeof(t.meta) && metaValid(t.meta);
  if (f) f.close();
  if (!metaOk) memset(&t.meta, 0, sizeof(t.meta));   // ถือว่ายังไม่ Sync (cloud รับซ้ำได้)
  if (!metaOk || t.meta.summary.samples != t.samples || t.meta.summary.events != t.events) rebuildMeta(t);
  return true;
}

static void deleteTrip(int i) {
  char p[32];
  for (char ext : {'s', 'e', 'm'}) { pathOf(p, sizeof(p), s_trips[i].trip, ext); FFat.remove(p); }
  Serial.printf("[log] deleted synced trip %lu\n", (unsigned long)s_trips[i].trip);
  memmove(&s_trips[i], &s_trips[i + 1], (s_n - i - 1) * sizeof(TripInfo));
  s_n--;
  if (s_cur > i) s_cur--;
  s_deleted++;
}

static float fsUse() {
  size_t total = FFat.totalBytes();
  return total ? (float)FFat.usedBytes() / total : 1.0f;
}

static void manageSpace() {
  FsGuard g;
  float use = fsUse();
  if (use >= FS_CLEAN_AT) {
    for (int i = 0; i < s_n && use >= FS_CLEAN_TO; ) {
      if (i != s_cur && tripSynced(s_trips[i])) { deleteTrip(i); use = fsUse(); }
      else i++;
    }
  }
  bool pause = use >= FS_PAUSE_AT;
  if (pause != s_paused) Serial.printf("[log] samples %s (FFat %.0f%%)\n", pause ? "PAUSED" : "resumed", use * 100);
  s_paused = pause;
}

static int cmpTrip(const void* a, const void* b) {
  uint32_t x = ((const TripInfo*)a)->trip, y = ((const TripInfo*)b)->trip;
  return x < y ? -1 : x > y;
}

// อ่าน Trip เก่าทั้งหมดเข้า index แล้วสร้างไฟล์ของ Trip ใหม่
static void tripBegin() {
  s_fs = xSemaphoreCreateRecursiveMutex();
  s_trips = (TripInfo*)heap_caps_calloc(MAX_TRIPS, sizeof(TripInfo), MALLOC_CAP_SPIRAM);

  Preferences prefs;
  prefs.begin("drowsy", false);
  uint32_t boot = prefs.getUInt("boot", 0) + 1;

  if (!s_trips || !FFat.begin(true)) {                // true = format ถ้ายังไม่มีระบบไฟล์
    Serial.println("[log] FFat mount FAILED - check partitions.csv / PartitionScheme=custom");
    prefs.putUInt("boot", boot);
    prefs.end();
    { StateGuard g; g_state.bootCount = boot; }
    return;
  }
  FFat.mkdir(TRIP_DIR);

  uint32_t maxTrip = 0;
  File dir = FFat.open(TRIP_DIR);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    char name[32];
    strlcpy(name, f.name(), sizeof(name));            // "12.s"
    f.close();
    const char* dot = strrchr(name, '.');
    if (!dot || strcmp(dot, ".s") != 0 || s_n >= MAX_TRIPS) continue;
    uint32_t trip = strtoul(name, nullptr, 10);
    if (!trip) continue;
    if (loadTrip(trip, s_trips[s_n])) s_n++;
    if (trip > maxTrip) maxTrip = trip;
  }
  dir.close();
  qsort(s_trips, s_n, sizeof(TripInfo), cmpTrip);

  // NVS ถูกล้าง (เช่น erase flash) แต่ไฟล์เก่ายังอยู่: เลขต่อจากไฟล์ ไม่เขียนทับ Trip เดิม
  if (boot <= maxTrip) boot = maxTrip + 1;
  prefs.putUInt("boot", boot);
  prefs.end();
  { StateGuard g; g_state.bootCount = boot; }
  Serial.printf("[log] Trip #%u starts · %d old trips on FFat\n", boot, s_n);

  if (s_n >= MAX_TRIPS) {                             // index เต็ม: ลบเก่าสุดที่ Sync แล้วให้ที่ว่าง 1 ช่อง
    for (int i = 0; i < s_n; i++) if (tripSynced(s_trips[i])) { deleteTrip(i); break; }
  }
  if (s_n >= MAX_TRIPS) { Serial.println("[log] too many unsynced trips - not logging"); return; }

  TripInfo& t = s_trips[s_n];
  memset(&t, 0, sizeof(t));
  t.trip = boot;
  t.nonce = esp_random();
  t.current = true;
  TripHeader h = {};
  h.magic = TRIP_MAGIC;
  h.version = TRIP_FORMAT_VER;
  h.sampleSize = sizeof(SampleRec);
  h.trip = boot;
  h.nonce = t.nonce;
  strlcpy(h.fw, FW_VERSION, sizeof(h.fw));
  memcpy(t.fw, h.fw, sizeof(t.fw));
  char p[32];
  pathOf(p, sizeof(p), boot, 's');
  File f = FFat.open(p, FILE_WRITE);
  if (!f || f.write((const uint8_t*)&h, sizeof(h)) != sizeof(h)) {
    Serial.println("[log] cannot create trip file");
    if (f) f.close();
    return;
  }
  f.close();
  pathOf(p, sizeof(p), boot, 'e');
  FFat.open(p, FILE_WRITE).close();
  metaSave(t);
  s_cur = s_n++;
  s_fsOk = true;
  Serial.printf("[log] FFat %u KB total, %u KB used\n",
                (unsigned)(FFat.totalBytes() / 1024), (unsigned)(FFat.usedBytes() / 1024));
  manageSpace();
}

static void writeEvent(const EventRec& e) {
  if (!s_fsOk || s_cur < 0) return;
  FsGuard g;
  TripInfo& t = s_trips[s_cur];
  if (!appendRec(t.trip, 'e', &e, sizeof(e))) return;
  t.events++;
  summaryAddEvent(t.meta.summary, e);
  if (e.type == (uint8_t)LogEventType::TimeKnown && !t.meta.timeEpoch) {
    t.meta.timeMs = e.ms;
    t.meta.timeEpoch = e.value;
    metaSave(t);
  }
}

static void writeSample(const SampleRec& r) {
  if (!s_fsOk || s_cur < 0 || s_paused) return;
  FsGuard g;
  TripInfo& t = s_trips[s_cur];
  if (!appendRec(t.trip, 's', &r, sizeof(r))) return;
  t.samples++;
  summaryAddSample(t.meta.summary, r);
}

static void logTask(void*) {
  uint32_t next = millis() + 1000, lastMeta = millis();
  bool timeLogged = false;
  DrowsyEvent de;
  for (;;) {
    int32_t wait = (int32_t)(next - millis());
    if (wait > 0 && drowsyEventPop(de, wait)) {
      drowsyEventPrint(de);
      EventRec e = {};
      e.ms = de.ms;
      e.type = (uint8_t)de.type;
      e.from = (uint8_t)de.from;
      e.to = (uint8_t)de.to;
      e.value = de.closedMs;
      writeEvent(e);
      continue;
    }
    uint32_t now = millis();
    next += 1000;
    if ((int32_t)(now - next) > 3000) next = now + 1000;   // ค้างนาน: ไม่เขียนย้อนรัวๆ

    SharedState s;
    {
      StateGuard g;
      s = g_state;
      g_state.closedPeakMs = g_state.closedMs;              // เริ่มนับยอดของวินาทีถัดไป
    }
    if (s.timeKnown && !timeLogged) {
      timeLogged = true;
      EventRec e = {};
      e.ms = now;
      e.type = (uint8_t)LogEventType::TimeKnown;
      e.value = (uint32_t)time(nullptr);
      writeEvent(e);
    }
    writeSample(sampleFrom(now, s.eyeState, s.level, s.pClosed, s.perclos, s.distraction, s.unmonitored,
                           s.irOn, s.faceFound, s.fps, s.ldrRaw, s.closedPeakMs));

    if (now - lastMeta >= META_SAVE_MS && s_fsOk) {
      lastMeta = now;
      { FsGuard g; if (s_cur >= 0) metaSave(s_trips[s_cur]); }
      manageSpace();
    }
  }
}

void startLogTask() {
  tripBegin();
  xTaskCreatePinnedToCore(logTask, "log", 4096, nullptr, 2, nullptr, CORE_NET);
}

// ---- ใช้จาก sync task / หน้าเว็บ ----

bool tripSynced(const TripInfo& t) {
  return t.meta.rowSent && t.meta.samplesSynced >= t.samples && t.meta.eventsSynced >= t.events &&
         (t.meta.rowTimeKnown || !t.meta.timeEpoch);
}

int tripList(TripInfo* out, int max) {
  if (!s_fs) return 0;
  FsGuard g;
  int n = s_n < max ? s_n : max;
  memcpy(out, s_trips, n * sizeof(TripInfo));
  return n;
}

bool tripGet(uint32_t trip, TripInfo& out) {
  if (!s_fs) return false;
  FsGuard g;
  int i = indexOf(trip);
  if (i < 0) return false;
  out = s_trips[i];
  return true;
}

static int readRecs(uint32_t trip, char ext, size_t base, size_t size, uint32_t from, void* out, int max) {
  char p[32];
  pathOf(p, sizeof(p), trip, ext);
  File f = FFat.open(p, FILE_READ);
  if (!f) return 0;
  int got = 0;
  if (f.seek(base + (size_t)from * size)) got = f.read((uint8_t*)out, (size_t)max * size) / size;
  f.close();
  return got;
}

int tripReadSamples(uint32_t trip, uint32_t from, SampleRec* out, int max) {
  if (!s_fs) return 0;
  FsGuard g;
  int i = indexOf(trip);
  if (i < 0 || from >= s_trips[i].samples) return 0;
  if ((uint32_t)max > s_trips[i].samples - from) max = s_trips[i].samples - from;  // ไม่อ่าน record ที่เขียนค้างครึ่ง
  return readRecs(trip, 's', sizeof(TripHeader), sizeof(SampleRec), from, out, max);
}

int tripReadEvents(uint32_t trip, uint32_t from, EventRec* out, int max) {
  if (!s_fs) return 0;
  FsGuard g;
  int i = indexOf(trip);
  if (i < 0 || from >= s_trips[i].events) return 0;
  if ((uint32_t)max > s_trips[i].events - from) max = s_trips[i].events - from;
  return readRecs(trip, 'e', 0, sizeof(EventRec), from, out, max);
}

void tripMarkSynced(uint32_t trip, uint32_t samplesSynced, uint32_t eventsSynced, bool rowTimeKnown) {
  if (!s_fs) return;
  FsGuard g;
  int i = indexOf(trip);
  if (i < 0) return;
  TripMeta& m = s_trips[i].meta;
  m.samplesSynced = samplesSynced;
  m.eventsSynced = eventsSynced;
  m.rowSent = 1;
  m.rowTimeKnown = rowTimeKnown;
  metaSave(s_trips[i]);
}

LogStatus logStatus() {
  LogStatus st = {};
  if (!s_fs) return st;
  FsGuard g;
  st.fsOk = s_fsOk;
  st.samplesPaused = s_paused;
  st.totalKB = FFat.totalBytes() / 1024;
  st.usedKB = FFat.usedBytes() / 1024;
  st.trips = s_n;
  st.deletedTrips = s_deleted;
  return st;
}
