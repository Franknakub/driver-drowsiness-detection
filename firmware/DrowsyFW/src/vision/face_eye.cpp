// หาหน้า + จุดตา (ESP-DL) · ตรรกะหมุนภาพและชดเชยหน้าเอียงย้ายมาจาก FaceSpike (ทดสอบ 16–17 ต.ค.)
// 0.2.4: ไม่หมุนทั้งเฟรมก่อนหาหน้าแล้ว · ส่ง "แผนที่" ภาพดิบ -> ภาพตั้งตรง+ชดเชย ให้ run_view() หยิบเฉพาะพิกเซลที่
// โมเดลใช้ (หมุน+ย่อในขั้นเดียว ขนาดเท่า model input) · พิกัดผลลัพธ์อยู่ในระบบเดิมทุกอย่าง
#include "face_eye.h"
#include "esp_heap_caps.h"
#include "human_face_detect.hpp"
#include "../core/config.h"

static HumanFaceDetect* s_detector = nullptr;

struct RotBuf {
  uint8_t* buf = nullptr;
  size_t size = 0;
  camera_fb_t fb;
};
static RotBuf s_rb;

// ---- ชดเชยหน้าเอียง (โมเดลทน roll ได้ราว ±10–15°) + บริเวณที่ส่งเข้า MSR (ROI) ----
// ภาพตั้งตรง 320x480 ย่อทั้งเฟรมลง input MSR 160x120 = กว้างเหลือ 1/2 สูงเหลือ 1/4 -> หน้าไกลเล็กกว่า anchor
// เล็กสุด (16 px) และถูกบีบเตี้ย · 0.3.9: ส่งแค่ส่วน 4:3 เข้า MSR (ย่อเท่ากันสองแกน)
// เจอหน้า: ปรับมุมตามเส้นตา + ROI ตามหน้า (หน้าใหญ่ใช้แถบ 4:3 ใหญ่สุดรอบหน้า)
// หลุดเกิน COMP_KEEP เฟรม: ลองรอบตำแหน่งเดิมด้วย ROI กว้างขึ้น 2 เท่า 1 เฟรม แล้วไล่ SEARCH ทีละเฟรม
// SEARCH: ทั้งเฟรม -> แถบ 4:3 (ย่อ 2 เท่า) -> ช่อง 160x120 (ไม่ย่อ สำหรับหน้าไกล ครึ่งกลางของภาพ) -> มุมเอียง
// fx,fy = จุดกลางเป็นสัดส่วนของภาพ (ถูกเลื่อนให้อยู่ในภาพ)
enum Region : uint8_t { R_FULL, R_BAND, R_TILE };
struct SearchStep { Region region; float fx, fy; int8_t ang; };
static const SearchStep SEARCH[] = {
  {R_FULL, .5f, .5f, 0}, {R_BAND, .5f, .5f, 0},
  {R_TILE, .25f, .375f, 0}, {R_TILE, .5f, .375f, 0}, {R_TILE, .75f, .375f, 0},
  {R_TILE, .25f, .625f, 0}, {R_TILE, .5f, .625f, 0}, {R_TILE, .75f, .625f, 0},
  {R_BAND, .5f, 0, 0}, {R_BAND, .5f, 1, 0},
  {R_FULL, .5f, .5f, 20}, {R_FULL, .5f, .5f, -20}, {R_BAND, .5f, .5f, 20}, {R_BAND, .5f, .5f, -20},
  {R_FULL, .5f, .5f, 40}, {R_FULL, .5f, .5f, -40},
};
static const int COMP_KEEP = 2;
static const int MSR_IN_W = 160, MSR_IN_H = 120;   // input MSR (esp-dl human_face_detect README) · ROI ไม่เล็กกว่านี้ (ไม่ขยายภาพ)
static int s_comp = 0, s_compMiss = 0, s_searchIdx = 0;
static bool s_retry = false;                // เฟรมถัดไปลองรอบตำแหน่งหน้าเดิมด้วย ROI กว้างขึ้น
static bool s_track = false;                // มี ROI ตามหน้าล่าสุด
static float s_trackX = 0, s_trackY = 0;    // จุดกลางหน้าในภาพตั้งตรง (ไม่รวมมุมชดเชย)
static int s_trackSide = 0;                 // ด้านยาวของกรอบหน้า (px)

static void fullRoi(const FaceView& v, int roi[4]) { roi[0] = 0; roi[1] = 0; roi[2] = v.width; roi[3] = v.height; }

