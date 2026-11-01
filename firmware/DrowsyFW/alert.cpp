// Alert: buzzer / LED + ปุ่ม BOOT ปิดเสียง (จอ OLED อยู่ใน oled.cpp)
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

void hwTestStart(HwTest t, uint32_t durationMs) {
  StateGuard g;
  g_state.hwTest = t;
  g_state.hwTestUntilMs = (millis() + durationMs) | 1;   // | 1: 0 แปลว่าไม่ได้ทดสอบ
}

static void buzzer(bool on) { digitalWrite(PIN_BUZZER, on ? LOW : HIGH); }

static void alertTask(void*) {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  uint32_t pressedSince = 0;
  bool pressHandled = false;
  for (;;) {
    SharedState s = stateSnapshot();
    uint32_t now = millis();

    // ปุ่ม BOOT: กดค้าง BTN_DEBOUNCE_MS = ปิดเสียง 1 ครั้งต่อการกด (ระดับ 3 ยังดังต่อตามกฎข้างล่าง)
    if (digitalRead(PIN_BOOT_BTN) == LOW) {
      if (!pressedSince) pressedSince = now | 1;
      if (!pressHandled && now - pressedSince >= BTN_DEBOUNCE_MS) {
        alertMute(MUTE_DEFAULT_MS);
        pressHandled = true;
      }
    } else {
      pressedSince = 0;
      pressHandled = false;
    }

    bool muted = s.mutedUntilMs && (int32_t)(s.mutedUntilMs - now) > 0;
    bool blink = (now / 250) % 2;

    // LED: เขียว = ปกติ · แดงกระพริบ = ระดับ 1 ขึ้นไป หรือ Distraction · ดับทั้งคู่ = Unmonitored
    bool alarm = s.level != DrowsinessLevel::Normal || s.distraction;
    bool green = !alarm && !s.unmonitored;
    bool red = alarm && blink;

    // buzzer
    bool sound = false;
    if (s.level == DrowsinessLevel::Microsleep) {
      sound = true;                                  // ดังค้าง ปิดไม่ได้
    } else if (!muted) {
      if (s.level == DrowsinessLevel::Drowsy) sound = (now % 600) < 200;    // เป็นจังหวะ
      else if (s.distraction)                 sound = (now % 1000) < 80;    // สั้นๆ
    }

    // ทดสอบจากหน้าเว็บ: ทับเฉพาะตัวที่ทดสอบ · buzzer รวมกับเสียงเตือนจริง (OR) จึงไม่กลบ Microsleep
    if (s.hwTestUntilMs && (int32_t)(s.hwTestUntilMs - now) > 0) {
      bool beat = (now % 500) < 250;
      if (s.hwTest == HwTest::Buzzer)   sound = sound || beat;
      if (s.hwTest == HwTest::LedGreen) green = beat;
      if (s.hwTest == HwTest::LedRed)   red = beat;
    }

    digitalWrite(PIN_LED_GREEN, green);
    digitalWrite(PIN_LED_RED, red);
    buzzer(sound);

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void startAlertTask() {
  xTaskCreatePinnedToCore(alertTask, "alert", 4096, nullptr, 3, nullptr, CORE_VISION);
}
