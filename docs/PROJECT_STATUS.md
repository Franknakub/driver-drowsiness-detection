# Driver Drowsiness Detection with TinyML — Project Status

> ไฟล์นี้สรุปสถานะโปรเจคทั้งหมด สำหรับให้ Claude Code เข้าใจบริบทก่อนเริ่มเขียนโค้ด
> อัปเดตล่าสุด: อยู่ระหว่างวาด Schematic ใน EasyEDA
>
> ⚠️ **เอกสารนี้เขียนช่วง ส.ค. 2026 บางส่วนล้าสมัยแล้ว** เช่น ขนาดบอร์ดจริงคือ 58.55 × 65.66 มม. (ไม่ใช่ 80×70), เกณฑ์ alert ต้องนับเป็นวินาที, LINE Notify ปิดบริการแล้ว ข้อมูลปัจจุบันดูที่ README หลักและ `docs/firmware-plan.html`

---

## 1. บริบทโปรเจค

- **วิชา:** Embedded System 03603323 กลุ่มที่ 17
- **มหาวิทยาลัย:** เกษตรศาสตร์ ศรีราชา
- **สมาชิก:** 2 คน
- **หัวข้อ:** ระบบตรวจจับความง่วงของคนขับรถด้วยกล้อง + TinyML บน custom PCB
- **Deadline ส่งโปรเจค:** 3–5 พฤศจิกายน 2026

---

## 2. ฮาร์ดแวร์ที่ยืนยันแล้ว (ห้ามเปลี่ยนโดยไม่ถามทีมก่อน)

| ส่วน | อุปกรณ์ | หมายเหตุ |
|------|---------|---------|
| MCU | ESP32-S3-WROOM-1-N16R8 | 16MB Flash / 8MB PSRAM · LCSC: `C2913202` |
| Dev board สำหรับต่อแยก | ESP32-S3 CAM N16R8 (Robotronik) | pin header บัดกรีมาแล้ว เสียบ female header บน PCB ได้ ถอดเปลี่ยนได้เมื่อพัง |
| Camera (หลัก) | OV5640 5MP Autofocus, DVP interface | เสียบผ่าน FPC 24-pin ถอดเปลี่ยนได้ |
| Camera (สำรอง) | OV2640 2MP | ใช้ train ML ช่วงแรกเพราะ driver เสถียรกว่า OV5640 |
| Display | OLED 0.96" SSD1306, I2C addr `0x3C` | |
| Alert เสียง | Active Buzzer 5V | |
| Alert ไฟ | LED เขียว + แดง | |
| IR LED | TSAL6200 850nm ×4 | ส่องหน้าตอนกลางคืน |
| Light sensor | LDR (GL5528) | วัดแสง → เปิด/ปิด IR LED อัตโนมัติ |
| Power chain | **Power bank 5V → USB-C → 5V → (dev board ทำ 3.3V เอง)** | ⚠️ **เปลี่ยนแผน 13 ส.ค. 2026** — เดิมเป็น 12V จากรถ ดูเหตุผลด้านล่าง |
| PCB | 80×70mm, 2-layer FR4 | สั่งผลิตที่ JLCPCB |
| EDA Tool | EasyEDA Pro | |

### ⚠️ ตัดออกจากแผนแล้ว
- **MPU-6050 (IMU) — ไม่ใช้** ตัดสินใจตัดแล้ว ระบบตรวจจับจาก **กล้องอย่างเดียว** ไม่มี sensor fusion กับ IMU อีกต่อไป
- **AMS1117-3.3 — ไม่ใช้** (ตัด 13 ส.ค. 2026) บอร์ด ESP32-S3-CAM มี regulator 3.3V ในตัวอยู่แล้ว และ schematic ดึงไฟ 3V3 จากขา header ของ dev board (U5.1/U5.2) ถ้าใส่ AMS1117 แล้วต่อ output เข้าเส้น `3V3` เดียวกัน = **regulator 2 ตัวจ่ายไฟชนกัน** แรงดันไม่มีทางเท่ากันเป๊ะ ตัวที่สูงกว่าจะดันกระแสย้อนเข้าอีกตัว
  - โหลด 3.3V บนบอร์ดมีแค่ OLED (~20mA) + I2C pull-up + LDR divider ≈ **25mA** ซึ่ง regulator บน dev board จ่ายไหวสบาย
