// ชนิดข้อมูลตาม GLOSSARY.md · ไม่พึ่ง Arduino เพื่อให้ compile ทดสอบบน PC ได้ (firmware/tests/)
#pragma once
#include <stdint.h>

enum class EyeState : uint8_t { Unknown, Open, Closed, Uncertain, NotVisible };
enum class DrowsinessLevel : uint8_t { Normal = 0, Fatigue = 1, Drowsy = 2, Microsleep = 3 };
