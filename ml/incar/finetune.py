"""Fine-tune โมเดลตาด้วย MRL + ภาพในรถ แล้ว export INT8 → model_data.h (โครงข่ายเดียวกับบนบอร์ด: deep2_half, op 5 ตัวเดิม)

    python ml/incar/finetune.py --train รอบ1 รอบ2 --val รอบ3 --test รอบ4 --run 2026-11-06_a
    python ml/incar/finetune.py ... --quick       (ลองสคริปต์: 1 epoch, ข้อมูลส่วนน้อย ผลไม่มีความหมาย)

แบ่งข้อมูลตามรอบ (session) ไม่ใช่สุ่มรายภาพ: ภาพติดกันในรอบเดียวเกือบเหมือนกัน ถ้าสุ่มรายภาพ test จะสูงเกินจริง
ใช้เฉพาะป้าย open/closed (ป้ายตามท่า + แก้ด้วยมือจาก review.py) · ภาพบนโต๊ะห้ามใส่ (ผู้ใช้ตัดสิน: ใช้เฉพาะภาพในรถ)

ผล: ml/output/incar/<run>/ (ไม่ขึ้น git)
  model_int8.tflite · model_data.h (คัดลอกไปเป็น ~/Documents/Arduino/DrowsyFW/src/vision/eye_model_data.h)
  report.json · report.md  เทียบกับโมเดลบนบอร์ดตอนนี้ (อ่านจาก firmware/DrowsyFW/src/vision/eye_model_data.h) ทีละตา
  p_<รอบ>.csv  P ของทุกเฟรมในรอบ val/test ทั้งโมเดลเดิมและใหม่ สำหรับ replay.py
"""
import argparse
import datetime
import json
import math
import pathlib
import re
import sys
import tempfile
import time
import warnings

import numpy as np

import common

sys.stdout.reconfigure(encoding="utf-8")
warnings.filterwarnings("ignore")
import tensorflow as tf  # noqa: E402
import keras  # noqa: E402
from keras import layers  # noqa: E402

N = common.N
REPO = common.ML.parent
FW_MODEL = REPO / "firmware" / "DrowsyFW" / "src" / "vision" / "eye_model_data.h"
# ค่าเดียวกับ deep2_half_p8 ที่อยู่บนบอร์ด (ml/README.md "ทดลองปรับปรุง 32×32") · aug "full" ของ ml/output/experiments/exp.py
CFG = dict(filters=[8, 16, 32], convs=2, dropout=0.3, batch=128, lr=1e-3, epochs=40, patience=8)
AUG = dict(tr=0.08, zoom=(-0.15, 0.10), lowres=0.5, blur=2 / 3, gamma=(0.7, 1.4), con=(0.75, 1.3), bri=0.1, noise=0.02)


def incar(names):
    """ภาพในรถที่ใช้ได้: X uint8 [n,32,32] · y (1 = ปิด) · ref [(รอบ, เฟรม, ตา)]"""
    X, y, ref = [], [], []
    for s in names:
        _, frames, eyes, labels = common.load(s)
        for f in frames:
            for e in (0, 1):
                if labels[f["i"], e] in common.TRAINABLE:
                    X.append(eyes[f["i"], e]); y.append(1.0 if labels[f["i"], e] == "closed" else 0.0); ref.append((s, f["i"], e))
    return (np.array(X, np.uint8).reshape(-1, N, N), np.array(y, np.float32), ref)


def mrl():
    d = np.load(common.ML / "data" / "cache_mrl_32_48_64.npz")
    names = d["names"]
    split = dict(l.split(",")[:2] for l in (common.ML / "output" / "subject_split.csv").read_text().splitlines()[1:])
    sp = np.array([split[s.split("_")[0]] for s in names])
    y = np.array([1 - int(s.split("_")[4]) for s in names], np.float32)
    return {k: (d["x32"][sp == k], y[sp == k]) for k in ("train", "val", "test")}


def build(seed):
    keras.utils.set_random_seed(seed)
    inp = keras.Input((N, N, 1)); x = inp
    for f in CFG["filters"]:
        for _ in range(CFG["convs"]):
            x = layers.Conv2D(f, 3, padding="same", use_bias=False)(x)
            x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
        x = layers.MaxPooling2D()(x)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(CFG["dropout"])(x)
    return keras.Model(inp, layers.Dense(1, activation="sigmoid")(x))


