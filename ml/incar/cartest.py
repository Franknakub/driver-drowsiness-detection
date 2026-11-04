"""ทดสอบในรถ: พูดบอกท่าทีละช่วง → อ่าน /api/status 2 ครั้ง/วิ → ให้คะแนนว่าเตือนครบไหม เตือนผิดไหม

    python ml/incar/cartest.py --name 2026-11-05_day_noglasses --person frank --light day --glasses none
    python ml/incar/cartest.py --protocol drive --name 2026-11-05_drive1 ...      (ขับจริง ไม่มีเสียงบอก Ctrl+C หยุด)

ต่อคอมเข้า WiFi AP ของบอร์ด (Drowsy-XXXX) ก่อน · บอร์ดอยู่ที่ 192.168.4.1
ถ้า firmware มี /eyes.raw (ตั้งแต่ 0.3.13-raw) จะเก็บภาพตา 32x32 ที่เข้าโมเดลไว้ด้วย ใช้ fine-tune ต่อได้
ผลอยู่ใน ml/data/incar/<name>/ (ไม่ขึ้น git) · ให้คะแนนซ้ำได้ด้วย score.py
⚠️ ท่าหลับตา: ทำตอนรถจอดเท่านั้น
"""
import argparse
import datetime
import json
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

import numpy as np

import common
import score

sys.stdout.reconfigure(encoding="utf-8")


class Voice:
    """เสียงบอกท่า: SpeechSynthesizer ของ Windows ค้างไว้หนึ่ง process (เริ่มพูดทันที ป้ายเวลาไม่เลื่อน) + เสียงบี๊บ"""

    def __init__(self, on):
        self.p = None
        if on and sys.platform == "win32":
            ps = ("Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.Rate = 1; "
                  "while (($l = [Console]::In.ReadLine()) -ne $null) { $s.SpeakAsyncCancelAll(); [void]$s.SpeakAsync($l) }")
            self.p = subprocess.Popen(["powershell", "-NoProfile", "-Command", ps], stdin=subprocess.PIPE, text=True)

    def say(self, text, label):
        if sys.platform == "win32":
            import winsound
            for f in ((1400, 1400) if label == "closed" else (700,)):
                winsound.Beep(f, 120)
        if self.p:
            self.p.stdin.write(text + "\n"); self.p.stdin.flush()

    def close(self):
        if self.p:
            self.p.stdin.close(); self.p.wait(timeout=5)


