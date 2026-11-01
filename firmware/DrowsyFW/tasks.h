// จุดเริ่มของทุก task · DrowsyFW.ino เรียกตามลำดับใน setup()
#pragma once
#include <stdint.h>
#include <time.h>

void alertBegin();      // ตั้งขา buzzer/LED ให้เงียบก่อนทุกอย่าง
void startAlertTask();
void startSensorTask();
void startVisionTask();
void startLogTask();
void startNetTask();
void startSyncTask();
void startOledTask();
void startWebServer();  // AsyncWebServer ทำงานบน task ของ AsyncTCP เอง

// ใช้ร่วมระหว่าง task
void alertMute(uint32_t durationMs);
enum class IrMode : uint8_t;
enum class HwTest : uint8_t;
void irSetMode(IrMode m);                           // ตั้งแล้วจำใน NVS
void hwTestStart(HwTest t, uint32_t durationMs);    // ปุ่มทดสอบในหน้าเว็บ
bool timeSetFromEpoch(time_t epoch, const char* source);

// WiFi ที่ตั้งจากหน้า /wifi (NVS) · net task เป็นคนเชื่อมต่อ/สแกน · ฟังก์ชันเหล่านี้เรียกจาก web ได้
struct WifiNet { char ssid[33]; int8_t rssi; bool secure; };
int  wifiSavedList(WifiNet* out, int max);           // ลำดับที่จะลองเชื่อม (rssi/secure ไม่ใช้)
bool wifiSave(const char* ssid, const char* pass);   // เพิ่ม/แก้ แล้วย้ายขึ้นบนสุด · เชื่อมใหม่ทันที
bool wifiForget(const char* ssid);
void wifiScanRequest();                              // สแกนใหม่ถ้าผลเก่ากว่า WIFI_SCAN_TTL_MS
int  wifiScanResults(WifiNet* out, int max, bool* scanning);
bool snapshotJpeg(uint8_t** out, size_t* outLen);   // ผู้เรียกต้อง free(*out)
struct DrowsyEvent;
bool drowsyEventPop(DrowsyEvent& out, uint32_t waitMs);  // log task รับ Event จาก vision
void drowsyEventPrint(const DrowsyEvent& e);
bool eyesJpeg(uint8_t** out, size_t* outLen);       // ภาพตา 32x32 ที่เข้าโมเดลจริง · ผู้เรียกต้อง free(*out)