def augment_fn(seed):
    jitter = keras.Sequential([layers.RandomTranslation(AUG["tr"], AUG["tr"], fill_mode="reflect", seed=seed),
                               layers.RandomZoom(AUG["zoom"], fill_mode="reflect", seed=seed + 1)])

    def gk(s, k=5):
        ax = np.arange(k) - k // 2; g = np.exp(-(ax ** 2) / (2 * s ** 2)); g = np.outer(g, g); g /= g.sum()
        return tf.constant(g[:, :, None, None], tf.float32)
    BK = [gk(0.7), gk(1.3)]

    def augment(x, yy, w):
        b = tf.shape(x)[0]
        u = lambda lo, hi: tf.random.uniform([b, 1, 1, 1], lo, hi)
        x = jitter(x, training=True)
        x = tf.where(u(0, 1) < 0.5, tf.reverse(x, axis=[2]), x)
        s = tf.random.uniform([], 14, N + 1, tf.int32)
        lo = tf.image.resize(tf.image.resize(x, [s, s], "bilinear", antialias=True), [N, N], "bilinear")
        x = tf.where(u(0, 1) < AUG["lowres"], lo, x)
        pad = tf.pad(x, [[0, 0], [2, 2], [2, 2], [0, 0]], mode="REFLECT")
        b1 = tf.nn.conv2d(pad, BK[0], 1, "VALID"); b2 = tf.nn.conv2d(pad, BK[1], 1, "VALID")
        r = u(0, 1); h = AUG["blur"] / 2
        x = tf.where(r < h, b1, tf.where(r < 2 * h, b2, x))
        x = tf.pow(tf.clip_by_value(x, 1e-6, 1.0), tf.exp(u(math.log(AUG["gamma"][0]), math.log(AUG["gamma"][1]))))
        m = tf.reduce_mean(x, axis=[1, 2, 3], keepdims=True)
        x = (x - m) * u(*AUG["con"]) + m + u(-AUG["bri"], AUG["bri"])
        x = x + tf.random.normal(tf.shape(x)) * u(0.0, AUG["noise"])
        return tf.round(tf.clip_by_value(x, 0.0, 1.0) * 255.0) / 255.0, yy, w
    return augment


def ds(X, y, w=None, train=False, seed=0):
    w = np.ones(len(X), np.float32) if w is None else w
    d = tf.data.Dataset.from_tensor_slices((X[..., None], y, w))
    if train:
        d = d.shuffle(len(X), seed=seed, reshuffle_each_iteration=True)
    d = d.batch(CFG["batch"]).map(lambda a, b, c: (tf.cast(a, tf.float32) / 255.0, b, c), num_parallel_calls=tf.data.AUTOTUNE)
    if train:
        d = d.map(augment_fn(seed), num_parallel_calls=tf.data.AUTOTUNE)
    return d.prefetch(tf.data.AUTOTUNE)


def firmware_tflite():
    """โมเดลที่อยู่บนบอร์ดตอนนี้ (ไบต์ใน eye_model_data.h)"""
    src = FW_MODEL.read_text(encoding="utf-8")
    body = src.split("g_eye_model_data[] = {")[1].split("};")[0]
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", body))


def predict_int8(tfl, X):
    """P(ปิด) จาก INT8 บน PC: input = gray − 128 เหมือน eye_to_int8 บนบอร์ด"""
    it = tf.lite.Interpreter(model_content=tfl, experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES)
    inp, out = it.get_input_details()[0], it.get_output_details()[0]
    assert abs(inp["quantization"][0] * 255 - 1) < 1e-6 and inp["quantization"][1] == -128, "input quantization ไม่ใช่ 1/255, -128"
    qs = []
    for s in range(0, len(X), 256):
        xb = (X[s:s + 256].astype(np.int16) - 128).astype(np.int8)[..., None]
        it.resize_tensor_input(inp["index"], [len(xb), N, N, 1]); it.allocate_tensors()
        it.set_tensor(inp["index"], xb); it.invoke(); qs.append(it.get_tensor(out["index"])[:, 0].copy())
    q = np.concatenate(qs) if qs else np.zeros(0)
    return (q.astype(np.float32) - out["quantization"][1]) * out["quantization"][0]


