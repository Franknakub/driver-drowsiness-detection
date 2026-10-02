# Driver Drowsiness Detection

ระบบตรวจจับความง่วงของคนขับด้วยกล้อง + TinyML บน ESP32-S3
โปรเจกต์วิชา Embedded System 03603323 · กลุ่ม 17 · มหาวิทยาลัยเกษตรศาสตร์ วิทยาเขตศรีราชา · ส่งงาน 3–5 พ.ย. 2026

```
LDR วัดแสง → มืด? → เปิด IR LED → กล้องถ่ายหน้า → หาตา → crop 96×96
   → TinyML ตัดสินตาเปิด/ปิด → นับเวลาตาปิด / PERCLOS
   → buzzer + LED + OLED + แจ้งเตือนมือถือ
```

PCB ในโปรเจกต์นี้เป็น **carrier board** ไม่ได้ประมวลผลเอง โมดูล ESP32-S3-CAM (N16R8) เสียบลงบน header 2×20 ส่วนบอร์ดทำหน้าที่จ่ายไฟและเดินสายไปเซนเซอร์กับตัวเตือน กล้องเสียบที่ FPC ของโมดูลโดยตรง ไม่ผ่าน PCB

## สถานะ (2 ต.ค. 2026)

| ส่วน | สถานะ |
|---|---|
| Schematic + PCB | เสร็จ, DRC 0 error, สั่งผลิตที่ JLCPCB แล้ว (ชุด 31 ส.ค.) |
| ตรวจบอร์ดโมดูล | ESP32-S3 rev2, Flash 16 MB, PSRAM 8 MB, กล้อง **OV5640** |
| Firmware | มีสเก็ตช์ตรวจฮาร์ดแวร์ ตัวจริงยังไม่เริ่ม ดูแผน |
| ML | เลือก dataset แล้ว (MRL Eye) ยังไม่ train |
| กล่อง 3D | วางแผนแล้ว ยังไม่ออกแบบ |

## โครงสร้าง repo

```
docs/
  firmware-plan.html        แผน firmware, ML, เว็บ, กล่อง 3D, timeline (เปิดในเบราว์เซอร์)
  PROJECT_STATUS.md         สเปกฮาร์ดแวร์ + GPIO + logic (เขียนช่วง ส.ค. บางส่วนล้าสมัย)
  reviews/                  รีวิว schematic/PCB วันที่ 14 ส.ค.
  archive/early-design/     เอกสารออกแบบช่วงแรก (ใช้ไฟ 12V, มี IMU) เก็บไว้อ้างอิงเท่านั้น
hardware/
  easyeda/                  ไฟล์โปรเจกต์ EasyEDA Pro (.epro2)
  fabrication/              Gerber + BOM + Pick and Place
firmware/
  DrowsinessDiag/           สเก็ตช์ตรวจบอร์ด กล้อง และทดสอบ IR filter
ml/                         แผน dataset และการ train
enclosure/                  ข้อกำหนดกล่อง 3D
```

## GPIO

```
Camera DVP:  XCLK=15 PCLK=13 VSYNC=6 HREF=7 · D0-D7=11,9,8,10,12,18,17,16 · SCCB SDA=4 SCL=5
OLED I2C:    SDA=1  SCL=42        LDR (ADC1): 3
Buzzer: 14   IR LED array: 21     LED เขียว: 47   LED แดง: 40
```

ห้ามใช้ GPIO 26–37 (Flash + Octal PSRAM ในโมดูล) · GPIO 2 = LED บนโมดูล · GPIO 48 = WS2812 บนโมดูล

## เริ่มต้น

- Firmware: ดู [firmware/README.md](firmware/README.md)
- ฮาร์ดแวร์และไฟล์ผลิต: ดู [hardware/README.md](hardware/README.md)
- แผนทั้งหมด: เปิด [docs/firmware-plan.html](docs/firmware-plan.html)
