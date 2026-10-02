# Firmware

| สเก็ตช์ | หน้าที่ |
|---|---|
| `DrowsinessDiag/` | ตรวจ Flash/PSRAM, ระบุรุ่นกล้อง, ทดสอบ IR-cut filter (ล็อก exposure แล้วนับพิกเซลสว่าง) |
| `DrowsyFW/` | firmware ตัวจริง ยังไม่ได้เขียน ดูโครงสร้างที่วางไว้ใน [docs/firmware-plan.html](../docs/firmware-plan.html) |

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
- Partition `app3M_fat9M_16MB` มี `ota_0` / `ota_1` รองรับ OTA และมี FFat 9.9 MB

## แฟลชผ่าน USB

บอร์ดมี USB 2 ช่อง แต่ละช่องทำงานต่างกัน

| VID:PID | คืออะไร | แฟลชได้ไหม |
|---|---|---|
| `303A:1001` | USB-Serial-JTAG ใน ROM ของ S3 | ✅ (= อยู่ใน download mode) |
| `303A:4001` | USB CDC ที่สเก็ตช์สร้าง | ❌ esptool รีเซ็ตไม่ได้ |
| `1A86:55D3` | ชิป CH343 (UART) | ⚠️ auto-reset ไม่ทำงาน ต้องกดปุ่มเอง |

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
