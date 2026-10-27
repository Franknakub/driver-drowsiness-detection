# Cloud: Supabase + Cloud Dashboard

ประวัติ Trip จาก Device ขึ้น Supabase แล้ว Driver ดูผ่าน Cloud Dashboard ([ADR 0002](../docs/adr/0002-device-first-history-synced-to-supabase.md), [ADR 0003](../docs/adr/0003-device-ingest-via-postgres-rpc.md))

```
Device (FFat) --sync.cpp--> POST /rest/v1/rpc/ingest (publishable key + DEVICE_SECRET)
                              -> trips / samples / events  (RLS: Driver เห็นเฉพาะ Device ของตัวเอง)
Cloud Dashboard (docs/dashboard/) --login อีเมล--> trip_summary (view) + trip_series() + events
```

- โปรเจกต์: `https://iatzjmxjbxcakotczoko.supabase.co` · publishable key อยู่ใน `config.h` และหน้า dashboard (เป็นค่าสาธารณะโดยออกแบบ)
- **ห้ามใส่ใน repo:** service_role / secret key, รหัสฐานข้อมูล, `DEVICE_SECRET`

## ติดตั้ง (ครั้งเดียว)

1. Supabase → SQL Editor → วาง [sql/001_trip_history.sql](sql/001_trip_history.sql) ทั้งไฟล์ → Run (รันซ้ำได้ ไม่ลบข้อมูล)
2. Authentication → URL Configuration: Site URL = `https://franknakub.github.io/driver-drowsiness-detection/dashboard/` · Redirect URLs เพิ่ม `http://127.0.0.1:8765/dashboard/` (ทดสอบในเครื่อง)
3. ตั้ง `DEVICE_SECRET` ใน `secrets.h` (ตัวอักษร/ตัวเลข ≥ 16 ตัว) แล้วแฟลช
4. เปิด Cloud Dashboard → สมัคร/login → "เพิ่ม Device" ใส่ `device_id` (จาก `/api/status`) + `DEVICE_SECRET` ค่าเดียวกัน

## ทดสอบ

```bash
cd firmware/tests && g++ -std=c++17 -Wall -I../DrowsyFW trip_record_test.cpp ../DrowsyFW/trip_record.cpp -o trip_record_test && ./trip_record_test   # สร้าง trip_fixture.json
cd cloud/test && npm install && node sql_test.mjs     # รัน SQL บน Postgres จำลอง (PGlite): RLS, ส่งซ้ำ, Trip Summary ตรงกับ Device
```

เปิด dashboard ในเครื่อง: `cd docs && python -m http.server 8765 --bind 127.0.0.1` → <http://127.0.0.1:8765/dashboard/>

เช็คจากภายนอกว่า SQL ขึ้นแล้ว (ควรได้ 403 `unknown device or wrong secret`):

```bash
curl -s -X POST https://iatzjmxjbxcakotczoko.supabase.co/rest/v1/rpc/ingest -H "apikey: <publishable key>" -H "Content-Type: application/json" -d '{"payload":{"device":"00:00:00:00:00:00","secret":"x","trip":{"boot":1,"nonce":1}}}'
```

## หมายเหตุ

- โปรเจกต์ฟรีถูก pause ถ้าไม่มีการใช้งาน 7 วัน — เช็คก่อนวันเดโม
- Trip Summary บน cloud คิดจาก Sample ที่ Sync แล้วเท่านั้น จะตรงกับ Device เมื่อ Sync ครบ
- กราฟใช้ `trip_series(trip_id, bucket_s)` รวม Sample เป็นช่วงๆ เพราะ API คืนได้ครั้งละ 1000 แถว
