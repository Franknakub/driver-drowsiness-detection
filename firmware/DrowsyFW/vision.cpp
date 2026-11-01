// vision_task: กล้อง -> หาหน้า + จุดตา (ESP-DL) -> Eye Region สองข้าง -> โมเดลตา -> Eye State
//   -> DrowsinessJudge (PERCLOS, Drowsiness Level, Distraction, Unmonitored) -> SharedState ให้ alert อ่าน
#include "esp_camera.h"
#include "config.h"
#include "shared_state.h"
#include "tasks.h"
#include "face_eye.h"
#include "eye_classifier.h"
#include "drowsiness.h"
#include "img_converters.h"
#include "esp_heap_caps.h"

// ---- ภาพสดสำหรับ Device Dashboard ----
// vision คัดลอกภาพที่หมุนแล้ว + ผลหาหน้าไว้ เฉพาะตอนมีคนเปิดดูภาพสดภายใน 3 วิที่ผ่านมา
// web แปลงเป็น JPEG พร้อมวาดกรอบเอง (บน Core 0) · ไม่ส่ง camera fb ข้าม core
struct Snapshot {
  uint8_t* buf = nullptr;
  size_t size = 0;
  int w = 0, h = 0;
  FaceEyes face;
  int eyeSide = 0;
  float pL = -1, pR = -1;
  bool valid = false;
};
static Snapshot s_snap;
static SemaphoreHandle_t s_snapMutex = nullptr;
static volatile uint32_t s_snapWantedMs = 0;

// เก็บภาพตั้งตรง (หมุนแค่ CAM_ROTATE_CW) ไม่ใช่ภาพที่หมุนชดเชย จะได้ไม่แกว่งตอนระบบไล่หามุม
// หมุนเฉพาะตอนมีคนเปิดดู (ทางหาหน้าไม่ได้สร้างภาพตั้งตรงแล้ว)
static void snapshotStore(camera_fb_t* raw, const FaceEyes& f, int side, float pL, float pR) {
  if (!s_snapWantedMs || millis() - s_snapWantedMs > 3000) return;
  if (xSemaphoreTake(s_snapMutex, 0) != pdTRUE) return;      // web กำลังอ่านอยู่ ข้ามเฟรมนี้
  const camera_fb_t* fr = faceEyesUprightView(raw);
  if (fr->len > s_snap.size) {
    if (s_snap.buf) heap_caps_free(s_snap.buf);
    s_snap.buf = (uint8_t*)heap_caps_malloc(fr->len, MALLOC_CAP_SPIRAM);
    s_snap.size = s_snap.buf ? fr->len : 0;
  }
  if (s_snap.buf) {
    memcpy(s_snap.buf, fr->buf, fr->len);
    s_snap.w = fr->width; s_snap.h = fr->height;
    s_snap.face = f; s_snap.face.view.raw = nullptr;
    s_snap.eyeSide = side; s_snap.pL = pL; s_snap.pR = pR;
    s_snap.valid = true;
  }
  xSemaphoreGive(s_snapMutex);
}

static void px565(uint8_t* img, int w, int h, int x, int y, uint16_t c) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  uint8_t* p = img + ((size_t)y * w + x) * 2;
  p[0] = c >> 8; p[1] = c & 0xFF;                 // big-endian เหมือน esp32-camera
}
static void line565(uint8_t* img, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    px565(img, w, h, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// สี่เหลี่ยมในพิกัดภาพที่หมุนชดเชย -> วาดเป็นสี่เหลี่ยมเอียงบนภาพตั้งตรง
static void rbox565(uint8_t* img, int w, int h, int comp, float x0, float y0, float x1, float y1, uint16_t c) {
  float cx[4] = {x0, x1, x1, x0}, cy[4] = {y0, y0, y1, y1}, ux[4], uy[4];
  for (int i = 0; i < 4; i++) faceEyesToUpright(comp, w, h, cx[i], cy[i], ux[i], uy[i]);
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    line565(img, w, h, lroundf(ux[i]), lroundf(uy[i]), lroundf(ux[j]), lroundf(uy[j]), c);
  }
}

// JPEG ของภาพ 32x32 ที่ส่งเข้าโมเดลตาจริง ซ้าย = ตาซ้ายของภาพ, ขวา = ตาขวาของภาพ · ขยาย 6 เท่าให้ดูง่าย
bool eyesJpeg(uint8_t** out, size_t* outLen) {
  constexpr int N = 32, S = 6, GAP = 6, W = 2 * N * S + GAP, H = N * S;
  static uint8_t in[2][N * N];
  bool have[2] = {eyeLastInput(0, in[0]), eyeLastInput(1, in[1])};
  if (!have[0] && !have[1]) return false;
  uint8_t* img = (uint8_t*)heap_caps_malloc(W * H, MALLOC_CAP_SPIRAM);
  if (!img) return false;
  memset(img, 40, W * H);
  for (int e = 0; e < 2; e++) {
    if (!have[e]) continue;
    int ox = e * (N * S + GAP);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < N * S; x++) img[y * W + ox + x] = in[e][(y / S) * N + x / S];
  }
  bool ok = fmt2jpg(img, W * H, W, H, PIXFORMAT_GRAYSCALE, 90, out, outLen);
  heap_caps_free(img);
  return ok;
}

