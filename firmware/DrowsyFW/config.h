// ค่าคงที่ทั้งหมดของ DrowsyFW: ขา GPIO, เกณฑ์เริ่มต้น, การจัด task
// คำศัพท์ตาม GLOSSARY.md ใน repo
#pragma once
#include <Arduino.h>

#define FW_VERSION "0.2.3-hvga-eyeview"

// ---- GPIO (GPIO map ใน CLAUDE.md · ห้ามใช้ 26–37) ----
#define PIN_LED_GREEN 47
#define PIN_LED_RED   40
#define PIN_LDR        3    // ADC1 · R1 10k ขึ้น 3V3, LDR ลง GND -> มืด = ค่าสูง
#define PIN_BUZZER    14    // MH-FMD active-LOW: HIGH = เงียบ
#define PIN_IR        21    // base S8050 ผ่าน 1k: HIGH = IR ติด
#define PIN_OLED_SDA  42    // จอจริงต่อแบบนี้: Wire.begin(42, 1)
#define PIN_OLED_SCL   1

// ---- กล้อง GOOUUU ESP32-S3-CAM ----
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5
#define CAM_PIN_D7    16
#define CAM_PIN_D6    17
#define CAM_PIN_D5    18
#define CAM_PIN_D4    12
#define CAM_PIN_D3    10
#define CAM_PIN_D2     8
#define CAM_PIN_D1     9
#define CAM_PIN_D0    11
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK  13

// ---- หาหน้า + ตา (ผล FaceSpike 16 ต.ค.) ----
#define CAM_FRAME_SIZE       FRAMESIZE_HVGA  // 480x320: ตาใหญ่ขึ้น ~1.5 เท่าจาก QVGA · FaceSpike วัดได้ 8.9 fps, หาหน้า ~49 ms
#define CAM_ROTATE_CW        270    // ภาพจากกล้องตะแคง 90° ต้องหมุนก่อนหาหน้า · ถ้าออกแบบกล่องให้ภาพตั้งตรงได้ ตั้งเป็น 0
#define FACE_TILT_COMP       1      // หมุนภาพชดเชยหน้าเอียงตามมุมเส้นตา
#define FACE_MSR_SCORE_THR   0.30f  // ค่าเดิม 0.5 ตัดหน้าในภาพที่หมุนชดเชยทิ้ง
#define EYE_CROP_SCALE       0.8f   // ด้าน Eye Region = 0.8 × ระยะระหว่างจุดตา (ให้ใกล้ภาพ MRL · ml/README.md)
#define EYE_MIN_CROP_PX      10     // Eye Region เล็กกว่านี้ถือว่ามองไม่เห็นตา
#define P_CLOSED_ON          0.5f   // P(ตาปิด) ≥ ค่านี้ = ปิด · < P_CLOSED_OFF = เปิด · ระหว่างนั้น = ไม่แน่ใจ
#define P_CLOSED_OFF         0.3f   //   ค่าชั่วคราวจาก ml/output/threshold_note.md (วัดบน MRL) ต้องปรับใหม่ด้วยภาพกล้องเรา

// ---- task: Core 1 = ภาพ/ML/Alert/เซนเซอร์ · Core 0 = WiFi stack, เว็บ, log, Sync ----
#define CORE_VISION 1
#define CORE_NET    0

// ---- เกณฑ์เริ่มต้น (หน่วยเป็นมิลลิวินาที ไม่นับเป็นเฟรม) ----
#define PERCLOS_WINDOW_MS      60000
#define PERCLOS_LEVEL1         0.15f
#define PERCLOS_LEVEL2         0.25f
#define CLOSED_LEVEL2_MS       1500
#define MICROSLEEP_MS          3000
#define DISTRACTION_MS         3000
#define UNMONITORED_MS         5000
#define EYES_OPEN_CLEAR_MS     1000
#define MUTE_DEFAULT_MS        (5UL * 60UL * 1000UL)

// ---- LDR -> IR (hysteresis กัน IR กระพริบตอนแสงก้ำกึ่ง) · ยังไม่ได้คาลิเบรตกับของจริง ----
#define LDR_DARK_ON_RAW        2800   // ค่าสูงกว่านี้ = มืด เปิด IR
#define LDR_DARK_OFF_RAW       2400   // ค่าต่ำกว่านี้ = สว่าง ปิด IR
#define LDR_PERIOD_MS          500

// ---- เครือข่าย ----
#define NET_RETRY_MS           60000  // ลอง WiFi ที่บันทึกไว้ทุก 1 นาที
#define MDNS_NAME              "drowsy"
#define TZ_THAILAND            "ICT-7"
#define TIME_VALID_EPOCH       1767225600UL  // 1 ม.ค. 2026: ก่อนหน้านี้ถือว่ายังไม่มีเวลาจริง
