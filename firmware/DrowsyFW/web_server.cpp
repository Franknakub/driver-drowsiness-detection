// Device Dashboard (ขั้นต้น) + OTA ผ่านเว็บ
//   GET  /            หน้าสถานะ (อัปเดตทุก 1 วิ) + ส่งเวลาจาก browser ให้ Device
//   GET  /api/status  JSON สถานะ
//   POST /api/time    epoch=<วินาที>  เวลาจาก browser (ใช้เมื่อไม่มี NTP)
//   POST /api/mute    ปิดเสียง buzzer ระดับ 1–2 ตาม MUTE_DEFAULT_MS
//   GET/POST /update  อัปโหลด .bin (ต้องใส่รหัส admin / OTA_PASSWORD)
//   GET  /trips       ประวัติ Trip บน Device (รายการ + Trip Summary + กราฟ)
//   GET  /api/trips   JSON รายการ Trip ทั้งหมดบน FFat พร้อม Trip Summary
//   GET  /api/trips/<n>?from=0&count=600&step=1   Sample ช่วงหนึ่ง + Event ทั้งหมด + Trip Summary ของ Trip n
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "config.h"
#include "secrets.h"
#include "shared_state.h"
#include "tasks.h"
#include "trip_log.h"
#include "sync.h"
#include <esp_heap_caps.h>

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
<p><button onclick="fetch('/api/mute',{method:'POST'})">ปิดเสียง 5 นาที (ระดับ 1–2)</button> <a href="/trips">ประวัติ Trip</a> · <a href="/update">อัปเดต firmware</a></p>
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
  static char buf[1600];          // handler ทุกตัวรันบน task ของ AsyncTCP ตัวเดียว ใช้ static ได้
  LogStatus ls = logStatus();
  SyncStatus ss = syncStatus();
  int n = snprintf(buf, sizeof(buf),
    "{\"fw\":\"%s\",\"device_id\":\"%s\",\"trip\":%u,\"uptime_s\":%u,\"time_known\":%s,\"trip_start_epoch\":%lu,"
    "\"fps\":%.1f,\"face\":%s,\"face_score\":%.2f,\"eye_state\":\"%s\",\"p_closed\":%.2f,"
    "\"p_left\":%.2f,\"p_right\":%.2f,\"eye_dist_px\":%d,\"tilt_comp_deg\":%d,"
    "\"rotate_ms\":%.2f,\"detect_ms\":%.1f,\"pre_ms\":%.1f,\"classify_ms\":%.1f,\"perclos\":%.2f,\"perclos_ready\":%s,\"closed_ms\":%lu,\"frame_age_ms\":%lu,\"level\":\"%s\","
    "\"distraction\":%s,\"unmonitored\":%s,\"mute_left_s\":%ld,"
    "\"ldr_raw\":%u,\"ir_on\":%s,\"ap\":\"%s\",\"sta\":\"%s\",\"sta_ip\":\"%s\",\"free_psram_kb\":%u,"
    "\"free_internal_kb\":%u,\"largest_internal_kb\":%u,"
    "\"log\":{\"fs_ok\":%s,\"used_kb\":%lu,\"total_kb\":%lu,\"samples_paused\":%s,\"trips\":%lu,\"deleted\":%lu},"
    "\"sync\":{\"configured\":%s,\"busy\":%s,\"ok\":%lu,\"fail\":%lu,\"last_ok_age_s\":%ld,\"last_http\":%d,"
    "\"last_err\":\"%s\",\"pending_samples\":%lu,\"pending_events\":%lu}}",
    FW_VERSION, deviceId(), s.bootCount, (unsigned)((now - s.tripStartMs) / 1000), s.timeKnown ? "true" : "false",
    (unsigned long)s.tripStartEpoch, s.fps, s.faceFound ? "true" : "false", s.faceScore,
    eyeStateName(s.eyeState), s.pClosed, s.pLeft, s.pRight, s.eyeDistPx, s.tiltCompDeg,
    s.rotateMs, s.detectMs, s.preMs, s.classifyMs, s.perclos, s.perclosReady ? "true" : "false", (unsigned long)s.closedMs, (unsigned long)(now - s.lastFrameMs),
    levelName(s.level), s.distraction ? "true" : "false", s.unmonitored ? "true" : "false",
    muteLeft > 0 ? muteLeft : 0L, s.ldrRaw, s.irOn ? "true" : "false", s.apSsid, s.staSsid, s.staIp,
    (unsigned)(ESP.getFreePsram() / 1024),
    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
    (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
    ls.fsOk ? "true" : "false", (unsigned long)ls.usedKB, (unsigned long)ls.totalKB,
    ls.samplesPaused ? "true" : "false", (unsigned long)ls.trips, (unsigned long)ls.deletedTrips,
    ss.configured ? "true" : "false", ss.busy ? "true" : "false", (unsigned long)ss.okCalls, (unsigned long)ss.failCalls,
    ss.lastOkMs ? (long)((now - ss.lastOkMs) / 1000) : -1L, ss.lastHttp, ss.lastErr,
    (unsigned long)ss.pendingSamples, (unsigned long)ss.pendingEvents);
  if (n < 0 || n >= (int)sizeof(buf)) { req->send(500, "text/plain", "status buffer too small"); return; }
  req->send(200, "application/json", buf);
}

