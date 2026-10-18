# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **repo นี้เป็น public** (<https://github.com/Franknakub/driver-drowsiness-detection>) และเป็น**สำเนาที่จัดระเบียบแล้ว**ของโฟลเดอร์งานชั้นบน (`../`) ไม่ใช่ต้นฉบับทั้งหมด
> - ต้นฉบับ PCB อยู่ใน **EasyEDA Pro cloud** — `.epro2` และ Gerber ในนี้เป็นแค่ export
> - ต้นฉบับสเก็ตช์อยู่ที่ `~/Documents/Arduino/<ชื่อสเก็ตช์>/` — แก้และ build ที่นั่น แล้ว copy เข้า `firmware/` (ยกเว้น `secrets.h` และ `build/`)
> - `../CLAUDE.md` ละเอียดกว่า (การต่อ EasyEDA API, สถานะล่าสุด, ประเด็นค้าง, กับดัก USB) อ่านคู่กันเสมอ
> - งาน firmware/ML/กล่องอยู่บน branch **`firmware`** · คำศัพท์ใช้ตาม [GLOSSARY.md](GLOSSARY.md) · การตัดสินใจหลักอยู่ใน [docs/adr/](docs/adr/) · แผนและผลทดลองอยู่ใน [docs/firmware-plan.html](docs/firmware-plan.html)

## ก่อน commit

- ใช้ noreply email ที่ตั้งไว้เฉพาะ repo นี้แล้ว (`git config user.email`) อย่าเปลี่ยน
- **ห้ามใส่:** `EasyEDALisenceFree.txt`, ชื่อ/รหัสนักศึกษา, `secrets.h` (WiFi/รหัส OTA — อยู่ใน `.gitignore`), ภาพหน้าคนที่เก็บมาทำ dataset
- `ml/data/`, `ml/output/`, `build/`, `*.bin`/`*.elf` อยู่ใน `.gitignore`
- ไฟล์ผลิตชุดใหม่ใส่ `hardware/fabrication/<YYYY-MM-DD>_vN/` แล้วย้ายชุดเดิมเข้า `archive/`

## Firmware — build / flash

ไม่มี lint หรือ unit test บนบอร์ด · การตรวจคือ compile ผ่าน แล้วดู serial / `GET /api/status` จากบอร์ดจริง

```bash
CLI="$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom"   # DrowsyFW
S=~/Documents/Arduino/DrowsyFW
"$CLI" compile --fqbn "$FQBN" --export-binaries $S
"$CLI" upload -p COM6 --fqbn "$FQBN" --input-dir $S/build/esp32.esp32.esp32s3 $S
```

- core `esp32 by Espressif Systems` **3.3.12** · ไลบรารี: `Async TCP` + `ESP Async WebServer` (ของ ESP32Async) · `esp-dl`, `esp-tflite-micro`, `esp-nn` มากับ core แล้ว ไม่ต้องลงเพิ่ม
- **`upload` ไม่ compile ให้** ต้อง compile ก่อนทุกครั้ง ไม่งั้นแฟลชไบนารีเก่า (`No changed sectors found`)
- **`PartitionScheme=custom`** ใช้ `partitions.csv` ในโฟลเดอร์สเก็ตช์ (app 6 MB × 2 + FFat ~3.9 MB) · arduino-cli จะเช็คขนาดเทียบ 16 MB ไม่ใช่ 6 MB ต้องดูขนาดเอง (ตอนนี้ ~3.4 MB) · สเก็ตช์ทดสอบอื่น (`DrowsinessDiag`, `BenchTest`) ใช้ `PartitionScheme=app3M_fat9M_16MB`
- **`PSRAM=opi` บังคับ** (N16R8 = Octal PSRAM)
- ต้องมี `secrets.h` (คัดลอกจาก `secrets.example.h`) · **อย่าเปิดอ่าน `secrets.h` ของผู้ใช้** ให้แก้เฉพาะบรรทัดที่สั่ง
- แฟลชผ่าน **USB ของโมดูล** เท่านั้น (USB-C บน PCB มีแค่ไฟ) · ลำดับเข้า download mode, VID:PID และ error ที่เจอบ่อยอยู่ใน [firmware/README.md](firmware/README.md)
- **OTA:** อัปโหลด `build/.../DrowsyFW.ino.bin` ที่ `http://<ip>/update` (user `admin`, รหัส `OTA_PASSWORD`) · ผู้ใช้ต้องใส่รหัสเอง
- บอร์ดต่อ hotspot iPhone ได้ต่อเมื่อเปิด **Maximize Compatibility** (2.4 GHz) · AP ของบอร์ด `Drowsy-XXXX` → `192.168.4.1`
- เปลี่ยน `FW_VERSION` ใน `config.h` ทุกครั้งที่แฟลช เพื่อยืนยันจาก `/api/status` ว่าบอร์ดรันตัวใหม่

