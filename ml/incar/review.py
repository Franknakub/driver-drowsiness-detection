"""ตรวจป้ายภาพตาที่ cartest.py เก็บมา ก่อนใช้ fine-tune (เปิดในเบราว์เซอร์บนเครื่อง ไม่ขึ้นเว็บ)

    python ml/incar/review.py            → http://127.0.0.1:8797/

กดที่ภาพตาเพื่อเปลี่ยนป้าย: ตามท่า → เปิด → ปิด → ทิ้ง → ตามท่า · บันทึกลง labels.json ของรอบทันที
ภาพตาปิดที่เครื่องหมายว่าเปิด (หรือกลับกัน) ให้ดูจาก P ของบอร์ดใต้ภาพประกอบ
"""
import http.server
import io
import json
import sys
import urllib.parse

import numpy as np
from PIL import Image

import common

sys.stdout.reconfigure(encoding="utf-8")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8797
CACHE = {}

PAGE = r"""<!doctype html><html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ตรวจป้ายภาพตา</title><style>
:root{--bg:#f6f6f4;--card:#fff;--fg:#1d2433;--mut:#5b6170;--line:#ddd}
@media(prefers-color-scheme:dark){:root{--bg:#141619;--card:#1e2126;--fg:#eceae4;--mut:#a7abb3;--line:#33373e}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,"Segoe UI","Noto Sans Thai",sans-serif}
header{position:sticky;top:0;background:var(--bg);padding:10px 16px;border-bottom:1px solid var(--line);display:flex;gap:12px;flex-wrap:wrap;align-items:center;z-index:2}
select,button{font:inherit;padding:5px 10px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg)}
.sum{color:var(--mut)}.sp{flex:1}
main{padding:12px 16px;display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}
.f{background:var(--card);border-radius:10px;padding:6px;font-size:11px;color:var(--mut)}
.f .ey{display:flex;gap:4px}
.f img{width:68px;height:68px;image-rendering:pixelated;border:4px solid #999;border-radius:4px;cursor:pointer;background:#000}
.open{border-color:#2f7bf6!important}.closed{border-color:#e5384f!important}.drop{border-color:#888!important;opacity:.35}
.other{border-color:#d9a400!important}.edited{outline:2px dashed var(--fg);outline-offset:1px}
.lg span{display:inline-flex;gap:4px;align-items:center;margin-right:10px}.lg i{width:12px;height:12px;border-radius:3px;display:inline-block}
</style></head><body>
<header><b>ตรวจป้ายภาพตา</b><select id="ses"></select>
<select id="flt"><option value="">ทุกท่า</option><option>closed</option><option>open</option><option>glance</option><option>squint</option><option>blink</option><option>drive</option></select>
<label><input type="checkbox" id="sus"> เฉพาะที่น่าสงสัย (P ของบอร์ดขัดกับป้าย)</label>
<span class="lg"><span><i style="background:#2f7bf6"></i>เปิด</span><span><i style="background:#e5384f"></i>ปิด</span><span><i style="background:#d9a400"></i>ไม่เข้า train</span><span><i style="background:#888"></i>ทิ้ง</span></span>
<span class="sp"></span><span class="sum" id="sum"></span></header>
<main id="g"></main>
<script>
const $=id=>document.getElementById(id);let D=null;
const NEXT={open:'closed',closed:'drop',drop:null};
async function sessions(){const s=await (await fetch('/api/sessions')).json();$('ses').innerHTML=s.map(n=>`<option>${n}</option>`).join('');load();}
async function load(){D=await (await fetch('/api/session/'+encodeURIComponent($('ses').value))).json();draw();}
function lab(i,e){return D.labels[i][e];}
function draw(){const f=$('flt').value,sus=$('sus').checked,g=[];let n=0;
 for(const fr of D.frames){if(f&&fr.cue!==f)continue;
  const p=[fr.pL,fr.pR];
  if(sus&&![0,1].some(e=>(lab(fr.i,e)==='closed'&&p[e]>=0&&p[e]<0.3)||(lab(fr.i,e)==='open'&&p[e]>=0.5)))continue;
  if(++n>3000)break;
  g.push(`<div class="f"><div class="ey">${[0,1].map(e=>{const l=lab(fr.i,e),c=['open','closed','drop'].includes(l)?l:'other';
   return `<img loading="lazy" src="/img/${encodeURIComponent(D.name)}/${fr.i}/${e}" data-i="${fr.i}" data-e="${e}" class="${c}${D.edited[fr.i+':'+e]?' edited':''}" title="${l}">`}).join('')}</div>
   ${fr.t.toFixed(1)} วิ · ${fr.seg}<br>P ${p.map(x=>x<0?'–':x.toFixed(2)).join(' / ')}</div>`);}
 $('g').innerHTML=g.join('');
 const c={};for(const r of D.labels)for(const l of r)c[l]=(c[l]||0)+1;
 $('sum').textContent=`${D.frames.length} เฟรม · `+Object.entries(c).map(([k,v])=>k+' '+v).join(' · ')+(n>3000?' · แสดง 3000 แรก':'');}
$('g').onclick=async ev=>{const im=ev.target.closest('img');if(!im)return;const i=+im.dataset.i,e=+im.dataset.e,k=i+':'+e;
 const cur=lab(i,e),cue=D.cue[i][e];let nx=cur in NEXT?NEXT[cur]:'open';if(nx===null)nx=cue;
 if(nx===cue)delete D.edited[k];else D.edited[k]=nx;D.labels[i][e]=nx;
 await fetch('/api/labels/'+encodeURIComponent(D.name),{method:'POST',body:JSON.stringify(D.edited)});
 im.className=(['open','closed','drop'].includes(nx)?nx:'other')+(D.edited[k]?' edited':'');im.title=nx;};
$('ses').onchange=load;$('flt').onchange=draw;$('sus').onchange=draw;sessions();
</script></body></html>"""


