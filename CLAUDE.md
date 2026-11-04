# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **repo นี้เป็น public** (<https://github.com/Franknakub/driver-drowsiness-detection>) และเป็น**สำเนาที่จัดระเบียบแล้ว**ของโฟลเดอร์งานชั้นบน (`../`) ไม่ใช่ต้นฉบับทั้งหมด
> - ต้นฉบับ PCB อยู่ใน **EasyEDA Pro cloud** — `.epro2` และ Gerber ในนี้เป็นแค่ export
> - ต้นฉบับสเก็ตช์อยู่ที่ `~/Documents/Arduino/<ชื่อสเก็ตช์>/` — แก้และ build ที่นั่น แล้ว copy เข้า `firmware/` (ยกเว้น `secrets.h` และ `build/`)
> - `../CLAUDE.md` ละเอียดกว่า (การต่อ EasyEDA API, สถานะล่าสุด, ประเด็นค้าง, กับดัก USB) อ่านคู่กันเสมอ
> - **`main` = งานของเพื่อน (PR #6, 0.3.6-wifi) เป็นฐาน** ตั้งแต่ 2 พ.ย. · งานใหม่แตก branch จาก `main` แล้วเปิด PR (0.3.7-oled merge แล้วใน PR #7 · 0.3.8-oled-info, 0.3.9-face-roi, 0.3.10-dash merge เข้า `main` แล้วผ่าน branch `face-roi` 3 พ.ย. · 0.3.11-toon + Cloud Dashboard หน้าตาการ์ตูน merge ผ่าน PR #9 4 พ.ย. · 0.3.12-p30 ของเพื่อน (PR #10: P(ปิด) ≥ 0.3 = ปิด, เตือนเมื่อหลับตา 2 วิ) · 0.3.13-raw เพิ่ม `/eyes.raw` สำหรับทดสอบในรถ · **บอร์ดรัน 0.3.20-dash (4 พ.ย. = 0.3.18-p50 + ป้ายและขีด PERCLOS บน Device Dashboard ตรงเกณฑ์ 0.40) · 0.3.18-p50 (4 พ.ย. ผู้ใช้เลือกใช้โค้ดใน repo แฟลชทับ 0.3.15-p15 ของเพื่อน: P(ปิด) ≥ 0.5 · PERCLOS เตือนที่ 0.40 · ตัดระดับ TIRED)** · ซอร์ส 0.3.15-p15 ยังอยู่เครื่องเพื่อน `C:\Users\User\Desktop\DriverDrowsiness\driver-drowsiness-detection\firmware\DrowsyFW\` ไม่อยู่บน GitHub · flash เดิมสำรองที่ `Desktop\esp32-flash-backup\`) · branch `firmware` = งานเก่าของเรา (OLED แบบ U8g2 + camtest 0.3.5) ที่ revert ออกจาก `main` ใน PR #5 เก็บไว้อ้างอิง ห้าม merge ตรง · คำศัพท์ใช้ตาม [GLOSSARY.md](GLOSSARY.md) · การตัดสินใจหลักอยู่ใน [docs/adr/](docs/adr/) · แผนและผลทดลองอยู่ใน [docs/firmware-plan.html](docs/firmware-plan.html)

## ก่อน commit

- ใช้ noreply email ที่ตั้งไว้เฉพาะ repo นี้แล้ว (`git config user.email`) อย่าเปลี่ยน
- **ห้ามใส่:** `EasyEDALisenceFree.txt`, ชื่อ/รหัสนักศึกษา, `secrets.h` (WiFi/รหัส OTA — อยู่ใน `.gitignore`), ภาพหน้าคนที่เก็บมาทำ dataset
- `ml/data/`, `ml/output/`, `build/`, `*.bin`/`*.elf`, `.taste-skill/` (ผลจาก skill ออกแบบ) อยู่ใน `.gitignore`
- repo ตั้ง `core.autocrlf=true` แต่สเก็ตช์ใน `~/Documents/Arduino/` เป็น LF · `diff` ธรรมดาจะฟ้องว่าต่างทุกบรรทัด → เทียบด้วย `diff --strip-trailing-cr`
- ไฟล์ผลิตชุดใหม่ใส่ `hardware/fabrication/<YYYY-MM-DD>_vN/` แล้วย้ายชุดเดิมเข้า `archive/`

## Firmware — build / flash

ไม่มี lint หรือ unit test บนบอร์ด · การตรวจคือ compile ผ่าน แล้วดู serial / `GET /api/status` จากบอร์ดจริง

logic ความง่วง (`drowsiness.cpp`) และรูปแบบไฟล์ Trip (`trip_record.cpp`) ไม่พึ่ง Arduino มีเทสต์บน PC · แก้ไฟล์เหล่านี้แล้วต้องรันผ่านก่อน · เทสต์เลือกรันทีละข้อไม่ได้ ต้องรันทั้งไฟล์ (เรียกจาก `main()` ถ้าจะลองข้อเดียวให้คอมเมนต์ข้ออื่นชั่วคราว) · เปลี่ยนเกณฑ์ใน `drowsy_config.h` แล้วค่าในเทสต์ PERCLOS ต้องแก้ตาม · แก้ SQL ใน `cloud/sql/` แล้วรันเทสต์ cloud:

```bash
cd firmware/tests && /c/ProgramData/mingw64/mingw64/bin/g++ -std=c++17 -Wall -I../DrowsyFW drowsiness_test.cpp ../DrowsyFW/drowsiness.cpp -o drowsiness_test && ./drowsiness_test
cd firmware/tests && /c/ProgramData/mingw64/mingw64/bin/g++ -std=c++17 -Wall -I../DrowsyFW trip_record_test.cpp ../DrowsyFW/trip_record.cpp -o trip_record_test && ./trip_record_test   # สร้าง trip_fixture.json ด้วย
cd cloud/test && npm install && node sql_test.mjs   # รัน cloud/sql บน PGlite (Postgres จำลอง ไม่ต้องใช้ Supabase): RLS, ส่งซ้ำ, Trip Summary ตรงกับ Device
```

```bash
CLI="$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom"   # DrowsyFW
S=~/Documents/Arduino/DrowsyFW
"$CLI" compile --fqbn "$FQBN" --export-binaries $S
"$CLI" upload -p COM6 --fqbn "$FQBN" --input-dir $S/build/esp32.esp32.esp32s3 $S
```

- core `esp32 by Espressif Systems` **3.3.12** · ไลบรารี: `Async TCP` + `ESP Async WebServer` (ของ ESP32Async) · `esp-dl`, `esp-tflite-micro`, `esp-nn` มากับ core แล้ว ไม่ต้องลงเพิ่ม
- **`upload` ไม่ compile ให้** ต้อง compile ก่อนทุกครั้ง ไม่งั้นแฟลชไบนารีเก่า (`No changed sectors found`)
- **ลบ/เปลี่ยนชื่อไฟล์ในสเก็ตช์แล้วต้อง `compile --clean`** แคชของ arduino-cli เก็บไฟล์เก่าไว้ (3 พ.ย. `display.cpp` ของ 0.3.6 ที่ลบไปแล้วยัง build อยู่ ทำให้ 0.3.7 compile ไม่ผ่าน)
- **`PartitionScheme=custom`** ใช้ `partitions.csv` ในโฟลเดอร์สเก็ตช์ (app 6 MB × 2 + FFat ~3.9 MB) · arduino-cli จะเช็คขนาดเทียบ 16 MB ไม่ใช่ 6 MB ต้องดูขนาดเอง (ตอนนี้ ~3.66 MB) · สเก็ตช์ทดสอบอื่น (`DrowsinessDiag`, `BenchTest`) ใช้ `PartitionScheme=app3M_fat9M_16MB` · ⚠️ แฟลชด้วยตารางนี้จะทับ partition ของ DrowsyFW (FFat/Trip หายได้) · ถ้าจะแฟลชสเก็ตช์ทดสอบแล้วกลับมา DrowsyFW ให้ build ด้วย `PartitionScheme=custom` และคัดลอก `partitions.csv` ของ DrowsyFW ไปไว้ข้างสเก็ตช์ (ทำแบบนี้เมื่อ 23 ต.ค. เลข trip ต่อเนื่องไม่รีเซ็ต)
- **`PSRAM=opi` บังคับ** (N16R8 = Octal PSRAM)
- ต้องมี `secrets.h` (คัดลอกจาก `secrets.example.h`) · **อย่าเปิดอ่าน `secrets.h` ของผู้ใช้** ให้แก้เฉพาะบรรทัดที่สั่ง · **build ใช้ตัวในโฟลเดอร์สเก็ตช์** ไม่ใช่ `firmware/DrowsyFW/secrets.h` ใน repo (3 พ.ย. ผู้ใช้แก้ตัวใน repo แล้วแฟลชไม่เปลี่ยน) · เทียบสองตัวได้โดยไม่เห็นค่า: hash ทีละบรรทัดแล้ว diff เฉพาะชื่อ `#define`
- แฟลชผ่าน **USB ของโมดูล** เท่านั้น (USB-C บน PCB มีแค่ไฟ) · ลำดับเข้า download mode, VID:PID และ error ที่เจอบ่อยอยู่ใน [firmware/README.md](firmware/README.md)
- **OTA:** อัปโหลด `build/.../DrowsyFW.ino.bin` ที่ `http://<ip>/update` (user `admin`, รหัส `OTA_PASSWORD`) · ผู้ใช้ต้องใส่รหัสเอง · ห้ามเลือก `DrowsyFW.ino.merged.bin` (16 MB) · ขณะ DrowsyFW รันอยู่และเสียบ USB (`303A:1001`) `upload -p COM6` รีเซ็ตเข้า download mode ได้เอง ไม่ต้องกดปุ่ม (ยืนยัน 18 ต.ค.) · 0.2.9 OTA ค้างถ้าอัปโหลดหลุดกลางทาง (แก้ใน 0.3.0: abort ของเก่าก่อน begin)
- บอร์ดต่อ hotspot iPhone ได้ต่อเมื่อเปิด **Maximize Compatibility** (2.4 GHz) · hotspot Android ต่อได้ (3 พ.ย. แต่ปิดตัวเองบ่อย) · AP ของบอร์ด `Drowsy-XXXX` → `192.168.4.1` · คอมต้องอยู่วงเดียวกับบอร์ดถึงดึง `/api/status` ได้ (KUWIN ของมหาลัยใช้ไม่ได้)
- serial บอกเหตุที่ต่อ WiFi ไม่ได้ (`net.cpp`, 0.3.9): `201 NO_AP_FOUND` = มองไม่เห็นเครือข่าย (hotspot ปิด/5 GHz/ไกล) · `15` / `202` = รหัสผิด · `210–212` = security ไม่ตรง · `36 STA_LEAVING` = ฝั่งบอร์ดตัดเองระหว่างลองใหม่ ไม่ใช่สาเหตุ
- เปลี่ยน `FW_VERSION` ใน `config.h` ทุกครั้งที่แฟลช เพื่อยืนยันจาก `/api/status` ว่าบอร์ดรันตัวใหม่

## สถาปัตยกรรม DrowsyFW

```
กล้อง OV5640 (RGB565 HVGA) → หมุนภาพ 270° (+ ชดเชยหน้าเอียงตามมุมเส้นตา)
  → ROI 4:3 (0.3.9) → ESP-DL HumanFaceDetect (MSR 160×120 + MNP 48×48, score thr 0.30) → จุดตา 2 ข้าง
  → Eye Region ด้าน 0.8 × ระยะระหว่างตา → gray → 32×32 → int8 → CNN (TFLite Micro)
  → P(ตาปิด) เฉลี่ยสองข้าง → Eye State (ปิด ≥ `P_CLOSED_ON` 0.5 · เปิด < `P_CLOSED_OFF` 0.5 · ตั้งเท่ากันจึงไม่มีช่วงไม่แน่ใจ)
```

- **แบ่ง task ตาม core:** Core 1 = `vision` / `alert` / `sensor` (LDR→IR) · Core 0 = `net` (AP ตลอด + ลอง WiFi ที่บันทึกไว้ + NTP + mDNS), `log`, `sync`, `oled` (priority 1), เว็บ (AsyncWebServer ทำงานบน task ของ AsyncTCP)
- task คุยกันผ่าน `SharedState` + mutex (`shared_state.h`) อ่านด้วย `stateSnapshot()` เขียนใน `StateGuard` · **ห้ามส่ง camera `fb` ข้าม core** — ภาพสดใช้การคัดลอกเข้า buffer แยก (`snapshotStore` ใน `vision.cpp`)
- ค่าคงที่ ขา GPIO และเกณฑ์ P(ปิด) อยู่ใน `config.h` · เกณฑ์ความง่วงอยู่ใน `drowsy_config.h` (ไม่พึ่ง Arduino เพื่อให้เทสต์บน PC ได้ · เกณฑ์เวลาเป็นมิลลิวินาที ไม่นับเป็นเฟรม)
- **ROI ของขั้น MSR** (`pickRoi` / `compUpdate` ใน `face_eye.cpp`, 0.3.9): ภาพตั้งตรง 320×480 ส่งทั้งเฟรมเข้า input 160×120 = กว้าง ½ สูง ¼ หน้าถูกบีบเตี้ย → **A/B 3 พ.ย. ทั้งเฟรมเจอหน้า 0% ทุกระยะ** (50 ซม.–1.5 ม.) ส่วน ROI ได้ 75–100% · จึงส่งแค่ส่วน 4:3 (ย่อเท่ากันสองแกน):
  - เจอหน้า: ROI สูง `FACE_ROI_SCALE` (2.5) × ด้านยาวกรอบหน้า ต่ำสุด 160×120 (ไม่ขยายภาพ) · หน้าใหญ่เกินใช้แถบ 320×240 รอบหน้า — **ห้ามถอยไปใช้ทั้งเฟรม** (เคยทำแล้วหน้าใกล้หลุด 11/13 เฟรม)
  - หลุดเกิน 2 เฟรม: ลองรอบตำแหน่งเดิมด้วย ROI กว้าง 2 เท่า 1 เฟรม แล้วไล่ `SEARCH` ทีละเฟรม (ทั้งเฟรม → แถบ 4:3 → ช่อง 160×120 ครึ่งกลางภาพ 6 ช่อง → มุมเอียง ±20/±40) รอบละ ~16 เฟรม
  - พิกัดกรอบ/จุดตาแปลงกลับเป็นพิกัดเต็มภาพใน `ViewMSRPostprocessor` (offset `set_map`) · `FACE_ROI 0` = ทั้งเฟรมแบบเดิม
- `face_eye.cpp` = หาหน้า + หมุนภาพ (ย้ายมาจากสเก็ตช์ทดลอง FaceSpike) · `human_face_detect*.{hpp,cpp}` + `human_face_detect_model.cpp` = โค้ดและโมเดลจาก esp-dl v3.3.11 (MIT) · `eye_classifier.cpp` + `eye_preprocess.h` + `eye_model_data.h` = โมเดลตา
- **Device Dashboard** (`INDEX_HTML` ใน `web_server.cpp`, หน้า `/`, ออกแบบใหม่ 0.3.10 ผู้ใช้เลือก: เน้นสาธิต/คนขับ · สีตามเครื่อง · ภาพกล้องใหญ่ · **0.3.11-toon** คงโครงเดิมแต่หน้าตาการ์ตูนเหมือน Cloud Dashboard: รถมาสคอตในการ์ดสถานะ เปลือกตารถตามระดับ/ตาคนขับ (ตาปิด = รถหลับตา, ละสายตา = ตามองข้าง) สั่นที่ระดับ 3 · ฟอนต์ระบบเท่านั้น · เปิดตรวจหน้าเว็บบนบอร์ดจริงแล้ว 4 พ.ย. (0.3.20-dash) · ตัวก่อนแก้ backup ที่ `~/Documents/DrowsyFW-backups/web_server_0.3.10-dash_before_cartoon.cpp`): การ์ดสถานะใหญ่สีตามระดับ (กะพริบที่ระดับ 3 / Distraction) · ปุ่มปิดเสียงนับถอยหลัง · ภาพสดพร้อมป้ายเจอหน้า/ตา · การ์ดตา / PERCLOS / การเชื่อมต่อ · กราฟ 2 นาที · ส่วน "สำหรับนักพัฒนา" พับไว้ (เวลา, ROI, IR/LDR, RAM, ภาพตา 32×32, โหมด IR, ทดสอบฮาร์ดแวร์, JSON) · ภาพตาโหลดเฉพาะตอนเปิดส่วนนั้น และหยุดโหลดภาพเมื่อหน้าถูกซ่อน · ไม่มีไฟล์/ฟอนต์ภายนอก (มือถือที่ต่อ AP ไม่มีเน็ต) · แก้หน้าแล้วพรีวิวบน PC ได้ด้วยเซิร์ฟเวอร์เล็กๆ ที่เสิร์ฟ HTML และส่งต่อ `/api/*` กับภาพไปบอร์ด · ห้ามมี `)HTML"` ในเนื้อหา · `POST /api/ir mode=auto|on|off` (จำใน NVS) · `POST /api/test what=buzzer|green|red|ir|oled` ทดสอบฮาร์ดแวร์ `HW_TEST_MS` · `POST /api/mute` · `/wifi` สแกน/เพิ่ม/ลบ WiFi (เข้าผ่าน AP ของบอร์ดไม่ต้อง login · ผ่าน WiFi อื่นต้อง admin / `OTA_PASSWORD` · `wifiAuthorized` เทียบ `localIP` ของ connection กับ `softAPIP`) เก็บใน NVS สูงสุด `WIFI_SAVED_MAX` ลองตามลำดับแล้วค่อย `WIFI_NETWORKS` ใน `secrets.h` · `/update` OTA
- **ปุ่ม BOOT (GPIO0) = ปิดเสียง** ระดับ 2 และ Distraction `MUTE_DEFAULT_MS` (5 นาที) หลังบูตแล้ว (`alert.cpp`) · ระดับ 3 ปิดเสียงไม่ได้
- **endpoint ดีบัก:** `/api/status` (JSON ทุกค่าใน state รวมเวลา rotate/detect/classify และ `face_roi`) · `/snapshot.jpg` (ภาพตั้งตรงพร้อมกรอบหน้าเขียว/ตา · กรอบเทา = ROI ที่ส่งเข้า MSR) · `/eyes.jpg` (ภาพ 32×32 ที่เข้าโมเดลจริง ซ้าย = ตาซ้ายของภาพ) · `/eyes.raw` (0.3.13: ภาพ 32×32 ดิบสองข้าง + P ของเฟรมเดียวกัน 2072 ไบต์ รูปแบบอยู่ที่ `eyeRawStore` ใน `vision.cpp` · buffer อยู่ PSRAM · เก็บเฉพาะตอนมีคนขอภายใน 3 วิ) · `/api/ab?on=1` เทียบทางหาหน้าปัจจุบันกับทางเดิม (`faceEyesRunLegacy`: หมุนทั้งเฟรม + ส่งทั้งเฟรม) บนเฟรมเดียวกัน นับ `found_new` / `found_old` · รีเซ็ตตัวนับ = `on=0` แล้ว `on=1` · ระหว่างเปิด fps ลดราวครึ่ง
- **ภาพสด:** `snapshotJpeg` ถือ mutex แค่ตอนคัดลอก แล้ววาดกรอบ + เข้ารหัส JPEG (~150–200 ms) หลังปล่อย · vision เก็บภาพแบบไม่รอ mutex ถ้าถือ mutex ตอนเข้ารหัส เฟรมใหม่ถูกข้ามจนหน้าเว็บได้ภาพเดิมซ้ำ (ภาพค้าง แก้ 3 พ.ย.) · `sendJpeg` ให้ buffer เป็นของแต่ละ response (ปล่อยเมื่อ response ถูกลบ) ดูพร้อมกันหลายเครื่องได้
- RAM ภายในเหลือก้อนใหญ่สุดแค่ ~30 KB หลัง WiFi + esp-dl · arena ของ TFLM จึงตั้ง 28 KB และ fallback ไป PSRAM · **ตอนต่อเน็ตแล้ว Sync ส่ง Trip ค้าง (3 พ.ย.) เหลือ `free_internal_kb` 10–22 / ก้อนใหญ่สุด 7 KB** → JPEG ภาพสดเสียครึ่งล่าง · `cam_hal: FB-OVF` · fps ~3 · Sync `send payload failed` · ยังไม่ได้ไล่ว่าใครกิน · วัด 4 พ.ย. (0.3.18): ไม่มีโหลด 6–7 fps · ดึงเว็บถี่เหลือ 5–6 · เห็นหน้าช้ากว่าไม่เห็นหน้าเพราะมี classify ~43 ms · ค่าต่ำสุด 2–3 fps เกิดได้แม้ไม่มีโหลด ยังไม่รู้สาเหตุ · firmware ตัวเดียวกันได้ 4.0 และ 5.8 fps ในคนละรอบ การแฟลชจึงไม่ใช่สาเหตุ
- **logic ความง่วง** อยู่ใน `drowsiness.{h,cpp}` (class `DrowsinessJudge`, เกณฑ์ใน `drowsy_config.h`) · vision เรียกทุกเฟรมหลังได้ Eye State · นับเป็นเวลา (dt) ไม่นับเฟรม · Event ส่งผ่านคิวให้ log task พิมพ์ — **ห้ามพิมพ์ Serial จาก vision task** (USB CDC ที่ไม่มีใครเปิดพอร์ตทำให้ค้าง ~3 วิ เจอใน 0.2.6)
- **กฎเตือนปัจจุบัน (0.3.18-p50):**

  | เหตุการณ์ | ผล |
  |---|---|
  | ตาปิดต่อเนื่อง 2 วิ (`CLOSED_LEVEL2_MS`) | ระดับ 2 `SLEEPY` + เสียงเป็นจังหวะ |
  | ตาปิดต่อเนื่อง 3 วิ (`MICROSLEEP_MS`) | ระดับ 3 `WAKE UP!` + เสียงดังค้าง ปิดไม่ได้ |
  | ลืมตาต่อเนื่อง 1 วิ (`EYES_OPEN_CLEAR_MS`) | ปลดระดับที่เกิดจากตาปิด |
  | PERCLOS ≥ 0.40 (`PERCLOS_LEVEL2`, ลดเมื่อ < 0.38) | ระดับ 2 แม้ลืมตาอยู่ |

  - ระดับ 1 (TIRED) ถูกตัดออก 0.3.17 ตามผู้ใช้ · ค่า `DrowsinessLevel::Fatigue` และ `level1_s` ยังอยู่เพื่ออ่าน Trip เก่า
  - 4 พ.ย. ผู้ใช้ลอง `MICROSLEEP_MS` 2 วิ (`0.3.19-ms2` ไม่ได้ commit) ในห้องขึ้น `WAKE UP!` 5/5 แต่เล่นซ้ำรอบขับได้เสียงดังค้างราว 11 ครั้งใน 7.5 นาที จึงเลือกกลับมา 3 วิ เพราะกล้องและโมเดลยังตรวจไม่ดีพอ (ถ้าเท่ากับ `CLOSED_LEVEL2_MS` จะข้ามระดับ 2 และถ้าหน้าหายเสียงจะค้างถึง `MICROSLEEP_ABSENT_CLEAR_MS` 5 นาที)
  - **ข้อจำกัดที่ผู้ใช้รับแล้ว อย่าเสนอแก้เอง:** หันหัวแล้วเตือนผิดได้ (บอร์ดตัดแก้มเป็นตา ขึ้นกับกล้องและตำแหน่งนั่ง) · PERCLOS สูงทำให้ `SLEEPY` และเสียงค้างหลังลืมตาได้ราว 11 วิ
- **ประวัติ Trip** (`trip_record.*` = รูปแบบไฟล์ + Trip Summary ไม่พึ่ง Arduino · `trip_log.*` = FFat + log task): 1 Trip = `/trips/<n>.s` (Sample 16 ไบต์/วิ) + `<n>.e` (Event) + `<n>.m` (ตำแหน่ง Sync + Summary + crc) · เวลาใน record = `millis()` แล้วเทียบเวลาจริงจาก Event `TimeKnown` · FFat ถึง 80% ลบ Trip ที่ Sync แล้ว · endpoint `/trips`, `/api/trips`, `/api/trips/<n>?from=&count=&step=` · ขอ samples มากตอน RAM ภายในตึงแล้ว `String` จองไม่พอ ได้ HTTP 200 แต่ JSON ขาดกลางทาง → ถ้าต้องการแค่ events ใช้ `?count=1`
- **Sync** (`sync.cpp`): POST `/rest/v1/rpc/ingest` ของ Supabase ทีละ ≤ 300 Sample จาก Trip เก่าสุดที่ค้าง · Trip ปัจจุบันส่งทุก 30 วิ · สถานะอยู่ใน `/api/status` ช่อง `sync` · ตั้ง `DEVICE_SECRET` ใน `secrets.h` ให้ตรงกับที่ลงทะเบียนใน Cloud Dashboard ("เพิ่ม Device") · **Supabase โปรเจกต์ `iatzjmxjbxcakotczoko` (ของเพื่อน ใช้ตั้งแต่ 27 ต.ค., ผู้ใช้ยืนยัน 2 พ.ย.)** · Trip ก่อนหน้านั้นอยู่ในโปรเจกต์เก่า `wcuagguhwtprdnguiozn` ซึ่งเลิกใช้แล้ว · SQL/ขั้นติดตั้ง/Cloud Dashboard อยู่ใน [cloud/README.md](cloud/README.md) (ADR 0003)
- **TLS ต้องใช้ PSRAM:** RAM ภายในเหลือก้อนใหญ่สุด ~13 KB ไม่พอ buffer 16 KB ของ mbedTLS (0.3.0 ได้ `connection refused`) → `sync.cpp` เรียก `mbedtls_platform_set_calloc_free` ให้ก้อน ≥ 1 KB ไปอยู่ PSRAM
- **จอ OLED** (`oled.cpp`, 0.3.8): SSD1306 driver เขียนเอง (ฟอนต์ 5x7 ขยาย x1–x2) ไม่ใช้ไลบรารี · หาจอไม่เจอ/จอหลุด ลองใหม่ทุก `OLED_RETRY_MS` · ไม่กะพริบ หรี่จอด้วยเกณฑ์ LDR เดียวกับ IR · **ทดสอบบนบอร์ดผ่าน 3 พ.ย.**
  - หน้าเดียวตลอด (ผู้ใช้ตัดสิน 3 พ.ย. แทนแบบทีละหน้าของ 0.3.7): แถบบนตัว x2 = สถานะ + 5 บรรทัดตัว x1 = `EYE:` / `SOUND:` / `AP` ชื่อ AP / `URL 192.168.4.1` / `LAN` IP ผ่าน WiFi (`try <ssid>`, `starting`, `no wifi`)
  - สถานะ: `WAKE UP!` (ระดับ 3) > `SLEEPY` (2) > `LOOK AHEAD` (Distraction) — 3 อันนี้กลับสีแถบ > `UNCLEAR` (Unmonitored) > `NO DRIVER` (`driverPresent` = false: ยังไม่เห็นตา หรือหายเกิน `DRIVER_ABSENT_MS`) > `AWAKE`
  - `EYE:` `OPEN` / `CLOSED x.xs` / `UNSURE` / `NOT FOUND` (เจอหน้าไม่เจอตา) / `NO FACE` · `SOUND:` คิดจากกฎเดียวกับ buzzer ใน `alert.cpp` (`ALARM` / `TEST` / `MUTED m:ss` / `BEEPING` / `ON`) แก้กฎเสียงต้องแก้ทั้งสองที่
  - ภาพจำลองทุกสถานะ (เรนเดอร์จาก `drawInfo` จริงบน PC) อยู่ที่ `../docs/images/oled_info_preview.png` นอก repo · ปุ่มทดสอบ OLED บนเว็บแสดงภาพทดสอบทั้งจอ

## Cloud Dashboard (`docs/dashboard/`)

```bash
cd docs && python -m http.server 8765 --bind 127.0.0.1   # http://127.0.0.1:8765/dashboard/  ต่อ Supabase จริง (ต้อง login)
python cloud/test/dashboard_preview.py                    # http://127.0.0.1:8766/  ข้อมูลจำลอง ไม่ต้องมีบัญชี (?out=1 ?nodev=1 ?empty=1 ?delok=1)
```

- ไฟล์เดียว `index.html` (HTML/CSS/JS ไม่มี build) · supabase-js แบบ UMD จาก CDN · URL + publishable key ในไฟล์เป็นค่าสาธารณะโดยออกแบบ สิทธิ์อ่าน/ลบคุมด้วย RLS ใน `cloud/sql/`
- ข้อมูลที่อ่าน: `devices` · `trip_summary` (view) · `trips.updated_at` (ทริปที่อัปเดตภายใน `LIVE_MS` 3 นาที = กำลังบันทึก ห้ามลบ ไม่งั้น Device ส่งต่อแล้วได้ทริปครึ่งๆ) · `rpc trip_series(p_trip_id, p_bucket_s)` สำหรับกราฟ (API คืนได้ครั้งละ 1000 แถว) · `events` · เพิ่ม Device ด้วย `rpc claim_device`
- **คะแนนความปลอดภัยคิดฝั่งหน้าเว็บ** (`score()` / `grade()` เกณฑ์ 85/70/50) ไม่ได้เก็บในฐานข้อมูล
- `dashboard_preview.py` แทนแท็ก `<script>` ของ supabase-js ด้วย `cloud/test/dashboard_stub.js` ตอนเสิร์ฟ (ไฟล์ใน `docs/` ไม่ถูกแก้) · ถ้าหน้าใช้ตาราง/RPC ใหม่ต้องเพิ่มใน stub ด้วย
- **หน้าตาการ์ตูน** (ผู้ใช้เลือกแบบ D จาก mockup 6 แบบ, 4 พ.ย.): มาสคอตรถที่ไฟหน้าเป็นตา ตาปรือตามคะแนน (`openOf`) · ฉากถนนกลางวัน/กลางคืนตามธีมเครื่อง · ฟอนต์ Mali (หัวข้อ) + IBM Plex Sans Thai · ไอคอนวาดเอง ไม่ใช้ emoji · การเคลื่อนไหว (ถนนเลื่อน ล้อหมุน ตากะพริบ การ์ดลอยขึ้นครั้งแรกหลัง login) ปิดตาม `prefers-reduced-motion` — **เครื่องผู้ใช้ปิด Animation effects ของ Windows อยู่ จึงเห็นแค่ fade** · ทดสอบหน้าหลัง login แล้วแค่กับข้อมูลจำลอง ยังไม่เคยลองกับบัญชีจริง
- **Deploy 2 ที่:** GitHub Pages ของ repo นี้ (`main` โฟลเดอร์ `/docs`) build เองภายใน ~1 นาทีหลัง merge · **Render** ของเพื่อน <https://drowsy-dashboard.onrender.com/dashboard> (บัญชีเพื่อน · ไฟล์ที่เสิร์ฟตรงกับ `docs/` ของ `main` ณ วันที่ deploy) **ไม่ deploy เองหลัง merge** (4 พ.ย. รอ 9 นาทีหน้ายังเป็นตัวเก่า) → ต้องให้เพื่อนกด Manual Deploy หรือเปิด Auto-Deploy ของ `main`

## ML (`ml/`)

```bash
cd ml
.venv/Scripts/python.exe test_preprocess.py      # ตรวจว่า eye_preprocess.h (C, คอมไพล์ด้วย gcc) กับ preprocess.py ให้ byte เดียวกัน
```

ทดสอบในรถ (รันใน `ml/` · คอมต้องอยู่วงเดียวกับบอร์ด · ท่าหลับตาทำตอนรถจอดเท่านั้น):

```bash
.venv/Scripts/python.exe incar/cartest.py --name <รอบ> --host <ip> --person <ชื่อ> --light day --glasses none   # 3 นาที 43 วิ พิมพ์ท่า พูดบอก เก็บภาพตา + บันทึกในบอร์ด
.venv/Scripts/python.exe incar/cartest.py --protocol drive --name <รอบ> --host <ip>   # ขับจริง ไม่มีเสียงบอก ต้องกด Ctrl+C ให้จบ ไม่งั้นข้อมูลไม่ลงไฟล์
.venv/Scripts/python.exe incar/score.py <รอบ>                  # ให้คะแนนซ้ำ ใช้บันทึกในบอร์ดเติมส่วนที่ status หาย
.venv/Scripts/python.exe incar/replay.py <รอบ> --on 0.5 --off 0.5   # ลองเกณฑ์ P ผ่าน DrowsinessJudge บน PC โดยไม่ต้องแฟลช
.venv/Scripts/python.exe incar/oled_check.py --host <ip>       # แสดงสิ่งที่จอควรขึ้น ไว้เทียบกับจอจริง
```

ตัวให้คะแนนนับหลับตาถึงหลังบี๊บลืมตา 1.5 วิ ถ้าคนหลับตาช้า ให้ดูเหตุการณ์ใน `board_trip.json` ด้วย

- `train.ipynb` = pipeline ทั้งหมด (MRL → แบ่ง train/val/test **ตาม subject** → train → INT8 → `model_data.h` + `golden_data.h`) · วิธีตั้ง venv และรันอยู่ใน [ml/README.md](ml/README.md)
- **preprocess ต้องเหมือนกันทุกบิต** ระหว่าง `preprocess.py` กับ `eye_preprocess.h` · แก้ฝั่งหนึ่งต้องแก้อีกฝั่งและรัน `test_preprocess.py`
- การทดลองหลายแบบอยู่ใน `ml/output/experiments/` (`exp.py`, `run_queue.py`, `summ.py`, ทางรันต่ออยู่ใน `RESUME.md`) · train บน CPU (TensorFlow บน Windows ไม่ใช้ GPU)
- **โมเดลบนบอร์ด train จาก MRL อย่างเดียว ไม่ได้ fine-tune:** ในรถ 4 พ.ย. (ระยะตา ~37 px) ตาปิดจริงได้ P≈1.0 · ใส่แว่นแล้วตาข้างหนึ่งอาจเหลือ 0.1–0.3 · ปัญหาหลักคือภาพที่ตัดผิดตอนหันหัวและตอนแสงเปลี่ยน · ประวัติ 3 พ.ย. บนโต๊ะที่ 50–60 ซม. หลับตาได้ 0/13 เฟรม ซึ่งไม่ตรงกับผลในรถ และยังไม่รู้สาเหตุ · ถ้าจะ fine-tune **ผู้ใช้ให้ใช้เฉพาะภาพที่เก็บในรถ** (ภาพบนโต๊ะห้ามเข้าโมเดลที่ใช้จริง)
- **ทดสอบในรถก่อนตัดสินใจ fine-tune** (ผู้ใช้ตัดสิน 4 พ.ย. เพราะเวลาเหลือน้อย · ผลรอบรถจอด รอบขับ ผลวัดความช้า และข้อจำกัดที่ผู้ใช้ยอมรับอยู่ใน [ml/incar/README.md](ml/incar/README.md) · ไม่พบว่าบอร์ดตามหลังหลายวินาที · ยังไม่ fine-tune): `ml/incar/cartest.py` พูดบอกท่า อ่าน `/api/status` แล้วให้คะแนนว่าหลับตาแล้วเตือนครบไหม/ลืมตาแล้วเตือนผิดไหม · เก็บภาพจาก `/eyes.raw` ไว้ fine-tune ไปด้วย · วิธีทำและเกณฑ์ตัดสินอยู่ใน [ml/incar/README.md](ml/incar/README.md) · ลองบน PC ด้วย `fake_board.py` · รันกับบอร์ดจริงแล้ว 4 พ.ย. (รถจอด + ขับจริง 1 รอบ) · ดึงเหตุการณ์จริงจาก `/api/trips/<n>?count=1` มาให้คะแนน เพราะเว็บบอร์ดค้างแล้ว status หายได้
- **เครื่องมือ fine-tune เตรียมไว้แล้ว** (ใช้ถ้าทดสอบในรถไม่ผ่านเพราะโมเดลตา): `review.py` ตรวจป้าย → `finetune.py` (MRL + ภาพในรถ แบ่งตามรอบ · deep2_half เดิม · INT8 → `model_data.h` · รายงานเทียบโมเดลบนบอร์ดที่อ่านจาก `eye_model_data.h` ทีละตา) → `replay.py` (ป้อน P ผ่าน `DrowsinessJudge` จริงด้วย `firmware/tests/replay.cpp` ซึ่งต้องแปลง P → Eye State แบบเดียวกับ `combineEyes`/`eyeStateFrom` ใน `vision.cpp`) · ลองทั้งชุดกับข้อมูลจำลองผ่าน 4 พ.ย. (`--quick`) · ผลอยู่ `ml/output/incar/<run>/`
- เปลี่ยนโมเดลบนบอร์ด = คัดลอก `model_data.h` ไปเป็น `eye_model_data.h` ของสเก็ตช์ · ถ้าโมเดลใหม่ใช้ op นอกเหนือ CONV_2D / MAX_POOL_2D / MEAN / FULLY_CONNECTED / LOGISTIC ต้องเพิ่มใน resolver ของ `eye_classifier.cpp`

## กล่อง 3D (`enclosure/`)

```bash
cd enclosure/cad
PYTHONIOENCODING=utf-8 "$LOCALAPPDATA/ddd-enclosure-venv/Scripts/python.exe" enclosure.py   # → out/*.step, out/*.stl
PYTHONIOENCODING=utf-8 "$LOCALAPPDATA/ddd-enclosure-venv/Scripts/python.exe" check.py       # ต้องได้ "no clashes" และทุกบรรทัด SW1 เป็น ok
PYTHONIOENCODING=utf-8 "$LOCALAPPDATA/ddd-enclosure-venv/Scripts/python.exe" assembly.py    # → out/assembly/*.stl ชิ้นทั้งชุด (รวมหุ่นบอร์ด/สาย) สำหรับหน้าดู 3D
```

- สคริปต์ build123d · ค่าทุกตัวอยู่หัว `enclosure.py` พร้อมป้าย `MEAS` (วัดจริง) / `DESIGN` (จาก Gerber/PnP) / `ASSUMED` (ต้องวัดก่อนปริ้นตัวจริง)
- พิกัด: x จากขอบซ้าย PCB, y จากขอบบน (ฝั่ง `MODULE USB`), z จากใต้พื้นกล่อง · ใน CAD แกน Y กลับทิศ (CAD Y = −y) · ยึดกับขาดูดกระจกด้วยน็อต 1/4"-20
- ตัวจริงต้อง PETG/ASA (ในรถร้อน 70–80°C) · ข้อกำหนดอยู่ใน `enclosure/README.md` และ `DESIGN_INPUTS.md`
- **ชุดส่งร้านเป็นสำเนาคงที่** ไม่เปลี่ยนตาม `cad/out/`: `print_2026-10-18/` = ฝา + ฐาน (ปริ้นแล้ว) · `print_2026-10-26/` = ชิ้น SW1 (ด้ามโยก + ขาแขวน + หมุด) · ด้ามต่อ SW1 ในชุด 18 ต.ค. ใช้ไม่ได้
- **SW1 ตอนนี้ไม่มีด้ามต่อ:** **4 พ.ย. (ผู้ใช้): ชิ้น SW1 ที่ปริ้น 3D พัง และสวิตช์ใหม่ที่ซื้อมาใช้ไม่ได้ → ใช้ SW1 เดิมบนบอร์ด เลื่อนด้วยไม้จิ้มฟันแหย่ผ่านร่องฝา ดันที่โคนก้าน** · ชุดด้ามโยกด้านล่างเก็บไว้เป็นประวัติ
- **ด้ามโยกที่ลองไว้** (`make_sw_rocker` / `make_sw_bracket` / `make_sw_pin`, ปริ้นแล้วพัง): ก้าน SW1 งอ/เอียงได้มาก ดันปลายก้านสวิตช์ไม่เลื่อน ต้องดันโคนก้านชิดตัวสวิตช์เท่านั้น → ง่ามแตะก้านเฉพาะแถบล่าง 1 มม. · ด้ามหมุนรอบหมุดในขาแขวนที่ติดกาวใต้ฝา **หัวด้ามขยับกลับทิศกับก้าน** · ด้ามต่อแบบเลื่อนทุกแบบ (สวมปลายก้าน) ล้มเหลวเพราะแรงงัด/แรงเสียดทาน · `check.py` ตรวจด้ามโยกที่ OFF/กลาง/ON กับฝา ขาแขวน หมุด ก้าน และหุ่นชิ้นส่วน · รายละเอียดและตัวเลขอยู่ใน `enclosure/README.md`
- รัน `enclosure.py` แล้ว `cad/out/lid.step` / `base.step` เปลี่ยนแค่วันเวลาในหัวไฟล์ → `git checkout` คืนก่อน commit ถ้ารูปทรงไม่ได้แก้

## ฮาร์ดแวร์ที่ต้องระวังเวลาเขียนโค้ด

GPIO map อยู่ใน [README.md](README.md) · เพิ่มเติม:
- **GPIO 26–37 ห้ามใช้** (Flash + Octal PSRAM) · GPIO 2 = LED บนโมดูล · GPIO 48 = WS2812 บนโมดูล
- **buzzer (GPIO14) เป็น active-LOW** ต้องดึงเป็น HIGH ตั้งแต่ต้น `setup()` (`alertBegin()`) และเลี้ยงด้วย 3V3 เท่านั้น
- **OLED:** จอจริงเรียงขา `VCC GND SCL SDA` ต่อกับ H1 (`3V3 GND IO1 IO42`) เส้นต่อเส้นด้วยสาย dupont · ใช้ `Wire.begin(42, 1)` (SDA=42, SCL=1)
- **ภาพจากกล้องตะแคง 90°** ตามการวางโมดูลบน PCB → `CAM_ROTATE_CW` ใน `config.h` · ถ้ากล่องวางให้ภาพตั้งตรงได้ ตั้งเป็น 0 จะประหยัด 17–38 ms ต่อเฟรม
- กล้อง: ไม่ต้องโหลด AF (fixed-focus ตั้งใจไว้) · เวลาวัดความสว่างให้ล็อก AEC/AGC และนับจำนวนพิกเซลที่เกินเกณฑ์ ไม่ใช้ peak
- **PCB v2 = บอร์ดสุดท้าย** (ได้รับและทดสอบผ่านแล้ว 23 ต.ค. · ไม่ทำ v3) ดูผลทดสอบด้านล่าง · บอร์ด v1 ห้ามเสียบโมดูล (header เป็นภาพกระจก) · ทดสอบอุปกรณ์รอบตัวด้วยการต่อสายตาม [docs/bench-wiring-v1.md](docs/bench-wiring-v1.md) + สเก็ตช์ `BenchTest`

## ผลทดสอบบอร์ด v2 (23 ต.ค.)

ผ่าน: บูต · กล้อง · IR ×4 · buzzer · LED เขียว/แดง · LDR · OLED (เจอที่ 0x3C) · ไฟเข้าทาง USB-C บน PCB + SW1 · WiFi + Sync Supabase · ความร้อนที่อุณหภูมิห้อง (มีแค่ฝาโมดูลอุ่น)

- **SW1: ขวา = ON, ซ้าย = OFF** (มองจากด้านบน โมดูลกล้องหงาย ปลาย USB โมดูลหันขอบบน)
- **กล้อง OV5640 เห็น IR 940 nm** (ไม่มี IR-cut) · วัดด้วยโหมดความสว่างเฉลี่ยของ `BenchTest`: IR เปิดสว่างขึ้น +12 / +31 ระดับที่ exposure กลาง/สูง · **ต้องมีกระดาษขาวสะท้อนห่าง ~5 ซม. ในห้องมืด** ไม่มีของสะท้อนจะวัดไม่ขึ้นและสรุปผิดได้
- **`cam_hal: FB-SIZE` เฟรมเสีย ~17%** (~140–150 ครั้ง/2 นาที ที่ ~7 fps) · **ไม่เกี่ยวกับ PCB** (เท่ากันตอนถอดโมดูลออกจากบอร์ด) · เฟรมขาดทีละ 16 แถวพอดี · ลด XCLK เป็น 10 MHz แย่กว่า (fps ตกเหลือ ~3) จึงคง 20 MHz · เฟรมเสียถูกทิ้ง fps ยังได้ ~7 ยังไม่ได้ไล่สาเหตุ (สงสัย PSRAM/WiFi แย่งบัส)
- **ค่า LDR:** ห้องมืดอ่าน ~2900–3000 (เกินเกณฑ์ `LDR_DARK_ON_RAW` 2800 → IR ติด) · ปิดมือ ~3900–4095 · สว่าง ~500–700 · เกณฑ์ยังไม่ได้คาลิเบรตในรถ · **4 พ.ย. ติดในรถตอนกลางวันอ่าน 4095 ตลอด** IR จึงเปิดตลอด และจอตั้ง contrast โหมดมืด (`OLED_CONTRAST_DARK`) · ยังไม่รู้สาเหตุ
- hotspot iPhone ต้องเปิด Maximize Compatibility ถึงต่อได้ (ก่อนเปิด บอร์ดวนลองแต่ไม่ติด)

## เอกสารที่ล้าสมัยบางส่วน

- `README.md`: ตารางสถานะและโครงสร้าง repo ยังเป็นของ 3 ต.ค. (firmware "ยังไม่เริ่ม", กล่อง "ยังไม่ออกแบบ", pipeline crop 96×96 / PERCLOS) · GPIO map ยังถูก · สถานะล่าสุดอยู่ใน `docs/STATUS.md`
- `docs/PROJECT_STATUS.md` (ส.ค.): ขนาดบอร์ด, เกณฑ์ alert นับเป็นเฟรม, PERCLOS 80%, EAR, LINE Notify, MobileNetV2 96×96 ล้าสมัยแล้ว · GPIO map ยังถูก
- `docs/archive/early-design/` ดีไซน์ช่วงแรก (ไฟ 12V, IMU) ตัดออกแล้ว
