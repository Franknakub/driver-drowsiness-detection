// ทดสอบ cloud/sql/001_trip_history.sql บน Postgres จำลองในเครื่อง (PGlite) ไม่แตะ Supabase จริง
//   cd firmware/tests && ./trip_record_test          (สร้าง trip_fixture.json = Trip จำลอง + Trip Summary ของ Device)
//   cd cloud/test && npm install && node sql_test.mjs
// จำลองส่วนของ Supabase ที่ SQL ใช้: role anon/authenticated, auth.users, auth.uid(), extensions.digest()
import { PGlite } from '@electric-sql/pglite';
import { readFileSync } from 'node:fs';

const here = new URL('.', import.meta.url);
const sql = readFileSync(new URL('../sql/001_trip_history.sql', here), 'utf8')
  .replace(/create extension if not exists pgcrypto[^;]*;/, '');           // PGlite ไม่มี pgcrypto: ใช้ stub ด้านล่าง
const fx = JSON.parse(readFileSync(new URL('../../firmware/tests/trip_fixture.json', here), 'utf8'));

let fails = 0, checks = 0;
const check = (ok, what) => { checks++; if (!ok) { fails++; console.log('FAIL', what); } };
const db = new PGlite();

await db.exec(`
  create role anon nologin; create role authenticated nologin;
  grant usage on schema public to anon, authenticated;
  create schema auth; grant usage on schema auth to anon, authenticated;
  create table auth.users (id uuid primary key);
  create function auth.uid() returns uuid language sql stable as
    $$ select nullif(current_setting('request.jwt.claim.sub', true), '')::uuid $$;
  create schema extensions; grant usage on schema extensions to anon, authenticated;
  create function extensions.digest(data text, alg text) returns bytea language sql immutable as
    $$ select sha256(convert_to(data, 'UTF8')) $$;
  insert into auth.users values ('00000000-0000-0000-0000-00000000000a'), ('00000000-0000-0000-0000-00000000000b');
`);
await db.exec(sql);
await db.exec(sql);                                   // รันซ้ำได้

const A = '00000000-0000-0000-0000-00000000000a', B = '00000000-0000-0000-0000-00000000000b';
const DEV = '28:84:85:65:63:44', SECRET = 'test-secret-1234567890';
async function as(role, uid, q, params) {
  await db.exec(`reset role; select set_config('request.jwt.claim.sub', '${uid ?? ''}', false); set role ${role};`);
  try { return await db.query(q, params); } finally { await db.exec('reset role;'); }
}
async function fails_(role, uid, q, params) {
  try { await as(role, uid, q, params); return null; } catch (e) { return e.message; }
}

// ---- claim_device ----
check(await fails_('anon', null, 'select public.claim_device($1,$2)', [DEV, SECRET]) !== null, 'anon cannot claim');
check(await fails_('authenticated', A, 'select public.claim_device($1,$2)', [DEV, 'short']) !== null, 'short secret rejected');
check(await fails_('authenticated', A, 'select public.claim_device($1,$2)', ['xx', SECRET]) !== null, 'bad device id rejected');
check(await fails_('authenticated', A, 'select public.claim_device($1,$2,$3)', [DEV.toLowerCase(), SECRET, 'รถฉัน']) === null, 'A claims');
check(await fails_('authenticated', B, 'select public.claim_device($1,$2)', [DEV, 'another-secret-123456']) !== null, 'B cannot steal');
check(await fails_('authenticated', A, 'select secret_hash from public.devices') !== null, 'secret_hash hidden');

// ---- ingest เป็นก้อนแบบที่ sync.cpp ส่ง ----
const call = (payload) => as('anon', null, 'select public.ingest($1::jsonb) as r', [JSON.stringify(payload)]);
const base = { device: DEV, secret: SECRET };
const tripNoTime = { ...fx.trip, started_at: null };
check(await fails_('anon', null, 'select public.ingest($1::jsonb)',
  [JSON.stringify({ device: DEV, secret: 'wrong-secret-123456', trip: fx.trip, samples: [], events: [] })]) !== null, 'wrong secret rejected');

let r = await call({ ...base, trip: tripNoTime, samples: fx.samples.slice(0, 250), events: fx.events.slice(0, 3) });
check(r.rows[0].r.samples === 250 && r.rows[0].r.events === 2, 'batch 1 counts (TimeKnown skipped) ' + JSON.stringify(r.rows[0].r));
r = await call({ ...base, trip: tripNoTime, samples: fx.samples.slice(200, 250), events: [] });     // ส่งซ้ำ
check(r.rows[0].r.samples === 0, 'duplicate batch inserts nothing');
r = await call({ ...base, trip: fx.trip, samples: fx.samples.slice(250), events: fx.events.slice(3) });
check(r.rows[0].r.samples === 350, 'batch 2 counts');

// ---- RLS ----
const anonRead = await fails_('anon', null, 'select count(*) from public.samples');
check(anonRead !== null, 'anon cannot read samples');
check((await as('authenticated', B, 'select count(*)::int as n from public.trip_summary')).rows[0].n === 0, 'B sees no trips');
check((await as('authenticated', B, 'select count(*)::int as n from public.samples')).rows[0].n === 0, 'B sees no samples');
check((await as('authenticated', A, 'select count(*)::int as n from public.samples')).rows[0].n === 600, 'A sees 600 samples');

// ---- Trip Summary ของ cloud = ของ Device ----
const s = (await as('authenticated', A, 'select * from public.trip_summary')).rows[0];
const d = fx.device_summary;
check(s.time_known === true && Math.round(s.started_at.getTime() / 1000) === fx.trip.started_at, 'time filled in by later batch');
check(s.duration_s === d.duration_s, `duration ${s.duration_s} vs ${d.duration_s}`);
check([s.level0_s, s.level1_s, s.level2_s, s.level3_s].join() === d.level_s.join(), 'level seconds');
check([s.level1_enter, s.level2_enter, s.level3_enter].join() === d.level_enter.join(), 'level entries');
check(s.microsleeps === d.microsleeps && s.distractions === d.distractions, 'event counts');
check(s.distraction_s === d.distraction_s && s.unmonitored_s === d.unmonitored_s, 'distraction/unmonitored seconds');
check(s.longest_closed_ms === d.longest_closed_ms, `longest closed ${s.longest_closed_ms} vs ${d.longest_closed_ms}`);
check(Math.abs(s.perclos_mean - d.perclos_mean) < 0.001, `perclos mean ${s.perclos_mean} vs ${d.perclos_mean}`);
console.log('cloud summary', JSON.stringify(s));

// ---- กราฟ ----
const series = (await as('authenticated', A, 'select * from public.trip_series($1, 60)', [s.trip_id])).rows;
check(series.length === 11 && series[0].t_s === 0, 'series buckets ' + series.length);
check((await as('authenticated', B, 'select count(*)::int as n from public.trip_series($1, 60)', [s.trip_id])).rows[0].n === 0, 'B gets empty series');

console.log(`${checks - fails}/${checks} checks passed`);
process.exit(fails ? 1 : 0);
