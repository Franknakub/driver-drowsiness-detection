// sync_task: ส่ง Sample/Event ที่ยังไม่ได้ส่งขึ้น Supabase Edge Function (ADR 0002)
// ขั้นต่อไป: อ่าน cursor จาก trip_log -> POST ครั้งละ 60 แถว -> ติดป้าย synced เมื่อ cloud ตอบรับ
#include "config.h"
#include "shared_state.h"
#include "tasks.h"

static void syncTask(void*) {
  for (;;) {
    // TODO: ทำงานเมื่อ g_state.staConnected เท่านั้น · HTTPS ใช้ 1–2 วิ ห้ามย้ายมาทำใน vision
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

void startSyncTask() {
  xTaskCreatePinnedToCore(syncTask, "sync", 8192, nullptr, 1, nullptr, CORE_NET);
}