- **ระบบไฟ 12V จากรถ (TVS + MP1584EN) — ไม่ใช้** (ตัด 13 ส.ค. 2026) เปลี่ยนไปจ่ายไฟด้วย **power bank 5V ผ่าน USB-C** แทน
  - วงจร USB-C ที่มีอยู่แล้ว (USB1 + R8/R9 pulldown 5.1kΩ บน CC1/CC2) รองรับการรับไฟ 5V ได้ถูกต้องตามสเปก USB-C sink อยู่แล้ว **ไม่ต้องเพิ่มชิ้นส่วนใดๆ**
  - ประหยัดชิ้นส่วน ~3-11 ชิ้น และไม่ต้องกังวลเรื่อง noise จาก switching regulator บน layout

---

## 3. GPIO Assignment (ยืนยันแล้ว)

```
Camera DVP:    XCLK=15  PCLK=13  VSYNC=6  HREF=7
Camera Data:   D0=11 D1=9 D2=8 D3=10 D4=12 D5=18 D6=17 D7=16
Camera SCCB:   SDA=4  SCL=5
I2C หลัก (OLED เท่านั้น):  SDA=1  SCL=42
LDR (ADC):     GPIO 3
Buzzer:        GPIO 14
IR LED control: GPIO 21
LED เขียว:      GPIO 47
LED แดง:        GPIO 40
ปุ่ม USER:      GPIO 41
USB:           D-=19  D+=20
```

⚠️ **ห้ามใช้ GPIO 26–37 เด็ดขาด** — เป็น Flash + Octal PSRAM ภายใน module N16R8 ถ้าต่ออะไรเข้าไปบอร์ดจะ boot ไม่ขึ้น
(ยืนยันจาก pinout บอร์ดจริง: GPIO 35/36/37 = PSRAM — ต่อออกมาที่ header แต่ห้ามใช้)

### 📌 แก้ไข 13 ส.ค. 2026 — ปรับให้ตรงกับ pinout บอร์ดจริง (GOOUUU ESP32-S3-CAM)

ตรวจ schematic เทียบกับ pinout จริงของบอร์ดแล้ว พบว่า **ค่าที่วาดใน schematic ถูกต้องกว่าที่แผนเดิมเขียนไว้** จึงแก้แผนตาม:

| รายการ | แผนเดิม | แก้เป็น | เหตุผล |
|--------|---------|---------|--------|
| I2C SCL | GPIO 2 | **GPIO 42** | GPIO 2 บนบอร์ดนี้คือ **LED ON** (LED บนบอร์ด) ถ้าใช้เป็น SCL จะทำ LED กะพริบตามสัญญาณ I2C และโหลด LED กวนบัส |
| IR LED control | GPIO 42 | **GPIO 21** | GPIO 21 บนบอร์ดนี้เป็น GPIO เปล่า ไม่มีฟังก์ชันผูก — ว่างจริง |
| LED แดง | GPIO 48 | **GPIO 40** | GPIO 48 บนบอร์ดนี้คือ **WS2812** (RGB LED บนบอร์ด) จะแย่งสายข้อมูลกัน |
| Camera PWDN | GPIO 21 | **ตัดออก** | pinout บอร์ดจริงไม่ได้ต่อ PWDN ออกมาที่ header (ขากล้องทุกขามีป้าย CAM_* กำกับ ไม่มี GPIO 21) |

⚠️ **ข้อควรระวังที่เหลืออยู่:**
- **GPIO 40 = SD_DATA** ของ SD card slot บนบอร์ด — ถ้าอนาคตจะใช้ SD card เก็บ log **ต้องย้าย LED แดงหนี**
- **GPIO 3 = strapping pin (JTAG EN)** เลือกเป็น ADC1_CH2 ถูกแล้ว (ADC1 ใช้ร่วมกับ WiFi ได้ ต่างจาก ADC2 ที่ใช้ไม่ได้) แต่ต้องคุมแรงดันไม่ให้มั่วตอน boot
- **GPIO 42/41/40/39 = ขา JTAG** (MTMS/MTDI/MTDO/MTCK) ใช้เป็น GPIO ได้ แต่จะ debug ผ่าน JTAG ไม่ได้

*หมายเหตุ: GPIO 22 เดิมจองไว้สำหรับ MPU-6050 ตอนนี้ว่างแล้วหลังตัด IMU ออก*

---

## 4. Logic การตรวจจับความง่วง (ไม่มี IMU)

ระบบตรวจจับจาก **กล้องอย่างเดียว** โดยดึง 3 สัญญาณจากภาพเดียวกันเพื่อลด false positive:

1. **EAR (Eye Aspect Ratio)** — สัญญาณหลัก, threshold < 0.20 = ตาหลับ
2. **PERCLOS** — % ของเวลาที่ตาปิดใน 1 นาที, > 80% = ง่วงสะสม
3. **Blink rate** — ปกติ 15–20 ครั้ง/นาที, > 25 ครั้ง/นาที = สัญญาณเริ่มง่วง
4. **การหาว** — ≥ 2 ครั้งใน 5 นาที (ถ้า dataset มี Yawning class)