def session(name):
    if name not in CACHE:
        CACHE[name] = common.load(name)
    return CACHE[name]


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, body, ctype, code=200):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Cache-Control", "no-store")
        self.end_headers(); self.wfile.write(body)

    def do_GET(self):
        p = [urllib.parse.unquote(x) for x in self.path.split("?")[0].strip("/").split("/")]
        if p == [""]:
            return self.send(PAGE.encode(), "text/html; charset=utf-8")
        if p == ["api", "sessions"]:
            return self.send(json.dumps([s for s in common.sessions() if (common.DATA / s / "eyes.npy").exists()]).encode(), "application/json")
        if p[:2] == ["api", "session"]:
            CACHE.pop(p[2], None)
            meta, frames, eyes, labels = session(p[2])
            d = common.DATA / p[2]
            edited = json.loads((d / "labels.json").read_text(encoding="utf-8")) if (d / "labels.json").exists() else {}
            cue = [[f["cue"] if (f["valid"] >> e) & 1 else "drop" for e in (0, 1)] for f in frames]
            body = dict(name=p[2], frames=[{k: f[k] for k in ("i", "t", "seg", "cue", "pL", "pR")} for f in frames],
                        labels=labels.tolist(), cue=cue, edited=edited)
            return self.send(json.dumps(body, ensure_ascii=False).encode(), "application/json")
        if p[0] == "img" and len(p) == 4:
            eyes = session(p[1])[2]
            buf = io.BytesIO(); Image.fromarray(eyes[int(p[2]), int(p[3])]).save(buf, "PNG")
            return self.send(buf.getvalue(), "image/png")
        self.send(b"not found", "text/plain", 404)

    def do_POST(self):
        p = [urllib.parse.unquote(x) for x in self.path.strip("/").split("/")]
        if p[:2] == ["api", "labels"] and p[2] in common.sessions():
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            (common.DATA / p[2] / "labels.json").write_text(json.dumps(body, ensure_ascii=False, indent=0), encoding="utf-8")
            CACHE.pop(p[2], None)
            return self.send(b"ok", "text/plain")
        self.send(b"not found", "text/plain", 404)


if __name__ == "__main__":
    print(f"ตรวจป้าย: http://127.0.0.1:{PORT}/  (Ctrl+C หยุด)")
    http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
