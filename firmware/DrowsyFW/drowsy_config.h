// เกณฑ์ตัดสินความง่วง (หน่วยเป็นมิลลิวินาที ไม่นับเป็นเฟรม) · ไม่พึ่ง Arduino เพื่อให้ทดสอบบน PC ได้
#pragma once

#define PERCLOS_WINDOW_MS      60000
#define PERCLOS_MIN_VALID_MS   30000  // ต้องเห็นตา (เปิด/ปิด) รวมเท่านี้ในหน้าต่างก่อนจะใช้ PERCLOS
#define PERCLOS_LEVEL1         0.15f
#define PERCLOS_LEVEL2         0.25f
#define PERCLOS_HYST           0.02f  // ลดระดับเมื่อ PERCLOS ต่ำกว่าเกณฑ์ลงไปเท่านี้ กันสลับไปมา
#define CLOSED_LEVEL2_MS       1500
#define MICROSLEEP_MS          3000
#define DISTRACTION_MS         3000
#define DRIVER_ABSENT_MS       60000  // มองไม่เห็นตานานเกินนี้ = Driver ลงจากรถ เลิกเตือน Distraction จนกว่าจะเห็นตาอีกครั้ง + ปลดระดับ 2 ที่ค้าง
#define MICROSLEEP_ABSENT_CLEAR_MS 300000  // มองไม่เห็นตานานเกินนี้ ปลดระดับ 3 ที่ค้าง (นานกว่าระดับ 2: หลับแล้วหัวก้มยังต้องดังต่อ)
#define UNMONITORED_MS         5000
#define EYES_OPEN_CLEAR_MS     1000   // ลืมตาต่อเนื่องเท่านี้ ระดับที่เกิดจากตาปิดจึงหาย
#define EYE_HOLD_MS            500    // หาหน้าไม่เจอสั้นกว่านี้ ถือ Eye State เดิม (หาหน้าพลาดราวครึ่งหนึ่งของเฟรม)
#define UNCERTAIN_BRIDGE_MS    1000   // "ไม่แน่ใจ" ระหว่างตาปิดไม่เกินเท่านี้ ยังนับตาปิดต่อเนื่อง
#define NOTVISIBLE_BRIDGE_MS   2000   // ช่องว่างระหว่างตาปิดรวมหาหน้าไม่เจอ เชื่อมได้ไม่เกินเท่านี้ (หลับตาแล้วหาหน้าไม่เจอบ่อย)
#define OPEN_CONFIRM_MS        300    // ลืมตาต่อเนื่องเท่านี้จึงตัดช่วงตาปิด (โมเดลตอบ "เปิด" ผิดบางเฟรมขณะหลับตา)
#define CLOSED_SEEN_LEAD_MS    300    // ช่วงตาปิดนับเข้าเกณฑ์ได้เมื่อเวลาเห็นตาปิดจริง ≥ เวลาเห็นตาเปิด+ไม่แน่ใจ + เท่านี้
#define CLOSED_SEEN_MIN_PCT    25     //   และเวลาเห็นตาปิดจริง ≥ เท่านี้ % ของช่วง (ที่เหลือคือหาหน้าไม่เจอที่เชื่อมไว้)
#define CLOSED_VISIBLE_MIN_PCT 40     //   และเวลาที่เห็นหน้าจริง (ปิด+เปิด+ไม่แน่ใจ) ≥ เท่านี้ % ของช่วง = หาหน้าไม่เจอ (ถือค่า+เชื่อม) ไม่เกิน 60%
#define FRAME_DT_MAX_MS        400    // เฟรมห่างกันเกินนี้ (vision ค้าง) นับแค่เท่านี้ · กันเฟรมเดียวหลังค้างปลด latch หรือดันเวลาตาปิด
#define MUTE_DEFAULT_MS        (5UL * 60UL * 1000UL)