// JPEG ของภาพที่โมเดลเห็นล่าสุด: กรอบเขียว = หน้า · กรอบตา แดง = ปิด, น้ำเงิน = เปิด, เหลือง = ไม่แน่ใจ/ใช้ไม่ได้
bool snapshotJpeg(uint8_t** out, size_t* outLen) {
  s_snapWantedMs = millis();
  if (!s_snapMutex || xSemaphoreTake(s_snapMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  bool ok = false;
  if (s_snap.valid) {
    size_t n = (size_t)s_snap.w * s_snap.h * 2;
    uint8_t* img = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (img) {
      memcpy(img, s_snap.buf, n);
      const FaceEyes& f = s_snap.face;
      if (f.found) {
        const int W = s_snap.w, H = s_snap.h, a = f.compDeg;
        rbox565(img, W, H, a, f.box[0], f.box[1], f.box[2], f.box[3], 0x07E0);
        float ps[2] = {s_snap.pL, s_snap.pR};
        float hs = s_snap.eyeSide / 2.0f;
        for (int e = 0; e < 2; e++) {
          uint16_t c = ps[e] < 0 ? 0xFFE0 : ps[e] >= P_CLOSED_ON ? 0xF800 : ps[e] <= P_CLOSED_OFF ? 0x001F : 0xFFE0;
          rbox565(img, W, H, a, f.eye[e][0] - hs, f.eye[e][1] - hs, f.eye[e][0] + hs, f.eye[e][1] + hs, c);
        }
      }
      ok = fmt2jpg(img, n, s_snap.w, s_snap.h, PIXFORMAT_RGB565, 80, out, outLen);
      heap_caps_free(img);
    }
  }
  xSemaphoreGive(s_snapMutex);
  return ok;
}

static bool cameraInit() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;  c.pin_d1 = CAM_PIN_D1;
  c.pin_d2 = CAM_PIN_D2;  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;  c.pin_d5 = CAM_PIN_D5;
  c.pin_d6 = CAM_PIN_D6;  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;    c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC;  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn  = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;   // ผล FaceSpike: RGB565 QVGA เร็วที่สุดสำหรับหาหน้า
  c.frame_size   = CAM_FRAME_SIZE;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;   // ไม่เปิด AF: fixed-focus ตั้งใจไว้
}

// ---- A/B: ทางเดิม (หมุนทั้งเฟรม) กับทางใหม่ (หยิบพิกเซลผ่านแผนที่) บนเฟรมเดียวกัน · เปิดด้วย /api/ab?on=1 ----
// ช้าลงมากระหว่างเปิด (ทำทางเดิมซ้ำ + หมุนอีกรอบเพื่อเทียบ model input) ใช้ตอนทดสอบเท่านั้น
struct AbAcc {
  uint32_t frames = 0, foundNew = 0, foundOld = 0, both = 0;
  double eyeErrSum = 0; int eyeErrMax = 0;
  uint32_t input0 = 0, input0Bad = 0; int input0DiffMax = 0;
  uint32_t inputTilt = 0; double inputTiltDiffSum = 0;
  uint32_t crops = 0, cropBad = 0;
  double oldRot = 0, oldDet = 0, newRot = 0, newDet = 0, newPre = 0;
  double msrPreOld = 0, msrPreNew = 0; uint32_t msrPreN = 0;
};
static AbAcc s_ab;
static uint8_t *s_abCropA = nullptr, *s_abCropB = nullptr;

static void abStep(camera_fb_t* fb, const FaceEyes& f, int side) {
  FaceEyes o;
  faceEyesRunLegacy(fb, f.compDeg, o);
  AbAcc& a = s_ab;
  a.frames++;
  a.foundNew += f.found; a.foundOld += o.found;
  a.oldRot += o.rotateMs; a.oldDet += o.detectMs;
  a.newRot += f.rotateMs; a.newDet += f.detectMs; a.newPre += f.preMs;
  if (f.found && o.found) {
    a.both++;
    int err = 0;
    for (int e = 0; e < 2; e++)
      err = max(err, (int)lroundf(hypotf((float)(f.eye[e][0] - o.eye[e][0]), (float)(f.eye[e][1] - o.eye[e][1]))));
    a.eyeErrSum += err; a.eyeErrMax = max(a.eyeErrMax, err);
  }
  // Eye Region จากภาพดิบผ่านแผนที่ ต้องตรงกับ crop จากภาพที่หมุนทั้งเฟรมทุก byte (ใช้จุดตาจริง หรือกลางภาพถ้าไม่เจอหน้า)
  if (!s_abCropA) {
    s_abCropA = (uint8_t*)heap_caps_malloc(160 * 160, MALLOC_CAP_SPIRAM);
    s_abCropB = (uint8_t*)heap_caps_malloc(160 * 160, MALLOC_CAP_SPIRAM);
  }
  if (s_abCropA && s_abCropB) {
    int cx = f.found ? f.eye[0][0] : f.view.width / 2, cy = f.found ? f.eye[0][1] : f.view.height / 2;
    int sd = f.found && side >= EYE_MIN_CROP_PX ? side : 40;
    if (eyeCropGray(f.view, cx, cy, sd, s_abCropA) && eyeCropGray(o.view, cx, cy, sd, s_abCropB)) {
      a.crops++;
      a.cropBad += memcmp(s_abCropA, s_abCropB, sd * sd) != 0;
    }
  }
  float ms[2];
  int d = faceEyesCompareInput(fb, f.compDeg, ms);
  if (d >= 0) {
    a.msrPreOld += ms[0]; a.msrPreNew += ms[1]; a.msrPreN++;
    if (f.compDeg == 0) { a.input0++; a.input0Bad += d != 0; a.input0DiffMax = max(a.input0DiffMax, d); }
    else { a.inputTilt++; a.inputTiltDiffSum += d; }
  }
}

static void abPublish(AbStats& out) {
  const AbAcc& a = s_ab;
  float n = a.frames ? a.frames : 1;
  out.frames = a.frames; out.foundNew = a.foundNew; out.foundOld = a.foundOld; out.both = a.both;
  out.eyeErrMean = a.both ? a.eyeErrSum / a.both : 0; out.eyeErrMax = a.eyeErrMax;
  out.input0 = a.input0; out.input0Bad = a.input0Bad; out.input0DiffMax = a.input0DiffMax;
  out.inputTilt = a.inputTilt; out.inputTiltDiffMean = a.inputTilt ? a.inputTiltDiffSum / a.inputTilt : 0;
  out.crops = a.crops; out.cropBad = a.cropBad;
  out.oldRotateMs = a.oldRot / n; out.oldDetectMs = a.oldDet / n;
  out.newRotateMs = a.newRot / n; out.newDetectMs = a.newDet / n; out.newPreMs = a.newPre / n;
  out.msrPreOldMs = a.msrPreN ? a.msrPreOld / a.msrPreN : 0; out.msrPreNewMs = a.msrPreN ? a.msrPreNew / a.msrPreN : 0;
}

// รวมสองข้าง: เฉลี่ยข้างที่ใช้ได้ · ใช้ไม่ได้ทั้งคู่ = -1
static float combineEyes(float a, float b) {
  if (a >= 0 && b >= 0) return (a + b) / 2;
  return a >= 0 ? a : b;
}

static EyeState eyeStateFrom(bool face, float p) {
  if (!face || p < 0) return EyeState::NotVisible;
  if (p >= P_CLOSED_ON)  return EyeState::Closed;
  if (p < P_CLOSED_OFF)  return EyeState::Open;
  return EyeState::Uncertain;
}

static DrowsinessJudge s_judge;
static QueueHandle_t s_eventQ = nullptr;

// vision ห้ามพิมพ์ Serial เอง: USB CDC ที่ไม่มีใครเปิดพอร์ตอ่าน ทำให้ write ค้างได้หลายวินาที (เจอใน 0.2.6)
// ส่ง Event เข้าคิวแบบไม่รอ ให้ log task (Core 0) เป็นคนพิมพ์/เขียนไฟล์
static void forwardEvents() {
  DrowsyEvent e;
  while (s_judge.popEvent(e)) xQueueSend(s_eventQ, &e, 0);
}

bool drowsyEventPop(DrowsyEvent& out, uint32_t waitMs) {
  return s_eventQ && xQueueReceive(s_eventQ, &out, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void drowsyEventPrint(const DrowsyEvent& e) {
  switch (e.type) {
    case DrowsyEventType::LevelChange:
      Serial.printf("[event] %lu ms level %u -> %u (closed %lu ms)\n", (unsigned long)e.ms,
                    (unsigned)e.from, (unsigned)e.to, (unsigned long)e.closedMs);
      break;
    case DrowsyEventType::Microsleep:
      Serial.printf("[event] %lu ms MICROSLEEP (closed %lu ms)\n", (unsigned long)e.ms, (unsigned long)e.closedMs);
      break;
    case DrowsyEventType::Distraction:
      Serial.printf("[event] %lu ms distraction\n", (unsigned long)e.ms);
      break;
  }
}

static void visionTask(void*) {
  if (!cameraInit())        { Serial.println("[vision] camera init FAILED");     vTaskDelete(nullptr); }
  if (!faceEyesInit())      { Serial.println("[vision] face detect init FAILED"); vTaskDelete(nullptr); }
  if (!eyeClassifierInit()) { Serial.println("[vision] eye model init FAILED");   vTaskDelete(nullptr); }
  Serial.println("[vision] ready (RGB565, HumanFaceDetect on rotated view map + eye CNN)");

  uint32_t windowStart = millis(), frames = 0;
  FaceEyes f;
  for (;;) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

    faceEyesRun(fb, f);
    float pL = -1, pR = -1, classifyMs = 0;
    int eyeDist = 0, side = 0;
    if (f.found) {
      eyeDist = (int)lroundf(hypotf((float)(f.eye[1][0] - f.eye[0][0]), (float)(f.eye[1][1] - f.eye[0][1])));
      side = (int)lroundf(EYE_CROP_SCALE * eyeDist);
      uint32_t t = micros();
      pL = eyeClassify(f.view, f.eye[0][0], f.eye[0][1], side, 0);
      pR = eyeClassify(f.view, f.eye[1][0], f.eye[1][1], side, 1);
      classifyMs = (micros() - t) / 1000.0f;
    }
    snapshotStore(fb, f, side, pL, pR);
    bool ab, abReset;
    { StateGuard g; ab = g_state.abOn; abReset = g_state.abReset; g_state.abReset = false; }
    if (abReset) s_ab = AbAcc();
    if (ab) abStep(fb, f, side);
    esp_camera_fb_return(fb);       // หลังจากนี้ f.view ใช้ไม่ได้ (ชี้เข้า fb)

    float p = combineEyes(pL, pR);
    frames++;
    uint32_t now = millis();
    EyeState es = eyeStateFrom(f.found, p);
    const DrowsyResult& dr = s_judge.update(now, es);
    {
      StateGuard g;
      g_state.faceFound = f.found;
      g_state.faceScore = f.found ? f.score : 0;
      g_state.pLeft = pL;
      g_state.pRight = pR;
      g_state.pClosed = p < 0 ? 0 : p;
      g_state.eyeState = es;
      g_state.perclos = dr.perclos;
      g_state.perclosReady = dr.perclos >= 0;
      g_state.closedMs = dr.closedMs;
      if (dr.closedMs > g_state.closedPeakMs) g_state.closedPeakMs = dr.closedMs;
      g_state.level = dr.level;
      g_state.distraction = dr.distraction;
      g_state.unmonitored = dr.unmonitored;
      g_state.lastFrameMs = now;
      g_state.eyeDistPx = eyeDist;
      g_state.tiltCompDeg = f.compDeg;
      g_state.rotateMs = f.rotateMs;
      g_state.detectMs = f.detectMs;
      g_state.classifyMs = classifyMs;
      g_state.preMs = f.preMs;
      if (ab) abPublish(g_state.ab);
      if (now - windowStart >= 1000) {
        g_state.fps = frames * 1000.0f / (now - windowStart);
        g_state.visionFrames += frames;
        frames = 0;
        windowStart = now;
      }
    }
    forwardEvents();
  }
}

void startVisionTask() {
  s_snapMutex = xSemaphoreCreateMutex();
  s_eventQ = xQueueCreate(32, sizeof(DrowsyEvent));
  xTaskCreatePinnedToCore(visionTask, "vision", 16384, nullptr, 5, nullptr, CORE_VISION);
}