### เกณฑ์ Alert 4 ระดับ (ที่ 30fps จาก OV5640)

| Level | เงื่อนไข | Buzzer | LED | OLED |
|-------|---------|--------|-----|------|
| 0 ปกติ | EAR > 0.20 | ปิด | เขียวติดค้าง | ALERT |
| 1 เริ่มง่วง | ตาหลับ 45 frames (1.5s) หรือ blink rate สูง + หาว | บีบ×2 สั้น | แดงกะพริบช้า | DROWSY L1 |
| 2 ง่วงมาก | ตาหลับ 90 frames (3s) หรือ PERCLOS > 80% | บีบ×3 ดัง | แดงกะพริบเร็ว | DROWSY L2 |
| 3 อันตราย | ตาหลับ 150 frames (5s) ต่อเนื่อง | ต่อเนื่อง! | แดงติดค้าง | DANGER!!! + Line Notify |

⚠️ **ข้อจำกัดที่ต้องระบุในเอกสาร:** ตรวจจากดวงตาเท่านั้น ถ้าคนขับหลับแบบตายังลืมครึ่งตา หรือใส่แว่นกันแดดบังตา ระบบอาจตรวจไม่ทัน

---

## 5. ML Pipeline

- **Dataset ที่แนะนำ:** MRL Eye Dataset (Kaggle, ~84,000 รูป, มี infrared ด้วย) หรือ Driver Drowsiness Dataset DDD (~41,790 รูป)
- **Train:** Edge Impulse, MobileNetV2, INT8 quantized
- **Deploy:** TFLite-Micro บน ESP32-S3
- **Input model:** 96×96 pixel (crop เฉพาะดวงตา)
- **สถานะ:** ยังไม่เริ่ม train — รอ hardware มาก่อน

---

## 6. Design Rules สำหรับ PCB (ตามที่อาจารย์กำหนด)

อาจารย์กำหนดเข้มกว่า JLCPCB minimum (4mil) เพราะเผื่อ tolerance ±20%:

| รายการ | ค่าที่ต้องใช้ |
|--------|--------------|
| Track width ทั่วไป | 0.30mm (12mil) |
| Clearance | 0.20mm (8mil) |
| Power trace (3.3V) | 0.50mm |
| Power trace (5V/12V) | 0.80mm |
| PTH Annular Ring | 0.25mm |
| Via drill | 0.30mm |
| FPC connector zone (เฉพาะจุด) | 0.15mm (6mil ขั้นต่ำ) |

ตั้งเป็น Net Class 3 กลุ่มใน EasyEDA: `Default` (0.30mm), `POWER` (0.80mm), `FPC_CAM` (0.15mm)

---

## 7. สถานะปัจจุบัน (Timeline)

### ✅ เสร็จแล้ว
- [x] กรอกชื่อหัวข้อ + สมาชิก + Input/Output ในชีตอาจารย์
- [x] ยืนยัน hardware spec ทั้งหมด (ตาราง section 2)
- [x] กำหนด GPIO assignment ครบทุก pin
- [x] ออกแบบ Logic การตรวจจับ (EAR + PERCLOS + blink + yawn)
- [x] สร้างแผนงาน 8 phase, BOM list (LCSC parts), schematic guide, build guide
- [x] ตัดสินใจใช้ ESP32-S3 CAM แบบ pin header (ถอดเปลี่ยนได้) แทนบัดกรี WROOM ติดบอร์ด — อาจารย์อนุมัติแล้ว
- [x] ตัดสินใจตัด MPU-6050 (IMU) ออกจากระบบ

### 🔄 กำลังทำ
- [x] วาด Schematic ใน EasyEDA Pro — **วาดเสร็จแล้ว 1 sheet (P1) มี 30 ชิ้นส่วน 43 net** (ไม่ได้แยก 6 sheet ตามแผนเดิม รวมอยู่หน้าเดียว)
- [x] **แก้บั๊ก LDR divider** (13 ส.ค. 2026) — R1 เดิมต่อกับ `5V` ทำให้ตอนมืด (LDR ~1MΩ) แรงดันที่ GPIO 3 พุ่งถึง **4.95V** เกินพิกัด ESP32-S3 (สูงสุด 3.6V) → **ขาพังแน่นอน** ย้าย R1 ไปต่อ `3V3` แล้ว ตอนนี้แรงดันสูงสุด 3.27V ปลอดภัย
  - ⚠️ ผลข้างเคียง: สเกลค่า ADC เปลี่ยน ตอนเขียน firmware ต้องคาลิเบรต threshold แสงใหม่
