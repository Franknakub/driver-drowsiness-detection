"""ให้คะแนนรอบทดสอบในรถจาก status.jsonl (ผลจริงของบอร์ด ไม่ได้จำลอง)

    python ml/incar/score.py <ชื่อรอบ> [<ชื่อรอบ> ...]

หลับตา (closed): ถือว่าเตือนถ้าระดับขึ้นเป็น drowsy/microsleep ภายในช่วงนั้น + 1.5 วิ หลังจบ (เผื่อเวลาลืมตาช้า)
ลืมตา/กะพริบ ในช่วง A (ยังไม่เคยหลับตา): ระดับ drowsy/microsleep ขึ้น = เตือนผิด · Distraction นับแยก
ทุกช่วงตัด MARGIN_START วิแรกทิ้งเวลาดูสถิติ Eye State (คนยังเปลี่ยนท่าไม่เสร็จ)
"""
import json
import sys

import common

sys.stdout.reconfigure(encoding="utf-8")
ALARM = ("drowsy", "microsleep")
LEVELS = ("normal", "fatigue", "drowsy", "microsleep")
LATE = 1.5


def load_status(name):
    d = common.DATA / name
    meta = json.loads((d / "session.json").read_text(encoding="utf-8"))
    rows = [json.loads(l) for l in (d / "status.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
    return meta, rows


def episodes(rows, pred):
    """ช่วงที่ pred เป็นจริงต่อเนื่อง: [(t เริ่ม, t จบ)]"""
    out, start = [], None
    for r in rows:
        if pred(r) and start is None:
            start = r["t"]
        elif not pred(r) and start is not None:
            out.append((start, r["t"])); start = None
    if start is not None:
        out.append((start, rows[-1]["t"]))
    return out


def share(rows, key, values):
    return round(sum(r[key] in values for r in rows) / len(rows), 3) if rows else None


def mean(xs):
    xs = [x for x in xs if x is not None and x >= 0]
    return round(sum(xs) / len(xs), 3) if xs else None


def merge_board_log(name, meta, rows):
    """เติมการเปลี่ยนระดับจากบันทึกในบอร์ดลงใน rows · เว็บบอร์ดค้างเป็นช่วงๆ ได้ แต่ Trip log ยังครบ (เจอ 4 พ.ย.: status บอก 1/6 จริง 6/6)"""
    path = common.DATA / name / "board_trip.json"
    if not rows or meta.get("board_ms0") is None or not path.exists():
        return rows
    out = list(rows)
    for e in json.loads(path.read_text(encoding="utf-8"))["events"]:   # [i, ms, type, from, to, value] · type 0 = LevelChange
        t = (e[1] - meta["board_ms0"]) / 1000
        if e[2] == 0 and rows[0]["t"] <= t <= rows[-1]["t"]:
            prev = max((r for r in rows if r["t"] <= t), key=lambda r: r["t"])
            out.append(dict(prev, t=round(t, 3), level=LEVELS[e[4]], closed_ms=e[5]))
    return sorted(out, key=lambda r: r["t"])


def score(name):
    meta, rows = load_status(name)
    return score_rows(name, meta, merge_board_log(name, meta, rows))


def score_rows(name, meta, rows):
    """rows: [{t, level, closed_ms, eye_state, pL, pR, face, distraction, fps}] จาก /api/status หรือจาก replay.py"""
    end = rows[-1]["t"] if rows else 0
    res = dict(name=name, fw=meta["fw"], protocol=meta["protocol"], person=meta.get("person"), light=meta.get("light"),
               glasses=meta.get("glasses"), rows=len(rows), duration_s=meta.get("duration_s"),
               fps=mean([r["fps"] for r in rows]), face_found=share(rows, "face", (True,)))
    closes, open_rows, glances = [], [], []
    for t0, t1, seg, label in meta["segments"]:
        if t0 >= end:
            break
        inside = [r for r in rows if t0 <= r["t"] < t1]
        settled = [r for r in inside if r["t"] >= t0 + common.MARGIN_START]
        if label == "closed":
            win = [r for r in rows if t0 <= r["t"] <= t1 + LATE]
            first = lambda lv: next((round(r["t"] - t0, 2) for r in win if r["level"] in lv), None)
            closes.append(dict(seg=seg, sec=t1 - t0, alarm_after_s=first(ALARM), microsleep_after_s=first(("microsleep",)),
                               max_closed_ms=max((r["closed_ms"] for r in win), default=0),
                               eye_closed=share(settled, "eye_state", ("closed",)), face=share(settled, "face", (True,)),
                               pL=mean([r["pL"] for r in settled]), pR=mean([r["pR"] for r in settled])))
        elif seg.startswith("A_") and label in ("open", "blink"):
            open_rows += settled
        elif label == "glance":
            glances.append(dict(seg=seg, distraction=any(r["distraction"] for r in inside + [r for r in rows if t1 <= r["t"] < t1 + 2]),
                                alarm=any(r["level"] in ALARM for r in inside)))
        elif label == "drive":
            open_rows += settled

    res["closes"] = closes
    res["closes_alarmed"] = f"{sum(c['alarm_after_s'] is not None for c in closes)}/{len(closes)}"
    res["closes_microsleep"] = f"{sum(c['microsleep_after_s'] is not None for c in closes)}/{len(closes)}"
    res["closed_eye_state"] = mean([c["eye_closed"] for c in closes])
    res["closed_pL"], res["closed_pR"] = mean([c["pL"] for c in closes]), mean([c["pR"] for c in closes])
    if open_rows:
        hours = max((open_rows[-1]["t"] - open_rows[0]["t"]) / 3600, 1e-9)
        fa = episodes(open_rows, lambda r: r["level"] in ALARM)
        dis = episodes(open_rows, lambda r: r["distraction"])
        res.update(open_s=round(hours * 3600), false_alarms=len(fa), false_alarm_times=[round(a, 1) for a, _ in fa],
                   false_distractions=len(dis), false_per_hour=round(len(fa) / hours, 1) if meta["protocol"] == "drive" else None,
                   open_eye_state={k: share(open_rows, "eye_state", (k,)) for k in ("open", "closed", "uncertain", "not_visible")},
                   open_pL=mean([r["pL"] for r in open_rows]), open_pR=mean([r["pR"] for r in open_rows]),
                   open_fatigue=share(open_rows, "level", ("fatigue",)))
    res["glances"] = glances
    return res


def verdict(r):
    """สรุปให้ตัดสินใจ: ผ่าน / ไม่ผ่านเพราะอะไร (ตาจำแนกผิด หรือหาหน้าไม่เจอ)"""
    notes = []
    if r["closes"]:
        n_ok = int(r["closes_alarmed"].split("/")[0]); n = len(r["closes"])
        if n_ok < n:
            missed = [c for c in r["closes"] if c["alarm_after_s"] is None]
            face_low = [c for c in missed if (c["face"] or 0) < 0.6]
            notes.append(f"หลับตาแล้วไม่เตือน {n - n_ok}/{n} ครั้ง" +
                         (f" · {len(face_low)} ครั้งหาหน้าไม่เจอเกิน 40% ของเวลา (ปัญหาหาหน้า/มุมกล้อง ไม่ใช่โมเดลตา)" if face_low else "") +
                         (f" · {len(missed) - len(face_low)} ครั้งเห็นหน้าแต่ตอบว่าตาเปิด (โมเดลตา → fine-tune ช่วยได้)" if len(missed) > len(face_low) else ""))
    if r.get("false_alarms"):
        notes.append(f"ลืมตาอยู่แต่เตือน {r['false_alarms']} ครั้ง (เวลา {r['false_alarm_times']} วิ)")
    if r.get("false_distractions"):
        notes.append(f"มองถนนอยู่แต่ขึ้น 'ไม่มองถนน' {r['false_distractions']} ครั้ง")
    if any(g["distraction"] for g in r["glances"]):
        notes.append("เหลือบมองกระจก/มือถือ 2–3 วิ แล้วขึ้น 'ไม่มองถนน' (เกณฑ์ 3 วิ อาจต้องดูท่าจริงอีกที)")
    ok = r["closes"] and r["closes_alarmed"].split("/")[0] == str(len(r["closes"])) and not r.get("false_alarms")
    return ("ผ่าน" if ok else "ยังไม่ผ่าน" if r["closes"] else "ดูตัวเลขเตือนผิด"), notes


def report(name, r=None, save=True):
    r = r or score(name)
    v, notes = verdict(r)
    if save:
        (common.DATA / name / "report.json").write_text(json.dumps(dict(r, verdict=v, notes=notes), ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"== {name} · fw {r['fw']} · {r['light']} · แว่น {r['glasses']} · fps {r['fps']} · เจอหน้า {r['face_found']}")
    if r["closes"]:
        print(f"หลับตา: เตือน {r['closes_alarmed']} · microsleep {r['closes_microsleep']} · ระหว่างหลับ Eye State = ปิด {r['closed_eye_state']} · P ซ้าย {r['closed_pL']} ขวา {r['closed_pR']}")
        for c in r["closes"]:
            print(f"   {c['seg']:<14} {c['sec']:>2} วิ  เตือนหลัง {c['alarm_after_s']} วิ  microsleep {c['microsleep_after_s']}  "
                  f"ปิดค้างสูงสุด {c['max_closed_ms']} ms  ปิด {c['eye_closed']}  หน้า {c['face']}  P {c['pL']}/{c['pR']}")
    if "open_s" in r:
        print(f"ลืมตา {r['open_s']} วิ: เตือนผิด {r['false_alarms']} · ไม่มองถนนผิด {r['false_distractions']} · "
              f"Eye State {r['open_eye_state']} · P ซ้าย {r['open_pL']} ขวา {r['open_pR']} · ระดับ 1 (เริ่มล้า) {r['open_fatigue']}"
              + (f" · เตือนผิด {r['false_per_hour']}/ชม." if r["false_per_hour"] is not None else ""))
    for g in r["glances"]:
        print(f"   {g['seg']:<14} ไม่มองถนน {g['distraction']}  เตือนง่วง {g['alarm']}")
    print(f"สรุป: {v}" + "".join(f"\n  - {n}" for n in notes))
    return r


if __name__ == "__main__":
    for n in sys.argv[1:] or common.sessions():
        report(n)
        print()
