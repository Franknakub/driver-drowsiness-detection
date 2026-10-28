// net_task: AP ของ Device เปิดตลอด + ลอง WiFi ที่บันทึกไว้ทุก 1 นาที + NTP + mDNS
//   WiFi ที่ลอง = รายการจากหน้า /wifi (NVS namespace "wifi") ตามลำดับ แล้วต่อด้วย WIFI_NETWORKS ใน secrets.h (สำรอง)
//   สแกน WiFi ใกล้เคียงทำใน task นี้เท่านั้น ไม่ให้ชนกับการเชื่อมต่อ
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <sys/time.h>
#include "config.h"
#include "secrets.h"
#include "shared_state.h"
#include "tasks.h"

// รับเวลาจริงครั้งแรกจาก NTP หรือ browser แล้วคำนวณเวลาเริ่ม Trip ย้อนกลับ
bool timeSetFromEpoch(time_t epoch, const char* source) {
  if ((unsigned long)epoch < TIME_VALID_EPOCH) return false;
  StateGuard g;
  if (g_state.timeKnown) return false;
  if (strcmp(source, "ntp") != 0) {              // NTP ตั้งนาฬิการะบบให้เองแล้ว
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
  }
  g_state.timeKnown = true;
  g_state.tripStartEpoch = epoch - (millis() - g_state.tripStartMs) / 1000;
  Serial.printf("[net] real time from %s: %lu\n", source, (unsigned long)epoch);
  return true;
}

// ---- WiFi ที่บันทึกจากหน้าเว็บ ----
struct SavedNet { char ssid[33]; char pass[65]; };
static SavedNet s_saved[WIFI_SAVED_MAX];
static int s_nSaved = 0;
static WifiNet s_scan[WIFI_SCAN_MAX];
static int s_nScan = 0;
static uint32_t s_scanAt = 0;
static SemaphoreHandle_t s_wifiMu = nullptr;
static volatile bool s_retryNow = false, s_scanReq = false, s_scanning = false;

struct WifiLock {
  WifiLock()  { xSemaphoreTake(s_wifiMu, portMAX_DELAY); }
  ~WifiLock() { xSemaphoreGive(s_wifiMu); }
};

static void savedLoad() {
  Preferences prefs;
  if (!prefs.begin("wifi", true)) return;
  int n = prefs.getUChar("n", 0);
  s_nSaved = 0;
  for (int i = 0; i < n && i < WIFI_SAVED_MAX; i++) {
    char k[4];
    SavedNet& s = s_saved[s_nSaved];
    snprintf(k, sizeof(k), "s%d", i);
    if (!prefs.getString(k, s.ssid, sizeof(s.ssid)) || !s.ssid[0]) continue;
    snprintf(k, sizeof(k), "p%d", i);
    if (!prefs.getString(k, s.pass, sizeof(s.pass))) s.pass[0] = 0;
    s_nSaved++;
  }
  prefs.end();
}

static void savedStore() {   // เรียกขณะถือ WifiLock
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putUChar("n", s_nSaved);
  for (int i = 0; i < s_nSaved; i++) {
    char k[4];
    snprintf(k, sizeof(k), "s%d", i); prefs.putString(k, s_saved[i].ssid);
    snprintf(k, sizeof(k), "p%d", i); prefs.putString(k, s_saved[i].pass);
  }
  prefs.end();
}

static int savedFind(const char* ssid) {
  for (int i = 0; i < s_nSaved; i++) if (strcmp(s_saved[i].ssid, ssid) == 0) return i;
  return -1;
}

int wifiSavedList(WifiNet* out, int max) {
  WifiLock l;
  int n = 0;
  for (; n < s_nSaved && n < max; n++) { strlcpy(out[n].ssid, s_saved[n].ssid, sizeof(out[n].ssid)); out[n].rssi = 0; out[n].secure = s_saved[n].pass[0]; }
  return n;
}

bool wifiSave(const char* ssid, const char* pass) {
  size_t ls = strlen(ssid), lp = strlen(pass);
  if (ls == 0 || ls > 32 || lp > 63 || (lp > 0 && lp < 8)) return false;
  {
    WifiLock l;
    int i = savedFind(ssid);
    int last = i >= 0 ? i : min(s_nSaved, WIFI_SAVED_MAX - 1);   // เต็มแล้ว: ตัวท้ายสุดหลุด
    for (int k = last; k > 0; k--) s_saved[k] = s_saved[k - 1];
    strlcpy(s_saved[0].ssid, ssid, sizeof(s_saved[0].ssid));
    strlcpy(s_saved[0].pass, pass, sizeof(s_saved[0].pass));
    if (i < 0 && s_nSaved < WIFI_SAVED_MAX) s_nSaved++;
    savedStore();
  }
  s_retryNow = true;
  return true;
}

