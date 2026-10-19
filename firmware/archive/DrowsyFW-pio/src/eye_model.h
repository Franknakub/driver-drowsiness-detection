#pragma once

#include <Arduino.h>

// TFLite Micro wrapper for the INT8 eye open/closed model (model_data.cpp).
bool eyeModelInit();

// gray: EYE_INPUT_SIZE x EYE_INPUT_SIZE raw grayscale pixels.
// Returns P(eye closed) in 0..1, or -1 on error.
float eyeModelPredictClosed(const uint8_t *gray);
