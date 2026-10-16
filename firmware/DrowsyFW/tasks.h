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
