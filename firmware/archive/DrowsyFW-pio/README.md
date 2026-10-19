# DrowsyFW

Firmware ตัวจริง (PlatformIO, pioarduino / Arduino core 3.3.12) ตอนนี้มีเฉพาะส่วนกล้อง + ML ยังไม่มี buzzer / OLED / LDR / IR

```
OV5640 grayscale SVGA → กรอบตาคงที่ (ตั้งผ่าน Serial) → crop 96×96 → MobileNetV2 INT8 (TFLite Micro) → P(ตาปิด)
```

ผลบนบอร์ดจริง: ~3 fps ที่ SVGA, โมเดลใช้ ~222 ms/ภาพ, arena 286 KB ใน PSRAM

## Build / Upload

```bash
pio run -t upload --upload-port COM4
```

- `platformio.ini` ตั้ง `core_dir = C:/pio` เพราะ path ของ core 3.x ยาวเกิน 260 ตัวอักษรบน Windows ที่ไม่ได้เปิด Long Paths
- build จาก PowerShell / cmd ไม่ใช่ Git Bash (pioarduino ไม่รองรับ MSYS)

## Serial (115200)

| คำสั่ง | ทำอะไร |
|---|---|
| `roi x y w h` | ตั้งกรอบตาในพิกัดภาพกล้อง |
| `stream 1` / `stream 0` | ส่งภาพให้ `ml/eye_viewer.py` |
| `reg`, `xclk`, `fs`, `sharp`, `contrast`, `pull`, `scan` | คำสั่ง debug กล้อง ใช้ตอนไล่ปัญหา |

## ML

- `ml/train_eye.py` train จาก MRL Eye Dataset (วางที่ `ml/data/mrlEyes_2018_01/`) แบ่ง train/val/test ตาม subject แล้ว export INT8 ลง `src/model_data.cpp`
  ผล: test accuracy 91.2% (INT8) บน 7,190 ภาพของคนที่ไม่เคยเห็น
- `ml/eye_viewer.py` ดูภาพสดจากบอร์ด ลากกรอบครอบตา กด `o` / `c` เก็บภาพตาเปิด/ปิดไว้ fine-tune ที่ `ml/data/own/`

## ข้อควรรู้

- **บอร์ดเขียว (PCB1_1) ทำให้ภาพกล้องเพี้ยน**: เสียบโมดูลบนบอร์ดเขียวแล้วบิต D6 (GPIO17) อ่านผิด เกิดวงขาว/ดำรอบของสว่าง ภาพ SVGA มีเส้นลาย และ JPEG ถอดรหัสไม่ได้ ถอดโมดูลออกจากบอร์ดเขียวแล้วภาพสะอาด ต้องตรวจขา 17 บน PCB
- ตอนเปิด lens correction (0x5000 bit7) ภาพทดสอบแบบไล่ระดับของ OV5640 มีวงโค้งเพี้ยน
