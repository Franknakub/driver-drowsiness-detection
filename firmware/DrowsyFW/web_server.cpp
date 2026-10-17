// Device Dashboard (ขั้นต้น) + OTA ผ่านเว็บ
//   GET  /            หน้าสถานะ (อัปเดตทุก 1 วิ) + ส่งเวลาจาก browser ให้ Device
//   GET  /api/status  JSON สถานะ
//   POST /api/time    epoch=<วินาที>  เวลาจาก browser (ใช้เมื่อไม่มี NTP)
//   POST /api/mute    ปิดเสียง buzzer ระดับ 1–2 ตาม MUTE_DEFAULT_MS
//   GET/POST /update  อัปโหลด .bin (ต้องใส่รหัส admin / OTA_PASSWORD)
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "config.h"
#include "secrets.h"
#include "shared_state.h"
#include "tasks.h"

extern volatile bool g_rebootRequested;

static AsyncWebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DrowsyFW</title>
<style>body{font-family:system-ui,sans-serif;margin:16px;max-width:520px}pre{background:#eee;padding:12px;border-radius:6px;white-space:pre-wrap}button{font-size:16px;padding:8px 14px}</style>
</head><body>
<h2>DrowsyFW</h2>
<p><img id="v" alt="ภาพจากกล้อง" style="width:100%;max-width:360px;image-rendering:pixelated;background:#000"><br>
<small>กรอบเขียว = หน้า · กรอบตา: แดง = ปิด, น้ำเงิน = เปิด, เหลือง = ไม่แน่ใจ</small></p>
<p><b>ภาพตาที่เข้าโมเดล (32×32)</b> <span id="pe"></span><br>
<img id="e" alt="ภาพตา" style="width:100%;max-width:390px;image-rendering:pixelated;background:#333"><br>
<small>ซ้าย = ตาซ้ายของภาพ (ตาขวาของคนขับ) · ขวา = ตาขวาของภาพ</small></p>
<pre id="s">loading...</pre>
<p><button onclick="fetch('/api/mute',{method:'POST'})">ปิดเสียง 5 นาที (ระดับ 1–2)</button> <a href="/update">อัปเดต firmware</a></p>
<script>
fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'epoch='+Math.floor(Date.now()/1000)});
async function tick(){try{const r=await fetch('/api/status');const d=await r.json();document.getElementById('s').textContent=JSON.stringify(d,null,2);
document.getElementById('pe').textContent='P(ปิด) ซ้าย '+d.p_left.toFixed(2)+' · ขวา '+d.p_right.toFixed(2)+' · '+d.eye_state;}catch(e){}}
tick();setInterval(tick,1000);
const v=document.getElementById('v');function next(){v.src='/snapshot.jpg?'+Date.now();}
v.onload=()=>setTimeout(next,150);v.onerror=()=>setTimeout(next,1000);next();
const ei=document.getElementById('e');function nextE(){ei.src='/eyes.jpg?'+Date.now();}
ei.onload=()=>setTimeout(nextE,300);ei.onerror=()=>setTimeout(nextE,1000);nextE();
</script></body></html>)HTML";

static const char UPDATE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DrowsyFW OTA</title></head>
<body style="font-family:system-ui,sans-serif;margin:16px">
<h2>อัปเดต firmware</h2>
<p>เลือกไฟล์ <code>DrowsyFW.ino.bin</code> จากโฟลเดอร์ build แล้วกดอัปโหลด Device จะรีสตาร์ตเองเมื่อเสร็จ</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin"> <button>อัปโหลด</button></form>
</body></html>)HTML";

static const char* levelName(DrowsinessLevel l) {
  switch (l) {
    case DrowsinessLevel::Normal:     return "normal";
    case DrowsinessLevel::Fatigue:    return "fatigue";
    case DrowsinessLevel::Drowsy:     return "drowsy";
    case DrowsinessLevel::Microsleep: return "microsleep";
  }
  return "?";
}

static const char* eyeStateName(EyeState e) {
  switch (e) {
    case EyeState::Unknown:    return "unknown";
    case EyeState::Open:       return "open";
    case EyeState::Closed:     return "closed";
    case EyeState::Uncertain:  return "uncertain";
    case EyeState::NotVisible: return "not_visible";
  }
  return "?";
}