// /api/ab?on=1 เปิดการเทียบทางเดิมกับทางใหม่ (ล้างค่าสะสม) · ?on=0 ปิด · ไม่มี on = อ่านผลอย่างเดียว
static void handleAb(AsyncWebServerRequest* req) {
  if (req->hasParam("on")) {
    bool on = req->getParam("on")->value() == "1";
    StateGuard g;
    if (on && !g_state.abOn) g_state.abReset = true;
    g_state.abOn = on;
  }
  SharedState s = stateSnapshot();
  const AbStats& a = s.ab;
  char buf[840];
  snprintf(buf, sizeof(buf),
    "{\"on\":%s,\"frames\":%u,\"found_new\":%u,\"found_old\":%u,\"found_both\":%u,"
    "\"eye_err_mean_px\":%.2f,\"eye_err_max_px\":%d,"
    "\"msr_input_comp0_frames\":%u,\"msr_input_comp0_mismatch_frames\":%u,\"msr_input_comp0_diff_max\":%d,"
    "\"msr_input_tilt_frames\":%u,\"msr_input_tilt_diff_mean\":%.1f,"
    "\"eye_crop_checked\":%u,\"eye_crop_mismatch\":%u,"
    "\"old_rotate_ms\":%.2f,\"old_detect_ms\":%.2f,\"new_rotate_ms\":%.3f,\"new_detect_ms\":%.2f,\"new_pre_ms\":%.2f,\"msr_pre_old_ms\":%.2f,\"msr_pre_new_ms\":%.2f}",
    s.abOn ? "true" : "false", (unsigned)a.frames, (unsigned)a.foundNew, (unsigned)a.foundOld, (unsigned)a.both,
    a.eyeErrMean, (int)a.eyeErrMax, (unsigned)a.input0, (unsigned)a.input0Bad, (int)a.input0DiffMax,
    (unsigned)a.inputTilt, a.inputTiltDiffMean, (unsigned)a.crops, (unsigned)a.cropBad,
    a.oldRotateMs, a.oldDetectMs, a.newRotateMs, a.newDetectMs, a.newPreMs, a.msrPreOldMs, a.msrPreNewMs);
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

// ---- ประวัติ Trip บน Device ----

static const char TRIPS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DrowsyFW Trips</title>
<style>body{font-family:system-ui,sans-serif;margin:16px;max-width:760px}table{border-collapse:collapse;width:100%;font-size:14px}
td,th{border-bottom:1px solid #ddd;padding:4px 6px;text-align:right}td:first-child,th:first-child{text-align:left}
tr.t{cursor:pointer}tr.t:hover{background:#f3f3f3}canvas{width:100%;height:220px;border:1px solid #ccc;margin-top:8px}
.k{display:inline-block;margin-right:12px}small{color:#555}</style></head><body>
<p><a href="/">← สถานะสด</a></p><h2>ประวัติ Trip บน Device</h2><p id="fs"><small>loading...</small></p>
<table><thead><tr><th>Trip</th><th>เริ่ม</th><th>เวลาขับ</th><th>Microsleep</th><th>ตาปิดนานสุด</th><th>PERCLOS เฉลี่ย</th><th>Unmonitored</th><th>Sync</th></tr></thead><tbody id="tb"></tbody></table>
<div id="det"></div>
<script>
const fmtD=s=>{const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return (h?h+' ชม. ':'')+m+' นาที '+(s%60)+' วิ'};
const fmtT=e=>e?new Date(e*1000).toLocaleString('th-TH'):'ไม่ทราบเวลา';
const pc=v=>v==null?'-':(v*100).toFixed(1)+'%';
async function list(){const d=await (await fetch('/api/trips')).json();
document.getElementById('fs').innerHTML='<small>FFat ใช้ '+d.fs.used_kb+' / '+d.fs.total_kb+' KB'+(d.fs.samples_paused?' · <b>เต็ม หยุดเก็บ Sample</b>':'')+'</small>';
document.getElementById('tb').innerHTML=d.trips.slice().reverse().map(t=>{const s=t.summary;
return '<tr class="t" onclick="show('+t.trip+')"><td>#'+t.trip+(t.current?' (ตอนนี้)':'')+'</td><td>'+fmtT(t.started_at)+'</td><td>'+fmtD(s.duration_s)+
'</td><td>'+s.microsleeps+'</td><td>'+(s.longest_closed_ms/1000).toFixed(1)+' วิ</td><td>'+pc(s.perclos_mean)+
'</td><td>'+(s.unmonitored_s/60).toFixed(1)+' นาที</td><td>'+(t.synced?'✓':t.synced_samples+'/'+t.samples)+'</td></tr>'}).join('');}
async function show(n){const det=document.getElementById('det');det.innerHTML='<p>loading...</p>';
const head=await (await fetch('/api/trips/'+n+'?count=1')).json();const step=Math.max(1,Math.ceil(head.samples_total/600));
const d=await (await fetch('/api/trips/'+n+'?count=600&step='+step)).json();const s=d.summary;
det.innerHTML='<h3>Trip #'+n+' · '+fmtT(d.started_at)+'</h3><p>'+
['เวลาขับ '+fmtD(s.duration_s),'เริ่มล้า '+fmtD(s.level_s[1])+' ('+s.level_enter[0]+' ครั้ง)','ง่วง '+fmtD(s.level_s[2])+' ('+s.level_enter[1]+' ครั้ง)',
'Microsleep '+s.microsleeps+' ครั้ง','ตาปิดนานสุด '+(s.longest_closed_ms/1000).toFixed(1)+' วิ','PERCLOS เฉลี่ย '+pc(s.perclos_mean),
'Distraction '+s.distractions+' ครั้ง','Unmonitored '+(s.unmonitored_s/60).toFixed(1)+' นาที'].map(x=>'<span class="k">'+x+'</span>').join('')+
'</p><canvas id="c" width="1400" height="440"></canvas><small>น้ำเงิน = PERCLOS (0–100%) · ส้ม = Drowsiness Level (0–3) · ขีดแดง = Microsleep · ขีดม่วง = Distraction · แกนนอน = นาทีนับจากเปิดเครื่อง</small>';
draw(d);}
function draw(d){const c=document.getElementById('c'),g=c.getContext('2d'),W=c.width,H=c.height,P=40;g.clearRect(0,0,W,H);g.font='20px sans-serif';
const S=d.samples;if(!S.length){g.fillText('ไม่มี Sample',P,H/2);return}const t1=Math.max(S[S.length-1][1],1);
const X=ms=>P+(W-2*P)*ms/t1,Y=v=>H-P-(H-2*P)*v;g.strokeStyle='#ccc';g.strokeRect(P,P,W-2*P,H-2*P);
g.fillStyle='#555';for(let m=0;m<=t1/60000;m+=Math.max(1,Math.ceil(t1/60000/10)))g.fillText(m,X(m*60000)-6,H-12);
for(const e of d.events){if(e[2]!=1&&e[2]!=2)continue;g.strokeStyle=e[2]==1?'#d00':'#90c';g.beginPath();g.moveTo(X(e[1]),P);g.lineTo(X(e[1]),H-P);g.stroke();}
g.lineWidth=2;g.strokeStyle='#e80';g.beginPath();S.forEach((r,i)=>{const x=X(r[1]),y=Y(r[3]/3);i?g.lineTo(x,y):g.moveTo(x,y)});g.stroke();
g.strokeStyle='#06c';g.beginPath();let on=false;for(const r of S){if(r[6]==null){on=false;continue}const x=X(r[1]),y=Y(r[6]/1000);on?g.lineTo(x,y):g.moveTo(x,y);on=true}g.stroke();g.lineWidth=1;}
list();
</script></body></html>)HTML";

static void startedAt(char* st, size_t n, uint32_t start) {
  if (start) snprintf(st, n, "%lu", (unsigned long)start); else strlcpy(st, "null", n);
}

static void addTripJson(String& out, const TripInfo& t) {
  char b[480], st[16];
  uint32_t start = tripStartEpoch(t.meta);
  startedAt(st, sizeof(st), start);
  snprintf(b, sizeof(b),
    "{\"trip\":%lu,\"current\":%s,\"time_known\":%s,\"started_at\":%s,\"samples\":%lu,\"events\":%lu,"
    "\"synced_samples\":%lu,\"synced_events\":%lu,\"synced\":%s,\"summary\":",
    (unsigned long)t.trip, t.current ? "true" : "false", start ? "true" : "false", st,
    (unsigned long)t.samples, (unsigned long)t.events, (unsigned long)t.meta.samplesSynced,
    (unsigned long)t.meta.eventsSynced, tripSynced(t) ? "true" : "false");
  out += b;
  summaryJson(b, sizeof(b), t.meta.summary);
  out += b;
  out += '}';
}

static void handleTripList(AsyncWebServerRequest* req) {
  TripInfo* list = (TripInfo*)heap_caps_malloc(MAX_TRIPS * sizeof(TripInfo), MALLOC_CAP_SPIRAM);
  if (!list) { req->send(500, "text/plain", "no memory"); return; }
  int n = tripList(list, MAX_TRIPS);
  LogStatus ls = logStatus();
  String out;
  out.reserve(160 + n * 480);
  char b[160];
  snprintf(b, sizeof(b), "{\"fs\":{\"ok\":%s,\"used_kb\":%lu,\"total_kb\":%lu,\"samples_paused\":%s},\"trips\":[",
           ls.fsOk ? "true" : "false", (unsigned long)ls.usedKB, (unsigned long)ls.totalKB, ls.samplesPaused ? "true" : "false");
  out += b;
  for (int i = 0; i < n; i++) {
    if (i) out += ',';
    addTripJson(out, list[i]);
  }
  out += "]}";
  free(list);
  req->send(200, "application/json", out);
}

static uint32_t paramU(AsyncWebServerRequest* req, const char* name, uint32_t def) {
  return req->hasParam(name) ? strtoul(req->getParam(name)->value().c_str(), nullptr, 10) : def;
}

static void handleTripDetail(AsyncWebServerRequest* req, uint32_t trip) {
  TripInfo t;
  if (!tripGet(trip, t)) { req->send(404, "application/json", "{\"error\":\"no such trip\"}"); return; }
  uint32_t from = paramU(req, "from", 0), count = paramU(req, "count", 600), step = paramU(req, "step", 1);
  if (count > 1800) count = 1800;
  if (step < 1) step = 1;

  String out;
  out.reserve(1024 + count * 48 + t.events * 32);
  char b[480], st[16];
  uint32_t start = tripStartEpoch(t.meta);
  startedAt(st, sizeof(st), start);
  snprintf(b, sizeof(b),
    "{\"trip\":%lu,\"time_known\":%s,\"started_at\":%s,\"samples_total\":%lu,\"from\":%lu,\"step\":%lu,"
    "\"cols\":[\"i\",\"ms\",\"eye\",\"level\",\"flags\",\"p_closed_permille\",\"perclos_permille\",\"fps10\",\"ldr\",\"closed_ms\"],"
    "\"summary\":",
    (unsigned long)trip, start ? "true" : "false", st, (unsigned long)t.samples, (unsigned long)from, (unsigned long)step);
  out += b;
  summaryJson(b, sizeof(b), t.meta.summary);
  out += b;

  out += ",\"samples\":[";
  SampleRec buf[64];
  uint32_t idx = from, emitted = 0;
  bool first = true;
  while (emitted < count && idx < t.samples) {
    int got = tripReadSamples(trip, idx, buf, 64);
    if (got <= 0) break;
    for (int i = 0; i < got && emitted < count; i++) {
      uint32_t k = idx + i;
      if ((k - from) % step) continue;
      if (!first) out += ',';
      first = false;
      sampleJson(b, sizeof(b), k, buf[i]);
      out += b;
      emitted++;
    }
    idx += got;
  }
  out += "],\"events\":[";
  EventRec ev[32];
  first = true;
  for (uint32_t i = 0; i < t.events; ) {
    int got = tripReadEvents(trip, i, ev, 32);
    if (got <= 0) break;
    for (int j = 0; j < got; j++) {
      if (!first) out += ',';
      first = false;
      eventJson(b, sizeof(b), i + j, ev[j]);
      out += b;
    }
    i += got;
  }
  out += "]}";
  req->send(200, "application/json", out);
}

// "/api/trips" ตรงกับทั้ง /api/trips และ /api/trips/<n>
static void handleTrips(AsyncWebServerRequest* req) {
  const String& url = req->url();
  const char* rest = url.c_str() + strlen("/api/trips");
  if (*rest == '/') rest++;
  if (!*rest) handleTripList(req);
  else handleTripDetail(req, strtoul(rest, nullptr, 10));
}

static bool otaAuthorized(AsyncWebServerRequest* req) {
  return req->authenticate("admin", OTA_PASSWORD);
}

static void handleUpload(AsyncWebServerRequest* req, const String& filename, size_t index,
                         uint8_t* data, size_t len, bool final) {
  if (!otaAuthorized(req)) return;
  if (index == 0) {
    Serial.printf("[ota] start %s\n", filename.c_str());
    // อัปโหลดครั้งก่อนหลุดกลางทาง (ไม่มี final) ค้าง Update ไว้ ทำให้ begin ครั้งใหม่ล้มเงียบ (เจอตอนอัปจาก 0.2.9)
    if (Update.isRunning()) { Serial.println("[ota] abort unfinished upload"); Update.abort(); }
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
  server.on("/api/ab", HTTP_GET, handleAb);
  server.on("/api/trips", HTTP_GET, handleTrips);
  server.on("/trips", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", TRIPS_HTML); });

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
