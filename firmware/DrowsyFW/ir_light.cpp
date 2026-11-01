// sensor_task: อ่าน LDR แล้วเปิด/ปิด IR ด้วย hysteresis
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

static void sensorTask(void*) {
  pinMode(PIN_IR, OUTPUT);
  digitalWrite(PIN_IR, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
  bool irOn = false;

  for (;;) {
    uint16_t raw = analogRead(PIN_LDR);           // มืด = ค่าสูง
    if (!irOn && raw >= LDR_DARK_ON_RAW)  irOn = true;
    if (irOn  && raw <= LDR_DARK_OFF_RAW) irOn = false;
    digitalWrite(PIN_IR, irOn ? HIGH : LOW);
    {
      StateGuard g;
      g_state.ldrRaw = raw;
      g_state.irOn = irOn;
    }
    vTaskDelay(pdMS_TO_TICKS(LDR_PERIOD_MS));
  }
}

void startSensorTask() {
  xTaskCreatePinnedToCore(sensorTask, "sensor", 3072, nullptr, 2, nullptr, CORE_VISION);
}
