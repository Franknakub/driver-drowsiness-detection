# Device ส่งข้อมูลเข้า Supabase ผ่านฟังก์ชัน Postgres (RPC) แทน Edge Function

แผนเดิม (ADR 0002, firmware-plan) ให้ Device ส่ง Sample/Event ผ่าน Edge Function `ingest` ที่ตรวจ secret ของ Device เราเปลี่ยนเป็นฟังก์ชัน `public.ingest(payload jsonb)` แบบ `security definer` ใน Postgres ซึ่ง Device เรียกผ่าน PostgREST (`/rest/v1/rpc/ingest`) ด้วย publishable key ฟังก์ชันเทียบ sha256 ของ `DEVICE_SECRET` กับค่าใน `devices` แล้วเขียน trips/samples/events ใน transaction เดียว ส่งซ้ำได้ (`on conflict do nothing`) ส่วน Driver ผูก Device กับบัญชีตัวเองผ่าน `claim_device()` จาก Cloud Dashboard ทุกอย่างอยู่ในไฟล์เดียว `cloud/sql/001_trip_history.sql`

## Considered Options

- **Edge Function**: ต้องลง Supabase CLI แล้ว login/deploy (ต้องจับ access token) และมีโค้ด TypeScript อีกชั้นที่ต้อง deploy ใหม่ทุกครั้งที่แก้ · cold start ~0.5–1 วิ · ข้อดีเดียวคือเพิ่ม logic ฝั่ง server ที่ไม่ใช่ SQL ได้ง่าย (เช่น ส่ง Telegram) ซึ่งไม่อยู่ในแผน และเพิ่มทีหลังได้โดยไม่ต้องรื้อของเดิม
- **ให้ Device insert ลงตารางตรงๆ ด้วย publishable key**: ง่ายสุดแต่ใครได้ key (สาธารณะ และ repo เป็น public) ก็เขียนข้อมูลปลอมได้

## Consequences

- ติดตั้งด้วยการวาง SQL ใน SQL Editor ครั้งเดียว ไม่ต้องใช้ CLI · ไฟล์อยู่ใน `cloud/sql/` ไม่ใช่ `supabase/migrations/` เพื่อไม่ให้ GitHub integration ของ Supabase รันเอง
- TLS บน ESP32 ต้องใช้ buffer 16 KB ซึ่ง RAM ภายในไม่พอหลัง WiFi + esp-dl · `sync.cpp` จึงให้ mbedTLS จองก้อน ≥ 1 KB จาก PSRAM (`mbedtls_platform_set_calloc_free`)
- Trip Summary บน cloud เป็น view `trip_summary` นิยามเดียวกับ `trip_record.cpp` บน Device · `cloud/test/sql_test.mjs` เทียบสองฝั่งด้วย Trip จำลอง
