// หาหน้า + จุดตาด้วย ESP-DL HumanFaceDetect (MSR+MNP) · ย้ายมาจาก FaceSpike
#pragma once
#include "esp_camera.h"

struct FaceEyes {
  bool found;
  float score;
  int box[4];               // x0,y0,x1,y1 ในภาพที่หมุนแล้ว
  int eye[2][2];            // [0] = ตาที่อยู่ซ้ายของภาพ (= ตาขวาของคนขับ), [1] = ขวาของภาพ · (x,y)
  int compDeg;              // มุมชดเชยที่ใช้กับเฟรมนี้
  float rotateMs, detectMs;
  const camera_fb_t* frame; // ภาพ RGB565 ที่ใช้หาหน้า (หมุนแล้ว) · ใช้ได้จนกว่าจะเรียก faceEyesRun ครั้งถัดไป
};

bool faceEyesInit();
void faceEyesRun(camera_fb_t* raw, FaceEyes& out);

// ภาพที่หมุนแค่ CAM_ROTATE_CW (ไม่รวมมุมชดเชย) สำหรับแสดงผล · ใช้ buffer แยกจากภาพที่ใช้หาหน้า
camera_fb_t* faceEyesUprightView(camera_fb_t* raw);

// แปลงพิกัดในภาพที่หมุนชดเชย compDeg แล้ว กลับเป็นพิกัดในภาพตั้งตรง (ขนาด w x h)
void faceEyesToUpright(int compDeg, int w, int h, float x, float y, float& ox, float& oy);
