// Eye Region -> grayscale -> 32x32 -> int8 -> CNN -> P(ตาปิด)
// preprocess ใช้ eye_preprocess.h ตัวเดียวกับที่ตรวจ bit-exact กับ ml/preprocess.py แล้ว
#include "eye_classifier.h"
#include "esp_heap_caps.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "eye_preprocess.h"
#include "eye_model_data.h"
#include "config.h"

static constexpr size_t kArenaSize = 29 * 1024 + 256;   // deep2_half ใช้จริง 29,324 B · ให้พอดีก้อนว่างใน RAM ภายใน (ก้อนใหญ่สุด ~30 KB) ไม่พอก็ตกไป PSRAM เอง
static constexpr int N = EYE_MODEL_INPUT_SIZE;
static constexpr int kMaxCrop = 160;              // กรอบใหญ่กว่านี้ (หน้าชิดกล้องมาก) ถือว่าใช้ไม่ได้

static tflite::MicroInterpreter* s_interp = nullptr;
static uint8_t* s_crop = nullptr;                 // gray kMaxCrop x kMaxCrop
static uint8_t s_small[N * N];
static uint8_t s_last[2][N * N];
static bool s_lastValid[2] = {false, false};

bool eyeClassifierInit() {
  const tflite::Model* model = tflite::GetModel(g_eye_model_data);
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddMean();
  resolver.AddFullyConnected();
  resolver.AddLogistic();

  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("[eye] model schema %u != %d\n", (unsigned)model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }
  // RAM ภายในเร็วกว่า แต่ WiFi + esp-dl ใช้ไปมาก ถ้าไม่พอให้ใช้ PSRAM แทน (โมเดลเล็ก ช้าลงไม่มาก)
  Serial.printf("[eye] free internal %u KB (largest block %u KB)\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
  uint8_t* arena = (uint8_t*)heap_caps_malloc(kArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!arena) {
    arena = (uint8_t*)heap_caps_aligned_alloc(16, kArenaSize, MALLOC_CAP_SPIRAM);
    Serial.println("[eye] arena in PSRAM");
  }
  s_crop = (uint8_t*)heap_caps_malloc(kMaxCrop * kMaxCrop, MALLOC_CAP_SPIRAM);
  if (!arena || !s_crop) { Serial.println("[eye] alloc failed"); return false; }
  static tflite::MicroInterpreter interp(model, resolver, arena, kArenaSize);
  if (interp.AllocateTensors() != kTfLiteOk) { Serial.println("[eye] AllocateTensors failed"); return false; }
  s_interp = &interp;
  Serial.printf("[eye] model %dx%d, arena used %u bytes, arena in %s\n", N, N, (unsigned)interp.arena_used_bytes(),
                esp_ptr_external_ram(arena) ? "PSRAM" : "internal RAM");
  return true;
}

bool eyeLastInput(int slot, uint8_t* out) {
  if (slot < 0 || slot > 1 || !s_lastValid[slot]) return false;
  memcpy(out, s_last[slot], N * N);
  return true;
}

bool eyeCropGray(const FaceViewMap& v, int cx, int cy, int side, uint8_t* out) {
  if (side < EYE_MIN_CROP_PX || side > kMaxCrop) return false;
  const int x0 = cx - side / 2, y0 = cy - side / 2;
  if (x0 < 0 || y0 < 0 || x0 + side > v.width || y0 + side > v.height) return false;
  // RGB565 ของ esp32-camera เก็บ byte สูงก่อน -> px = (hi << 8) | lo · นอกภาพ (มุมชดเชย) = ดำ เหมือนภาพที่หมุนแล้วแบบเดิม
  for (int y = 0; y < side; y++)
    for (int x = 0; x < side; x++) {
      const uint8_t* p = v.px(x0 + x, y0 + y);
      out[y * side + x] = p ? eye_rgb565_to_gray((uint16_t)((p[0] << 8) | p[1])) : 0;
    }
  return true;
}

float eyeClassify(const FaceViewMap& v, int cx, int cy, int side, int slot) {
  if (slot >= 0 && slot < 2) s_lastValid[slot] = false;
  if (!s_interp || !eyeCropGray(v, cx, cy, side, s_crop)) return -1;
  eye_resize_bilinear(s_crop, side, side, side, s_small, N, N);
  if (slot >= 0 && slot < 2) { memcpy(s_last[slot], s_small, N * N); s_lastValid[slot] = true; }
  TfLiteTensor* in = s_interp->input(0);
  eye_to_int8(s_small, in->data.int8, N * N);
  if (s_interp->Invoke() != kTfLiteOk) return -1;
  int q = s_interp->output(0)->data.int8[0];
  return (q - EYE_MODEL_OUTPUT_ZERO_POINT) * EYE_MODEL_OUTPUT_SCALE;
}