// สี่เหลี่ยม bw x bh ที่มีจุดกลาง (cx,cy) เลื่อนให้อยู่ในภาพ
static void placeRoi(const FaceView& v, float cx, float cy, int bw, int bh, int roi[4]) {
  bw = min(bw, v.width); bh = min(bh, v.height);
  int x0 = constrain((int)lroundf(cx - bw / 2.0f), 0, v.width - bw);
  int y0 = constrain((int)lroundf(cy - bh / 2.0f), 0, v.height - bh);
  roi[0] = x0; roi[1] = y0; roi[2] = x0 + bw; roi[3] = y0 + bh;
}

static void pickRoi(const FaceView& v, int roi[4]) {
#if FACE_ROI
  const int bw = min(v.width, v.height * 4 / 3), bh = bw * 3 / 4;   // แถบ 4:3 ใหญ่สุดในภาพ
  if (s_track || s_retry) {
    int h = max(MSR_IN_H, (int)lroundf(s_trackSide * FACE_ROI_SCALE));
    if (s_retry) h *= 2;
    float cx, cy;
    faceEyesToUpright(-s_comp, v.width, v.height, s_trackX, s_trackY, cx, cy);   // ตั้งตรง -> มุมชดเชยปัจจุบัน
    // หน้าใหญ่ (อยู่ใกล้): แถบ 4:3 ใหญ่สุดรอบหน้า · ห้ามใช้ทั้งเฟรม (บีบเตี้ย A/B วัดได้ 0% แม้ที่ 50 ซม.)
    if (h > bh) placeRoi(v, cx, cy, bw, bh, roi);
    else placeRoi(v, cx, cy, h * 4 / 3, h, roi);
    return;
  }
  const SearchStep& s = SEARCH[s_searchIdx];
  if (s.region == R_BAND) { placeRoi(v, s.fx * v.width, s.fy * v.height, bw, bh, roi); return; }
  if (s.region == R_TILE) { placeRoi(v, s.fx * v.width, s.fy * v.height, MSR_IN_W, MSR_IN_H, roi); return; }
#endif
  fullRoi(v, roi);
}

static void searchStep(int step) {
  s_searchIdx = step % (int)(sizeof SEARCH / sizeof SEARCH[0]);
#if !FACE_TILT_COMP
  while (SEARCH[s_searchIdx].ang) s_searchIdx = (s_searchIdx + 1) % (int)(sizeof SEARCH / sizeof SEARCH[0]);
#endif
  s_comp = SEARCH[s_searchIdx].ang;
}

static void compUpdate(const FaceEyes& f) {
  if (f.found) {
    s_compMiss = 0; s_searchIdx = 0;
    s_track = true; s_retry = false;
    s_trackSide = max(f.box[2] - f.box[0], f.box[3] - f.box[1]);
    faceEyesToUpright(f.compDeg, f.view.width, f.view.height,
                      (f.box[0] + f.box[2]) / 2.0f, (f.box[1] + f.box[3]) / 2.0f, s_trackX, s_trackY);
#if FACE_TILT_COMP
    float e = atan2f((float)(f.eye[1][1] - f.eye[0][1]), (float)(f.eye[1][0] - f.eye[0][0])) * 180 / (float)M_PI;
    if (fabsf(e) > 4) s_comp = constrain(s_comp - (int)lroundf(e * 0.7f), -60, 60);
#endif
    return;
  }
  if (++s_compMiss <= COMP_KEEP) return;     // หลุดแป๊บเดียว (กะพริบ/เบลอ) ใช้มุมและ ROI เดิมต่อ
  if (s_track) { s_track = false; s_retry = FACE_ROI; if (!s_retry) searchStep(0); return; }
  if (s_retry) { s_retry = false; searchStep(0); return; }
  searchStep(s_searchIdx + 1);
}

// จุดเริ่มแถว y (fixed-point 16.16) ของการหมุนชดเชยรอบจุดกลาง · FaceView::row_start ต้องเป็นนิพจน์เดียวกันนี้
static inline void compRowStart(float ca, float sa, float cx, float cy, int y, int32_t& fx, int32_t& fy) {
  fx = (int32_t)lroundf((cx - cx * ca + (y - cy) * sa + 0.5f) * 65536);
  fy = (int32_t)lroundf((cy + cx * sa + (y - cy) * ca + 0.5f) * 65536);
}

// พิกเซล (px,py) ของภาพตั้งตรง -> (sx,sy) ในภาพดิบขนาด w x h
static inline void uprightToRaw(int rot, int w, int h, int px, int py, int& sx, int& sy) {
  if (rot == 0)        { sx = px;         sy = py; }
  else if (rot == 90)  { sx = py;         sy = h - 1 - px; }
  else if (rot == 180) { sx = w - 1 - px; sy = h - 1 - py; }
  else                 { sx = w - 1 - py; sy = px; }   // 270
}

