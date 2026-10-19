// Golden test of the eye-state model on the ESP32-S3 (TFLite Micro from Arduino core 3.3.12).
// Copy these three files next to this sketch first (they are git-ignored here):
//   ml/eye_preprocess.h, ml/output/size_N/model_data.h, ml/output/size_N/golden_data.h
// Prints arena usage, inference time and whether each golden sample matches the PC.
#include <Arduino.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "eye_preprocess.h"
#include "model_data.h"
#include "golden_data.h"

constexpr size_t kArenaSize = 128 * 1024;
constexpr int N = EYE_MODEL_INPUT_SIZE;

static uint32_t fnv1a(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < len; i++) h = (h ^ p[i]) * 0x01000193u;
  return h;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  const tflite::Model *model = tflite::GetModel(g_eye_model_data);
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddMean();
  resolver.AddFullyConnected();
  resolver.AddLogistic();

  uint8_t *arena = (uint8_t *)heap_caps_malloc(kArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  static tflite::MicroInterpreter interp(model, resolver, arena, kArenaSize);
  if (interp.AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed");
    return;
  }
  TfLiteTensor *in = interp.input(0);
  TfLiteTensor *out = interp.output(0);
  Serial.printf("model %ux%u, arena used %u bytes\n", N, N, (unsigned)interp.arena_used_bytes());

  static uint8_t small[N * N];
  int input_ok = 0, output_ok = 0;
  uint32_t total_us = 0;
  for (int i = 0; i < GOLDEN_COUNT; i++) {
    eye_resize_bilinear(g_golden_crops[i], GOLDEN_CROP, GOLDEN_CROP, GOLDEN_CROP, small, N, N);
    eye_to_int8(small, in->data.int8, N * N);
    bool in_match = fnv1a(in->data.int8, N * N) == g_golden_input_fnv1a[i];
    uint32_t t0 = micros();
    interp.Invoke();
    uint32_t dt = micros() - t0;
    total_us += dt;
    int q = out->data.int8[0];
    // 1-2 LSB of rounding in the logit grows to <= ~15 LSB after LOGISTIC (PC ref vs optimized kernels)
    bool out_match = abs(q - g_golden_expected_q[i]) <= 20;
    input_ok += in_match;
    output_ok += out_match;
    float p = (q - EYE_MODEL_OUTPUT_ZERO_POINT) * EYE_MODEL_OUTPUT_SCALE;
    Serial.printf("#%2d closed=%d  input %s  q=%4d expected %4d %s  P(closed)=%.3f  %u us\n", i,
                  g_golden_label_closed[i], in_match ? "OK " : "BAD", q, g_golden_expected_q[i],
                  out_match ? "OK " : "BAD", p, (unsigned)dt);
  }
  Serial.printf("input %d/%d, output %d/%d, mean inference %u us\n", input_ok, GOLDEN_COUNT, output_ok,
                GOLDEN_COUNT, (unsigned)(total_us / GOLDEN_COUNT));
}

void loop() { delay(1000); }
