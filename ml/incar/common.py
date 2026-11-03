"""ของที่สคริปต์ใน ml/incar/ ใช้ร่วมกัน: รูปแบบ /eyes.raw, ท่าทางของรอบเก็บภาพ, อ่าน/เขียนข้อมูลรอบ

ข้อมูลหนึ่งรอบ (session) อยู่ที่ ml/data/incar/<ชื่อรอบ>/  (ml/data/ อยู่ใน .gitignore: ภาพหน้า/ตาห้ามขึ้น git)
  session.json   ข้อมูลรอบ: คน แสง แว่น วันที่ เวอร์ชัน firmware ท่าทางที่ใช้
  status.jsonl   /api/status ราว 5 ครั้ง/วิ: t level closed_ms eye_state p_* face distraction ... seg cue (cartest.py)
  frames.jsonl   หนึ่งบรรทัดต่อเฟรมจาก /eyes.raw: i seq ms t pL pR valid dist seg cue (มีเมื่อ firmware มี /eyes.raw)
  eyes.npy       uint8 [เฟรม, 2, 32, 32] ภาพที่เข้าโมเดลจริง (0 = ซ้ายของภาพ, 1 = ขวาของภาพ)
  labels.json    ป้ายที่แก้ด้วยมือจาก review.py: {"<i>:<ตา>": "open" | "closed" | "drop"}
"""
import json
import pathlib
import struct

import numpy as np

ML = pathlib.Path(__file__).resolve().parents[1]
DATA = ML / "data" / "incar"
N = 32
RAW_LEN = 24 + 2 * N * N

# ป้ายของท่า: open/closed เข้า train ได้ · glance (เหลือบมอง) squint (ตาปรือ) blink (กะพริบตามปกติ) drive (ขับจริง) ไม่เข้า train
# แต่ใช้วัดผลและเล่นซ้ำ · ทุกท่าตัดภาพช่วงเปลี่ยนท่าทิ้ง (MARGIN_START / MARGIN_END วินาที)
TRAINABLE = ("open", "closed")
MARGIN_START, MARGIN_END = 1.0, 0.3

# (ชื่อท่า, ป้าย, วินาที, คำพูด (เสียงอังกฤษ เครื่องนี้ไม่มีเสียงไทย), ข้อความไทยบนจอ)
PROTOCOLS = {
    # ทดสอบในรถตอนจอด (cartest.py) และได้ภาพไว้ fine-tune ไปด้วยถ้าบอร์ดมี /eyes.raw
    # ช่วง A: ไม่มีการหลับตาเลย ใช้นับการเตือนผิด (PERCLOS ยังไม่ถูกดันขึ้นจากการหลับตา)
    # ช่วง B: หลับตา 5 วิ 5 ครั้ง + 8 วิ 1 ครั้ง ใช้นับว่าเตือนครบไหม และเตือนเร็วแค่ไหน
    "cartest": [
        ("A_road", "open", 30, "Look at the road", "มองถนนตรงหน้า ลืมตาปกติ"),
        *[step for m, th in (("left", "ซ้าย"), ("right", "ขวา"), ("rear", "หลัง")) for step in (
            (f"A_glance_{m}", "glance", 2, f"Glance {m} mirror", f"เหลือบมองกระจก{th} แล้วกลับมามองถนน"),
            ("A_road", "open", 6, "Road", "มองถนน"))],
        ("A_phone", "glance", 3, "Glance at phone", "ก้มดูมือถือแป๊บเดียว"),
        ("A_road", "open", 10, "Road", "มองถนน"),
        ("A_blink", "blink", 40, "Blink normally", "มองถนน กะพริบตาตามปกติ"),
        ("A_squint", "squint", 10, "Squint", "หรี่ตา ตาปรือ"),
        ("A_road", "open", 15, "Road", "มองถนน ลืมตาปกติ"),
        *[step for k in range(5) for step in (
            ("B_close", "closed", 5, "Close your eyes", "หลับตาค้างไว้"),
            ("B_open", "open", 10, "Open", "ลืมตา มองถนน"))],
        ("B_close_long", "closed", 8, "Close your eyes, long", "หลับตาค้างไว้นานหน่อย"),
        ("B_end", "open", 8, "Open. Done", "ลืมตา เสร็จรอบนี้"),
    ],
    # ขับจริง: ไม่มีเสียงบอก ลืมตาตลอด ใช้นับการเตือนผิดต่อชั่วโมง · ไม่เข้า train โดยไม่ผ่านการตรวจป้าย
    "drive": [("drive", "drive", 4 * 3600, "Recording drive", "กำลังขับ บันทึกต่อเนื่อง (กด Ctrl+C เพื่อหยุด)")],
}
SAFETY = {"cartest": True, "drive": False}   # ท่าที่มีการหลับตา ต้องยืนยันว่ารถจอดอยู่


def parse_raw(b: bytes):
    """ถอด /eyes.raw (รูปแบบอยู่ที่ firmware/DrowsyFW/vision.cpp eyeRawStore)"""
    if len(b) != RAW_LEN or b[:4] != b"EYR1":
        raise ValueError(f"not an EYR1 frame ({len(b)} bytes)")
    seq, ms, pL, pR, dist, valid, n = struct.unpack_from("<IIffHBB", b, 4)
    if n != N:
        raise ValueError(f"eye size {n} != {N}")
    eyes = np.frombuffer(b, np.uint8, 2 * N * N, 24).reshape(2, N, N).copy()
    return dict(seq=seq, ms=ms, pL=round(pL, 4), pR=round(pR, 4), dist=dist, valid=valid), eyes


def make_raw(seq, ms, pL, pR, dist, valid, eyes) -> bytes:
    """ประกอบเฟรม /eyes.raw (ใช้ใน fake_board.py)"""
    return b"EYR1" + struct.pack("<IIffHBB", seq, ms, pL, pR, dist, valid, N) + np.asarray(eyes, np.uint8).tobytes()


def segments(protocol, rounds=1):
    """รายการท่าตามเวลา: [(t0, t1, ชื่อ, ป้าย, คำพูด, ข้อความไทย)] เริ่มที่ 0 วินาที"""
    out, t = [], 0.0
    for r in range(rounds):
        for name, label, sec, say, thai in PROTOCOLS[protocol]:
            out.append((t, t + sec, f"{name}#{r + 1}" if rounds > 1 else name, label, say, thai))
            t += sec
    return out


def load(session):
    """อ่านรอบ: (meta, frames, eyes, labels) · labels = ป้ายสุดท้ายต่อ (เฟรม, ตา) หลังรวมการแก้ด้วยมือ"""
    d = DATA / session if not pathlib.Path(session).is_dir() else pathlib.Path(session)
    meta = json.loads((d / "session.json").read_text(encoding="utf-8"))
    frames = [json.loads(l) for l in (d / "frames.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
    eyes = np.load(d / "eyes.npy") if frames else np.zeros((0, 2, N, N), np.uint8)
    edits = json.loads((d / "labels.json").read_text(encoding="utf-8")) if (d / "labels.json").exists() else {}
    labels = np.empty((len(frames), 2), object)
    for f in frames:
        for e in (0, 1):
            lab = f["cue"] if (f["valid"] >> e) & 1 else "drop"
            labels[f["i"], e] = edits.get(f"{f['i']}:{e}", lab)
    return meta, frames, eyes, labels


def sessions():
    return sorted(p.name for p in DATA.iterdir() if (p / "session.json").exists()) if DATA.exists() else []
