-- ประวัติ Trip บน Supabase (ADR 0002) · วางทั้งไฟล์ใน Supabase → SQL Editor แล้วกด Run ครั้งเดียว
-- รันซ้ำได้ (ทุกคำสั่งเป็น if not exists / or replace) แต่ไม่ลบข้อมูลเดิม
--
-- ใครทำอะไรได้:
--   Device  → เรียก public.ingest(payload) ด้วย publishable key + DEVICE_SECRET ของตัวเอง (เทียบกับ sha256 ใน devices)
--   Driver  → login ด้วยอีเมล แล้วเพิ่ม Device ของตัวเองด้วย public.claim_device(device_id, secret)
--             อ่านได้เฉพาะ trips / samples / events / trip_summary ของ Device ที่ตัวเองเป็นเจ้าของ (RLS)
--   anon    → เรียก ingest ได้อย่างเดียว อ่านตารางไม่ได้
-- ไฟล์นี้อยู่ใน cloud/ ไม่ใช่ supabase/migrations/ ตั้งใจไม่ให้ GitHub integration ของ Supabase รันเอง

create extension if not exists pgcrypto with schema extensions;

-- ---------- ตาราง ----------

create table if not exists public.devices (
  device_id   text primary key,                       -- MAC ของ Device เช่น 28:84:85:65:63:44
  owner       uuid not null references auth.users(id) on delete cascade,
  secret_hash text not null,                          -- sha256 (hex) ของ DEVICE_SECRET
  name        text,
  created_at  timestamptz not null default now(),
  last_seen   timestamptz
);

create table if not exists public.trips (
  id          bigint generated always as identity primary key,
  device_id   text not null references public.devices(device_id) on delete cascade,
  boot        integer not null,                       -- หมายเลข Trip บน Device
  nonce       bigint not null,                        -- สุ่มทุกบูต: แยก Trip ได้แม้เลข boot ซ้ำหลังล้าง flash
  fw          text,
  time_known  boolean not null default false,
  started_at  timestamptz,                            -- null = ไม่ทราบเวลา
  samples_on_device integer not null default 0,       -- จำนวน Sample บน Device ตอนส่งครั้งล่าสุด
  events_on_device  integer not null default 0,
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now(),
  unique (device_id, boot, nonce)
);
create index if not exists trips_device_idx on public.trips (device_id, started_at desc);

create table if not exists public.samples (             -- 1 แถว = 1 วินาที
  trip_id     bigint not null references public.trips(id) on delete cascade,
  i           integer not null,                       -- ลำดับใน Trip (0, 1, 2, ...)
  ms          bigint not null,                        -- millis() นับจากเปิดเครื่อง (= เริ่ม Trip)
  eye         smallint not null,                      -- Eye State: 0 unknown 1 open 2 closed 3 uncertain 4 not_visible
  level       smallint not null,                      -- Drowsiness Level 0..3
  distraction boolean not null,
  unmonitored boolean not null,
  ir          boolean not null,
  face        boolean not null,
  p_closed    real not null,
  perclos     real,                                   -- null = ยังไม่พร้อม
  fps         real not null,
  ldr         integer not null,                       -- uint16 บน Device · record ที่เขียนไม่ครบอ่านได้ 65535 เกิน smallint
  closed_ms   integer not null,                       -- ตาปิดต่อเนื่องที่ยาวที่สุดในวินาทีนั้น
  primary key (trip_id, i)
);
alter table public.samples alter column ldr type integer;   -- โปรเจกต์ที่สร้างตอน ldr ยังเป็น smallint

create table if not exists public.events (
  trip_id     bigint not null references public.trips(id) on delete cascade,
  i           integer not null,                       -- ลำดับใน Event ของ Trip (เว้นช่วงได้: ไม่เก็บ TimeKnown)
  ms          bigint not null,
  type        smallint not null,                      -- 0 LevelChange · 1 Microsleep · 2 Distraction
  from_level  smallint,
  to_level    smallint,
  value       bigint,                                 -- ตาปิดต่อเนื่อง (ms) ตอนเกิด
  primary key (trip_id, i)
);

-- ---------- Trip Summary (นิยามเดียวกับ trip_record.cpp บน Device) ----------

create or replace view public.trip_summary with (security_invoker = true) as
select
  t.id as trip_id, t.device_id, t.boot, t.fw, t.time_known, t.started_at,
  t.samples_on_device, t.events_on_device,
  coalesce(s.n, 0)                      as duration_s,
  coalesce(s.l0, 0) as level0_s, coalesce(s.l1, 0) as level1_s,
  coalesce(s.l2, 0) as level2_s, coalesce(s.l3, 0) as level3_s,
  coalesce(e.enter1, 0) as level1_enter, coalesce(e.enter2, 0) as level2_enter, coalesce(e.enter3, 0) as level3_enter,
  coalesce(e.microsleeps, 0)            as microsleeps,
  coalesce(e.distractions, 0)           as distractions,
  coalesce(s.distraction_s, 0)          as distraction_s,
  coalesce(s.unmonitored_s, 0)          as unmonitored_s,
  greatest(coalesce(s.closed_max, 0), coalesce(e.micro_max, 0)) as longest_closed_ms,
  s.perclos_mean
