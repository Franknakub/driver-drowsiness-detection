# ML

ML ทำงาน 2 ขั้น

1. **หาหน้าและตำแหน่งตา** ใช้ `HumanFaceDetect` ของ ESP-DL (MSR+MNP) ได้จุดตาซ้าย/ขวามาด้วย ไม่ต้องใช้ ESP-IDF เพราะ Arduino core 3.3.12 มี `esp-dl` 3.3.11 มาให้แล้ว (ไฟล์โมเดล `.espdl` ต้องเอามาจาก repo esp-dl) ยังไม่ได้ทดลองบนบอร์ด ถ้าใช้ไม่ได้ให้ถอยไปใช้กรอบตาคงที่ที่ตั้งจากหน้าเว็บ
2. **ตัดสินตาเปิด/ปิด** crop รอบจุดตา → grayscale 64×64 → CNN เล็ก INT8 ที่ train เองด้วย Keras บน Colab → TFLite Micro (`esp-tflite-micro` 1.4.0 มากับ core) → P(ตาปิด)

## Dataset

| Dataset | ใช้ทำอะไร |
|---|---|
| [MRL Eye Dataset](https://mrl.cs.vsb.cz/eyedataset.html) | **ตัวหลัก** ภาพตา IR 84,898 ภาพ จาก 37 คน ถ่ายในบริบทขับรถ มี label ตาเปิด/ปิดในชื่อไฟล์ (~326 MB) |
| [CEW](https://parnec.nuaa.edu.cn/_upload/tpl/02/db/731/template731/pages/xtan/ClosedEyeDatabases.html) | ตัวเสริม ภาพตากลางวัน 24×24 |
| ภาพจากกล้องเราเอง | **ต้องมี** สำหรับ fine-tune ราว 300–500 ภาพต่อ class ทั้งกลางวัน กลางคืน ใส่/ไม่ใส่แว่น |

ชื่อไฟล์ MRL: `subject_image_gender_glasses_eyestate_reflections_lighting_sensor` โดย `eyestate` 0 = ปิด, 1 = เปิด

- **แบ่ง train/test ตาม subject ID** ไม่ใช่สุ่มรายภาพ ไม่งั้นความแม่นจะสูงเกินจริง
- ขนาดกรอบ crop ตอนใช้งานจริงต้องใกล้กับภาพใน MRL
- ถ้าใช้ MRL ในรายงาน ให้อ้างอิงเปเปอร์ *"Pupil localization using geodesic distance"*

ตัว dataset ไม่ commit ลง repo ให้วางไว้ที่ `ml/data/` (อยู่ใน `.gitignore`)
