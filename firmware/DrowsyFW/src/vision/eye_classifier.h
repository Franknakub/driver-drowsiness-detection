// โมเดลตาเปิด/ปิด (train เองจาก MRL · ml/train.ipynb) บน TFLite Micro
#pragma once
#include "face_eye.h"

bool eyeClassifierInit();

// ตัด Eye Region สี่เหลี่ยมจัตุรัสด้าน side px รอบ (cx, cy) ในภาพที่ใช้หาหน้า (หยิบจากภาพดิบผ่าน v) แล้วคืน P(ตาปิด)
// คืน -1 ถ้ากรอบเล็กเกินหรือหลุดขอบภาพ · slot = 0/1 (ตาซ้าย/ขวาของภาพ) ใช้เก็บภาพไว้ดีบัก
float eyeClassify(const FaceViewMap& v, int cx, int cy, int side, int slot);

// แค่ตัด Eye Region เป็น gray side x side (ก่อนย่อ) ลง out · false = กรอบใช้ไม่ได้ · ใช้เทียบ A/B
bool eyeCropGray(const FaceViewMap& v, int cx, int cy, int side, uint8_t* out);

// ภาพ 32x32 ที่ส่งเข้าโมเดลล่าสุดของตาแต่ละข้าง (slot 0/1) สำหรับดีบัก · คืน false ถ้ายังไม่มี
bool eyeLastInput(int slot, uint8_t* out);
