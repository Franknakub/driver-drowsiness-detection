// คัดลอกไฟล์นี้เป็น secrets.h แล้วใส่ค่าจริง · secrets.h อยู่ใน .gitignore (repo เป็น public)
#pragma once

// WiFi ที่ Device จะลองต่อ (hotspot มือถือ + WiFi บ้าน) ใส่ได้หลายเครือข่าย
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred WIFI_NETWORKS[] = {
  {"phone-hotspot", "password"},
  {"home-wifi",     "password"},
};

// รหัส AP ของ Device (Drowsy-XXXX) อย่างน้อย 8 ตัวอักษร
#define AP_PASSWORD    "change-me-123"

// รหัสหน้า OTA (/update) ผู้ใช้ชื่อ admin
#define OTA_PASSWORD   "change-me-ota"

// รหัสลับของ Device สำหรับ Sync ขึ้น Supabase (URL + publishable key อยู่ใน config.h)
// ตั้งเอง: ตัวอักษร/ตัวเลขล้วน อย่างน้อย 16 ตัว แล้วกรอกค่าเดียวกันในหน้า Cloud Dashboard ตอนเพิ่ม Device
// ยังเป็นค่านี้อยู่ = ไม่ Sync
#define DEVICE_SECRET       "<device-secret>"
