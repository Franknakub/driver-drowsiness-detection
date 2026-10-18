// "ภาพที่ใช้หาหน้า" (ตั้งตรงตาม CAM_ROTATE_CW + หมุนชดเชยหน้าเอียง ang องศา) ที่ไม่ได้สร้างจริงในหน่วยความจำ
// บอกว่าพิกเซล (x,y) ของภาพนี้อยู่ตรงไหนในภาพดิบจากกล้อง · สูตร fixed-point 16.16 เดียวกับการหมุนทั้งเฟรมแบบเดิม
// (rotateInto ใน face_eye.cpp) ทุกบิต -> ทั้งขั้นหาหน้าและ Eye Region ได้พิกเซลเดียวกับของเดิม
#pragma once
#include <stdint.h>
#include <math.h>

struct FaceView {
  int width = 0, height = 0;     // ขนาดภาพที่ใช้หาหน้า
  int raw_w = 0, raw_h = 0;      // ขนาดภาพดิบ
  int rot = 0, ang = 0;          // มุมหมุนทีละ 90° และมุมชดเชย
  float ca = 1, sa = 0, cx = 0, cy = 0;
  int32_t dpx = 65536, dpy = 0;
  int idx_a = 1, idx_b = 0, idx_c = 0;   // index ในภาพดิบของพิกเซลตั้งตรง (px,py) = idx_c + idx_a*px + idx_b*py

  // จุดเริ่มแถว y ของการหมุนชดเชยรอบจุดกลาง · ต้องเป็นนิพจน์เดียวกับ compRowStart ของ rotateInto
  void row_start(int y, int32_t& fx, int32_t& fy) const {
    fx = (int32_t)lroundf((cx - cx * ca + (y - cy) * sa + 0.5f) * 65536);
    fy = (int32_t)lroundf((cy + cx * sa + (y - cy) * ca + 0.5f) * 65536);
  }

  // index พิกเซลในภาพดิบของ (x,y) · fx0/fy0 = row_start(y) (ไม่ใช้เมื่อ ang = 0) · -1 = ตกนอกภาพ (ของเดิมเติมดำ)
  int raw_index(int x, int y, int32_t fx0, int32_t fy0) const {
    int px = x, py = y;
    if (ang) {
      px = (fx0 + x * dpx) >> 16;   // = rotateInto บวก dpx ทีละพิกเซลจากต้นแถว
      py = (fy0 + x * dpy) >> 16;
      if (px < 0 || py < 0 || px >= width || py >= height) return -1;
    }
    return idx_c + idx_a * px + idx_b * py;
  }

  static FaceView make(int raw_w, int raw_h, int rot, int ang) {
    FaceView v;
    v.raw_w = raw_w; v.raw_h = raw_h; v.rot = rot; v.ang = ang;
    v.width = (rot == 90 || rot == 270) ? raw_h : raw_w;
    v.height = (rot == 90 || rot == 270) ? raw_w : raw_h;
    v.ca = cosf(ang * (float)M_PI / 180); v.sa = sinf(ang * (float)M_PI / 180);
    v.cx = v.width / 2.0f; v.cy = v.height / 2.0f;
    v.dpx = (int32_t)lroundf(v.ca * 65536); v.dpy = (int32_t)lroundf(-v.sa * 65536);
    // (sx,sy) ของ uprightToRaw: 0 = (px,py) · 90 = (py, h-1-px) · 180 = (w-1-px, h-1-py) · 270 = (w-1-py, px)
    if (rot == 0)        { v.idx_a = 1;      v.idx_b = raw_w;  v.idx_c = 0; }
    else if (rot == 90)  { v.idx_a = -raw_w; v.idx_b = 1;      v.idx_c = (raw_h - 1) * raw_w; }
    else if (rot == 180) { v.idx_a = -1;     v.idx_b = -raw_w; v.idx_c = (raw_h - 1) * raw_w + raw_w - 1; }
    else                 { v.idx_a = raw_w;  v.idx_b = -1;     v.idx_c = raw_w - 1; }
    return v;
  }
};