// หมุน RGB565 ด้วย CAM_ROTATE_CW (ทีละ 90°) แล้วหมุนเพิ่ม ang องศารอบจุดกลาง (nearest neighbour, fixed-point)
// พื้นที่นอกภาพเติมดำ · ไม่ต้องหมุนเลยก็คืน fb เดิม · ทางเดิม (0.2.3) คงไว้ไม่แก้ ใช้เทียบ A/B เท่านั้น
static camera_fb_t* rotateInto(camera_fb_t* fb, int ang, RotBuf& rb) {
  const int rot = CAM_ROTATE_CW;
  if (rot == 0 && ang == 0) return fb;
  if (fb->format != PIXFORMAT_RGB565) return fb;
  if (fb->len > rb.size) {
    if (rb.buf) heap_caps_free(rb.buf);
    rb.buf = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
    rb.size = rb.buf ? fb->len : 0;
    if (!rb.buf) return fb;
  }
  const int w = fb->width, h = fb->height;
  const int W = (rot == 90 || rot == 270) ? h : w, H = (rot == 90 || rot == 270) ? w : h;
  const uint8_t* src = fb->buf;
  uint8_t* dst = rb.buf;
  float ca = cosf(ang * (float)M_PI / 180), sa = sinf(ang * (float)M_PI / 180), cx = W / 2.0f, cy = H / 2.0f;
  const int32_t dpx = (int32_t)lroundf(ca * 65536), dpy = (int32_t)lroundf(-sa * 65536);
  for (int y = 0; y < H; y++) {
    int32_t fx, fy;
    compRowStart(ca, sa, cx, cy, y, fx, fy);
    for (int x = 0; x < W; x++, fx += dpx, fy += dpy) {
      int px = x, py = y;
      if (ang) {
        px = fx >> 16; py = fy >> 16;
        if (px < 0 || py < 0 || px >= W || py >= H) { *dst++ = 0; *dst++ = 0; continue; }
      }
      int sx, sy;
      uprightToRaw(rot, w, h, px, py, sx, sy);
      const uint8_t* sp = src + ((size_t)sy * w + sx) * 2;
      *dst++ = sp[0];
      *dst++ = sp[1];
    }
  }
  rb.fb = *fb;
  rb.fb.buf = rb.buf;
  rb.fb.width = W;
  rb.fb.height = H;
  return &rb.fb;
}

const uint8_t* FaceViewMap::px(int x, int y) const {
  int32_t fx0 = 0, fy0 = 0;
  if (ang) row_start(y, fx0, fy0);
  int idx = raw_index(x, y, fx0, fy0);
  return idx < 0 ? nullptr : raw->buf + (size_t)idx * 2;
}

static void makeView(const camera_fb_t* raw, int ang, FaceViewMap& m) {
  static_cast<FaceView&>(m) = FaceView::make(raw->width, raw->height, CAM_ROTATE_CW, ang);
  m.raw = raw;
}

static RotBuf s_rbView;

// หมุนทีละ 90° ล้วน ทีละบล็อก 16x16 ให้ cache ของ PSRAM ได้ใช้ · ผลเท่ากับ rotateInto(fb, 0) ทุกพิกเซล
template <int ROT>
static void rotate90Blocks(const uint16_t* src, int w, int h, uint16_t* dst, int W, int H) {
  constexpr int B = 16;
  for (int by = 0; by < H; by += B)
    for (int bx = 0; bx < W; bx += B) {
      const int ye = min(by + B, H), xe = min(bx + B, W);
      for (int y = by; y < ye; y++) {
        uint16_t* d = dst + (size_t)y * W;
        for (int x = bx; x < xe; x++) {
          int sx, sy;
          uprightToRaw(ROT, w, h, x, y, sx, sy);
          d[x] = src[(size_t)sy * w + sx];
        }
      }
    }
}

