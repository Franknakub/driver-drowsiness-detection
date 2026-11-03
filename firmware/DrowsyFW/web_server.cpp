// Device Dashboard (ขั้นต้น) + OTA ผ่านเว็บ
//   GET  /            หน้าสถานะ (อัปเดตทุก 1 วิ) + ส่งเวลาจาก browser ให้ Device
//   GET  /api/status  JSON สถานะ
//   POST /api/time    epoch=<วินาที>  เวลาจาก browser (ใช้เมื่อไม่มี NTP)
//   POST /api/mute    ปิดเสียง buzzer ระดับ 1–2 ตาม MUTE_DEFAULT_MS
//   POST /api/ir      mode=auto|on|off  โหมด IR (จำใน NVS)
//   POST /api/test    what=buzzer|green|red|ir|oled  ทดสอบฮาร์ดแวร์ HW_TEST_MS
//   GET/POST /update  อัปโหลด .bin (ต้องใส่รหัส admin / OTA_PASSWORD)
//   GET  /wifi        หน้าตั้งค่า WiFi (ผ่าน AP ของ Device ไม่ต้อง login · ทางอื่น admin / OTA_PASSWORD) · GET /api/wifi[?scan=1] · POST /api/wifi/save ssid,pass · POST /api/wifi/forget ssid
//   GET  /trips       ประวัติ Trip บน Device (รายการ + Trip Summary + กราฟ)
//   GET  /api/trips   JSON รายการ Trip ทั้งหมดบน FFat พร้อม Trip Summary
//   GET  /api/trips/<n>?from=0&count=600&step=1   Sample ช่วงหนึ่ง + Event ทั้งหมด + Trip Summary ของ Trip n
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <memory>
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
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0f1218">
<title>DrowsyFW</title>
<style>
:root{--bg:#f3f4f7;--card:#fff;--fg:#151a23;--mut:#677083;--line:#e2e5ec;--soft:#eef0f4;
--ok:#16a34a;--l1:#ca8a04;--l2:#ea580c;--l3:#dc2626;--dis:#7c3aed;--gray:#64748b;--pc:#2563eb;--r:16px}
@media(prefers-color-scheme:dark){:root{--bg:#0f1218;--card:#181c25;--fg:#e8eaef;--mut:#98a1b3;--line:#272d3a;--soft:#20252f;
--ok:#22c55e;--l1:#eab308;--l2:#f97316;--l3:#ef4444;--dis:#a78bfa;--gray:#94a3b8;--pc:#60a5fa}}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans Thai",sans-serif}
main{max-width:760px;margin:0 auto;padding:12px 16px calc(28px + env(safe-area-inset-bottom))}
.top{display:flex;align-items:center;gap:10px;padding:4px 0 10px}
.brand{font-weight:700;font-size:18px;letter-spacing:.2px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--gray);flex:none}.dot.on{background:var(--ok);box-shadow:0 0 0 4px color-mix(in srgb,var(--ok) 20%,transparent)}
.meta{margin-left:auto;color:var(--mut);font-size:12px;text-align:right}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:14px;margin-top:12px}
.hero{--c:var(--ok);margin-top:4px;border:none;color:#fff;background:var(--c);padding:18px 18px 16px;display:flex;align-items:center;gap:14px;transition:background .3s}
.hero svg{width:44px;height:44px;flex:none;opacity:.95}
.hero b{display:block;font-size:28px;line-height:1.15}.hero span{font-size:14px;opacity:.92}
.hero.alarm{animation:pulse 1s ease-in-out infinite}
@keyframes pulse{50%{filter:brightness(1.25)}}
.mute{display:flex;align-items:center;justify-content:center;gap:8px;width:100%;margin-top:10px;padding:13px;border:none;border-radius:12px;font:inherit;font-weight:600;background:var(--fg);color:var(--bg);cursor:pointer}
.mute.muted{background:var(--soft);color:var(--fg)}
.mute svg{width:18px;height:18px}
.off{display:none;margin-top:10px;padding:10px 12px;border-radius:12px;background:color-mix(in srgb,var(--l3) 14%,var(--card));color:var(--l3);font-size:14px}
.cam{padding:0;overflow:hidden;position:relative;background:#000}
.cam img{width:100%;display:block;aspect-ratio:2/3;max-height:58vh;object-fit:contain;background:#000}
.chips{position:absolute;left:10px;top:10px;display:flex;gap:6px;flex-wrap:wrap}
.chip{background:rgba(0,0,0,.6);color:#fff;font-size:12px;padding:4px 9px;border-radius:999px;backdrop-filter:blur(4px)}
.chip i{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;vertical-align:0;background:#9ca3af}
.leg{display:flex;gap:12px;flex-wrap:wrap;font-size:12px;color:var(--mut);padding:9px 12px;background:var(--card)}
.leg i{display:inline-block;width:10px;height:10px;border:2px solid;border-radius:2px;margin-right:5px;vertical-align:-1px}
.tiles{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px}
.tile{margin:0;padding:12px}
.k{font-size:12px;color:var(--mut)}.v{font-size:21px;font-weight:650;margin-top:2px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.s{font-size:12px;color:var(--mut);margin-top:3px;min-height:1em}
.bar{position:relative;height:6px;border-radius:3px;background:var(--soft);margin-top:8px;overflow:hidden}
.bar b{position:absolute;left:0;top:0;bottom:0;background:var(--pc);border-radius:3px;transition:width .4s}
.bar i{position:absolute;top:0;bottom:0;width:2px;background:var(--mut);opacity:.6}
@media(max-width:520px){.tiles{grid-template-columns:1fr 1fr}.tiles .wide{grid-column:span 2}}
h2{font-size:15px;margin:0 0 10px;display:flex;align-items:center;gap:8px}
canvas{width:100%;height:170px;display:block}
.lg{display:flex;gap:12px;flex-wrap:wrap;font-size:12px;color:var(--mut);margin-top:8px}.lg i{display:inline-block;width:12px;height:3px;vertical-align:middle;margin-right:4px;border-radius:2px}
.nav{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px}
.nav a{display:flex;flex-direction:column;align-items:center;gap:4px;padding:12px 6px;border-radius:14px;background:var(--card);border:1px solid var(--line);color:var(--fg);text-decoration:none;font-size:13px}
.nav svg{width:22px;height:22px;color:var(--pc)}
details.card{padding:0}details>summary{list-style:none;cursor:pointer;padding:14px;font-weight:600;display:flex;align-items:center;gap:8px}
details>summary::-webkit-details-marker{display:none}details>summary:after{content:"";margin-left:auto;width:8px;height:8px;border-right:2px solid var(--mut);border-bottom:2px solid var(--mut);transform:rotate(45deg);transition:transform .2s}
details[open]>summary:after{transform:rotate(-135deg)}
.dbg{padding:0 14px 14px}
.kv{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.kv div{background:var(--soft);border-radius:10px;padding:8px 10px}
.eyes{display:flex;gap:12px;align-items:center;margin-top:12px}.eyes img{width:50%;max-width:280px;image-rendering:pixelated;border-radius:8px;background:#000}
.row{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
button.b,select{font:inherit;font-size:14px;padding:8px 12px;border-radius:10px;border:1px solid var(--line);background:var(--soft);color:var(--fg);cursor:pointer}
.seg{display:inline-flex;background:var(--soft);border-radius:10px;padding:3px;gap:3px}
.seg button{border:none;background:none;font:inherit;font-size:14px;padding:6px 12px;border-radius:8px;color:var(--mut);cursor:pointer}
.seg button.on{background:var(--card);color:var(--fg);box-shadow:0 1px 2px rgba(0,0,0,.15)}
pre{white-space:pre-wrap;word-break:break-all;font-size:11px;margin:10px 0 0;max-height:280px;overflow:auto;background:var(--soft);padding:10px;border-radius:10px}
.note{font-size:12px;color:var(--mut);margin-top:8px}
</style></head><body><main>

<div class="top"><span class="dot" id="dot"></span><span class="brand">DrowsyFW</span><span class="meta" id="hd">กำลังเชื่อมต่อ...</span></div>

<div class="card hero" id="hero">
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>
<div><b id="lt">--</b><span id="ls">กำลังรอข้อมูลจาก Device</span></div></div>
<div class="off" id="off">ขาดการเชื่อมต่อกับ Device · ข้อมูลด้านล่างอาจไม่ใช่ล่าสุด</div>
<button class="mute" id="mute" onclick="post('/api/mute')"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 5 6 9H2v6h4l5 4V5z"/><path d="m23 9-6 6M17 9l6 6"/></svg><span id="mt">ปิดเสียงเตือน 5 นาที</span></button>

<div class="card cam"><img id="v" alt="" aria-label="ภาพจากกล้อง">
<div class="chips"><span class="chip" id="ch-face"><i></i>หาหน้า</span><span class="chip" id="ch-eye"><i></i>ตา</span></div>
<div class="leg"><span><i style="border-color:#22c55e"></i>หน้า</span><span><i style="border-color:#3b82f6"></i>ตาเปิด</span><span><i style="border-color:#ef4444"></i>ตาปิด</span><span><i style="border-color:#eab308"></i>ไม่แน่ใจ</span><span><i style="border-color:#94a3b8"></i>บริเวณที่ค้นหา</span></div></div>

<div class="tiles">
<div class="card tile"><div class="k">ตา</div><div class="v" id="c-eye">--</div><div class="s" id="c-eye2"></div></div>
<div class="card tile"><div class="k">PERCLOS · 60 วิ</div><div class="v" id="c-pc">--</div><div class="bar"><b id="pcb" style="width:0"></b><i style="left:30%"></i><i style="left:50%"></i></div><div class="s">ขีด = ระดับ 1 (15%) · 2 (25%)</div></div>
<div class="card tile wide"><div class="k">การเชื่อมต่อ</div><div class="v" id="c-net">--</div><div class="s" id="c-net2"></div></div>
</div>

<div class="card"><h2>ย้อนหลัง 2 นาที</h2><canvas id="g"></canvas>
<div class="lg"><span><i style="background:var(--l3)"></i>P(ตาปิด)</span><span><i style="background:var(--pc)"></i>PERCLOS</span><span>แถบล่าง = ระดับ · ว่าง = มองไม่เห็นตา</span></div></div>

<nav class="nav">
<a href="/trips"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3v18h18"/><path d="m7 15 4-4 3 3 5-6"/></svg>ประวัติ Trip</a>
<a href="/wifi"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M2 9a15 15 0 0 1 20 0"/><circle cx="12" cy="19.5" r=".8"/></svg>ตั้งค่า WiFi</a>
<a href="/update"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m17 8-5-5-5 5M12 3v12"/></svg>อัปเดต firmware</a>
</nav>

<details class="card" id="dbgd"><summary>สำหรับนักพัฒนา</summary><div class="dbg">
<div class="kv">
<div><div class="k">ความเร็ว</div><div id="d-fps">--</div><div class="s" id="d-fps2"></div></div>
<div><div class="k">หาหน้า</div><div id="d-face">--</div><div class="s" id="d-face2"></div></div>
<div><div class="k">IR / LDR</div><div id="d-ir">--</div><div class="s" id="d-ir2"></div></div>
<div><div class="k">หน่วยความจำ</div><div id="d-mem">--</div><div class="s" id="d-mem2"></div></div>
</div>
<div class="eyes"><img id="e" alt="" aria-label="ภาพตาที่เข้าโมเดล"><div class="s" id="pe">ภาพตา 32×32 ที่เข้าโมเดล · ซ้าย = ตาซ้ายของภาพ</div></div>
<div class="row" style="align-items:center"><span class="k">IR</span><div class="seg" id="irm"><button data-m="auto">อัตโนมัติ</button><button data-m="on">เปิดค้าง</button><button data-m="off">ปิด</button></div></div>
<div class="row"><span class="k" style="align-self:center">ทดสอบ 3 วิ</span>
<button class="b" onclick="post('/api/test?what=buzzer')">Buzzer</button><button class="b" onclick="post('/api/test?what=green')">LED เขียว</button>
<button class="b" onclick="post('/api/test?what=red')">LED แดง</button><button class="b" onclick="post('/api/test?what=ir')">IR</button><button class="b" onclick="post('/api/test?what=oled')">จอ OLED</button></div>
<div class="note" id="hw"></div>
<div class="note">IR กะพริบ 1 วิ ดูภาพสว่างขึ้นลง (ในที่มืดต้องมีของสะท้อนใกล้ๆ) · ปุ่ม BOOT บนโมดูล = ปิดเสียง 5 นาที</div>
<pre id="s">loading...</pre>
</div></details>
</main>
<script>
fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'epoch='+Math.floor(Date.now()/1000)}).catch(()=>{});
const $=id=>document.getElementById(id);
function post(u){fetch(u,{method:'POST'}).then(tick).catch(()=>{});}
const LV={normal:['ตื่นตัวปกติ','--ok',0],fatigue:['เริ่มล้า','--l1',1],drowsy:['ง่วงนอน','--l2',2],microsleep:['หลับใน!','--l3',3]};
const EYE={open:'ลืมตา',closed:'หลับตา',uncertain:'ไม่แน่ใจ',not_visible:'มองไม่เห็น',unknown:'--'};
const EC={open:'#3b82f6',closed:'#ef4444',uncertain:'#eab308'};
const hist=[];let lastOk=0;
function css(n){return getComputedStyle(document.documentElement).getPropertyValue(n).trim();}
function ago(s){return s<60?s+' วิ':s<3600?Math.floor(s/60)+' นาที':Math.floor(s/3600)+' ชม.';}
function mmss(s){return Math.floor(s/60)+':'+String(s%60).padStart(2,'0');}
async function tick(){try{const r=await fetch('/api/status');const d=await r.json();lastOk=Date.now();show(d);}catch(e){}}
function chip(id,txt,col){const c=$(id);c.lastChild.textContent=txt;c.firstChild.style.background=col;}
function show(d){
if($('dbgd').open)$('s').textContent=JSON.stringify(d,null,2);
$('hd').textContent='Trip '+d.trip+' · '+ago(d.uptime_s)+' · v'+d.fw;
let lv=LV[d.level]||LV.normal,t=lv[0],c=lv[1],sub='ระดับ '+lv[2]+' · ไม่พบสัญญาณง่วง';
if(lv[2]>0)sub='ระดับ '+lv[2];
if(lv[2]<2&&d.distraction){t='ไม่มองถนน';c='--dis';sub='มองไม่เห็นตาเกิน 3 วิ';}
else if(lv[2]==0&&d.unmonitored){t='มองไม่ชัด';c='--gray';sub='ตาอยู่ในสถานะไม่แน่ใจเกิน 5 วิ';}
else if(lv[2]==0&&!d.face){t='ไม่พบคนขับ';c='--gray';sub='หันหน้าเข้ากล้อง';}
if(d.closed_ms>0)sub+=' · ตาปิดต่อเนื่อง '+(d.closed_ms/1000).toFixed(1)+' วิ';
const h=$('hero');h.style.setProperty('--c',css(c));h.classList.toggle('alarm',lv[2]==3||(d.distraction&&lv[2]<2));
$('lt').textContent=t;$('ls').textContent=sub;
const m=d.mute_left_s>0;$('mute').classList.toggle('muted',m);$('mt').textContent=m?'ปิดเสียงอยู่ · เหลือ '+mmss(d.mute_left_s):'ปิดเสียงเตือน 5 นาที';
const seen=d.eye_state=='open'||d.eye_state=='closed'||d.eye_state=='uncertain';
$('c-eye').textContent=EYE[d.eye_state]||d.eye_state;$('c-eye').style.color=EC[d.eye_state]||'';
$('c-eye2').textContent=d.closed_ms>0?'ปิดมา '+(d.closed_ms/1000).toFixed(1)+' วิ':seen?'P(ตาปิด) '+d.p_closed.toFixed(2):'';
$('c-pc').textContent=d.perclos_ready?Math.round(d.perclos*100)+'%':'กำลังเก็บ';
$('pcb').style.width=(d.perclos_ready?Math.min(100,d.perclos*100*2):0)+'%';$('pcb').style.background=css(d.perclos>=.25?'--l2':d.perclos>=.15?'--l1':'--pc');
const sy=d.sync;$('c-net').textContent=d.sta?d.sta:'AP '+d.ap;
$('c-net2').textContent=(d.sta?d.sta_ip+' · ':'192.168.4.1 · ')+(!sy.configured?'Sync ปิด':sy.last_ok_age_s>=0?'Sync '+ago(sy.last_ok_age_s)+'ที่แล้ว'+(sy.pending_samples?' · ค้าง '+sy.pending_samples:''):sy.fail?'Sync ผิดพลาด':'Sync รอส่ง');
chip('ch-face',d.face?'เจอหน้า':'ไม่เจอหน้า',d.face?'#22c55e':'#9ca3af');
chip('ch-eye',EYE[d.eye_state]||'--',EC[d.eye_state]||'#9ca3af');
$('d-fps').textContent=d.fps.toFixed(1)+' fps';$('d-fps2').textContent='หาหน้า '+d.detect_ms.toFixed(0)+' ms · ตา '+d.classify_ms.toFixed(0)+' ms';
$('d-face').textContent=d.face?'คะแนน '+d.face_score.toFixed(2):'ไม่เจอ';$('d-face2').textContent='ระยะตา '+d.eye_dist_px+' px · ROI '+(d.face_roi?d.face_roi.join(','):'-');
$('d-ir').textContent=(d.ir_on?'ติด':'ดับ')+' · LDR '+d.ldr_raw;$('d-ir2').textContent={auto:'อัตโนมัติ',on:'เปิดค้าง',off:'ปิด'}[d.ir_mode]||'';
$('d-mem').textContent='ภายใน '+d.free_internal_kb+' KB';$('d-mem2').textContent='ก้อนใหญ่สุด '+d.largest_internal_kb+' KB · PSRAM '+d.free_psram_kb+' KB';
$('pe').textContent='ภาพตา 32×32 ที่เข้าโมเดล · P(ปิด) ซ้าย '+d.p_left.toFixed(2)+' · ขวา '+d.p_right.toFixed(2);
$('hw').textContent='OLED '+(d.oled_ok?'เจอจอ':'ไม่เจอจอ')+(d.hw_test!=='none'?' · กำลังทดสอบ '+d.hw_test:'');
for(const b of $('irm').children)b.classList.toggle('on',b.dataset.m==d.ir_mode);
hist.push({t:Date.now(),p:seen?d.p_closed:null,pc:d.perclos_ready?d.perclos:null,l:lv[2],dis:d.distraction});
while(hist.length&&hist[0].t<Date.now()-120000)hist.shift();
draw();}
for(const b of $('irm').children)b.onclick=()=>post('/api/ir?mode='+b.dataset.m);
function draw(){
const cv=$('g'),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;
cv.width=W*dpr;cv.height=H*dpr;const x=cv.getContext('2d');x.scale(dpr,dpr);
const L=26,B=22,T=6,ph=H-B-T-4,now=Date.now();
const X=t=>L+(W-L-4)*(1-(now-t)/120000),Y=v=>T+ph*(1-v);
x.font='11px system-ui';x.fillStyle=css('--mut');x.strokeStyle=css('--line');x.lineWidth=1;
for(const v of [0,.5,1]){x.beginPath();x.moveTo(L,Y(v));x.lineTo(W-4,Y(v));x.stroke();x.fillText(v.toFixed(1),2,Y(v)+4);}
x.setLineDash([3,4]);x.strokeStyle=css('--mut');for(const v of [.5,.3]){x.beginPath();x.moveTo(L,Y(v));x.lineTo(W-4,Y(v));x.stroke();}x.setLineDash([]);
const cols=['--ok','--l1','--l2','--l3'];
for(let i=0;i<hist.length;i++){const h=hist[i],n=hist[i+1],x1=X(h.t),x2=n?X(n.t):W-4;x.fillStyle=css(h.dis&&h.l<2?'--dis':cols[h.l]);x.fillRect(x1,H-B+3,Math.max(1,x2-x1),7);}
x.fillStyle=css('--mut');x.fillText('-2 นาที',L,H-1);x.textAlign='right';x.fillText('ตอนนี้',W-4,H-1);x.textAlign='left';
function line(k,c){x.strokeStyle=css(c);x.lineWidth=2;x.lineJoin='round';x.beginPath();let on=false;
for(const h of hist){if(h[k]==null){on=false;continue;}const px=X(h.t),py=Y(h[k]);if(on)x.lineTo(px,py);else x.moveTo(px,py);on=true;}x.stroke();}
line('pc','--pc');line('p','--l3');}
setInterval(()=>{const ok=lastOk&&Date.now()-lastOk<3000;$('off').style.display=lastOk&&!ok?'block':'none';$('dot').classList.toggle('on',!!ok);},1000);
window.addEventListener('resize',draw);
tick();setInterval(()=>{if(!document.hidden)tick();},1000);
// ภาพสดโหลดต่อกันเมื่อหน้าเปิดอยู่เท่านั้น · ภาพตาโหลดเฉพาะตอนเปิดส่วนนักพัฒนา (ประหยัด RAM ของบอร์ด)
const v=$('v');function next(){if(document.hidden){setTimeout(next,1000);return;}v.src='/snapshot.jpg?'+Date.now();}
v.onload=()=>setTimeout(next,150);v.onerror=()=>setTimeout(next,1000);next();
const ei=$('e');function nextE(){if(document.hidden||!$('dbgd').open){setTimeout(nextE,1000);return;}ei.src='/eyes.jpg?'+Date.now();}
ei.onload=()=>setTimeout(nextE,300);ei.onerror=()=>setTimeout(nextE,1000);nextE();
</script></body></html>)HTML";

// หน้าตั้งค่า WiFi (ผ่าน AP ของ Device ไม่ต้อง login · ทางอื่น admin / OTA_PASSWORD) · ข้อมูลจาก /api/wifi · บันทึกใน NVS ผ่าน net.cpp
static const char WIFI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ตั้งค่า WiFi</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--line:#e3e6eb;--ok:#1f9d55;--bad:#d61f1f;--ac:#2f6fed}
@media(prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1c2029;--fg:#e6e8ec;--mut:#9aa3b2;--line:#2c3240;--ac:#7aa2ff}}
:root{color-scheme:light dark}
*{box-sizing:border-box}body{font-family:system-ui,sans-serif;margin:0;background:var(--bg);color:var(--fg)}
main{max-width:560px;margin:0 auto;padding:12px 16px 32px}
header{display:flex;justify-content:space-between;align-items:baseline}h1{font-size:20px;margin:4px 0}h2{font-size:16px;margin:0 0 10px}
a{color:var(--ac)}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-top:12px}
label{display:block;font-size:13px;color:var(--mut);margin:10px 0 4px}
input{font:inherit;font-size:16px;width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg)}
button{font:inherit;font-size:15px;padding:9px 14px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg)}
.primary{background:var(--ac);color:#fff;border:none;width:100%;margin-top:14px;padding:12px}
.pw{display:flex;gap:8px}.pw input{flex:1}
.s{font-size:12px;color:var(--mut);margin-top:6px}.msg{font-size:14px;margin-top:10px;min-height:1em}.err{color:var(--bad)}.okc{color:var(--ok)}
ul{list-style:none;margin:0;padding:0}li{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid var(--line)}li:last-child{border-bottom:0}
li .n{flex:1;word-break:break-all}.bars{font-size:12px;color:var(--mut);letter-spacing:-1px;width:34px}
.near li{cursor:pointer}.badge{font-size:12px;color:var(--ok);border:1px solid var(--ok);border-radius:999px;padding:1px 8px}
#st b{font-size:17px}
</style></head><body><main>
<header><h1>ตั้งค่า WiFi</h1><a href="/">← กลับ</a></header>
<div class="card" id="st">กำลังโหลด...</div>
<div class="card"><h2>เพิ่ม WiFi / hotspot</h2>
<form id="f" autocomplete="off">
<label for="ssid">ชื่อ WiFi (SSID)</label><input id="ssid" maxlength="32" required>
<label for="pass">รหัสผ่าน</label><div class="pw"><input id="pass" type="password" maxlength="63"><button type="button" id="eye">แสดง</button></div>
<div class="s">เว้นว่างได้ถ้าเป็น WiFi ไม่มีรหัส · ถ้ามีรหัสต้องยาว 8–63 ตัว</div>
<button class="primary" id="save">บันทึกและเชื่อมต่อ</button><div class="msg" id="msg"></div></form>
<div style="display:flex;align-items:center;justify-content:space-between;margin-top:16px"><h2 style="margin:0">WiFi ใกล้เคียง</h2><button id="scan">ค้นหาใหม่</button></div>
<ul class="near" id="near"><li class="s">กด "ค้นหาใหม่" เพื่อดู WiFi รอบตัว</li></ul>
<div class="s">บอร์ดรองรับเฉพาะ 2.4 GHz · hotspot ของ iPhone ต้องเปิด "Maximize Compatibility" · ระหว่างค้นหา WiFi ของบอร์ดอาจสะดุด 2–3 วินาที</div></div>
<div class="card"><h2>WiFi ที่บันทึกไว้</h2><ul id="saved"></ul>
<div class="s">บอร์ดจะลองเชื่อมจากบนลงล่าง และลองใหม่ทุก 1 นาทีเมื่อหลุด · ค่าใน secrets.h ใช้เป็นตัวสำรองต่อท้าย</div></div>
<div class="s" style="margin-top:12px">แนะนำให้ตั้งค่าผ่าน WiFi ของบอร์ด (Drowsy-XXXX, เปิด 192.168.4.1) เพราะตอนเปลี่ยน WiFi บอร์ดจะหลุดจากเครือข่ายเดิมชั่วคราว</div>
</main><script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const bars=r=>r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂';
let polling=0;
async function load(scan){
  try{const r=await fetch('/api/wifi'+(scan?'?scan=1':''));if(!r.ok){$('st').textContent='โหลดไม่ได้ ('+r.status+')';return;}
  const d=await r.json();render(d);if(d.scanning)setTimeout(()=>load(false),1500);}catch(e){$('st').textContent='ติดต่อบอร์ดไม่ได้';}}
function render(d){
  $('st').innerHTML=d.connected?`เชื่อมต่ออยู่กับ <b>${esc(d.ssid)}</b><div class="s">IP ${esc(d.ip)} · WiFi ของบอร์ด ${esc(d.ap)} (192.168.4.1)</div>`
    :`<b>ยังไม่ได้เชื่อมต่ออินเทอร์เน็ต</b><div class="s">ใช้ได้ผ่าน WiFi ของบอร์ด ${esc(d.ap)} (192.168.4.1) · ข้อมูลจะส่งขึ้น cloud เมื่อเชื่อมได้</div>`;
  $('saved').innerHTML=d.saved.length?d.saved.map((s,i)=>`<li><span class="n">${i+1}. ${esc(s.ssid)}</span>${d.connected&&s.ssid===d.ssid?'<span class="badge">เชื่อมอยู่</span>':''}<button data-ssid="${esc(s.ssid)}">ลบ</button></li>`).join('')
    :'<li class="s">ยังไม่มี WiFi ที่บันทึกไว้</li>';
  $('saved').querySelectorAll('button').forEach(b=>b.onclick=()=>forget(b.dataset.ssid));
  if(d.scan.length||!d.scanning)$('near').innerHTML=d.scan.length?d.scan.map(n=>`<li data-ssid="${esc(n.ssid)}"><span class="bars">${bars(n.rssi)}</span><span class="n">${esc(n.ssid)}</span><span class="s">${n.secure?'🔒':'เปิด'}</span></li>`).join(''):'<li class="s">ไม่พบ WiFi</li>';
  if(d.scanning&&!d.scan.length)$('near').innerHTML='<li class="s">กำลังค้นหา...</li>';
  $('near').querySelectorAll('li[data-ssid]').forEach(li=>li.onclick=()=>{$('ssid').value=li.dataset.ssid;$('pass').value='';$('pass').focus();});
}
const form=o=>Object.entries(o).map(([k,v])=>k+'='+encodeURIComponent(v)).join('&');
async function post(u,o){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form(o)});}
$('eye').onclick=()=>{const p=$('pass');p.type=p.type==='password'?'text':'password';$('eye').textContent=p.type==='password'?'แสดง':'ซ่อน';};
$('scan').onclick=()=>{$('near').innerHTML='<li class="s">กำลังค้นหา...</li>';load(true);};
$('f').onsubmit=async e=>{e.preventDefault();const m=$('msg'),ssid=$('ssid').value.trim(),pass=$('pass').value;
  if(pass&&pass.length<8){m.className='msg err';m.textContent='รหัสผ่านต้องยาวอย่างน้อย 8 ตัว';return;}
  m.className='msg';m.textContent='กำลังบันทึก...';
  const r=await post('/api/wifi/save',{ssid,pass});
  if(!r.ok){m.className='msg err';m.textContent='บันทึกไม่ได้: '+await r.text();return;}
  $('pass').value='';m.className='msg okc';m.textContent='บันทึกแล้ว กำลังเชื่อมต่อ "'+ssid+'" (ไม่เกิน ~10 วินาที)';
  clearInterval(polling);let n=0;polling=setInterval(async()=>{await load(false);if(++n>=10)clearInterval(polling);},2000);};
async function forget(ssid){if(!confirm('ลบ "'+ssid+'" ออกจากรายการ?'))return;await post('/api/wifi/forget',{ssid});load(false);}
load(false);
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
  static char buf[2048];          // handler ทุกตัวรันบน task ของ AsyncTCP ตัวเดียว ใช้ static ได้
  LogStatus ls = logStatus();
  SyncStatus ss = syncStatus();
  int n = snprintf(buf, sizeof(buf),
    "{\"fw\":\"%s\",\"device_id\":\"%s\",\"trip\":%u,\"uptime_s\":%u,\"time_known\":%s,\"trip_start_epoch\":%lu,"
    "\"fps\":%.1f,\"face\":%s,\"face_score\":%.2f,\"eye_state\":\"%s\",\"p_closed\":%.2f,"
    "\"p_left\":%.2f,\"p_right\":%.2f,\"eye_dist_px\":%d,\"tilt_comp_deg\":%d,\"face_roi\":[%d,%d,%d,%d],"
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
    s.roi[0], s.roi[1], s.roi[2], s.roi[3],
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

// buffer JPEG เป็นของ response แต่ละตัว ปล่อยเมื่อ response ถูกลบ (ส่งเสร็จหรือหลุด)
// เดิมใช้ก้อนเดียวต่อ endpoint แล้วปล่อยของเก่าตอนมีคำขอถัดไป -> เปิดดูพร้อมกันสองเครื่องจะส่งหน่วยความจำที่ปล่อยแล้ว
static void sendJpeg(AsyncWebServerRequest* r, bool (*make)(uint8_t**, size_t*)) {
  uint8_t* raw = nullptr;
  size_t len = 0;
  if (!make(&raw, &len)) { r->send(503, "text/plain", "no frame yet"); return; }
  std::shared_ptr<uint8_t> jpg(raw, free);
  AsyncWebServerResponse* res = r->beginResponse("image/jpeg", len, [jpg, len](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
    size_t n = min(maxLen, len - index);
    memcpy(buf, jpg.get() + index, n);
    return n;
  });
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

// ---- ตั้งค่า WiFi ----
// เข้าผ่าน AP ของ Device (Drowsy-XXXX ที่มี AP_PASSWORD อยู่แล้ว) ไม่ต้อง login · เข้าผ่าน WiFi อื่นต้อง login เหมือน /update
static bool wifiAuthorized(AsyncWebServerRequest* req) {
  if (req->client() && req->client()->localIP() == WiFi.softAPIP()) return true;
  return otaAuthorized(req);
}

static void jsonStr(String& out, const char* s) {
  out += '"';
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') { out += '\\'; out += *s; }
    else if ((uint8_t)*s < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", *s); out += b; }
    else out += *s;
  }
  out += '"';
}

static void handleWifiGet(AsyncWebServerRequest* r) {
  if (!wifiAuthorized(r)) return r->requestAuthentication();
  if (r->hasParam("scan")) wifiScanRequest();
  SharedState s = stateSnapshot();
  WifiNet saved[WIFI_SAVED_MAX], near[WIFI_SCAN_MAX];
  bool scanning = false;
  int ns = wifiSavedList(saved, WIFI_SAVED_MAX), nn = wifiScanResults(near, WIFI_SCAN_MAX, &scanning);
  String j = "{\"connected\":";
  j += s.staConnected ? "true" : "false";
  j += ",\"ssid\":"; jsonStr(j, s.staSsid);
  j += ",\"ip\":"; jsonStr(j, s.staIp);
  j += ",\"ap\":"; jsonStr(j, s.apSsid);
  j += ",\"scanning\":"; j += scanning ? "true" : "false";
  j += ",\"saved\":[";
  for (int i = 0; i < ns; i++) { if (i) j += ','; j += "{\"ssid\":"; jsonStr(j, saved[i].ssid); j += '}'; }
  j += "],\"scan\":[";
  for (int i = 0; i < nn; i++) {
    if (i) j += ',';
    j += "{\"ssid\":"; jsonStr(j, near[i].ssid);
    j += ",\"rssi\":"; j += (int)near[i].rssi;   // int8_t จะถูกต่อเป็นตัวอักษรถ้าไม่แปลง
    j += ",\"secure\":"; j += near[i].secure ? "true" : "false"; j += '}';
  }
  j += "]}";
  r->send(200, "application/json", j);
}

void startWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", INDEX_HTML); });
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!wifiAuthorized(r)) return r->requestAuthentication();
    r->send(200, "text/html", WIFI_HTML);
  });
  server.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!wifiAuthorized(r)) return r->requestAuthentication();
    if (!r->hasParam("ssid", true)) { r->send(400, "text/plain", "missing ssid"); return; }
    String ssid = r->getParam("ssid", true)->value(), pass = r->hasParam("pass", true) ? r->getParam("pass", true)->value() : "";
    if (!wifiSave(ssid.c_str(), pass.c_str())) { r->send(400, "text/plain", "ชื่อ 1-32 ตัว · รหัสผ่านเว้นว่าง หรือ 8-63 ตัว"); return; }
    r->send(200, "application/json", "{\"saved\":true}");
  });
  server.on("/api/wifi/forget", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!wifiAuthorized(r)) return r->requestAuthentication();
    if (!r->hasParam("ssid", true)) { r->send(400, "text/plain", "missing ssid"); return; }
    bool ok = wifiForget(r->getParam("ssid", true)->value().c_str());
    r->send(ok ? 200 : 404, "application/json", ok ? "{\"forgotten\":true}" : "{\"forgotten\":false}");
  });
  server.on("/api/wifi", HTTP_GET, handleWifiGet);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/ab", HTTP_GET, handleAb);
  server.on("/api/trips", HTTP_GET, handleTrips);
  server.on("/trips", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", TRIPS_HTML); });

  // ภาพที่โมเดลเห็นล่าสุด (หมุนแล้ว) พร้อมกรอบหน้า/ตา · vision เริ่มคัดลอกภาพเมื่อมีคนขอ จึงอาจว่างในครั้งแรก
  server.on("/snapshot.jpg", HTTP_GET, [](AsyncWebServerRequest* r) { sendJpeg(r, snapshotJpeg); });
  server.on("/eyes.jpg", HTTP_GET, [](AsyncWebServerRequest* r) { sendJpeg(r, eyesJpeg); });

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