def to_int8(model, rep_X, seed):
    with tempfile.TemporaryDirectory() as tmp:
        model.export(tmp, format="tf_saved_model", verbose=False)
        rep_idx = np.random.default_rng(seed).choice(len(rep_X), min(500, len(rep_X)), replace=False)

        def rep():
            edge = np.zeros((1, N, N, 1), np.float32); edge[:, :, N // 2:] = 1.0
            yield [edge]
            for j in rep_idx:
                yield [rep_X[j][None, ..., None].astype(np.float32) / 255.0]
        conv = tf.lite.TFLiteConverter.from_saved_model(tmp)
        conv.optimizations = [tf.lite.Optimize.DEFAULT]
        conv.representative_dataset = rep
        conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        conv.inference_input_type = tf.int8
        conv.inference_output_type = tf.int8
        return conv.convert()


def header(tfl, note):
    it = tf.lite.Interpreter(model_content=tfl, experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    it.allocate_tensors()
    (in_s, in_zp), (out_s, out_zp) = it.get_input_details()[0]["quantization"], it.get_output_details()[0]["quantization"]
    assert abs(out_s * 256 - 1) < 1e-6 and out_zp == -128
    ops = sorted({op["op_name"] for op in it._get_ops_details()})
    assert set(ops) <= {"CONV_2D", "MAX_POOL_2D", "MEAN", "FULLY_CONNECTED", "LOGISTIC"}, f"op ใหม่ {ops} ต้องเพิ่มใน resolver ของ eye_classifier.cpp"
    rows = "\n".join("  " + ", ".join(f"0x{b:02x}" for b in tfl[i:i + 16]) + "," for i in range(0, len(tfl), 16))
    return f"""// Generated by ml/incar/finetune.py -- eye-state classifier, {note}, INT8 full-integer.
// Input : int8 [1,{N},{N},1] = gray - 128  (scale {in_s:.9g}, zero_point {in_zp})
// Output: int8 [1,1] -> P(closed) = (q - ({out_zp})) * {out_s:.9g}
// Ops   : {", ".join(ops)}
#pragma once
#include <stdint.h>

#define EYE_MODEL_INPUT_SIZE {N}
#define EYE_MODEL_INPUT_ZERO_POINT ({in_zp})
#define EYE_MODEL_OUTPUT_SCALE ({out_s:.9g}f)
#define EYE_MODEL_OUTPUT_ZERO_POINT ({out_zp})

alignas(16) const uint8_t g_eye_model_data[] = {{
{rows}
}};
const unsigned int g_eye_model_data_len = {len(tfl)};
"""


def mets(p, y, thr):
    """recall ตาปิด / ตาเปิดที่ทายเป็นปิด / accuracy ที่เกณฑ์ thr"""
    if not len(y):
        return None
    pr = p >= thr; c = y == 1
    return dict(n_closed=int(c.sum()), n_open=int((~c).sum()),
                closed_recall=round(float((pr & c).sum() / max(c.sum(), 1)), 4),
                open_as_closed=round(float((pr & ~c).sum() / max((~c).sum(), 1)), 4),
                acc=round(float((pr == c).mean()), 4))


def per_eye(p, y, ref, thr):
    e = np.array([r[2] for r in ref]) if ref else np.zeros(0, int)
    return {name: mets(p[e == k], y[e == k], thr) for k, name in ((0, "left"), (1, "right"))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", nargs="+", required=True)
    ap.add_argument("--val", nargs="*", default=[])
    ap.add_argument("--test", nargs="*", default=[])
    ap.add_argument("--run", default=datetime.datetime.now().strftime("%Y-%m-%d_%H%M"))
    ap.add_argument("--incar-share", type=float, default=0.25, help="สัดส่วนภาพในรถในแต่ละ epoch (สุ่มซ้ำให้ถึง)")
    ap.add_argument("--thr", type=float, nargs="+", default=[0.3, 0.5], help="เกณฑ์ที่รายงาน (บอร์ดตอนนี้ P_CLOSED_ON 0.5)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--quick", action="store_true")
    a = ap.parse_args()
    overlap = (set(a.train) & set(a.val)) | (set(a.train) & set(a.test)) | (set(a.val) & set(a.test))
    if overlap:
        sys.exit(f"รอบซ้ำกันระหว่าง train/val/test: {overlap}")
    out = common.ML / "output" / "incar" / a.run
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()

    M = mrl()
    Xi, yi, ri = incar(a.train)
    Xv, yv, rv = incar(a.val)
    Xt, yt, rt = incar(a.test)
    if not len(Xi):
        sys.exit("ไม่มีภาพในรถที่ใช้ train ได้ (ป้าย open/closed)")
    print(f"ภาพในรถ: train {len(Xi)} (ปิด {int(yi.sum())}) · val {len(Xv)} · test {len(Xt)} · MRL train {len(M['train'][0])}")
    Xm, ym = M["train"]
    if a.quick:
        r = np.random.default_rng(0); k = r.choice(len(Xm), 3000, replace=False); Xm, ym = Xm[k], ym[k]
        CFG.update(epochs=1, patience=1)
    # ภาพในรถมีน้อย → สุ่มซ้ำให้เป็น incar_share ของชุด train · ถ่วงคลาสให้ปิด/เปิดหนักเท่ากัน
    rep = max(1, round(a.incar_share * len(Xm) / ((1 - a.incar_share) * len(Xi))))
    Xtr = np.concatenate([Xm, np.repeat(Xi, rep, 0)]); ytr = np.concatenate([ym, np.repeat(yi, rep)])
    pos = ytr.mean(); wtr = np.where(ytr == 1, 0.5 / pos, 0.5 / (1 - pos)).astype(np.float32)
    # หยุด/ลด LR ตาม val ภาพในรถถ้ามี (ตรงกับงานจริงกว่า MRL val)
    Xva, yva = (Xv, yv) if len(Xv) else M["val"]
    model = build(a.seed)
    model.compile(optimizer=keras.optimizers.Adam(CFG["lr"]), loss="binary_crossentropy", metrics=[keras.metrics.AUC(name="auc")])
    cbs = [keras.callbacks.ReduceLROnPlateau(monitor="val_loss", factor=0.3, patience=2, min_lr=1e-5),
           keras.callbacks.EarlyStopping(monitor="val_loss", patience=CFG["patience"], restore_best_weights=True)]
    h = model.fit(ds(Xtr, ytr, wtr, True, a.seed), validation_data=ds(Xva, yva), epochs=CFG["epochs"], callbacks=cbs, verbose=2)

    tfl = to_int8(model, np.concatenate([Xm[:2000], Xi]), a.seed)
    base = firmware_tflite()
    res = dict(run=a.run, train=a.train, val=a.val, test=a.test, rep=rep, epochs=len(h.history["loss"]), sec=round(time.time() - t0),
               quick=a.quick, int8_bytes=len(tfl), cfg=CFG)
    Xmt, ymt = M["test"]
    if a.quick:
        Xmt, ymt = Xmt[:3000], ymt[:3000]
    res["mrl_test"] = {f"{thr}": dict(new=mets(predict_int8(tfl, Xmt), ymt, thr), board=mets(predict_int8(base, Xmt), ymt, thr)) for thr in a.thr}
    for split, X, y, ref in (("val", Xv, yv, rv), ("test", Xt, yt, rt)):
        if not len(X):
            continue
        pn, pb = predict_int8(tfl, X), predict_int8(base, X)
        res[f"incar_{split}"] = {f"{thr}": dict(new=per_eye(pn, y, ref, thr), board=per_eye(pb, y, ref, thr)) for thr in a.thr}
    # P ทุกเฟรม (รวมท่าที่ไม่เข้า train) ของรอบ val/test → replay.py
    for s in a.val + a.test:
        _, frames, eyes, _ = common.load(s)
        if not frames:
            continue
        flat = eyes.reshape(-1, N, N)
        pn, pb = predict_int8(tfl, flat).reshape(-1, 2), predict_int8(base, flat).reshape(-1, 2)
        lines = ["i,new_L,new_R,board_model_L,board_model_R"]
        for f in frames:
            v = [((f["valid"] >> e) & 1) for e in (0, 1)]
            fmt = lambda p, e: f"{p[f['i'], e]:.4f}" if v[e] else "-1"
            lines.append(f"{f['i']},{fmt(pn, 0)},{fmt(pn, 1)},{fmt(pb, 0)},{fmt(pb, 1)}")
        (out / f"p_{s}.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")

    (out / "model_int8.tflite").write_bytes(tfl)
    (out / "model_data.h").write_text(header(tfl, f"MRL + in-car {a.run}"), encoding="utf-8")
    (out / "report.json").write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    md = [f"# fine-tune {a.run}", "", f"train {a.train} · val {a.val} · test {a.test} · {res['epochs']} epoch · {res['sec']} วิ" + (" · **quick (ผลไม่มีความหมาย)**" if a.quick else ""), "",
          "| ชุด | เกณฑ์ | ตา | recall ตาปิด ใหม่ / บนบอร์ด | ตาเปิดทายเป็นปิด ใหม่ / บนบอร์ด |", "|---|---|---|---|---|"]
    for thr in a.thr:
        m = res["mrl_test"][f"{thr}"]
        md.append(f"| MRL test | {thr} | ทั้งคู่ | {m['new']['closed_recall']} / {m['board']['closed_recall']} | {m['new']['open_as_closed']} / {m['board']['open_as_closed']} |")
        for split in ("val", "test"):
            for eye, v in res.get(f"incar_{split}", {}).get(f"{thr}", {}).get("new", {}).items():
                b = res[f"incar_{split}"][f"{thr}"]["board"][eye]
                if v:
                    md.append(f"| ในรถ {split} | {thr} | {eye} | {v['closed_recall']} / {b['closed_recall']} | {v['open_as_closed']} / {b['open_as_closed']} |")
    md += ["", f"ใส่บอร์ด: คัดลอก `{out / 'model_data.h'}` ไปเป็น `~/Documents/Arduino/DrowsyFW/src/vision/eye_model_data.h` → เปลี่ยน FW_VERSION → compile → แฟลช",
           f"ก่อนแฟลช: `python ml/incar/replay.py <รอบ test> --run {a.run}` ดูว่าเตือนครบ/ไม่เตือนผิด"]
    (out / "report.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(md))


if __name__ == "__main__":
    main()