bool wifiForget(const char* ssid) {
  {
    WifiLock l;
    int i = savedFind(ssid);
    if (i < 0) return false;
    for (int k = i; k < s_nSaved - 1; k++) s_saved[k] = s_saved[k + 1];
    s_nSaved--;
    savedStore();
  }
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) s_retryNow = true;   // ต่ออยู่กับตัวที่ลบ: ตัดแล้วลองตัวอื่น
  return true;
}

void wifiScanRequest() {
  if (!s_scanning && (s_scanAt == 0 || millis() - s_scanAt > WIFI_SCAN_TTL_MS)) s_scanReq = true;
}

int wifiScanResults(WifiNet* out, int max, bool* scanning) {
  WifiLock l;
  *scanning = s_scanning || s_scanReq;
  int n = min(s_nScan, max);
  memcpy(out, s_scan, n * sizeof(WifiNet));
  return n;
}

static void doScan() {
  s_scanning = true;
  int n = WiFi.scanNetworks(false, false);
  WifiNet tmp[WIFI_SCAN_MAX];
  int m = 0;
  for (int i = 0; i < n && m < WIFI_SCAN_MAX; i++) {            // เรียงแรงสุดก่อนอยู่แล้ว · ตัดชื่อซ้ำ (หลายจุดกระจายสัญญาณ)
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int k = 0; k < m && !dup; k++) dup = s == tmp[k].ssid;
    if (dup) continue;
    strlcpy(tmp[m].ssid, s.c_str(), sizeof(tmp[m].ssid));
    tmp[m].rssi = (int8_t)WiFi.RSSI(i);
    tmp[m].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    m++;
  }
  WiFi.scanDelete();
  { WifiLock l; memcpy(s_scan, tmp, m * sizeof(WifiNet)); s_nScan = m; s_scanAt = millis(); }
  s_scanning = false;
}

static bool tryOne(const char* ssid, const char* pass) {
  Serial.printf("[net] trying %s\n", ssid);
  WiFi.begin(ssid, pass[0] ? pass : nullptr);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) vTaskDelay(pdMS_TO_TICKS(500));
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false, false);
  return false;
}

static bool tryNetworks() {
  SavedNet list[WIFI_SAVED_MAX];
  int n;
  { WifiLock l; n = s_nSaved; memcpy(list, s_saved, n * sizeof(SavedNet)); }
  for (int i = 0; i < n; i++) if (tryOne(list[i].ssid, list[i].pass)) return true;
  for (const WifiCred& w : WIFI_NETWORKS) {
    bool dup = false;
    for (int i = 0; i < n && !dup; i++) dup = strcmp(list[i].ssid, w.ssid) == 0;
    if (!dup && tryOne(w.ssid, w.pass)) return true;
  }
  return false;
}

static void netTask(void*) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ap[33];
  snprintf(ap, sizeof(ap), "Drowsy-%02X%02X", mac[4], mac[5]);
  WiFi.softAP(ap, AP_PASSWORD);
  { StateGuard g; strlcpy(g_state.apSsid, ap, sizeof(g_state.apSsid)); }
  Serial.printf("[net] AP %s at %s\n", ap, WiFi.softAPIP().toString().c_str());

  MDNS.begin(MDNS_NAME);
  MDNS.addService("http", "tcp", 80);
  startWebServer();
  configTzTime(TZ_THAILAND, "pool.ntp.org", "time.google.com");

  uint32_t lastTry = 0;
  bool first = true;
  for (;;) {
    if (s_scanReq) { s_scanReq = false; doScan(); }
    bool up = WiFi.status() == WL_CONNECTED;
    if (s_retryNow) {                     // เพิ่ม/ลบ WiFi จากหน้าเว็บ: เริ่มลองตามลำดับใหม่ทันที
      s_retryNow = false;
      if (up) { WiFi.disconnect(false, false); vTaskDelay(pdMS_TO_TICKS(300)); up = false; }
      first = true;
    }
    if (!up && (first || millis() - lastTry >= NET_RETRY_MS)) {
      first = false;
      up = tryNetworks();
      lastTry = millis();
      if (up) Serial.printf("[net] connected to %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
    {
      StateGuard g;
      g_state.staConnected = up;
      strlcpy(g_state.staSsid, up ? WiFi.SSID().c_str() : "", sizeof(g_state.staSsid));
      strlcpy(g_state.staIp, up ? WiFi.localIP().toString().c_str() : "", sizeof(g_state.staIp));
    }
    if (up) timeSetFromEpoch(time(nullptr), "ntp");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void startNetTask() {
  s_wifiMu = xSemaphoreCreateMutex();
  savedLoad();
  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 2, nullptr, CORE_NET);
}
