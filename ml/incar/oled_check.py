"""แสดงสิ่งที่จอ OLED ควรขึ้น (คิดจาก /api/status กฎเดียวกับ pickInfo ใน oled.cpp) ให้เทียบกับจอจริง

    python ml/incar/oled_check.py --host 172.20.10.3

Ctrl+C หยุด · อ่าน 2 ครั้ง/วิ
"""
import argparse
import json
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")


def expected(s):
    level = s["level"]
    status = ("WAKE UP!" if level == "microsleep" else "SLEEPY" if level == "drowsy" else "LOOK AHEAD" if s["distraction"]
              else "UNCLEAR" if s["unmonitored"] else "TIRED" if level == "fatigue" else "AWAKE")
    es = s["eye_state"]
    eye = ("NO FACE" if not s["face"] else "OPEN" if es == "open" else f"CLOSED {s['closed_ms'] / 1000:.1f}s" if es == "closed"
           else "UNSURE" if es == "uncertain" else "NOT FOUND")
    sound = ("ALARM" if level == "microsleep" else f"MUTED {s['mute_left_s']}s" if s["mute_left_s"] > 0
             else "BEEPING" if level == "drowsy" or s["distraction"] else "ON")
    return status, eye, sound


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    a = ap.parse_args()
    print("จอ OLED ควรขึ้นตามนี้ · ถ้าไม่ตรงให้จำบรรทัดนั้นไว้\n")
    last = None
    while True:
        try:
            with urllib.request.urlopen(f"http://{a.host}/api/status", timeout=2) as r:
                s = json.load(r)
            now = expected(s)
            if now != last:
                last = now
                print(f"{time.strftime('%H:%M:%S')}  {now[0]:<11} EYE: {now[1]:<13} SOUND: {now[2]:<12} (P {s['p_closed']:.2f} · PERCLOS {s['perclos']:.2f} · fps {s['fps']})", flush=True)
        except OSError as e:
            print(f"{time.strftime('%H:%M:%S')}  ต่อบอร์ดไม่ได้: {e}", flush=True)
        time.sleep(0.5)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
