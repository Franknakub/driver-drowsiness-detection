// Device Dashboard (ขั้นต้น) + OTA ผ่านเว็บ
//   GET  /            หน้าสถานะ (อัปเดตทุก 1 วิ) + ส่งเวลาจาก browser ให้ Device
//   GET  /api/status  JSON สถานะ
//   POST /api/time    epoch=<วินาที>  เวลาจาก browser (ใช้เมื่อไม่มี NTP)
//   POST /api/mute    ปิดเสียง buzzer ระดับ 1–2 ตาม MUTE_DEFAULT_MS
//   POST /api/ir      mode=auto|on|off  โหมด IR (จำใน NVS)
//   POST /api/test    what=buzzer|green|red|ir|oled  ทดสอบฮาร์ดแวร์ HW_TEST_MS
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
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--line:#e3e6eb;--ok:#1f9d55;--l1:#d6a100;--l2:#e8590c;--l3:#d61f1f;--dis:#7048e8;--pc:#2f6fed}
@media(prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1c2029;--fg:#e6e8ec;--mut:#9aa3b2;--line:#2c3240}}
*{box-sizing:border-box}body{font-family:system-ui,sans-serif;margin:0;background:var(--bg);color:var(--fg)}
main{max-width:720px;margin:0 auto;padding:12px 16px 32px}
header{display:flex;justify-content:space-between;align-items:baseline;gap:8px;flex-wrap:wrap}h1{font-size:20px;margin:4px 0}
.mut{color:var(--mut);font-size:13px}.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;margin-top:12px}
#lvl{border:none;color:#fff;text-align:center;padding:18px 12px;background:var(--ok)}#lvl b{display:block;font-size:30px}#lvl span{font-size:14px;opacity:.9}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:12px}@media(max-width:520px){.grid{grid-template-columns:repeat(2,1fr)}}
.grid .card{margin:0}.k{font-size:12px;color:var(--mut)}.v{font-size:20px;font-weight:600;margin-top:2px;font-variant-numeric:tabular-nums}.s{font-size:12px;color:var(--mut);margin-top:2px}
canvas{width:100%;height:180px;display:block}.lg{display:flex;gap:12px;flex-wrap:wrap;font-size:12px;color:var(--mut);margin-top:6px}.lg i{display:inline-block;width:12px;height:3px;vertical-align:middle;margin-right:4px}
.imgs{display:flex;gap:12px;flex-wrap:wrap}.imgs div{flex:1 1 260px}img{width:100%;image-rendering:pixelated;background:#000;border-radius:6px;display:block}
button,select{font:inherit;font-size:15px;padding:8px 12px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg)}
#mute{width:100%;margin-top:12px;background:var(--fg);color:var(--bg);border:none;padding:12px}
#off{display:none;background:var(--l3);color:#fff;padding:8px 12px;border-radius:8px;margin-top:12px}
pre{white-space:pre-wrap;font-size:12px;margin:8px 0 0}a{color:var(--pc)}h2{font-size:15px;margin:0 0 8px}
</style>
</head><body><main>
<header><h1>DrowsyFW</h1><span class="mut" id="hd">กำลังเชื่อมต่อ...</span></header>
<div id="off">ขาดการเชื่อมต่อกับ Device · ข้อมูลด้านล่างอาจไม่ใช่ล่าสุด</div>
<div class="card" id="lvl"><b id="lt">--</b><span id="ls"></span></div>
<button id="mute" onclick="post('/api/mute')">ปิดเสียง 5 นาที (ระดับ 1–2 และ Distraction)</button>
<div class="grid">
<div class="card"><div class="k">ตา</div><div class="v" id="c-eye">--</div><div class="s" id="c-eye2"></div></div>
<div class="card"><div class="k">PERCLOS (60 วิ)</div><div class="v" id="c-pc">--</div><div class="s">ระดับ 1 ≥ 15% · ระดับ 2 ≥ 25%</div></div>
<div class="card"><div class="k">หาหน้า</div><div class="v" id="c-face">--</div><div class="s" id="c-face2"></div></div>
<div class="card"><div class="k">ความเร็ว</div><div class="v" id="c-fps">--</div><div class="s" id="c-fps2"></div></div>
<div class="card"><div class="k">IR</div><div class="v" id="c-ir">--</div><div class="s" id="c-ir2"></div></div>
<div class="card"><div class="k">เครือข่าย / Sync</div><div class="v" id="c-net">--</div><div class="s" id="c-net2"></div></div>
</div>
<div class="card"><h2>ย้อนหลัง 2 นาที</h2><canvas id="g"></canvas>
<div class="lg"><span><i style="background:var(--l3)"></i>P(ตาปิด)</span><span><i style="background:var(--pc)"></i>PERCLOS</span><span><i style="background:var(--mut)"></i>เกณฑ์ปิด 0.5 / เปิด 0.3</span><span>แถบล่าง = ระดับ · เว้นว่าง = มองไม่เห็นตา</span></div></div>
<div class="card imgs"><div><h2>กล้อง</h2><img id="v" alt="ภาพจากกล้อง"><div class="s">กรอบเขียว = หน้า · กรอบตา: แดง = ปิด, น้ำเงิน = เปิด, เหลือง = ไม่แน่ใจ</div></div>
<div><h2>ภาพตาที่เข้าโมเดล (32×32)</h2><img id="e" alt="ภาพตา"><div class="s" id="pe">ซ้าย = ตาซ้ายของภาพ (ตาขวาของคนขับ)</div></div></div>
<div class="card"><h2>ฮาร์ดแวร์</h2><div class="s" id="hw"></div>
<p>IR: <select id="irm" onchange="post('/api/ir?mode='+this.value)"><option value="auto">อัตโนมัติตาม LDR</option><option value="on">เปิดค้าง</option><option value="off">ปิด</option></select></p>
<p>ทดสอบ 3 วิ: <button onclick="post('/api/test?what=buzzer')">buzzer</button> <button onclick="post('/api/test?what=green')">LED เขียว</button>
<button onclick="post('/api/test?what=red')">LED แดง</button> <button onclick="post('/api/test?what=ir')">IR</button> <button onclick="post('/api/test?what=oled')">จอ OLED</button></p>
<div class="s">IR กะพริบ 1 วิ ดูภาพสว่างขึ้นลงในภาพกล้อง (ต้องมีของสะท้อนใกล้ๆ ในที่มืด) · ปุ่ม BOOT บนโมดูล = ปิดเสียง 5 นาที</div></div>
<div class="card"><a href="/trips">ประวัติ Trip</a> · <a href="/update">อัปเดต firmware</a>
<details><summary class="mut">ข้อมูลดิบ (/api/status)</summary><pre id="s">loading...</pre></details></div>
</main>
<script>
fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'epoch='+Math.floor(Date.now()/1000)});
const $=id=>document.getElementById(id);
function post(u){fetch(u,{method:'POST'}).then(tick);}
const LV={normal:['ปกติ','var(--ok)',0],fatigue:['เริ่มล้า · ระดับ 1','var(--l1)',1],drowsy:['ง่วง · ระดับ 2','var(--l2)',2],microsleep:['หลับใน · ระดับ 3','var(--l3)',3]};
const EYE={open:'ลืมตา',closed:'หลับตา',uncertain:'ไม่แน่ใจ',not_visible:'มองไม่เห็น',unknown:'--'};
const hist=[];let lastOk=0;
function css(n){return getComputedStyle(document.documentElement).getPropertyValue(n).trim();}
function ago(s){return s<60?s+' วิ':Math.floor(s/60)+' นาที';}
async function tick(){try{const r=await fetch('/api/status');const d=await r.json();lastOk=Date.now();show(d);}catch(e){}}
function show(d){
$('s').textContent=JSON.stringify(d,null,2);
$('hd').textContent='v'+d.fw+' · Trip '+d.trip+' · เปิดมา '+ago(d.uptime_s);
let lv=LV[d.level]||LV.normal,t=lv[0],c=lv[1],sub='';
if(lv[2]<2&&d.distraction){t='ไม่มองถนน';c='var(--dis)';sub='มองไม่เห็นตาเกิน 3 วิ';}
else if(lv[2]==0&&d.unmonitored){t='มองไม่ชัด';c='var(--mut)';sub='ตาอยู่ในสถานะไม่แน่ใจเกิน 5 วิ';}
else if(d.closed_ms>0)sub='ตาปิดต่อเนื่อง '+(d.closed_ms/1000).toFixed(1)+' วิ';
if(d.mute_left_s>0)sub+=(sub?' · ':'')+'ปิดเสียงอยู่อีก '+ago(d.mute_left_s);
$('lt').textContent=t;$('ls').textContent=sub;$('lvl').style.background=c;
const seen=d.eye_state=='open'||d.eye_state=='closed'||d.eye_state=='uncertain';
$('c-eye').textContent=EYE[d.eye_state]||d.eye_state;$('c-eye2').textContent=seen?'P(ตาปิด) '+d.p_closed.toFixed(2):'';
$('c-pc').textContent=d.perclos_ready?Math.round(d.perclos*100)+'%':'กำลังเก็บ';
$('c-face').textContent=d.face?'เจอ':'ไม่เจอ';$('c-face2').textContent=d.face?'คะแนน '+d.face_score.toFixed(2)+' · ระยะตา '+d.eye_dist_px+' px':'';
$('c-fps').textContent=d.fps.toFixed(1)+' fps';$('c-fps2').textContent='หาหน้า '+d.detect_ms.toFixed(0)+' ms · ตา '+d.classify_ms.toFixed(0)+' ms';
$('c-ir').textContent=d.ir_on?'ติด':'ดับ';$('c-ir2').textContent={auto:'อัตโนมัติ',on:'เปิดค้าง',off:'ปิด'}[d.ir_mode]+' · LDR '+d.ldr_raw;
$('c-net').textContent=d.sta?d.sta:'AP เท่านั้น';
const sy=d.sync;$('c-net2').textContent=(d.sta?d.sta_ip+' · ':'')+(!sy.configured?'Sync ปิด':sy.last_ok_age_s>=0?'Sync ล่าสุด '+ago(sy.last_ok_age_s)+' · ค้าง '+sy.pending_samples:sy.fail?'Sync ผิดพลาด '+sy.last_http:'Sync รอส่ง');
$('pe').textContent='P(ปิด) ซ้าย '+d.p_left.toFixed(2)+' · ขวา '+d.p_right.toFixed(2);
$('hw').textContent='OLED '+(d.oled_ok?'เจอจอ':'ไม่เจอจอ')+(d.hw_test!=='none'?' · กำลังทดสอบ '+d.hw_test:'');
if(document.activeElement!==$('irm'))$('irm').value=d.ir_mode;
hist.push({t:Date.now(),p:seen?d.p_closed:null,pc:d.perclos_ready?d.perclos:null,l:lv[2],dis:d.distraction});
while(hist.length&&hist[0].t<Date.now()-120000)hist.shift();
draw();}
function draw(){
const cv=$('g'),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;
cv.width=W*dpr;cv.height=H*dpr;const x=cv.getContext('2d');x.scale(dpr,dpr);
const L=28,B=24,T=6,ph=H-B-T-4,now=Date.now();
const X=t=>L+(W-L-4)*(1-(now-t)/120000),Y=v=>T+ph*(1-v);
x.font='11px system-ui';x.fillStyle=css('--mut');x.strokeStyle=css('--line');x.lineWidth=1;
for(const v of [0,.5,1]){x.beginPath();x.moveTo(L,Y(v));x.lineTo(W-4,Y(v));x.stroke();x.fillText(v.toFixed(1),2,Y(v)+4);}
x.setLineDash([4,4]);x.strokeStyle=css('--mut');for(const v of [.5,.3]){x.beginPath();x.moveTo(L,Y(v));x.lineTo(W-4,Y(v));x.stroke();}x.setLineDash([]);
const cols=['--ok','--l1','--l2','--l3'];
for(let i=0;i<hist.length;i++){const h=hist[i],n=hist[i+1],x1=X(h.t),x2=n?X(n.t):W-4;x.fillStyle=css(h.dis&&h.l<2?'--dis':cols[h.l]);x.fillRect(x1,H-B+2,Math.max(1,x2-x1),8);}
x.fillStyle=css('--mut');x.fillText('-2 นาที',L,H-2);x.textAlign='right';x.fillText('ตอนนี้',W-4,H-2);x.textAlign='left';
function line(k,c){x.strokeStyle=css(c);x.lineWidth=2;x.beginPath();let on=false;
for(const h of hist){if(h[k]==null){on=false;continue;}const px=X(h.t),py=Y(h[k]);if(on)x.lineTo(px,py);else x.moveTo(px,py);on=true;}x.stroke();}
line('pc','--pc');line('p','--l3');}
setInterval(()=>{$('off').style.display=lastOk&&Date.now()-lastOk>3000?'block':'none';},1000);
window.addEventListener('resize',draw);
tick();setInterval(tick,1000);
const v=$('v');function next(){v.src='/snapshot.jpg?'+Date.now();}
v.onload=()=>setTimeout(next,150);v.onerror=()=>setTimeout(next,1000);next();
const ei=$('e');function nextE(){ei.src='/eyes.jpg?'+Date.now();}
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

