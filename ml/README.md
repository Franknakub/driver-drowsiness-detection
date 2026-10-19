# ML

ML ทำงาน 2 ขั้น

1. **หาหน้าและตำแหน่งตา** ใช้ `HumanFaceDetect` ของ ESP-DL (MSR+MNP) ได้จุดตาซ้าย/ขวามาด้วย ไม่ต้องใช้ ESP-IDF เพราะ Arduino core 3.3.12 มี `esp-dl` 3.3.11 มาให้แล้ว ทดลองบนบอร์ดผ่านแล้ว 16–17 ต.ค. (ดู `docs/firmware-plan.html` ข้อ 4)
2. **ตัดสินตาเปิด/ปิด** ตัด Eye Region รอบจุดตา → grayscale → ย่อ/ขยายเป็น N×N → CNN เล็ก INT8 ที่ train เองด้วย Keras → TFLite Micro (`esp-tflite-micro` 1.4.0 มากับ core) → P(ตาปิด)

## ไฟล์

| ไฟล์ | หน้าที่ |
|---|---|
| `train.ipynb` | pipeline ทั้งหมด: ดาวน์โหลด MRL → แบ่งตาม subject → preprocess → train 32/48/64 → INT8 → `model_data.h` + `golden_data.h` |
| `preprocess.py` | preprocess ฝั่ง PC (integer ล้วน) ใช้ทั้งตอน train และตอนสร้าง golden test |
| `eye_preprocess.h` | preprocess ฝั่ง ESP32 ตัวเดียวกัน (C, header-only) — คัดลอกไปใส่ใน sketch |
| `test_preprocess.py` | compile `eye_preprocess.h` ด้วย gcc แล้วเทียบกับ `preprocess.py` ว่าได้ byte เดียวกัน |
| `golden_test/golden_test.ino` | สเก็ตช์รัน golden test บนบอร์ด (คัดลอก `eye_preprocess.h`, `model_data.h`, `golden_data.h` มาไว้ข้างกันก่อน) · compile ผ่านกับ core 3.3.12 แล้ว ยังไม่ได้แฟลช |
| `requirements.txt` | เวอร์ชันแพ็กเกจที่ใช้รันในเครื่อง |

ผลอยู่ที่ `output/` (ไม่ commit): `train_executed.ipynb` (notebook พร้อม output ของรอบล่าสุด), `size_N/model_int8.tflite`, `size_N/model_data.h`, `size_N/golden_data.h`, `size_N/model_float.keras`, `size_comparison.csv`, `test_by_group.csv`, `results.json`, `subject_split.csv`, รูป `samples.png` / `augment.png` / `curves.png`

## วิธีรัน

**Colab:** อัปโหลด `train.ipynb` → Runtime → Run all · cell แรก clone repo branch `REPO_BRANCH` (ตั้งไว้ `firmware`) เพื่อเอา `preprocess.py` ถ้า branch นั้นยังไม่มีไฟล์นี้ ให้อัปโหลด `preprocess.py` ขึ้น Colab เองก่อน · dataset ดาวน์โหลดให้อัตโนมัติ

**ในเครื่อง (Windows, Python 3.12):**

```bash
cd ml
uv venv --python 3.12 .venv
uv pip install --python .venv/Scripts/python.exe -r requirements.txt
.venv/Scripts/python.exe -m ipykernel install --sys-prefix --name ddd-ml
.venv/Scripts/python.exe test_preprocess.py
.venv/Scripts/python.exe -m nbconvert --to notebook --execute --inplace train.ipynb --ExecutePreprocessor.kernel_name=ddd-ml --ExecutePreprocessor.timeout=-1
```

ตั้ง `EYE_QUICK=1` ก่อนรันเพื่อลองแบบเร็ว (1 epoch, ชุดละ 3,000 ภาพ) · TensorFlow บน Windows ไม่ใช้ GPU (ตั้งแต่ 2.11) จึง train ด้วย CPU

## Dataset

