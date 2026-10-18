// หาหน้า + จุดตาด้วย ESP-DL HumanFaceDetect (MSR+MNP) · ย้ายมาจาก FaceSpike
#pragma once
#include "esp_camera.h"
#include "face_view.h"

// FaceView (face_view.h) + ภาพดิบของเฟรมนี้ · ใช้ตัด Eye Region โดยไม่ต้องหมุนทั้งเฟรม
struct FaceViewMap : FaceView {
  const camera_fb_t* raw = nullptr;
  // pointer ไปยังพิกเซล RGB565 ในภาพดิบ · nullptr = ตกนอกภาพ (ของเดิมเติมดำ)
  const uint8_t* px(int x, int y) const;
};

struct FaceEyes {
  bool found;
  float score;
  int box[4];               // x0,y0,x1,y1 ในภาพที่ใช้หาหน้า (ตั้งตรง + หมุนชดเชย)
  int eye[2][2];            // [0] = ตาที่อยู่ซ้ายของภาพ (= ตาขวาของคนขับ), [1] = ขวาของภาพ · (x,y)
  int compDeg;              // มุมชดเชยที่ใช้กับเฟรมนี้
  float rotateMs, detectMs; // rotateMs = หมุนภาพก่อนหาหน้า (ทางใหม่ไม่มีขั้นนี้แล้ว เหลือแค่คำนวณแผนที่)
  float preMs;              // ส่วนหนึ่งของ detectMs: หยิบพิกเซลจากภาพดิบ (หมุน + ย่อ) + แปลงสีเข้าโมเดลทั้งสองขั้น
  FaceViewMap view;         // ใช้ได้ตราบที่ยังไม่คืน fb ของเฟรมนี้
};

bool faceEyesInit();
void faceEyesRun(camera_fb_t* raw, FaceEyes& out);

// ทางเดิม (0.2.3): หมุนทั้งเฟรมลง buffer แล้วหาหน้าบนภาพนั้น · ใช้เทียบ A/B เท่านั้น ไม่ปรับมุมชดเชย
// out.view ชี้ไปภาพที่หมุนแล้ว (ใช้ได้จนกว่าจะเรียกฟังก์ชันนี้ครั้งถัดไป)
void faceEyesRunLegacy(camera_fb_t* raw, int compDeg, FaceEyes& out);
// จำนวน byte ของ model input ขั้น MSR ที่ต่างกันระหว่างทางเดิมกับทางใหม่ (ควรเป็น 0 เมื่อ compDeg = 0) · -1 = จองหน่วยความจำไม่ได้
// ms[0] = เวลา preprocess ทางเดิม (ย่อภาพที่หมุนแล้ว + แปลงสี) · ms[1] = ทางใหม่ (หยิบพิกเซลจากภาพดิบ + แปลงสี)
int faceEyesCompareInput(camera_fb_t* raw, int compDeg, float ms[2]);

// ภาพที่หมุนแค่ CAM_ROTATE_CW (ไม่รวมมุมชดเชย) สำหรับแสดงผล · ใช้ buffer แยกจากภาพที่ใช้หาหน้า
camera_fb_t* faceEyesUprightView(camera_fb_t* raw);

// แปลงพิกัดในภาพที่หมุนชดเชย compDeg แล้ว กลับเป็นพิกัดในภาพตั้งตรง (ขนาด w x h)
void faceEyesToUpright(int compDeg, int w, int h, float x, float y, float& ox, float& oy);
