// net_task: AP ของ Device เปิดตลอด + ลอง WiFi ที่บันทึกไว้ทุก 1 นาที + NTP + mDNS
#include <WiFi.h>
#include <ESPmDNS.h>
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

static bool tryNetworks() {
  for (const WifiCred& w : WIFI_NETWORKS) {
    Serial.printf("[net] trying %s\n", w.ssid);
    WiFi.begin(w.ssid, w.pass);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) vTaskDelay(pdMS_TO_TICKS(500));
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.disconnect(false, false);
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
    bool up = WiFi.status() == WL_CONNECTED;
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
  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 2, nullptr, CORE_NET);
}
