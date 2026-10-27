// sensor_task: อ่าน LDR แล้วเปิด/ปิด IR ตามโหมด
//   Auto = ตาม LDR (hysteresis) · On = เปิดค้าง · Off = ปิด · โหมดตั้งจากหน้าเว็บและจำใน NVS
#include <Preferences.h>
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

static IrMode loadIrMode() {
  Preferences prefs;
  uint8_t m = 0;
  if (prefs.begin("drowsy", true)) { m = prefs.getUChar("irmode", 0); prefs.end(); }
  return m <= (uint8_t)IrMode::Off ? (IrMode)m : IrMode::Auto;
}

void irSetMode(IrMode m) {
  { StateGuard g; g_state.irMode = m; }
  Preferences prefs;
  prefs.begin("drowsy", false);
  prefs.putUChar("irmode", (uint8_t)m);
  prefs.end();
}

static void sensorTask(void*) {
  pinMode(PIN_IR, OUTPUT);
  digitalWrite(PIN_IR, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
  { IrMode m = loadIrMode(); StateGuard g; g_state.irMode = m; }
  bool dark = false;

  for (;;) {
    uint16_t raw = analogRead(PIN_LDR);           // มืด = ค่าสูง
    if (!dark && raw >= LDR_DARK_ON_RAW)  dark = true;
    if (dark  && raw <= LDR_DARK_OFF_RAW) dark = false;

    SharedState s = stateSnapshot();
    uint32_t now = millis();
    bool irOn = s.irMode == IrMode::On || (s.irMode == IrMode::Auto && dark);
    // ทดสอบจากหน้าเว็บ: กะพริบ 1 วิติด/1 วิดับ ให้เห็นภาพสว่างขึ้นลงใน /snapshot.jpg ไม่ว่าโหมดไหน
    if (s.hwTest == HwTest::Ir && s.hwTestUntilMs && (int32_t)(s.hwTestUntilMs - now) > 0)
      irOn = (now / 1000) % 2 == 0;
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
