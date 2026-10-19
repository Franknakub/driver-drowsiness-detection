# เก็บประวัติบน Device ก่อนเสมอ แล้ว Sync ขึ้น Supabase

ระหว่างขับ รถมักไม่มีเน็ต และบอร์ดไม่มี RTC เราจึงให้ Device เขียน Sample และ Event ลง FFat ก่อนทุกครั้ง แล้วค่อย Sync ขึ้น Supabase เมื่อต่อเน็ตได้ (ใช้ hotspot มือถือเป็นหลัก) ข้อมูลที่ Sync แล้วลบออกจาก Device ได้ เพื่อประหยัดพื้นที่ Cloud Dashboard อ่านข้อมูลจาก Supabase ส่วน Device Dashboard แสดงเฉพาะสถานะสด การตั้งค่า และ Trip ที่ยังไม่ได้ Sync

## Considered Options

- **Firebase Firestore**: คิดเงินตามจำนวนครั้งที่เขียน ถ้าเขียน Sample ทีละ 1 วินาทีต้องรวมเป็นก้อนก่อนส่ง และ query แนวโน้มข้าม Trip ได้ยาก
- **InfluxDB + Grafana Cloud**: ได้ dashboard ทันทีโดยไม่ต้องเขียนเว็บ แต่ free tier ลบข้อมูลที่เก่ากว่า 30 วัน ซึ่งขัดกับเป้าหมายเรื่องดูประวัติ

## Consequences

- Trip ที่ขับโดยไม่มีเน็ตตลอดทั้ง Trip และไม่มีใครเปิด Device Dashboard จะไม่มีเวลาจริง ให้ติดป้าย "ไม่ทราบเวลา"
- โปรเจกต์ Supabase แบบ free จะถูก pause ถ้าไม่มีการใช้งาน 7 วัน
- repo เป็น public ดังนั้น key ของ Device ต้องอยู่นอก git
