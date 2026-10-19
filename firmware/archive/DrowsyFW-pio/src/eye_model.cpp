#include "eye_model.h"
#include "config.h"
#include "model_data.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static constexpr size_t kArenaSize = 512 * 1024;  // in PSRAM; trimmed after measuring

static uint8_t *tensorArena = nullptr;
static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input = nullptr;
static TfLiteTensor *output = nullptr;

bool eyeModelInit() {
  const tflite::Model *model = tflite::GetModel(g_eye_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("Model schema %lu != %d\n", (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }

  // Ops used by the MobileNetV2 export (see ml/train_eye.py)
  static tflite::MicroMutableOpResolver<8> resolver;
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddAdd();
  resolver.AddMul();
  resolver.AddPad();
  resolver.AddMean();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();

  tensorArena = (uint8_t *)heap_caps_aligned_alloc(16, kArenaSize, MALLOC_CAP_SPIRAM);
  if (tensorArena == nullptr) {
    Serial.println(F("Tensor arena alloc failed"));
    return false;
  }

  static tflite::MicroInterpreter staticInterpreter(model, resolver, tensorArena, kArenaSize);
  interpreter = &staticInterpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println(F("AllocateTensors failed"));
    return false;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);
  Serial.printf("Model OK: %u KB, arena used %u KB, input %dx%d\n", g_eye_model_len / 1024,
                (unsigned)(interpreter->arena_used_bytes() / 1024), input->dims->data[1],
                input->dims->data[2]);
  return true;
}

float eyeModelPredictClosed(const uint8_t *gray) {
  if (interpreter == nullptr) return -1.0f;

  // Quantize raw pixels (0..255) with the model's input scale/zero point
  const float scale = input->params.scale;
  const int zero = input->params.zero_point;
  const int n = EYE_INPUT_SIZE * EYE_INPUT_SIZE;
  for (int i = 0; i < n; i++) {
    int q = (int)lroundf(gray[i] / scale) + zero;
    input->data.int8[i] = (int8_t)constrain(q, -128, 127);
  }

  if (interpreter->Invoke() != kTfLiteOk) return -1.0f;

  // Output: softmax over [open, closed]
  return (output->data.int8[1] - output->params.zero_point) * output->params.scale;
}
