"""บอร์ดจำลอง สำหรับลองสคริปต์ cartest.py บน PC โดยไม่มีบอร์ด (ผลไม่มีความหมายทางโมเดล แค่ทดสอบการทำงานของสคริปต์)

    python ml/incar/fake_board.py [พอร์ต=8790]
    python ml/incar/cartest.py --host 127.0.0.1:8790 --name dryrun --yes --no-voice

ตามท่าจาก header X-Cue ที่ cartest.py ส่งมา: ตาปิด (closed) → Eye State ปิด 85% ของเฟรม · ระดับขึ้นตามเวลาปิดต่อเนื่อง
/eyes.raw ส่งภาพตา MRL (จาก ml/data/cache_mrl_32_48_64.npz) ตามป้าย ถ้าไม่มีไฟล์นี้ใช้ภาพสุ่ม
"""
import http.server
import json
import random
import sys
import threading
import time

import numpy as np

import common

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8790
rnd = random.Random(1)
try:
    d = np.load(common.ML / "data" / "cache_mrl_32_48_64.npz")
    closed = np.array([1 - int(s.split("_")[4]) for s in d["names"]])
    POOL = {1: d["x32"][closed == 1][:500], 0: d["x32"][closed == 0][:500]}
except OSError:
    POOL = {k: np.random.default_rng(k).integers(0, 255, (50, 32, 32), np.uint8) for k in (0, 1)}

lock = threading.Lock()
S = dict(seq=0, t=time.time(), run=0.0, level="normal", eye="open", p=0.1, open_run=0.0)


def step(cue):
    """เดินเวลาเครื่องจำลองหนึ่งเฟรม (ไม่ได้ใช้ DrowsinessJudge จริง เป็นแค่ประมาณ)"""
    now = time.time(); dt = now - S["t"]; S["t"] = now; S["seq"] += 1
    is_closed = rnd.random() < (0.85 if cue == "closed" else 0.03)
    S["eye"] = "closed" if is_closed else ("uncertain" if cue == "squint" else "open")
    S["p"] = round(rnd.uniform(0.5, 0.95) if is_closed else rnd.uniform(0.02, 0.25), 2)
    if is_closed:
        S["run"] += dt; S["open_run"] = 0
    else:
        S["open_run"] += dt
        if S["open_run"] > 0.3:
            S["run"] = 0
    if S["run"] >= 3: S["level"] = "microsleep"
    elif S["run"] >= 2: S["level"] = max(S["level"], "drowsy", key=["normal", "drowsy", "microsleep"].index)
    elif S["open_run"] > 1: S["level"] = "normal"


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, body, ctype):
        self.send_response(200); self.send_header("Content-Type", ctype); self.end_headers(); self.wfile.write(body)

    def do_GET(self):
        cue = self.headers.get("X-Cue", "open")
        with lock:
            step(cue)
            if self.path.startswith("/api/status"):
                self.send(json.dumps(dict(fw="fake", fps=6.0, face=True, eye_state=S["eye"], p_closed=S["p"], p_left=S["p"], p_right=S["p"],
                                          closed_ms=int(S["run"] * 1000), level=S["level"], distraction=False, unmonitored=False,
                                          perclos=0.1, mute_left_s=0, free_internal_kb=40)).encode(), "application/json")
            elif self.path.startswith("/eyes.raw"):
                k = 1 if S["eye"] == "closed" else 0
                eyes = np.stack([POOL[k][rnd.randrange(len(POOL[k]))] for _ in range(2)])
                self.send(common.make_raw(S["seq"], int(S["t"] * 1000) & 0xFFFFFFFF, S["p"], S["p"], 30, 3, eyes), "application/octet-stream")
            else:
                self.send_error(404)


print(f"fake board on http://127.0.0.1:{PORT}")
http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
