// จุดเริ่มของทุก task · DrowsyFW.ino เรียกตามลำดับใน setup()
#pragma once

void alertBegin();      // ตั้งขา buzzer/LED ให้เงียบก่อนทุกอย่าง
void startAlertTask();
void startSensorTask();
void startVisionTask();
void startLogTask();
void startNetTask();
void startSyncTask();
void startWebServer();  // AsyncWebServer ทำงานบน task ของ AsyncTCP เอง

// ใช้ร่วมระหว่าง task
void alertMute(uint32_t durationMs);
bool timeSetFromEpoch(time_t epoch, const char* source);
bool snapshotJpeg(uint8_t** out, size_t* outLen);   // ผู้เรียกต้อง free(*out)
struct DrowsyEvent;
bool drowsyEventPop(DrowsyEvent& out, uint32_t waitMs);  // log task รับ Event จาก vision
void drowsyEventPrint(const DrowsyEvent& e);
bool eyesJpeg(uint8_t** out, size_t* outLen);       // ภาพตา 32x32 ที่เข้าโมเดลจริง · ผู้เรียกต้อง free(*out)
