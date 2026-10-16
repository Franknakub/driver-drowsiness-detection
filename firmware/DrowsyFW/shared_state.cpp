#include "shared_state.h"

SharedState g_state;
static SemaphoreHandle_t s_mutex = nullptr;

void stateInit() {
  memset(&g_state, 0, sizeof(g_state));
  g_state.eyeState = EyeState::Unknown;
  g_state.level = DrowsinessLevel::Normal;
  g_state.tripStartMs = millis();
  s_mutex = xSemaphoreCreateMutex();
}

void stateLock()   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
void stateUnlock() { xSemaphoreGive(s_mutex); }

SharedState stateSnapshot() {
  StateGuard g;
  return g_state;
}