static const char* irModeName(IrMode m) {
  switch (m) {
    case IrMode::On:  return "on";
    case IrMode::Off: return "off";
    default:          return "auto";
  }
}

static const char* hwTestName(HwTest t) {
  switch (t) {
    case HwTest::Buzzer:   return "buzzer";
    case HwTest::LedGreen: return "green";
    case HwTest::LedRed:   return "red";
    case HwTest::Ir:       return "ir";
    case HwTest::Oled:     return "oled";
    default:               return "none";
  }
}

static void handleStatus(AsyncWebServerRequest* req) {
  SharedState s = stateSnapshot();
  uint32_t now = millis();
  long muteLeft = s.mutedUntilMs ? (long)(int32_t)(s.mutedUntilMs - now) / 1000 : 0;
  bool testing = s.hwTestUntilMs && (int32_t)(s.hwTestUntilMs - now) > 0;
  static char buf[1800];          // handler ทุกตัวรันบน task ของ AsyncTCP ตัวเดียว ใช้ static ได้
  LogStatus ls = logStatus();
  SyncStatus ss = syncStatus();
  int n = snprintf(buf, sizeof(buf),
    "{\"fw\":\"%s\",\"device_id\":\"%s\",\"trip\":%u,\"uptime_s\":%u,\"time_known\":%s,\"trip_start_epoch\":%lu,"
    "\"fps\":%.1f,\"face\":%s,\"face_score\":%.2f,\"eye_state\":\"%s\",\"p_closed\":%.2f,"
    "\"p_left\":%.2f,\"p_right\":%.2f,\"eye_dist_px\":%d,\"tilt_comp_deg\":%d,"
    "\"rotate_ms\":%.2f,\"detect_ms\":%.1f,\"pre_ms\":%.1f,\"classify_ms\":%.1f,\"perclos\":%.2f,\"perclos_ready\":%s,\"closed_ms\":%lu,\"frame_age_ms\":%lu,\"level\":\"%s\","
    "\"distraction\":%s,\"unmonitored\":%s,\"mute_left_s\":%ld,"
    "\"ldr_raw\":%u,\"ir_on\":%s,\"ir_mode\":\"%s\",\"oled_ok\":%s,\"hw_test\":\"%s\",\"ap\":\"%s\",\"sta\":\"%s\",\"sta_ip\":\"%s\",\"free_psram_kb\":%u,"
    "\"free_internal_kb\":%u,\"largest_internal_kb\":%u,"
    "\"log\":{\"fs_ok\":%s,\"used_kb\":%lu,\"total_kb\":%lu,\"samples_paused\":%s,\"trips\":%lu,\"deleted\":%lu},"
    "\"sync\":{\"configured\":%s,\"busy\":%s,\"ok\":%lu,\"fail\":%lu,\"last_ok_age_s\":%ld,\"last_http\":%d,"
    "\"last_err\":\"%s\",\"pending_samples\":%lu,\"pending_events\":%lu}}",
    FW_VERSION, deviceId(), s.bootCount, (unsigned)((now - s.tripStartMs) / 1000), s.timeKnown ? "true" : "false",
    (unsigned long)s.tripStartEpoch, s.fps, s.faceFound ? "true" : "false", s.faceScore,
    eyeStateName(s.eyeState), s.pClosed, s.pLeft, s.pRight, s.eyeDistPx, s.tiltCompDeg,
    s.rotateMs, s.detectMs, s.preMs, s.classifyMs, s.perclos, s.perclosReady ? "true" : "false", (unsigned long)s.closedMs, (unsigned long)(now - s.lastFrameMs),
    levelName(s.level), s.distraction ? "true" : "false", s.unmonitored ? "true" : "false",
    muteLeft > 0 ? muteLeft : 0L, s.ldrRaw, s.irOn ? "true" : "false", irModeName(s.irMode),
    s.oledOk ? "true" : "false", testing ? hwTestName(s.hwTest) : "none", s.apSsid, s.staSsid, s.staIp,
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

  // POST /api/ir?mode=auto|on|off · จำใน NVS
  server.on("/api/ir", HTTP_POST, [](AsyncWebServerRequest* r) {
    String m = r->hasParam("mode") ? r->getParam("mode")->value() : "";
    if (m == "auto")     irSetMode(IrMode::Auto);
    else if (m == "on")  irSetMode(IrMode::On);
    else if (m == "off") irSetMode(IrMode::Off);
    else { r->send(400, "text/plain", "mode must be auto, on or off"); return; }
    r->send(200, "application/json", "{\"ir_mode\":\"" + m + "\"}");
  });

  // POST /api/test?what=buzzer|green|red|ir|oled · ทำงาน HW_TEST_MS
  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest* r) {
    String w = r->hasParam("what") ? r->getParam("what")->value() : "";
    HwTest t = w == "buzzer" ? HwTest::Buzzer : w == "green" ? HwTest::LedGreen : w == "red" ? HwTest::LedRed
             : w == "ir" ? HwTest::Ir : w == "oled" ? HwTest::Oled : HwTest::None;
    if (t == HwTest::None) { r->send(400, "text/plain", "what must be buzzer, green, red, ir or oled"); return; }
    hwTestStart(t, HW_TEST_MS);
    r->send(200, "application/json", "{\"testing\":\"" + w + "\"}");
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