- [x] **เพิ่มปุ่ม USER ที่ GPIO 41** (13 ส.ค. 2026) — SW1 (TS-1088-AR02016, LCSC C720477, **JLCPCB Basic Part**) + R10 10kΩ pull-up ไป 3V3 + C3 100nF debounce ลง GND · firmware อ่านด้วย `INPUT_PULLUP` (กด = LOW)
- [x] **ระบบไฟ: ใช้ power bank 5V ผ่าน USB-C** — ไม่ต้องเพิ่มชิ้นส่วน วงจร USB-C เดิมรองรับอยู่แล้ว

#### ⚠️ ข้อควรระวังของการใช้ power bank (ต้องทดสอบจริง)
- **power bank หลายรุ่นตัดไฟอัตโนมัติเมื่อกระแสต่ำ** (ต่ำกว่า ~50-100mA) ถ้าระบบ idle แล้วกินไฟน้อย power bank อาจดับเอง → ต้องทดสอบกับรุ่นที่จะใช้จริง หรือเลือกรุ่นที่มีโหมด "low-current / อุปกรณ์เล็ก"
- **ไม่เปิดเองตอนสตาร์ตรถ** ต้องกดปุ่มที่ power bank ทุกครั้ง (ต่างจากไฟรถที่มาเองเมื่อบิดกุญแจ)
- **ประมาณเวลาใช้งาน:** บอร์ดกินไฟ ~700mA ตอนพีค (ESP32 + กล้อง + IR LED 4 ดวง) → power bank 10,000mAh ใช้ได้ราว 10 ชั่วโมง

### ⏭️ ยังไม่เริ่ม
- [ ] **วาด board outline 80×70mm** — ⚠️ ตอนนี้ยังไม่มีขอบบอร์ดเลย (Layer 11 ว่าง) **JLCPCB ผลิตไม่ได้**
- [ ] **ขยายลายไฟให้ตรง Design Rules** — 5V ต้อง 0.80mm (ตอนนี้ 0.610mm ❌), 3.3V ต้อง 0.50mm (ตอนนี้ต่ำสุด 0.486mm ⚠️)
- [ ] PCB Layout + DRC (80×70mm) — เดินลายไปแล้ว 340 segment / 33 via / GND pour 3 ก้อน ยังไม่รัน DRC
- [ ] Order PCB + Component ที่ JLCPCB
- [ ] หา dataset + train model บน Edge Impulse
- [ ] เขียน firmware (Arduino/ESP-IDF) — TFLite-Micro inference + GPIO control
- [ ] ประกอบ PCBA + ทดสอบจริง
- [ ] เขียน Google Doc project report

---

## 8. Naming Convention ที่อยากให้ Claude Code ใช้ (ถ้าจะช่วยเขียนโค้ด)

```
ตัวแปร/ฟังก์ชัน: snake_case (ตาม convention ทั่วไปของ Arduino/ESP-IDF C++)
Net name ใน schematic: ตัวพิมพ์ใหญ่ + underscore เช่น CAM_XCLK, I2C_SDA
ไฟล์ firmware: แยกตาม module เช่น camera.cpp, alert.cpp, ml_inference.cpp
```

## 9. Framework ที่จะใช้เขียน Firmware

- **Arduino IDE** (framework หลัก, ยังไม่ยืนยัน 100% — อาจสลับเป็น ESP-IDF ถ้า TFLite-Micro integration ง่ายกว่า)
- Library ที่คาดว่าจะใช้: `esp32-camera`, `TensorFlowLite_ESP32` หรือ Edge Impulse exported SDK, `Adafruit_SSD1306` (OLED)

---

## 10. ไฟล์อ้างอิงที่มีอยู่แล้ว (จาก Claude.ai)

ทีมมีเอกสารเหล่านี้แล้ว ถ้าต้องการรายละเอียดเพิ่มให้ขอทีมแนบมาให้:
- `drowsiness_project_plan.html` — แผนงาน 8 phase แบบ interactive checklist
- `bom_pcb.html` — BOM ครบ 45 รายการ พร้อม LCSC part number
- `schematic_guide.html` — คู่มือวาด schematic ทีละ sheet
- `build_guide_lcsc.html` — รวม BOM + wiring diagram + Design Rules ต่อบล็อก
- `system_logic.html` — Input/Output mapping + EAR/PERCLOS logic แบบละเอียด
- `lcsc_parts_easyeda.html` — รายการ LCSC part number สำหรับค้นใน EasyEDA
