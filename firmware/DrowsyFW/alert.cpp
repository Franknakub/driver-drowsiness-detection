// Alert: buzzer / LED (OLED จะเพิ่มทีหลัง)
// ปิดเสียงได้เฉพาะระดับ 1–2 และ Distraction · ระดับ 3 (Microsleep) ปิดเสียงไม่ได้
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

void alertBegin() {
  // buzzer เป็น active-LOW: ดึงขึ้นก่อนเป็นอย่างแรก กันดังตอนบูต
  pinMode(PIN_BUZZER, INPUT_PULLUP);
  digitalWrite(PIN_BUZZER, HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH);
  pinMode(PIN_LED_GREEN, OUTPUT); digitalWrite(PIN_LED_GREEN, LOW);
  pinMode(PIN_LED_RED, OUTPUT);   digitalWrite(PIN_LED_RED, LOW);
}

void alertMute(uint32_t durationMs) {
  StateGuard g;
  g_state.mutedUntilMs = millis() + durationMs;
}

static void buzzer(bool on) { digitalWrite(PIN_BUZZER, on ? LOW : HIGH); }

static void alertTask(void*) {
  for (;;) {
    SharedState s = stateSnapshot();
    uint32_t now = millis();
    bool muted = s.mutedUntilMs && (int32_t)(s.mutedUntilMs - now) > 0;
    bool blink = (now / 250) % 2;

    // LED: เขียว = ปกติ · แดงกระพริบ = ระดับ 1 ขึ้นไป หรือ Distraction · ดับทั้งคู่ = Unmonitored
    bool alarm = s.level != DrowsinessLevel::Normal || s.distraction;
    digitalWrite(PIN_LED_GREEN, !alarm && !s.unmonitored);
    digitalWrite(PIN_LED_RED, alarm && blink);

    // buzzer
    bool sound = false;
    if (s.level == DrowsinessLevel::Microsleep) {
      sound = true;                                  // ดังค้าง ปิดไม่ได้
    } else if (!muted) {
      if (s.level == DrowsinessLevel::Drowsy) sound = (now % 600) < 200;    // เป็นจังหวะ
      else if (s.distraction)                 sound = (now % 1000) < 80;    // สั้นๆ
    }
    buzzer(sound);

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void startAlertTask() {
  xTaskCreatePinnedToCore(alertTask, "alert", 4096, nullptr, 3, nullptr, CORE_VISION);
}
