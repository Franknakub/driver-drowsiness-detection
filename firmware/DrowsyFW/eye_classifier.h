// โมเดลตาเปิด/ปิด (train เองจาก MRL · ml/train.ipynb) บน TFLite Micro
#pragma once
#include "esp_camera.h"

bool eyeClassifierInit();

// ตัด Eye Region สี่เหลี่ยมจัตุรัสด้าน side px รอบ (cx, cy) จากภาพ RGB565 แล้วคืน P(ตาปิด)
// คืน -1 ถ้ากรอบเล็กเกินหรือหลุดขอบภาพ · slot = 0/1 (ตาซ้าย/ขวาของภาพ) ใช้เก็บภาพไว้ดีบัก
float eyeClassify(const camera_fb_t* frame, int cx, int cy, int side, int slot);

// ภาพ 32x32 ที่ส่งเข้าโมเดลล่าสุดของตาแต่ละข้าง (slot 0/1) สำหรับดีบัก · คืน false ถ้ายังไม่มี
bool eyeLastInput(int slot, uint8_t* out);
