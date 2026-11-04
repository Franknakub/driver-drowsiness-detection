# Driver Drowsiness Detection

ระบบตรวจจับความง่วงของคนขับด้วยกล้อง + TinyML บน ESP32-S3
โปรเจกต์วิชา Embedded System 03603323 · กลุ่ม 17 · มหาวิทยาลัยเกษตรศาสตร์ วิทยาเขตศรีราชา · ส่งงาน 3–5 พ.ย. 2026

```
กล้อง OV5640 → ROI 4:3 รอบหน้า → ESP-DL หาหน้าและจุดตา → ตัดภาพตา 32×32
   → CNN INT8 ตัดสินตาเปิด/ปิด → ตาปิด 2 / 3 วินาที และ PERCLOS
   → buzzer + LED + OLED → บันทึก Trip ลง Flash → Sync Supabase → Cloud Dashboard
```

PCB ในโปรเจกต์นี้เป็น **carrier board** ไม่ได้ประมวลผลเอง โมดูล ESP32-S3-CAM (N16R8) เสียบลงบน header 2×20 ส่วนบอร์ดทำหน้าที่จ่ายไฟและเดินสายไปเซนเซอร์กับตัวเตือน กล้องเสียบที่ FPC ของโมดูลโดยตรง ไม่ผ่าน PCB

## สถานะ (5 พ.ย. 2026)

| ส่วน | สถานะ |
|---|---|
| Schematic + PCB | **v2 = บอร์ดสุดท้าย** ผลิตและประกอบแล้ว DRC 0 error ไฟล์ผลิตอยู่ที่ `hardware/fabrication/2026-10-03_v2/` |
| Firmware | `firmware/DrowsyFW` เวอร์ชัน 0.3.20-dash รันบนบอร์ดแล้ว |
| ML | โมเดลตา `deep2_half_p8` INT8 27.5 KB accuracy 96.2% กับคนที่โมเดลไม่เคยเห็น (MRL Eye Dataset) |
| Cloud | Supabase + [Cloud Dashboard](https://franknakub.github.io/driver-drowsiness-detection/dashboard/) |
| กล่อง 3D | ปริ้น ASA แล้ว ไฟล์ส่งร้านอยู่ที่ `enclosure/print/` |
| ทดสอบในรถ | รถจอดกลางวันและขับจริง 1 รอบ (4 พ.ย.) ผลอยู่ใน [ml/incar/README.md](ml/incar/README.md) |
| ข้อจำกัดหลัก | ยังใช้ในที่มืดไม่ได้ เพราะ IR บนบอร์ดสว่างไม่พอ · หันหน้าออกนอกกล้องแล้วตรวจไม่ได้และเตือนผิดได้ |

## โครงสร้าง repo

```
firmware/
  DrowsyFW/                 firmware ตัวจริง (Arduino sketch)
  BenchTest/ DrowsinessDiag/ สเก็ตช์ทดสอบฮาร์ดแวร์
  tests/                    เทสต์ logic และ Trip บน PC
  archive/                  โค้ดรุ่นเก่า
ml/
  train.ipynb               เทรนโมเดลตาและแปลงเป็น INT8
  incar/                    เครื่องมือและผลทดสอบในรถ
cloud/
  sql/                      ตาราง, RPC ingest, RLS ของ Supabase
  test/                     เทสต์ SQL และพรีวิว Dashboard
hardware/
  easyeda/                  ไฟล์โปรเจกต์ EasyEDA Pro v2 (.epro2) · archive/ = v1
  fabrication/              Gerber + BOM + Pick and Place · archive/ = ชุดเก่า
enclosure/
  cad/                      สคริปต์ build123d
  print/                    ไฟล์ส่งร้านปริ้นตามวันที่
docs/
  dashboard/                Cloud Dashboard (GitHub Pages)
  status/STATUS.md          สถานะและงานที่เหลือ
  hardware/                 ผังต่อสายทดสอบ
  adr/                      บันทึกการตัดสินใจ
  reviews/                  รีวิว schematic และ PCB
  firmware-plan.html        แผน firmware, ML, เว็บ, กล่อง 3D
  archive/                  เอกสารช่วงแรก เก็บไว้อ้างอิง
GLOSSARY.md                 คำศัพท์ของโปรเจกต์
```

## GPIO

```
Camera DVP:  XCLK=15 PCLK=13 VSYNC=6 HREF=7 · D0-D7=11,9,8,10,12,18,17,16 · SCCB SDA=4 SCL=5
OLED I2C:    SDA=42 SCL=1         LDR (ADC1): 3
Buzzer: 14   IR LED array: 21     LED เขียว: 47   LED แดง: 40
```

ห้ามใช้ GPIO 26–37 (Flash + Octal PSRAM ในโมดูล) · GPIO 2 = LED บนโมดูล · GPIO 48 = WS2812 บนโมดูล

## เริ่มต้น

- Firmware: ดู [firmware/README.md](firmware/README.md)
- ฮาร์ดแวร์และไฟล์ผลิต: ดู [hardware/README.md](hardware/README.md)
- แผนทั้งหมด: เปิด [docs/firmware-plan.html](docs/firmware-plan.html)
- สถานะล่าสุด: ดู [docs/status/STATUS.md](docs/status/STATUS.md)