| Dataset | ใช้ทำอะไร |
|---|---|
| [MRL Eye Dataset](https://mrl.cs.vsb.cz/eyedataset.html) | **ตัวหลัก** ภาพตา IR grayscale 84,898 ภาพ จาก 37 คน ถ่ายในบริบทขับรถ · ไฟล์ `https://mrl.cs.vsb.cz/data/eyedataset/mrlEyes_2018_01.zip` (341,866,898 bytes, ตรวจ 17 ต.ค. 2026) · ภาพจัตุรัส 50–270 px ส่วนใหญ่ 60–130 px · ปิด 41,946 / เปิด 42,952 |
| [CEW](https://parnec.nuaa.edu.cn/_upload/tpl/02/db/731/template731/pages/xtan/ClosedEyeDatabases.html) | ตัวเสริม ภาพตากลางวัน 24×24 (ยังไม่ได้ใช้) |
| ภาพจากกล้องเราเอง | **ต้องมี** สำหรับ fine-tune ราว 300–500 ภาพต่อ class ทั้งกลางวัน กลางคืน ใส่/ไม่ใส่แว่น |

ชื่อไฟล์ MRL: `subject_image_gender_glasses_eyestate_reflections_lighting_sensor` โดย `eyestate` 0 = ปิด, 1 = เปิด · โมเดลใช้ target `closed = 1 − eyestate`

- **แบ่ง train/val/test ตาม subject ID** ไม่ใช่สุ่มรายภาพ (seed 42, test ≥15% ของภาพ, val ≥15%) รายชื่ออยู่ใน `output/subject_split.csv`
- ถ้าใช้ MRL ในรายงาน ให้อ้างอิง R. Fusek, *"Pupil localization using geodesic distance"*, ISVC 2018

## Preprocess — ต้องเหมือนกันทุกขั้นระหว่าง PC กับบอร์ด

ทุกขั้นเป็นเลขจำนวนเต็ม `preprocess.py` กับ `eye_preprocess.h` ให้ผลตรงกันทุก byte (`test_preprocess.py`: 300 crop สุ่มขนาด 8–199 px → 32/48/64 ผ่านหมด)

| ขั้น | สูตร | บอร์ด (`eye_preprocess.h`) | PC (`preprocess.py`) |
|---|---|---|---|
| 1. grayscale | ขยาย RGB565 เป็น 8 บิต (`r8 = r5<<3 \| r5>>2`, `g8 = g6<<2 \| g6>>4`, `b8` แบบเดียวกับ r) แล้ว `gray = (77·r8 + 150·g8 + 29·b8 + 128) >> 8` | `eye_rgb565_to_gray(px)` | `rgb565_to_gray` |
| 2. resize | bilinear จุดกึ่งกลางพิกเซล ทศนิยม 8 บิต: `s = ((2d+1)·src·256) / (2·dst) − 128` clamp `[0, (src−1)·256]` · `i0 = s>>8`, `f = s&255`, `i1 = min(i0+1, src−1)` · ผลรวมถ่วง 4 จุด `+32768 >> 16` | `eye_resize_bilinear(src, w, h, stride, dst, N, N)` | `resize_bilinear` |
| 3. normalize | `int8 = gray − 128` (= quantize ของ `gray/255` ที่ scale 1/255, zero_point −128) | `eye_to_int8` | `to_int8` |

- ภาพ MRL เป็น grayscale อยู่แล้ว จึงเริ่มที่ขั้น 2 · บนบอร์ดเริ่มที่ขั้น 1
- **ลำดับ byte ของ RGB565:** frame buffer ของ esp32-camera เก็บ byte สูงก่อน → `px = (buf[2i] << 8) | buf[2i+1]` · ถ้าสลับผิด golden test ผ่าน แต่ภาพสดจะเพี้ยน ให้ตรวจโดยดู crop ที่บอร์ดส่งออกมา
- `stride` ของ `eye_resize_bilinear` ใช้อ่าน crop ตรงจากเฟรมใหญ่ได้ โดยไม่ต้องคัดลอก (แปลงเฟรมเป็น gray ก่อน หรือแปลงเฉพาะช่วง crop)
- ค่า quantization ของ input/output ฝังอยู่ใน `model_data.h` และ notebook assert ไว้ว่าเป็น 1/255, −128 จริง ถ้า train ใหม่แล้ว assert ไม่ผ่าน ห้ามใช้สูตร `gray − 128`
- output: `P(ตาปิด) = (q + 128) / 256`

### ขนาด Eye Region

ภาพ MRL ตัดมาให้ตาอยู่กลางภาพ กว้างราว 55–70% ของภาพ มีขอบคิ้วติดมาบางภาพ (ดู `output/samples.png`) ตาคนกว้างประมาณ 0.47 เท่าของระยะระหว่างรูม่านตา จึงตั้งต้นที่ **ด้านของ Eye Region ≈ 0.8 × ระยะระหว่างจุดตาสองข้าง** โดยให้จุดตาอยู่กลางกรอบ ค่านี้เป็นค่าประมาณ ต้องเทียบภาพ crop จริงกับ `samples.png` ก่อนใช้ · ที่ระยะ 40–60 ซม. จุดตาห่างกัน ~30 px บน QVGA ได้ crop ~24 px (golden test จึงใช้ crop 24×24)

## Golden test (ยังไม่ได้รันบนบอร์ด)

`output/size_N/golden_data.h` มีภาพ test 16 ภาพ (ปิด 8 เปิด 8) ย่อเป็น crop gray 24×24 พร้อมค่าที่ PC คาดไว้ บนบอร์ดให้ทำทีละภาพ:

1. `eye_resize_bilinear(g_golden_crops[i], 24, 24, 24, tmp, N, N)` → `eye_to_int8(tmp, input->data.int8, N*N)`
2. FNV-1a 32 บิตของ input tensor ต้องเท่ากับ `g_golden_input_fnv1a[i]` → ถ้าไม่เท่า preprocess ไม่ตรง
3. `Invoke()` แล้วเทียบ `output->data.int8[0]` กับ `g_golden_expected_q[i]` (คำนวณด้วย kernel reference) → ยอมให้ต่าง **±20 LSB** (≈ ±0.08 ของ P)

ทำไมไม่ใช่ ±1: บน PC เอง kernel reference กับ optimized ให้ผลต่างกัน 1–2 LSB ทุก op ก่อนหน้า แต่ LOGISTIC ขยาย 1 LSB ของ logit เป็น 6–9 LSB ช่วง P ≈ 0.5 (วัดแล้ว: base 32px ต่างสูงสุด 6, 48px 9, 64px 7, deep2 7, deep2_half 15) · bug จริงแบบ DrowsyFW ตัวเก่า (P = 1.00 ทุกเฟรม) จะต่างเป็นร้อย LSB จึงถูกจับได้ชัดเจน

ใช้ `golden_test/golden_test.ino` ได้เลย จะพิมพ์ arena ที่ใช้จริง เวลา inference และผลเทียบทีละภาพ

## ผล (รันในเครื่อง 17 ต.ค. 2026, CPU, TF 2.21 / Keras 3.15, seed 42)

**แบ่งข้อมูล:** train 18 คน 51,456 ภาพ · val 12 คน 13,109 ภาพ · test 7 คน 20,333 ภาพ (24% — คนที่ s0037 มีภาพเยอะ 10,257 ภาพ ทำให้ test เกิน 15%) · test ทั้งหมดมาจากเซนเซอร์ RealSense (`sensor=01`)

**เทียบขนาดภาพเข้า** — วัดบน test (คนที่ไม่เคยเห็น) threshold 0.5 · โครงข่ายเดียวกันทุกขนาด (23,697 พารามิเตอร์)

| ภาพเข้า | float acc | **INT8 acc** | **INT8 recall ตาปิด** | INT8 precision ตาปิด | INT8 AUC | MACs | `.tflite` | activation สูงสุด* | train |
|---|---|---|---|---|---|---|---|---|---|
| **32×32** | 94.19% | **94.07%** | **90.94%** | 97.92% | 0.989 | 2.51 M | 30.6 KB | 20 KB | 18 epoch, 2.4 นาที |
| 48×48 | 88.59% | 88.84% | 81.21% | 97.69% | 0.977 | 5.64 M | 30.6 KB | 45 KB | 16 epoch, 4.8 นาที |
| 64×64 | 90.44% | 89.33% | 84.28% | 95.37% | 0.964 | 10.03 M | 30.6 KB | 80 KB | 20 epoch, 9.9 นาที |

\* in+out ของ op ที่ใหญ่สุด = ขั้นต่ำของ tensor arena · ค่าจริงให้อ่านจาก `golden_test.ino` · `model_data.h` เป็นข้อความ 189 KB แต่ใช้ flash จริงแค่ ~31 KB

Confusion matrix INT8 32×32 บน test: ตาปิดทายถูก 9,963 / พลาดเป็นเปิด 993 · ตาเปิดทายถูก 9,165 / พลาดเป็นปิด 212

**แยกกลุ่ม (INT8, accuracy / recall ตาปิด)**

| กลุ่มใน test | ภาพ | 32×32 | 48×48 | 64×64 |
|---|---|---|---|---|
| ไม่ใส่แว่น | 13,506 | 96.5% / 93.9% | 94.0% / 88.9% | 93.1% / 87.9% |
| ใส่แว่น | 6,827 | 89.3% / 86.2% | 78.6% / 68.9% | 81.9% / 78.5% |
| แสงสะท้อนมาก | 1,386 | 86.7% / 83.5% | 74.3% / 53.0% | 77.4% / 74.3% |
| s0037 (ครึ่งหนึ่งของ test) | 10,257 | 90.7% / 85.0% | 83.6% / 72.9% | 83.6% / 74.8% |
| s0019 | 6,175 | 99.9% / 100% | 99.9% / 100% | 99.3% / 100% |

ตารางเต็มอยู่ใน `output/test_by_group.csv`

**สรุปเพื่อเลือก:** 32×32 ดีที่สุดทุกด้านในรอบนี้ ทั้งแม่นกว่า ใช้ MAC น้อยกว่า 64×64 4 เท่า และ arena เล็กกว่า ขนาดนี้ยังใกล้กับตาจริงบน QVGA (~20–30 px) จึงแทบไม่ต้องขยายภาพ ส่วน 48/64 แย่กว่า เหตุผลที่น่าจะเป็นคือโครงข่ายเดียวกันมองเห็นได้กว้างแค่ส่วนเล็กของตาเมื่อภาพใหญ่ขึ้น (เป็นข้อสันนิษฐาน ยังไม่ได้ทดลองเพิ่มชั้นเพื่อพิสูจน์)

ข้อควรระวัง
- train รอบเดียว seed เดียว ยังไม่ได้วัดว่าผลแกว่งแค่ไหนเมื่อเปลี่ยน seed
- recall ตาปิด (90.9%) ต่ำกว่า precision (97.9%) ถ้าอยากจับตาปิดได้มากขึ้นให้ลด threshold ลงจาก 0.5 โดยเลือกจาก val หรือจากภาพของเราเอง
- ยังไม่มีผลบนภาพจาก OV5640 ซึ่ง MRL เป็นภาพ IR · ใส่แว่นและแสงสะท้อนเป็นจุดอ่อนชัดเจน → ต้อง fine-tune ด้วยภาพจริง
- INT8 ทำให้ accuracy ลดลงแค่ 0.1 จุด (32×32) · op ที่ใช้มีแค่ `CONV_2D, MAX_POOL_2D, MEAN, FULLY_CONNECTED, LOGISTIC` (resolver 5 ตัว)
- ยังไม่ได้วัดเวลา inference บนบอร์ด

### ทดลองปรับปรุง 32×32 (17 ต.ค. — เสร็จแล้ว)

รัน 12 แบบ (seed 42) แล้วเลือก 4 แบบมารัน seed ละ 3 รอบ + ทดลองต่ออีก 3 แบบ ผลดิบอยู่ใน `output/experiments/` (`exp_results.jsonl`, `seed_summary.csv`, `RESUME.md`)

**INT8 บน test (7 คน 20,333 ภาพ) threshold 0.5 · ค่าเฉลี่ย ± sd ข้าม seed**

| config | สิ่งที่เปลี่ยนจาก base | รอบ | accuracy | recall ตาปิด | AUC | params | MACs | INT8 |
|---|---|---|---|---|---|---|---|---|
| base | — | 3 | 93.60 ± 0.71% | 91.27 ± 0.93% | 0.984 | 23.7 k | 2.51 M | 30.5 KB |
| base_p8 | EarlyStopping patience 8 | 2 | 93.65 ± 0.20% | 90.71 ± 0.27% | 0.986 | 23.7 k | 2.51 M | 30.5 KB |
| deep2_half | 2 conv ต่อชั้น, filter 8/16/32 | 3 | 95.21 ± 0.64% | 92.16 ± 1.19% | 0.994 | 18.4 k | 2.43 M | 27.5 KB |
| **deep2_half_p8** | deep2_half + patience 8 | 2 | **96.24 ± 0.07%** | **94.32 ± 0.37%** | **0.995** | 18.4 k | **2.43 M** | **27.5 KB** |
| deep2 | 2 conv ต่อชั้น, filter 16/32/64 | 3 | 96.41 ± 0.15% | 94.23 ± 0.33% | 0.995 | 72.5 k | 9.58 M | 83.0 KB |
| deep2_p8 | deep2 + patience 8 | 2 | 96.14 ± 0.56% | 93.71 ± 1.23% | 0.994 | 72.5 k | 9.58 M | 83.0 KB |
| mon_auc | EarlyStopping ดู val AUC | 3 | 94.38 ± 1.23% | 93.49 ± 4.58% | 0.989 | 23.7 k | 2.51 M | 30.5 KB |
| ls01 | label smoothing 0.1 | 3 | 93.25 ± 1.93% | 90.63 ± 5.03% | 0.985 | 23.7 k | 2.51 M | 30.5 KB |

แบบอื่น (seed เดียว ไม่ได้รันซ้ำ):
- augmentation เบา / แรง / ไม่จำลองภาพเล็ก
- กว้าง 32/64/128
- LR แบบ cosine
- depthwise separable
- 4 ชั้นที่ 48 / 64 px — 64 px ได้ 96.3% แต่ใช้ 14.8 M MACs

**แนะนำสำหรับบอร์ด: `deep2_half_p8`** (`output/size_32_deep2half/`)
- **แม่นกว่า base ชัดเจน:** +2.6 จุด accuracy, +3 จุด recall ตาปิด, AUC 0.984 → 0.995 · ห่างกันเกิน 3 เท่าของ sd
- **ต้นทุนไม่เพิ่ม:** MAC น้อยกว่า base เล็กน้อย (2.43 M vs 2.51 M) · ไฟล์เล็กกว่า · op ชุดเดิม 5 ตัว · ถ้า MAC ใกล้กันเวลาบนบอร์ดน่าจะใกล้ ~21 ms ต่อตาเดิม (ยังไม่ได้วัด)
- **ส่วนที่ได้มาเพิ่มมาจากไหน:** โครงข่าย 2 conv ต่อชั้นเป็นตัวหลัก (patience 8 ไม่ช่วย base เลย)
- **deep2 เต็มไม่คุ้ม:** แม่นเท่ากันแต่ใช้ MAC เกือบ 4 เท่า

**ไฟล์ที่ export:**
- `output/size_32_deep2half/` และ `output/size_32_deep2/` มี `model_data.h`, `golden_data.h`, `model_int8.tflite`
- ทั้งสองชุด compile กับ `golden_test.ino` ผ่าน
- `output/size_32/` (base) ยังเป็นตัวเดิม ไม่เขียนทับ
- ตัวที่ export ของ deep2_half_p8 คือ seed 2 (เลือกด้วย val AUC 0.9931): test 96.19%, recall ตาปิด 94.06%, AUC 0.9955
- สคริปต์: `output/experiments/make_headers.py`

**ข้อควรระวัง:**
- **ผลแกว่งระหว่างรอบ:** train บน CPU ไม่ deterministic · base รอบ notebook ได้ 94.1% แต่ 3 seed ใหม่เฉลี่ย 93.6% · ตาราง "เทียบขนาดภาพเข้า" ข้างบนเป็นผลรอบเดียว
- **EarlyStopping อาจหยุดเร็วเกิน:** deep2 รอบหนึ่งหยุดที่ epoch 6 ได้แค่ 94.1% ทั้งที่ val AUC 0.993 เท่ารอบดี → val AUC ใช้คัดรอบแย่ไม่ได้ ต้องดูจำนวน epoch ด้วย
- **val ไม่สอดคล้องกับ test** (ดู `output/threshold_note.md` ด้วย)
- **golden test:** deep2_half ปัดเศษ ref กับ optimized ต่างได้ถึง 15 LSB จึงขยายเกณฑ์ใน `golden_test.ino` เป็น ±20
- **notebook ยังไม่ได้อัปเดต:** `train.ipynb` ยังสร้างแค่ base 16/32/64 ถ้าจะ train ตัวที่แนะนำจาก notebook ต้องเพิ่ม `convs=2`, filter 8/16/32, patience 8