static void handleStatus(AsyncWebServerRequest* req) {
  SharedState s = stateSnapshot();
  uint32_t now = millis();
  long muteLeft = s.mutedUntilMs ? (long)(int32_t)(s.mutedUntilMs - now) / 1000 : 0;
  char buf[900];
  snprintf(buf, sizeof(buf),
    "{\"fw\":\"%s\",\"trip\":%u,\"uptime_s\":%u,\"time_known\":%s,\"trip_start_epoch\":%lu,"
    "\"fps\":%.1f,\"face\":%s,\"face_score\":%.2f,\"eye_state\":\"%s\",\"p_closed\":%.2f,"
    "\"p_left\":%.2f,\"p_right\":%.2f,\"eye_dist_px\":%d,\"tilt_comp_deg\":%d,"
    "\"rotate_ms\":%.1f,\"detect_ms\":%.1f,\"classify_ms\":%.1f,\"perclos\":%.2f,\"level\":\"%s\","
    "\"distraction\":%s,\"unmonitored\":%s,\"mute_left_s\":%ld,"
    "\"ldr_raw\":%u,\"ir_on\":%s,\"ap\":\"%s\",\"sta\":\"%s\",\"sta_ip\":\"%s\",\"free_psram_kb\":%u}",
    FW_VERSION, s.bootCount, (unsigned)((now - s.tripStartMs) / 1000), s.timeKnown ? "true" : "false",
    (unsigned long)s.tripStartEpoch, s.fps, s.faceFound ? "true" : "false", s.faceScore,
    eyeStateName(s.eyeState), s.pClosed, s.pLeft, s.pRight, s.eyeDistPx, s.tiltCompDeg,
    s.rotateMs, s.detectMs, s.classifyMs, s.perclos,
    levelName(s.level), s.distraction ? "true" : "false", s.unmonitored ? "true" : "false",
    muteLeft > 0 ? muteLeft : 0L, s.ldrRaw, s.irOn ? "true" : "false", s.apSsid, s.staSsid, s.staIp,
    (unsigned)(ESP.getFreePsram() / 1024));
  req->send(200, "application/json", buf);
}

// ใช้ buffer JPEG ก้อนเดียวต่อ endpoint ปล่อยของเก่าตอนขอภาพถัดไป (หน้าเว็บขอภาพใหม่หลังภาพเดิมโหลดเสร็จ)
// รองรับผู้ดูทีละคน ซึ่งพอสำหรับดีบัก
static void sendJpeg(AsyncWebServerRequest* r, bool (*make)(uint8_t**, size_t*), uint8_t*& keep) {
  uint8_t* jpg = nullptr;
  size_t len = 0;
  if (!make(&jpg, &len)) { r->send(503, "text/plain", "no frame yet"); return; }
  if (keep) free(keep);
  keep = jpg;
  AsyncWebServerResponse* res = r->beginResponse(200, "image/jpeg", keep, len);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

static bool otaAuthorized(AsyncWebServerRequest* req) {
  return req->authenticate("admin", OTA_PASSWORD);
}

static void handleUpload(AsyncWebServerRequest* req, const String& filename, size_t index,
                         uint8_t* data, size_t len, bool final) {
  if (!otaAuthorized(req)) return;
  if (index == 0) {
    Serial.printf("[ota] start %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  }
  if (Update.isRunning() && Update.write(data, len) != len) Update.printError(Serial);
  if (final) {
    if (Update.end(true)) Serial.printf("[ota] done %u bytes\n", (unsigned)(index + len));
    else Update.printError(Serial);
  }
}

void startWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, handleStatus);

  // ภาพที่โมเดลเห็นล่าสุด (หมุนแล้ว) พร้อมกรอบหน้า/ตา · vision เริ่มคัดลอกภาพเมื่อมีคนขอ จึงอาจว่างในครั้งแรก
  server.on("/snapshot.jpg", HTTP_GET, [](AsyncWebServerRequest* r) {
    static uint8_t* buf = nullptr;
    sendJpeg(r, snapshotJpeg, buf);
  });
  server.on("/eyes.jpg", HTTP_GET, [](AsyncWebServerRequest* r) {
    static uint8_t* buf = nullptr;
    sendJpeg(r, eyesJpeg, buf);
  });

  server.on("/api/time", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("epoch", true)) { r->send(400, "text/plain", "missing epoch"); return; }
    time_t epoch = (time_t)strtoul(r->getParam("epoch", true)->value().c_str(), nullptr, 10);
    bool used = timeSetFromEpoch(epoch, "browser");
    r->send(200, "application/json", used ? "{\"used\":true}" : "{\"used\":false}");
  });

  server.on("/api/mute", HTTP_POST, [](AsyncWebServerRequest* r) {
    alertMute(MUTE_DEFAULT_MS);
    r->send(200, "application/json", "{\"muted\":true}");
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!otaAuthorized(r)) return r->requestAuthentication();
    r->send(200, "text/html", UPDATE_HTML);
  });
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      if (!otaAuthorized(r)) return r->requestAuthentication();
      bool ok = !Update.hasError() && Update.isFinished();
      r->send(ok ? 200 : 500, "text/plain", ok ? "OK - rebooting" : "FAILED - see serial log");
      if (ok) g_rebootRequested = true;
    },
    handleUpload);

  server.begin();
  Serial.println("[web] server on port 80");
}