def get(url, timeout, cue):
    req = urllib.request.Request(url, headers={"X-Cue": cue})   # fake_board.py ใช้ header นี้ บอร์ดจริงไม่สนใจ
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="ชื่อรอบ เช่น 2026-11-05_day_noglasses")
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--protocol", default="cartest", choices=list(common.PROTOCOLS))
    ap.add_argument("--person", default="")
    ap.add_argument("--light", default="", help="day / cloudy / night / ...")
    ap.add_argument("--glasses", default="", help="none / glasses / sunglasses")
    ap.add_argument("--notes", default="")
    ap.add_argument("--no-voice", action="store_true")
    ap.add_argument("--poll", type=float, default=0.5, help="อ่าน /api/status ทุกกี่วินาที (5 ครั้ง/วิ เคยทำเว็บบอร์ดค้างตอน RAM ตึง)")
    ap.add_argument("--yes", action="store_true", help="ข้ามคำถามยืนยันว่ารถจอดอยู่ (ใช้ตอนทดสอบสคริปต์)")
    a = ap.parse_args()

    out = common.DATA / a.name
    if out.exists():
        sys.exit(f"มีรอบชื่อ {a.name} อยู่แล้ว ({out}) ตั้งชื่อใหม่")
    base = f"http://{a.host}"
    try:
        st = json.loads(get(base + "/api/status", 3, "open"))
    except (OSError, ValueError) as e:
        sys.exit(f"ต่อบอร์ดที่ {base} ไม่ได้: {e}\nต่อ WiFi เข้า AP ของบอร์ด (Drowsy-XXXX) แล้วลองใหม่")
    try:
        common.parse_raw(get(base + "/eyes.raw", 3, "open")); has_raw = True
    except urllib.error.HTTPError as e:
        has_raw = e.code == 503       # มี endpoint แต่ยังไม่มีเฟรม
    except (OSError, ValueError):
        has_raw = False
    print(f"บอร์ด fw {st['fw']} · fps {st['fps']} · เจอหน้า {st['face']} · /eyes.raw {'มี (เก็บภาพตาด้วย)' if has_raw else 'ไม่มี (วัดผลอย่างเดียว)'}")
    if not st["face"]:
        print("⚠️ ตอนนี้บอร์ดยังหาหน้าไม่เจอ นั่งท่าขับแล้วดูภาพสดที่ http://192.168.4.1 ก่อน")
    plan = common.segments(a.protocol)
    if a.protocol != "drive":
        print("\nลำดับท่าในรอบนี้ (เสียงจะบอกเป็นภาษาอังกฤษ ดูภาษาไทยบนจอนี้ได้ตลอด):")
        for k, (t0, t1, _, _, say, thai) in enumerate(plan, 1):
            print(f"  {k:>2}. {int(t0) // 60}:{int(t0) % 60:02d}  {thai}  ({t1 - t0:.0f} วิ)  เสียง: {say}")
        print("เสียงบี๊บ: สูง 2 ครั้ง = หลับตา · ต่ำ 1 ครั้ง = ท่าอื่น\n")
    if common.SAFETY[a.protocol] and not a.yes:
        if input("รอบนี้มีการหลับตา ต้องทำตอนรถจอดเท่านั้น รถจอดอยู่ใช่ไหม? พิมพ์ y: ").strip().lower() != "y":
            sys.exit("ยกเลิก")

    segs = common.segments(a.protocol)
    out.mkdir(parents=True)
    meta = dict(name=a.name, protocol=a.protocol, segments=[s[:4] for s in segs], person=a.person, light=a.light,
                glasses=a.glasses, notes=a.notes, fw=st["fw"], host=a.host, has_raw=has_raw,
                started=datetime.datetime.now().isoformat(timespec="seconds"))
    stop = threading.Event()
    state = {"seg": "", "cue": "open", "t0": 0.0}
    status_rows, frames, eyes, errors = [], [], [], {"status": 0, "raw": 0}

    def poll_status():
        while not stop.is_set():
            t = time.perf_counter() - state["t0"]
            try:
                s = json.loads(get(base + "/api/status", 2, state["cue"]))
                status_rows.append(dict(t=round(t, 3), seg=state["seg"], cue=state["cue"], level=s["level"], closed_ms=s["closed_ms"],
                                        eye_state=s["eye_state"], p=s["p_closed"], pL=s["p_left"], pR=s["p_right"], face=s["face"],
                                        distraction=s["distraction"], unmonitored=s["unmonitored"], perclos=s["perclos"], fps=s["fps"],
                                        mute_left_s=s["mute_left_s"], free_internal_kb=s["free_internal_kb"]))
            except Exception:
                errors["status"] += 1
            time.sleep(max(0.0, a.poll - (time.perf_counter() - state["t0"] - t)))

    def poll_raw():
        last = None
        while not stop.is_set():
            t = time.perf_counter() - state["t0"]
            try:
                f, e = common.parse_raw(get(base + "/eyes.raw", 2, state["cue"]))
            except (OSError, ValueError):
                errors["raw"] += 1; time.sleep(0.2); continue
            if f["seq"] != last:
                last = f["seq"]
                frames.append(dict(i=len(frames), t=round(t, 3), seg=state["seg"], cue=state["cue"], **f)); eyes.append(e)
            time.sleep(0.12)

    voice = Voice(not a.no_voice and a.protocol != "drive")
    print("\nเริ่มใน 5 วิ · นั่งท่าขับ มองถนน\n")
    for k in range(5, 0, -1):
        print(f"  {k}", end="\r"); time.sleep(1)
    state["t0"] = time.perf_counter()
    threads = [threading.Thread(target=poll_status, daemon=True)] + ([threading.Thread(target=poll_raw, daemon=True)] if has_raw else [])
    for th in threads:
        th.start()
    stopped_early = False
    try:
        for k, (t0, t1, name, label, say, thai) in enumerate(segs):
            while time.perf_counter() - state["t0"] < t0:
                time.sleep(0.01)
            # ติดป้ายท่าใหม่ทันทีที่เริ่มพูด · ช่วงตอบสนองของคนถูกตัดทิ้งตอนให้คะแนน (common.MARGIN_START)
            state["seg"], state["cue"] = name, label
            voice.say(say, label)
            nxt = f"   · ถัดไป: {segs[k + 1][5]}" if k + 1 < len(segs) else ""
            print(f"[{k + 1:>2}/{len(segs)}  {t0:5.0f} วิ] >>> {thai}  ({t1 - t0:.0f} วิ){nxt}", flush=True)
        while time.perf_counter() - state["t0"] < segs[-1][1]:
            time.sleep(0.05)
    except KeyboardInterrupt:
        stopped_early = True
        print("\nหยุดก่อนครบ")
    stop.set()
    for th in threads:
        th.join(timeout=3)
    voice.close()

    # บันทึกในบอร์ดใช้นับการเตือนจริง (เว็บค้างแล้ว status หายได้ แต่ Trip log ไม่หาย)
    board_ms0 = None
    if len(frames) > 10:
        board_ms0 = round(sorted(f["ms"] - f["t"] * 1000 for f in frames[5:])[(len(frames) - 5) // 2])
    for _ in range(5):
        try:
            # count=1: เอาแค่ events · ขอ samples ทั้ง Trip แล้ว String บนบอร์ดจองไม่พอ JSON ขาดกลางทาง (RAM ภายในตึง)
            body = get(f"{base}/api/trips/{st['trip']}?count=1", 10, "open")
            json.loads(body)
            (out / "board_trip.json").write_bytes(body); break
        except (OSError, KeyError, ValueError):
            time.sleep(2)
    else:
        print("⚠️ ดึงบันทึกในบอร์ดไม่ได้ คะแนนจะใช้ status อย่างเดียว")

    meta.update(duration_s=round(time.perf_counter() - state["t0"], 1), stopped_early=stopped_early, board_ms0=board_ms0,
                errors=errors, status_rows=len(status_rows), raw_frames=len(frames))
    (out / "session.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1), encoding="utf-8")
    (out / "status.jsonl").write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in status_rows), encoding="utf-8")
    (out / "frames.jsonl").write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in frames), encoding="utf-8")
    np.save(out / "eyes.npy", np.stack(eyes) if eyes else np.zeros((0, 2, common.N, common.N), np.uint8))
    print(f"\nบันทึกที่ {out} · status {len(status_rows)} แถว · ภาพตา {len(frames)} เฟรม · ผิดพลาด {errors}\n")
    score.report(a.name)


if __name__ == "__main__":
    main()