## สถาปัตยกรรม DrowsyFW

```
กล้อง OV5640 (RGB565 HVGA) → หมุนภาพ 270° (+ ชดเชยหน้าเอียงตามมุมเส้นตา)
  → ESP-DL HumanFaceDetect (MSR+MNP, score thr 0.30) → จุดตา 2 ข้าง
  → Eye Region ด้าน 0.8 × ระยะระหว่างตา → gray → 32×32 → int8 → CNN (TFLite Micro)
  → P(ตาปิด) เฉลี่ยสองข้าง → Eye State (ปิด ≥ 0.5 · เปิด < 0.3 · ระหว่างนั้น = ไม่แน่ใจ)
```

- **แบ่ง task ตาม core:** Core 1 = `vision` / `alert` / `sensor` (LDR→IR) · Core 0 = `net` (AP ตลอด + ลอง WiFi ที่บันทึกไว้ + NTP + mDNS), `log`, `sync`, เว็บ (AsyncWebServer ทำงานบน task ของ AsyncTCP)
- task คุยกันผ่าน `SharedState` + mutex (`shared_state.h`) อ่านด้วย `stateSnapshot()` เขียนใน `StateGuard` · **ห้ามส่ง camera `fb` ข้าม core** — ภาพสดใช้การคัดลอกเข้า buffer แยก (`snapshotStore` ใน `vision.cpp`)
- ค่าคงที่และเกณฑ์ทั้งหมดอยู่ใน `config.h` (เกณฑ์เวลาเป็นมิลลิวินาที ไม่นับเป็นเฟรม)
- `face_eye.cpp` = หาหน้า + หมุนภาพ (ย้ายมาจากสเก็ตช์ทดลอง FaceSpike) · `human_face_detect*.{hpp,cpp}` + `human_face_detect_model.cpp` = โค้ดและโมเดลจาก esp-dl v3.3.11 (MIT) · `eye_classifier.cpp` + `eye_preprocess.h` + `eye_model_data.h` = โมเดลตา
- **endpoint ดีบัก:** `/api/status` (JSON ทุกค่าใน state รวมเวลา rotate/detect/classify) · `/snapshot.jpg` (ภาพตั้งตรงพร้อมกรอบหน้า/ตา) · `/eyes.jpg` (ภาพ 32×32 ที่เข้าโมเดลจริง ซ้าย = ตาซ้ายของภาพ)
- RAM ภายในเหลือก้อนใหญ่สุดแค่ ~30 KB หลัง WiFi + esp-dl · arena ของ TFLM จึงตั้ง 28 KB และ fallback ไป PSRAM
- **ยังไม่ได้ทำ:** logic ความง่วง (PERCLOS / Drowsiness Level / Distraction / Unmonitored), เขียน log ลง FFat, Sync ขึ้น Supabase, OLED

## ML (`ml/`)

```bash
cd ml
.venv/Scripts/python.exe test_preprocess.py      # ตรวจว่า eye_preprocess.h (C, คอมไพล์ด้วย gcc) กับ preprocess.py ให้ byte เดียวกัน
```