camera_fb_t* faceEyesUprightView(camera_fb_t* raw) {
  const int rot = CAM_ROTATE_CW;
  if (rot == 0 || raw->format != PIXFORMAT_RGB565) return raw;
  RotBuf& rb = s_rbView;
  if (raw->len > rb.size) {
    if (rb.buf) heap_caps_free(rb.buf);
    rb.buf = (uint8_t*)heap_caps_malloc(raw->len, MALLOC_CAP_SPIRAM);
    rb.size = rb.buf ? raw->len : 0;
    if (!rb.buf) return raw;
  }
  const int w = raw->width, h = raw->height;
  const int W = rot == 180 ? w : h, H = rot == 180 ? h : w;
  const uint16_t* src = (const uint16_t*)raw->buf;
  uint16_t* dst = (uint16_t*)rb.buf;
  if (rot == 90)       rotate90Blocks<90>(src, w, h, dst, W, H);
  else if (rot == 180) rotate90Blocks<180>(src, w, h, dst, W, H);
  else                 rotate90Blocks<270>(src, w, h, dst, W, H);
  rb.fb = *raw;
  rb.fb.buf = rb.buf;
  rb.fb.width = W;
  rb.fb.height = H;
  return &rb.fb;
}

// rotateInto หยิบพิกเซล (px,py) ของภาพตั้งตรงมาวางที่ (x,y) ของภาพที่หมุนชดเชย
// สูตรเดียวกันจึงแปลงพิกัดที่ตรวจเจอกลับเป็นพิกัดในภาพตั้งตรง
void faceEyesToUpright(int compDeg, int w, int h, float x, float y, float& ox, float& oy) {
  float ca = cosf(compDeg * (float)M_PI / 180), sa = sinf(compDeg * (float)M_PI / 180);
  float cx = w / 2.0f, cy = h / 2.0f;
  ox = cx + (x - cx) * ca + (y - cy) * sa;
  oy = cy - (x - cx) * sa + (y - cy) * ca;
}

bool faceEyesInit() {
  s_detector = new HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
  if (!s_detector) return false;
  s_detector->set_score_thr(FACE_MSR_SCORE_THR, 0);
  return true;
}

static dl::image::img_t imgOf(const camera_fb_t* fb) {
  dl::image::img_t img;
  img.data = fb->buf;
  img.width = fb->width;
  img.height = fb->height;
  img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;   // ค่าปกติของ esp32-camera
  return img;
}

// เลือกหน้าที่คะแนนสูงสุด · keypoint ของ esp-dl: left_eye, left_mouth, nose, right_eye, right_mouth (x,y)
static void pickBest(const std::list<dl::detect::result_t>& res, FaceEyes& out) {
  const dl::detect::result_t* best = nullptr;
  for (auto& r : res)
    if (r.keypoint.size() >= 10 && (!best || r.score > best->score)) best = &r;
  if (!best) return;
  out.found = true;
  out.score = best->score;
  for (int i = 0; i < 4; i++) out.box[i] = best->box[i];
  out.eye[0][0] = best->keypoint[0]; out.eye[0][1] = best->keypoint[1];
  out.eye[1][0] = best->keypoint[6]; out.eye[1][1] = best->keypoint[7];
}

void faceEyesRun(camera_fb_t* raw, FaceEyes& out) {
  out.found = false;
  out.compDeg = s_comp;
  uint32_t t0 = micros();
  makeView(raw, s_comp, out.view);
  pickRoi(out.view, out.roi);
  uint32_t t1 = micros();
  out.rotateMs = (t1 - t0) / 1000.0f;
  out.preMs = 0;
  std::list<dl::detect::result_t>& res = s_detector->run_view(imgOf(raw), out.view, out.roi, &out.preMs);
  out.detectMs = (micros() - t1) / 1000.0f;
  pickBest(res, out);
  compUpdate(out);
}

void faceEyesRunLegacy(camera_fb_t* raw, int compDeg, FaceEyes& out) {
  out.found = false;
  out.compDeg = compDeg;
  uint32_t t0 = micros();
  camera_fb_t* fb = rotateInto(raw, compDeg, s_rb);
  uint32_t t1 = micros();
  out.rotateMs = (t1 - t0) / 1000.0f;
  out.preMs = 0;
  std::list<dl::detect::result_t>& res = s_detector->run(imgOf(fb));
  out.detectMs = (micros() - t1) / 1000.0f;
  makeView(fb, 0, out.view);         // ภาพที่หมุนแล้ว อ่านตรงๆ ไม่ต้องแปลง
  static_cast<FaceView&>(out.view) = FaceView::make(fb->width, fb->height, 0, 0);
  fullRoi(out.view, out.roi);
  pickBest(res, out);
}

int faceEyesCompareInput(camera_fb_t* raw, int compDeg, float ms[2]) {
  camera_fb_t* fb = rotateInto(raw, compDeg, s_rb);
  FaceViewMap m;
  makeView(raw, compDeg, m);
  return s_detector->compare_msr_input(imgOf(fb), imgOf(raw), m, ms);
}