from public.trips t
left join lateral (
  select count(*)::int as n,
         count(*) filter (where level = 0)::int as l0, count(*) filter (where level = 1)::int as l1,
         count(*) filter (where level = 2)::int as l2, count(*) filter (where level = 3)::int as l3,
         count(*) filter (where distraction)::int as distraction_s,
         count(*) filter (where unmonitored)::int as unmonitored_s,
         max(closed_ms) as closed_max,
         avg(perclos)::real as perclos_mean
  from public.samples where trip_id = t.id
) s on true
left join lateral (
  select count(*) filter (where type = 0 and to_level > from_level and to_level = 1)::int as enter1,
         count(*) filter (where type = 0 and to_level > from_level and to_level = 2)::int as enter2,
         count(*) filter (where type = 0 and to_level > from_level and to_level = 3)::int as enter3,
         count(*) filter (where type = 1)::int as microsleeps,
         count(*) filter (where type = 2)::int as distractions,
         max(value) filter (where type = 1) as micro_max
  from public.events where trip_id = t.id
) e on true;

-- กราฟราย Trip: รวม Sample เป็นช่วงละ p_bucket_s วินาที (API คืนได้ครั้งละ 1000 แถว)
create or replace function public.trip_series(p_trip_id bigint, p_bucket_s integer default 10)
returns table (t_s integer, level smallint, perclos real, p_closed real, closed_ms integer,
               distraction boolean, unmonitored boolean, ir boolean, n integer)
language sql stable security invoker set search_path = ''
as $$
  select (s.ms / 1000 / greatest(p_bucket_s, 1) * greatest(p_bucket_s, 1))::int,
         max(s.level), avg(s.perclos)::real, avg(s.p_closed)::real, max(s.closed_ms),
         bool_or(s.distraction), bool_or(s.unmonitored), bool_or(s.ir), count(*)::int
  from public.samples s
  where s.trip_id = p_trip_id
  group by 1
  order by 1
$$;

-- ---------- Device ส่งข้อมูล ----------

-- payload = {"device","secret","trip":{"boot","nonce","fw","started_at"|null,"samples","events"},
--            "samples":[[i,ms,eye,level,flags,p_closed‰,perclos‰|null,fps×10,ldr,closed_ms],...],
--            "events":[[i,ms,type,from,to,value],...]}
-- flags: 1 distraction · 2 unmonitored · 4 ir · 8 face · รับซ้ำได้ (on conflict do nothing)
create or replace function public.ingest(payload jsonb)
returns jsonb
language plpgsql security definer set search_path = ''
as $$
declare
  d   text  := upper(payload->>'device');
  h   text  := encode(extensions.digest(coalesce(payload->>'secret', ''), 'sha256'), 'hex');
  tr  jsonb := payload->'trip';
  tid bigint;
  ns  integer;
  ne  integer;
begin
  update public.devices set last_seen = now() where device_id = d and secret_hash = h;
  if not found then
    raise exception 'unknown device or wrong secret' using errcode = '28000';
  end if;

  insert into public.trips as t (device_id, boot, nonce, fw, time_known, started_at, samples_on_device, events_on_device)
  values (d, (tr->>'boot')::int, (tr->>'nonce')::bigint, tr->>'fw', (tr->>'started_at') is not null,
          to_timestamp((tr->>'started_at')::double precision),
          coalesce((tr->>'samples')::int, 0), coalesce((tr->>'events')::int, 0))
  on conflict (device_id, boot, nonce) do update set
    fw = excluded.fw,
    time_known = t.time_known or excluded.time_known,
    started_at = coalesce(t.started_at, excluded.started_at),
    samples_on_device = greatest(t.samples_on_device, excluded.samples_on_device),
    events_on_device = greatest(t.events_on_device, excluded.events_on_device),
    updated_at = now()
  returning t.id into tid;

  insert into public.samples (trip_id, i, ms, eye, level, distraction, unmonitored, ir, face,
                              p_closed, perclos, fps, ldr, closed_ms)
  select tid, (x->>0)::int, (x->>1)::bigint, (x->>2)::smallint, (x->>3)::smallint,
         ((x->>4)::int & 1) <> 0, ((x->>4)::int & 2) <> 0, ((x->>4)::int & 4) <> 0, ((x->>4)::int & 8) <> 0,
         (x->>5)::real / 1000, (x->>6)::real / 1000, (x->>7)::real / 10, (x->>8)::int, (x->>9)::int
  from jsonb_array_elements(coalesce(payload->'samples', '[]'::jsonb)) x
  on conflict do nothing;
  get diagnostics ns = row_count;

  insert into public.events (trip_id, i, ms, type, from_level, to_level, value)
  select tid, (x->>0)::int, (x->>1)::bigint, (x->>2)::smallint, (x->>3)::smallint, (x->>4)::smallint, (x->>5)::bigint
  from jsonb_array_elements(coalesce(payload->'events', '[]'::jsonb)) x
  where (x->>2)::int in (0, 1, 2)                     -- TimeKnown (10) ใช้บน Device อย่างเดียว
  on conflict do nothing;
  get diagnostics ne = row_count;

  return jsonb_build_object('trip_id', tid, 'samples', ns, 'events', ne);
