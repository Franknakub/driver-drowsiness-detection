// log_task: เลข Trip (= จำนวนครั้งที่บูต) + FFat
// ขั้นต่อไป: เขียน Sample ทุก 1 วิ + Event ลง /trips/<boot>.bin และจัดการพื้นที่ 80%
#include <Preferences.h>
#include <FFat.h>
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

static uint32_t nextBootCount() {
  Preferences p;
  p.begin("drowsy", false);
  uint32_t n = p.getUInt("boot", 0) + 1;
  p.putUInt("boot", n);
  p.end();
  return n;
}

// เรียกจาก setup() ก่อน task อื่น เพื่อให้รู้เลข Trip ตั้งแต่ต้น
static void tripBegin() {
  uint32_t boot = nextBootCount();
  { StateGuard g; g_state.bootCount = boot; }
  Serial.printf("[log] Trip #%u starts\n", boot);

  if (!FFat.begin(true)) {                       // true = format ถ้ายังไม่มีระบบไฟล์
    Serial.println("[log] FFat mount FAILED - check partitions.csv / PartitionScheme=custom");
    return;
  }
  Serial.printf("[log] FFat %u KB total, %u KB used\n",
                (unsigned)(FFat.totalBytes() / 1024), (unsigned)(FFat.usedBytes() / 1024));
}

static void logTask(void*) {
  for (;;) {
    // TODO: Sample ทุก 1 วิ จาก stateSnapshot()
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void startLogTask() {
  tripBegin();
  xTaskCreatePinnedToCore(logTask, "log", 4096, nullptr, 2, nullptr, CORE_NET);
}
