// DrowsyFW — firmware ตัวจริงของ Driver Drowsiness Detection
// แผนและคำศัพท์: docs/firmware-plan.html, GLOSSARY.md ใน repo
//
// Build: PartitionScheme=custom (ใช้ partitions.csv ในโฟลเดอร์นี้: app 6 MB x 2 + FFat ~3.9 MB)
//   FQBN esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom
// ต้องมี secrets.h (คัดลอกจาก secrets.example.h) · ห้าม commit secrets.h

#include "config.h"
#include "shared_state.h"
#include "tasks.h"

volatile bool g_rebootRequested = false;

void setup() {
  alertBegin();                 // ต้องเป็นอย่างแรก: กัน buzzer ดังตอนบูต
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\n=== DrowsyFW %s ===\n", FW_VERSION);
  if (!psramFound()) Serial.println("PSRAM NOT FOUND - set PSRAM=opi");

  stateInit();
  startLogTask();               // เลข Trip + FFat ก่อน task อื่น
  startSensorTask();
  startAlertTask();
  startVisionTask();
  startNetTask();               // เปิด AP + เว็บ + OTA
  startSyncTask();
  startOledTask();
}

void loop() {
  if (g_rebootRequested) {
    delay(500);                 // ให้หน้าเว็บได้รับคำตอบก่อน
    ESP.restart();
  }
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    SharedState s = stateSnapshot();
    Serial.printf("[main] trip=%u fps=%.1f ldr=%u ir=%d ap=%s sta=%s %s time=%s psram=%uKB\n",
                  s.bootCount, s.fps, s.ldrRaw, s.irOn, s.apSsid,
                  s.staConnected ? s.staSsid : "-", s.staIp, s.timeKnown ? "yes" : "no",
                  (unsigned)(ESP.getFreePsram() / 1024));
  }
  delay(50);
}