end
$$;

-- ---------- Driver เพิ่ม Device ของตัวเอง ----------

-- ใส่ device_id (ดูจาก /api/status ของ Device) + DEVICE_SECRET ค่าเดียวกับใน secrets.h
-- เจ้าของเดิมเรียกซ้ำได้เพื่อเปลี่ยนรหัส · Device ที่คนอื่นเป็นเจ้าของแล้วเพิ่มไม่ได้
create or replace function public.claim_device(p_device_id text, p_secret text, p_name text default null)
returns void
language plpgsql security definer set search_path = ''
as $$
begin
  if auth.uid() is null then
    raise exception 'login required' using errcode = '28000';
  end if;
  if p_device_id !~* '^([0-9a-f]{2}:){5}[0-9a-f]{2}$' then
    raise exception 'device_id must look like 28:84:85:65:63:44';
  end if;
  if length(coalesce(p_secret, '')) < 16 then
    raise exception 'secret must be at least 16 characters';
  end if;
  insert into public.devices as d (device_id, owner, secret_hash, name)
  values (upper(p_device_id), auth.uid(), encode(extensions.digest(p_secret, 'sha256'), 'hex'), p_name)
  on conflict (device_id) do update set
    secret_hash = excluded.secret_hash,
    name = coalesce(excluded.name, d.name)
  where d.owner = auth.uid();
  if not found then
    raise exception 'this device belongs to another account';
  end if;
end
$$;

-- ---------- RLS + สิทธิ์ (โปรเจกต์ตั้ง auto-expose ตารางใหม่ = ปิด จึงต้อง grant เอง) ----------
-- Supabase ให้สิทธิ์ execute ฟังก์ชันใหม่แก่ anon/authenticated โดยตรง จึงต้อง revoke จากทั้งสามก่อน grant ใหม่

alter table public.devices enable row level security;
alter table public.trips   enable row level security;
alter table public.samples enable row level security;
alter table public.events  enable row level security;

drop policy if exists "driver reads own devices" on public.devices;
create policy "driver reads own devices" on public.devices
  for select to authenticated using (owner = (select auth.uid()));

drop policy if exists "driver reads own trips" on public.trips;
create policy "driver reads own trips" on public.trips
  for select to authenticated using (
    exists (select 1 from public.devices d where d.device_id = trips.device_id and d.owner = (select auth.uid())));

-- Driver ลบ Trip ของตัวเองได้จาก Cloud Dashboard · samples/events หายตาม on delete cascade
-- (Device ไม่ส่ง Trip ที่ Sync ครบแล้วซ้ำ · Trip ที่กำลังบันทึกอยู่ dashboard ไม่ให้ลบ)
drop policy if exists "driver deletes own trips" on public.trips;
create policy "driver deletes own trips" on public.trips
  for delete to authenticated using (
    exists (select 1 from public.devices d where d.device_id = trips.device_id and d.owner = (select auth.uid())));

drop policy if exists "driver reads own samples" on public.samples;
create policy "driver reads own samples" on public.samples
  for select to authenticated using (
    exists (select 1 from public.trips t join public.devices d on d.device_id = t.device_id
            where t.id = samples.trip_id and d.owner = (select auth.uid())));

drop policy if exists "driver reads own events" on public.events;
create policy "driver reads own events" on public.events
  for select to authenticated using (
    exists (select 1 from public.trips t join public.devices d on d.device_id = t.device_id
            where t.id = events.trip_id and d.owner = (select auth.uid())));

revoke all on public.devices, public.trips, public.samples, public.events, public.trip_summary from anon, authenticated;
grant select (device_id, owner, name, created_at, last_seen) on public.devices to authenticated;   -- ไม่ให้เห็น secret_hash
grant select on public.trips, public.samples, public.events, public.trip_summary to authenticated;
grant delete on public.trips to authenticated;

revoke all on function public.ingest(jsonb) from public, anon, authenticated;
revoke all on function public.claim_device(text, text, text) from public, anon, authenticated;
revoke all on function public.trip_series(bigint, integer) from public, anon, authenticated;
grant execute on function public.ingest(jsonb) to anon, authenticated;
grant execute on function public.claim_device(text, text, text) to authenticated;
grant execute on function public.trip_series(bigint, integer) to authenticated;
