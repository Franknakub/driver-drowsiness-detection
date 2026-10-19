# Firmware

| สเก็ตช์ | หน้าที่ |
|---|---|
| `DrowsinessDiag/` | ตรวจ Flash/PSRAM, ระบุรุ่นกล้อง, ทดสอบ IR-cut filter (ล็อก exposure แล้วนับพิกเซลสว่าง) |
| `BenchTest/` | ทดสอบ LED, LDR, OLED, buzzer, IR (+ นับพิกเซลจากกล้อง) บนบอร์ด v1 แบบไม่เสียบโมดูล ต่อสายตาม [docs/bench-wiring-v1.md](../docs/bench-wiring-v1.md) |
| `DrowsyFW/` | firmware ตัวจริง: หาหน้า (ESP-DL) + โมเดลตา + logic ความง่วง + Alert + บันทึก Trip ลง FFat + Sync ขึ้น Supabase + Device Dashboard + OTA · ยังไม่มี OLED · แผนอยู่ใน [docs/firmware-plan.html](../docs/firmware-plan.html) · ฝั่ง cloud ดู [cloud/README.md](../cloud/README.md) |
| `archive/DrowsyFW-pio/` | DrowsyFW รุ่นแรก (PlatformIO, 8 ต.ค. จาก PR #1): กรอบตาคงที่ + MobileNetV2 96×96 ~3 fps · เก็บไว้อ้างอิง ไม่ได้พัฒนาต่อ ดู [README](archive/DrowsyFW-pio/README.md) |

## Toolchain

- Arduino IDE 2.3.x + core `esp32 by Espressif Systems` **3.3.12**
- บอร์ด: ESP32-S3-CAM N16R8 (GOOUUU)

```bash
FQBN="esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile --fqbn "$FQBN" firmware/DrowsinessDiag
arduino-cli upload -p COM6 --fqbn "$FQBN" firmware/DrowsinessDiag
```

- **`PSRAM=opi` ต้องตั้ง** N16R8 เป็น Octal PSRAM ถ้าตั้งเป็น QSPI `psramFound()` จะคืน false
- **`upload` ไม่ compile ให้** แก้โค้ดแล้วต้อง compile ก่อน ไม่งั้นจะแฟลชไบนารีเก่า (สังเกตจาก `No changed sectors found`)
- Partition `app3M_fat9M_16MB` ใช้กับสเก็ตช์ทดสอบได้ แต่ **DrowsyFW ใช้ `PartitionScheme=custom`** กับ `partitions.csv` ในโฟลเดอร์สเก็ตช์ (app 6 MB × 2 + FFat ~3.9 MB) เพราะ face detect + WiFi ใหญ่ 3.18 MB แล้ว

### DrowsyFW

```bash
cp firmware/DrowsyFW/secrets.example.h firmware/DrowsyFW/secrets.h   # แล้วใส่ WiFi/รหัสจริง · ห้าม commit
FQBN="esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom"
arduino-cli compile --fqbn "$FQBN" --export-binaries firmware/DrowsyFW
arduino-cli upload -p COM6 --fqbn "$FQBN" firmware/DrowsyFW      # ครั้งแรกต้องผ่าน USB เพราะเปลี่ยน partition
```

- ครั้งต่อไปอัปเดตผ่านเว็บได้: ต่อ WiFi `Drowsy-XXXX` → เปิด `http://192.168.4.1/update` (ผู้ใช้ `admin` รหัส `OTA_PASSWORD`) → อัปโหลด `build/esp32.esp32.esp32s3/DrowsyFW.ino.bin`
- `arduino-cli` แสดงขนาดสูงสุด 16 MB เมื่อใช้ `custom` แต่ช่องโปรแกรมจริงมี **6 MB** ต้องดูขนาดเองทุกครั้ง
- ไลบรารีที่ต้องลง: `arduino-cli lib install "Async TCP" "ESP Async WebServer"` (ของ ESP32Async)

## แฟลชผ่าน USB

บอร์ดมี USB 2 ช่อง แต่ละช่องทำงานต่างกัน

| VID:PID | คืออะไร | แฟลชได้ไหม |
|---|---|---|
| `303A:1001` | USB-Serial-JTAG ใน ROM ของ S3 | ✅ (= อยู่ใน download mode) |
| `303A:4001` | USB CDC ที่สเก็ตช์สร้าง | ❌ esptool รีเซ็ตไม่ได้ |
| `1A86:55D3` | ชิป CH343 (UART) | ⚠️ auto-reset ไม่ทำงาน ต้องกดปุ่มเอง |

ถ้าโมดูลเสียบอยู่บน PCB ให้เลื่อน **SW1 ไปที่ OFF** ก่อนเสียบสายคอม (ขา 5V ของโมดูลต่อตรงกับราง 5V ของบอร์ด) และ USB-C บน PCB (USB1) รับไฟอย่างเดียว แฟลชไม่ได้

ลำดับที่ใช้ได้: ถอดสาย → กด BOOT ค้าง → เสียบช่อง native USB → รอ 2 วิ → ปล่อย BOOT → เช็คว่าได้ `303A:1001` → compile + upload → กด RST 1 ครั้ง

| error | สาเหตุ |
|---|---|
| `Invalid head of packet (0x1B)` | ยังรันเฟิร์มแวร์เดิม ไม่ได้เข้า download mode |
| `No serial data received` | เปิดพอร์ตได้แต่ชิปไม่ได้อยู่ใน download mode |
| `Hard resetting via RTS pin` แล้วเงียบ | native USB ไม่มีสาย RTS ไปขา EN ต้องกด RST เอง |
| `A device attached to the system is not functioning` | USB-Serial-JTAG ค้าง ถอดเสียบใหม่ |

## กล้อง OV5640

- ใช้ `FRAMESIZE_SVGA` ไม่ต้องใช้ความละเอียดสูง เพราะภาพจะถูกย่อเหลือ 96×96
- ไม่ต้องโหลด AF firmware ปล่อยเป็น fixed-focus เพราะคนขับนั่งระยะคงที่
- **ล็อก exposure/gain ก่อนวัดค่าความสว่างเสมอ** ถ้าปล่อย AGC/AEC ไว้ กล้องจะชดเชยแสง IR จนค่าไม่ขยับ
- **อย่าใช้ค่า peak ของพิกเซลเดียว** hot pixel จุดเดียวดันค่าขึ้นเต็มได้ ให้นับจำนวนพิกเซลที่เกินเกณฑ์แทน
