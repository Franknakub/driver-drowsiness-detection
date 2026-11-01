// DrowsyFW — firmware ตัวจริงของ Driver Drowsiness Detection
// แผนและคำศัพท์: docs/firmware-plan.html, GLOSSARY.md ใน repo
//
// Build: PartitionScheme=custom (ใช้ partitions.csv ในโฟลเดอร์นี้: app 6 MB x 2 + FFat ~3.9 MB)
//   FQBN esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom
// ต้องมี secrets.h (คัดลอกจาก secrets.example.h) · ห้าม commit secrets.h

#include "config.h"
#include "shared_state.h"
#include "tasks.h"
#include "esp_camera.h"
#include "eye_classifier.h"

volatile bool g_rebootRequested = false;

// ---- คำสั่งทดสอบกล้อง/IR ทาง Serial (ใช้ตอนวัดความสว่าง IR · ไม่ต้องต่อ WiFi) ----
// ir on|off|auto · gain 16-1023 (เพดาน gain ของ OV5640 หน่วย 1/16 เท่า · ค่าเริ่ม 248 = 15.5x) · ae -2..2 · aec2 0|1 · bright -2..2
// exp auto|<0-1200> · agc auto|<0-30> · cam (พิมพ์ค่าตั้ง) · log on|off (พิมพ์ [cam] ทุก 2 วิ)
static bool s_camLog = false;

static int ov5640Gain() {        // gain จริงที่ AGC เลือก หน่วย 1/16 เท่า (0x350A-0x350B)
  sensor_t* s = esp_camera_sensor_get();
  return ((s->get_reg(s, 0x350A, 0x03) << 8) | s->get_reg(s, 0x350B, 0xFF));
}
static int ov5640Exposure() {    // exposure จริงที่ AEC เลือก หน่วยแถว (0x3500-0x3502)
  sensor_t* s = esp_camera_sensor_get();
  return (s->get_reg(s, 0x3500, 0x0F) << 12) | (s->get_reg(s, 0x3501, 0xFF) << 4) | (s->get_reg(s, 0x3502, 0xF0) >> 4);
}

static void camPrintSettings() {
  sensor_t* s = esp_camera_sensor_get();
  camera_status_t& c = s->status;
  int8_t force;
  { StateGuard g; force = g_state.irForce; }
  Serial.printf("[cam] set: ir=%s gainceiling=%d ae_level=%d aec=%d aec_value=%d agc=%d agc_gain=%d aec2=%d brightness=%d\n",
                force < 0 ? "auto" : force ? "on" : "off", c.gainceiling, c.ae_level, c.aec, c.aec_value,
                c.agc, c.agc_gain, c.aec2, c.brightness);
}

static void camLogLine() {
  static uint32_t lastFrames = 0, lastFace = 0;
  SharedState st = stateSnapshot();
  uint32_t df = st.camFrames - lastFrames, dface = st.camFaceFrames - lastFace;
  lastFrames = st.camFrames; lastFace = st.camFaceFrames;
  int mean[2] = {-1, -1};
  static uint8_t buf[32 * 32];
  for (int e = 0; e < 2; e++)
    if (eyeLastInput(e, buf)) { uint32_t sum = 0; for (int i = 0; i < 32 * 32; i++) sum += buf[i]; mean[e] = sum / (32 * 32); }
  Serial.printf("[cam] ir=%d face=%u/%u frameMean=%u centerMean=%u eyeMean L=%d R=%d gain=%.1fx exposure=%d ldr=%u\n",
                st.irOn, dface, df, st.camFrameMean, st.camCenterMean, mean[0], mean[1],
                ov5640Gain() / 16.0f, ov5640Exposure(), st.ldrRaw);
}

static void camCommand(String line) {
  line.trim();
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  sensor_t* s = esp_camera_sensor_get();
  int v = arg.toInt();
  if (cmd == "ir") {
    StateGuard g;
    g_state.irForce = arg == "on" ? 1 : arg == "off" ? 0 : -1;
  } else if (cmd == "gain")   s->set_gainceiling(s, (gainceiling_t)constrain(v, 16, 1023));
  else if (cmd == "ae")       s->set_ae_level(s, constrain(v, -2, 2));
  else if (cmd == "aec2")     s->set_aec2(s, v != 0);
  else if (cmd == "bright")   s->set_brightness(s, constrain(v, -2, 2));
  else if (cmd == "exp")      { if (arg == "auto") s->set_exposure_ctrl(s, 1); else { s->set_exposure_ctrl(s, 0); s->set_aec_value(s, v); } }
  else if (cmd == "agc")      { if (arg == "auto") s->set_gain_ctrl(s, 1); else { s->set_gain_ctrl(s, 0); s->set_agc_gain(s, v); } }
  else if (cmd == "log")      s_camLog = arg != "off";
  else if (cmd != "cam")      { Serial.printf("[cam] unknown: %s\n", line.c_str()); return; }
  camPrintSettings();
}

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
  startDisplayTask();
  startVisionTask();
  startNetTask();               // เปิด AP + เว็บ + OTA
  startSyncTask();
}

void loop() {
  if (g_rebootRequested) {
    delay(500);                 // ให้หน้าเว็บได้รับคำตอบก่อน
    ESP.restart();
  }
  if (Serial.available()) camCommand(Serial.readStringUntil('\n'));
  static uint32_t lastCam = 0;
  if (s_camLog && millis() - lastCam >= 2000) { lastCam = millis(); camLogLine(); }
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
