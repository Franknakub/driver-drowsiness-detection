"""เล่นรอบที่เก็บไว้ผ่าน DrowsinessJudge จริงบน PC แล้วให้คะแนนแบบเดียวกับ score.py · ใช้ดูโมเดลใหม่ก่อนแฟลช

    python ml/incar/replay.py <รอบ> [--run <fine-tune run>] [--on 0.3 --off 0.3]

แหล่ง P ที่เทียบกัน:
  board        P ที่บอร์ดส่งมาจริงใน /eyes.raw (ใช้เช็คว่า replay ใกล้ผลจริงของบอร์ดใน status.jsonl)
  board_model  โมเดลบนบอร์ดตอนนี้ รันบน PC (ต้องมี --run)
  new          โมเดลจาก finetune.py (ต้องมี --run · อ่าน ml/output/incar/<run>/p_<รอบ>.csv)
เกณฑ์ P_CLOSED_ON / OFF อ่านจาก firmware/DrowsyFW/config.h ถ้าไม่ระบุ · ลองเกณฑ์อื่นได้โดยไม่ต้องแฟลช
ข้อจำกัด: replay เห็นเฉพาะเฟรมที่ cartest.py ดึงทัน (เฟรมห่างกว่าบนบอร์ด) และ "เจอหน้า" ประมาณจากตาที่จำแนกได้
"""
import argparse
import json
import re
import subprocess
import sys

import common
import score

sys.stdout.reconfigure(encoding="utf-8")
REPO = common.ML.parent
FW = REPO / "firmware" / "DrowsyFW"
TESTS = REPO / "firmware" / "tests"
EXE = TESTS / ("replay.exe" if sys.platform == "win32" else "replay")
GXX = "C:/ProgramData/mingw64/mingw64/bin/g++.exe" if sys.platform == "win32" else "g++"
LEVEL = ["normal", "fatigue", "drowsy", "microsleep"]


def build():
    srcs = [TESTS / "replay.cpp", FW / "drowsiness.cpp", FW / "drowsiness.h", FW / "drowsy_config.h", FW / "domain_types.h"]
    if EXE.exists() and EXE.stat().st_mtime > max(s.stat().st_mtime for s in srcs):
        return
    subprocess.run([GXX, "-std=c++17", "-Wall", "-O2", f"-I{FW}", str(TESTS / "replay.cpp"), str(FW / "drowsiness.cpp"), "-o", str(EXE)], check=True)


def thresholds():
    cfg = (FW / "config.h").read_text(encoding="utf-8")
    get = lambda k: float(re.search(rf"#define {k}\s+([\d.]+)f", cfg).group(1))
    return get("P_CLOSED_ON"), get("P_CLOSED_OFF")


def run(frames, p, on, off):
    """p: [(pL, pR)] ต่อเฟรม → rows สำหรับ score.score_rows"""
    inp = "".join(f"{f['ms']},{a:.4f},{b:.4f}\n" for f, (a, b) in zip(frames, p))
    out = subprocess.run([str(EXE), str(on), str(off)], input=inp, capture_output=True, text=True, check=True).stdout.splitlines()[1:]
    rows = []
    for f, (a, b), line in zip(frames, p, out):
        _, es, lv, cms, dis, _ = line.split(",")
        rows.append(dict(t=f["t"], level=LEVEL[int(lv)], closed_ms=int(cms), eye_state=es, pL=a, pR=b,
                         face=es != "not_visible", distraction=dis == "1", fps=None))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sessions", nargs="+")
    ap.add_argument("--run", help="ชื่อ run ของ finetune.py")
    ap.add_argument("--on", type=float)
    ap.add_argument("--off", type=float)
    a = ap.parse_args()
    on0, off0 = thresholds()
    on = a.on if a.on is not None else on0
    off = a.off if a.off is not None else min(off0, on)
    build()
    for s in a.sessions:
        meta, frames, _, _ = common.load(s)
        if not frames:
            print(f"{s}: ไม่มีภาพจาก /eyes.raw (firmware รอบนี้ไม่มี endpoint นี้) replay ไม่ได้"); continue
        meta = dict(meta, fw=f"{meta['fw']} · replay เกณฑ์ {on}/{off}")
        sources = {"board": [(f["pL"], f["pR"]) for f in frames]}
        if a.run:
            csv = common.ML / "output" / "incar" / a.run / f"p_{s}.csv"
            if not csv.exists():
                sys.exit(f"ไม่มี {csv} (รอบนี้ต้องอยู่ใน --val หรือ --test ตอนรัน finetune.py)")
            rows = [l.split(",") for l in csv.read_text(encoding="utf-8").splitlines()[1:]]
            sources["board_model"] = [(float(r[3]), float(r[4])) for r in rows]
            sources["new"] = [(float(r[1]), float(r[2])) for r in rows]
        summary = {}
        for name, p in sources.items():
            print(f"\n######## {s} · P จาก {name}")
            r = score.report(s, score.score_rows(s, meta, run(frames, p, on, off)), save=False)
            summary[name] = dict(closes_alarmed=r["closes_alarmed"], closes_microsleep=r["closes_microsleep"], false_alarms=r.get("false_alarms"),
                                 false_distractions=r.get("false_distractions"))
        if (common.DATA / s / "status.jsonl").exists():
            r = score.score(s)
            summary["board_actual"] = dict(closes_alarmed=r["closes_alarmed"], closes_microsleep=r["closes_microsleep"],
                                           false_alarms=r.get("false_alarms"), false_distractions=r.get("false_distractions"))
        print(f"\n== {s} สรุป (board_actual = ผลจริงของบอร์ดจาก status.jsonl)")
        for k, v in summary.items():
            print(f"   {k:<13} เตือนตอนหลับตา {v['closes_alarmed']:<5} microsleep {v['closes_microsleep']:<5} เตือนผิด {v['false_alarms']}  ไม่มองถนนผิด {v['false_distractions']}")
        if a.run:
            (common.ML / "output" / "incar" / a.run / f"replay_{s}.json").write_text(json.dumps(dict(on=on, off=off, summary=summary), ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