- `train.ipynb` = pipeline ทั้งหมด (MRL → แบ่ง train/val/test **ตาม subject** → train → INT8 → `model_data.h` + `golden_data.h`) · วิธีตั้ง venv และรันอยู่ใน [ml/README.md](ml/README.md)
- **preprocess ต้องเหมือนกันทุกบิต** ระหว่าง `preprocess.py` กับ `eye_preprocess.h` · แก้ฝั่งหนึ่งต้องแก้อีกฝั่งและรัน `test_preprocess.py`
- การทดลองหลายแบบอยู่ใน `ml/output/experiments/` (`exp.py`, `run_queue.py`, `summ.py`, ทางรันต่ออยู่ใน `RESUME.md`) · train บน CPU (TensorFlow บน Windows ไม่ใช้ GPU)
- เปลี่ยนโมเดลบนบอร์ด = คัดลอก `model_data.h` ไปเป็น `eye_model_data.h` ของสเก็ตช์ · ถ้าโมเดลใหม่ใช้ op นอกเหนือ CONV_2D / MAX_POOL_2D / MEAN / FULLY_CONNECTED / LOGISTIC ต้องเพิ่มใน resolver ของ `eye_classifier.cpp`

## กล่อง 3D (`enclosure/`)

```bash
cd enclosure/cad
PYTHONIOENCODING=utf-8 "$LOCALAPPDATA/ddd-enclosure-venv/Scripts/python.exe" enclosure.py   # → out/*.step, out/*.stl
PYTHONIOENCODING=utf-8 "$LOCALAPPDATA/ddd-enclosure-venv/Scripts/python.exe" check.py       # ต้องได้ "no clashes"
```

- สคริปต์ build123d · ค่าทุกตัวอยู่หัว `enclosure.py` พร้อมป้าย `MEAS` (วัดจริง) / `DESIGN` (จาก Gerber/PnP) / `ASSUMED` (ต้องวัดก่อนปริ้นตัวจริง)
- พิกัด: x จากขอบซ้าย PCB, y จากขอบบน (ฝั่ง `MODULE USB`), z จากใต้พื้นกล่อง · ยึดกับขาดูดกระจกด้วยน็อต 1/4"-20
- ตัวจริงต้อง PETG/ASA (ในรถร้อน 70–80°C) · ข้อกำหนดอยู่ใน `enclosure/README.md` และ `DESIGN_INPUTS.md`

## ฮาร์ดแวร์ที่ต้องระวังเวลาเขียนโค้ด

GPIO map อยู่ใน [README.md](README.md) · เพิ่มเติม:
- **GPIO 26–37 ห้ามใช้** (Flash + Octal PSRAM) · GPIO 2 = LED บนโมดูล · GPIO 48 = WS2812 บนโมดูล
- **buzzer (GPIO14) เป็น active-LOW** ต้องดึงเป็น HIGH ตั้งแต่ต้น `setup()` (`alertBegin()`) และเลี้ยงด้วย 3V3 เท่านั้น
- **OLED:** จอจริงเรียงขา `VCC GND SCL SDA` ต่อกับ H1 (`3V3 GND IO1 IO42`) เส้นต่อเส้นด้วยสาย dupont · ใช้ `Wire.begin(42, 1)` (SDA=42, SCL=1)
- **ภาพจากกล้องตะแคง 90°** ตามการวางโมดูลบน PCB → `CAM_ROTATE_CW` ใน `config.h` · ถ้ากล่องวางให้ภาพตั้งตรงได้ ตั้งเป็น 0 จะประหยัด 17–38 ms ต่อเฟรม
- กล้อง: ไม่ต้องโหลด AF (fixed-focus ตั้งใจไว้) · เวลาวัดความสว่างให้ล็อก AEC/AGC และนับจำนวนพิกเซลที่เกินเกณฑ์ ไม่ใช้ peak
- **PCB v2 ยังไม่มาถึง** · บอร์ด v1 ห้ามเสียบโมดูล (header เป็นภาพกระจก) · ทดสอบอุปกรณ์รอบตัวด้วยการต่อสายตาม [docs/bench-wiring-v1.md](docs/bench-wiring-v1.md) + สเก็ตช์ `BenchTest`

## เอกสารที่ล้าสมัยบางส่วน

- `docs/PROJECT_STATUS.md` (ส.ค.): ขนาดบอร์ด, เกณฑ์ alert นับเป็นเฟรม, PERCLOS 80%, EAR, LINE Notify, MobileNetV2 96×96 ล้าสมัยแล้ว · GPIO map ยังถูก
- `docs/archive/early-design/` ดีไซน์ช่วงแรก (ไฟ 12V, IMU) ตัดออกแล้ว
