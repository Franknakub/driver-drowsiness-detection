// sync_task: ส่ง Trip / Sample / Event ที่ยังไม่ได้ส่งขึ้น Supabase (ADR 0002)
// เรียกฟังก์ชัน public.ingest(payload) ผ่าน PostgREST (cloud/sql/) ด้วย publishable key + DEVICE_SECRET
// ส่งทีละก้อนจาก Trip เก่าสุดที่ยังค้าง · ตำแหน่งที่ส่งแล้วเก็บใน <n>.m (trip_log) จึงส่งต่อได้หลังไฟดับ
// cloud รับซ้ำได้ (on conflict do nothing) ถ้าตอบรับแล้วไฟดับก่อนบันทึกตำแหน่ง ส่งซ้ำก็ไม่เกิดแถวซ้ำ
// ทำงานบน Core 0 เท่านั้น: HTTPS ใช้ 1–2 วิ ห้ามย้ายไปทำใน vision
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <mbedtls/platform.h>
#include "config.h"
#include "secrets.h"
#include "shared_state.h"
#include "tasks.h"
#include "trip_log.h"
#include "sync.h"

#define BODY_CAP (SYNC_BATCH_SAMPLES * 56 + SYNC_BATCH_EVENTS * 40 + 1024)

// mbedTLS ของ core ตั้งให้จองจาก RAM ภายในอย่างเดียว แต่หลัง WiFi + esp-dl เหลือก้อนใหญ่สุด ~13 KB
// ไม่พอ buffer 16 KB ของ TLS (0.3.0 ต่อไม่ติด "connection refused") -> ก้อนตั้งแต่ 1 KB ไปจองใน PSRAM แทน
static void* tlsCalloc(size_t n, size_t size) {
  void* p = n * size >= 1024 ? heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void tlsFree(void* p) { heap_caps_free(p); }   // ใช้ได้กับก้อนที่จองจากตัวจองเดิมด้วย

static SemaphoreHandle_t s_mu = nullptr;
static SyncStatus s_st = {};
static char s_devId[18];

const char* deviceId() {
  if (!s_devId[0]) {
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(s_devId, sizeof(s_devId), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  return s_devId;
}

SyncStatus syncStatus() {
  SyncStatus st;
  if (!s_mu) { memset(&st, 0, sizeof(st)); return st; }
  xSemaphoreTake(s_mu, portMAX_DELAY);
  st = s_st;
  xSemaphoreGive(s_mu);
  return st;
}

static void setErr(int http, const char* msg) {
  xSemaphoreTake(s_mu, portMAX_DELAY);
  s_st.lastHttp = http;
  size_t j = 0;
  for (size_t i = 0; msg[i] && j < sizeof(s_st.lastErr) - 1; i++) {   // ไม่ให้ทำ JSON ของ /api/status พัง
    char c = msg[i];
    s_st.lastErr[j++] = (c == '"' || c == '\\' || (uint8_t)c < 0x20) ? '\'' : c;
  }
  s_st.lastErr[j] = 0;
  xSemaphoreGive(s_mu);
}

static bool secretConfigured() {
  const char* s = DEVICE_SECRET;
  return strlen(s) >= 16 && s[0] != '<';
}

struct Body {
  char* p;
  size_t len, cap;
  bool ok = true;
  void add(const char* s) {
    size_t n = strlen(s);
    if (len + n + 1 > cap) { ok = false; return; }
    memcpy(p + len, s, n + 1);
    len += n;
  }
};

// ส่ง 1 ก้อนของ Trip t · คืน true เมื่อ cloud ตอบรับ แล้วบันทึกตำแหน่งใหม่
static bool sendBatch(HTTPClient& http, NetworkClientSecure& tls, const TripInfo& t, char* buf,
                      SampleRec* samples, EventRec* events) {
  int ns = tripReadSamples(t.trip, t.meta.samplesSynced, samples, SYNC_BATCH_SAMPLES);
  int ne = tripReadEvents(t.trip, t.meta.eventsSynced, events, SYNC_BATCH_EVENTS);
  if (ns < 0) ns = 0;
  if (ne < 0) ne = 0;
  uint32_t start = tripStartEpoch(t.meta);

  Body b{buf, 0, BODY_CAP};
  char tmp[200];
  buf[0] = 0;
  snprintf(tmp, sizeof(tmp), "{\"payload\":{\"device\":\"%s\",\"secret\":\"", deviceId());
  b.add(tmp);
  b.add(DEVICE_SECRET);
  char st[16];
  if (start) snprintf(st, sizeof(st), "%lu", (unsigned long)start); else strcpy(st, "null");
  snprintf(tmp, sizeof(tmp),
           "\",\"trip\":{\"boot\":%lu,\"nonce\":%lu,\"fw\":\"%s\",\"started_at\":%s,\"samples\":%lu,\"events\":%lu},\"samples\":[",
           (unsigned long)t.trip, (unsigned long)t.nonce, t.fw, st, (unsigned long)t.samples, (unsigned long)t.events);
  b.add(tmp);
  for (int i = 0; i < ns; i++) {
    if (i) b.add(",");
    sampleJson(tmp, sizeof(tmp), t.meta.samplesSynced + i, samples[i]);
    b.add(tmp);
  }
  b.add("],\"events\":[");
  for (int i = 0; i < ne; i++) {
    if (i) b.add(",");
    eventJson(tmp, sizeof(tmp), t.meta.eventsSynced + i, events[i]);
    b.add(tmp);
  }
  b.add("]}}");
  if (!b.ok) { setErr(0, "body buffer too small"); return false; }

  xSemaphoreTake(s_mu, portMAX_DELAY); s_st.busy = true; xSemaphoreGive(s_mu);
  int code = -1;
  String resp;
  if (http.begin(tls, SUPABASE_URL "/rest/v1/rpc/ingest")) {
    http.setReuse(true);
    http.setTimeout(15000);
    http.addHeader("apikey", SUPABASE_PUBLISHABLE_KEY);
    http.addHeader("Content-Type", "application/json");
    code = http.POST((uint8_t*)buf, b.len);
    resp = code > 0 ? http.getString() : http.errorToString(code);
    http.end();
  }
  xSemaphoreTake(s_mu, portMAX_DELAY); s_st.busy = false; xSemaphoreGive(s_mu);

  if (code != 200) {
    setErr(code, resp.c_str());
    Serial.printf("[sync] trip %lu FAILED http %d: %.120s\n", (unsigned long)t.trip, code, resp.c_str());
    xSemaphoreTake(s_mu, portMAX_DELAY); s_st.failCalls++; xSemaphoreGive(s_mu);
    return false;
  }
  tripMarkSynced(t.trip, t.meta.samplesSynced + ns, t.meta.eventsSynced + ne, start != 0);
  setErr(200, "");
  xSemaphoreTake(s_mu, portMAX_DELAY);
  s_st.okCalls++;
  s_st.lastOkMs = millis();
  xSemaphoreGive(s_mu);
  Serial.printf("[sync] trip %lu +%d samples +%d events (%u bytes)\n", (unsigned long)t.trip, ns, ne, (unsigned)b.len);
  return true;
}

static void syncTask(void*) {
  bool configured = secretConfigured();
  xSemaphoreTake(s_mu, portMAX_DELAY); s_st.configured = configured; xSemaphoreGive(s_mu);
  if (!configured) {
    setErr(0, "DEVICE_SECRET in secrets.h not set (min 16 chars)");
    Serial.println("[sync] DEVICE_SECRET not set - sync disabled");
    vTaskDelete(nullptr);
  }

  // buffer ใหญ่ทั้งหมดอยู่ใน PSRAM · RAM ภายในเหลือน้อย (TLS ต้องใช้ ~40 KB)
  TripInfo*  list    = (TripInfo*)heap_caps_malloc(MAX_TRIPS * sizeof(TripInfo), MALLOC_CAP_SPIRAM);
  SampleRec* samples = (SampleRec*)heap_caps_malloc(SYNC_BATCH_SAMPLES * sizeof(SampleRec), MALLOC_CAP_SPIRAM);
  EventRec*  events  = (EventRec*)heap_caps_malloc(SYNC_BATCH_EVENTS * sizeof(EventRec), MALLOC_CAP_SPIRAM);
  char*      body    = (char*)heap_caps_malloc(BODY_CAP, MALLOC_CAP_SPIRAM);
  if (!list || !samples || !events || !body) {
    setErr(0, "no PSRAM for sync buffers");
    vTaskDelete(nullptr);
  }
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle();
  HTTPClient http;

  uint32_t lastSend = 0, retryAt = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    int n = tripList(list, MAX_TRIPS);

    uint32_t pendS = 0, pendE = 0;
    int pick = -1;
    for (int i = 0; i < n; i++) {
      TripInfo& t = list[i];
      if (!t.current && t.samples == 0 && !tripSynced(t)) {      // บูตที่ไม่ได้เก็บอะไรเลย ไม่ต้องขึ้น cloud
        tripMarkSynced(t.trip, 0, t.events, true);
        continue;
      }
      pendS += t.samples - t.meta.samplesSynced;
      pendE += t.events - t.meta.eventsSynced;
      if (pick < 0 && !tripSynced(t)) pick = i;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.pendingSamples = pendS;
    s_st.pendingEvents = pendE;
    xSemaphoreGive(s_mu);

    if (pick < 0) continue;
    if (!stateSnapshot().staConnected) continue;
    uint32_t now = millis();
    if (retryAt && (int32_t)(now - retryAt) < 0) continue;
    const TripInfo& t = list[pick];
    // Trip ปัจจุบันที่ค้างไม่ถึงก้อน: รวบส่งทุก SYNC_IDLE_MS ไม่ส่งทีละวินาที
    if (t.current && t.samples - t.meta.samplesSynced < SYNC_BATCH_SAMPLES && lastSend &&
        now - lastSend < SYNC_IDLE_MS) continue;

    if (sendBatch(http, tls, t, body, samples, events)) {
      lastSend = millis();
      retryAt = 0;
    } else {
      retryAt = millis() + SYNC_RETRY_MS;
    }
  }
}

void startSyncTask() {
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  s_mu = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(syncTask, "sync", 8192, nullptr, 1, nullptr, CORE_NET);
}
