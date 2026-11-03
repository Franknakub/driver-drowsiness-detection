// supabase-js ปลอม สำหรับดูหน้า Cloud Dashboard หลัง login โดยไม่ต้องมีบัญชี (ใช้กับ dashboard_preview.py)
// ?out=1 = ยังไม่ login · ?nodev=1 = ยังไม่มีอุปกรณ์ · ?empty=1 = ไม่มีทริป · ?delok=1 = ลบทริปสำเร็จ · ฟิลด์ตรงกับ cloud/sql
(() => {
  const q = new URLSearchParams(location.search), now = Date.now(), ago = (h, m = 0) => new Date(now - (h * 3600 + m * 60) * 1000).toISOString();
  const trips = q.has('empty') ? [] : [
    { trip_id: 41, boot: 41, started_at: ago(0, 41), duration_s: 2460, level0_s: 2300, level1_s: 120, level2_s: 40, level3_s: 0, microsleeps: 0, distractions: 3, perclos_mean: .07, longest_closed_ms: 1400, upd: ago(0, 1) },
    { trip_id: 40, boot: 40, started_at: ago(20, 13), duration_s: 5400, level0_s: 4200, level1_s: 700, level2_s: 420, level3_s: 80, microsleeps: 2, distractions: 5, perclos_mean: .16, longest_closed_ms: 3800 },
    { trip_id: 39, boot: 39, started_at: ago(35, 18), duration_s: 1980, level0_s: 1940, level1_s: 40, level2_s: 0, level3_s: 0, microsleeps: 0, distractions: 1, perclos_mean: .05, longest_closed_ms: 600 },
    { trip_id: 38, boot: 38, started_at: ago(49, 53), duration_s: 3120, level0_s: 2900, level1_s: 160, level2_s: 60, level3_s: 0, microsleeps: 0, distractions: 7, perclos_mean: .08, longest_closed_ms: 1900 },
    { trip_id: 37, boot: 37, started_at: ago(83, 33), duration_s: 90, level0_s: 90, level1_s: 0, level2_s: 0, level3_s: 0, microsleeps: 0, distractions: 0, perclos_mean: null, longest_closed_ms: 0 },
    { trip_id: 36, boot: 36, started_at: ago(94, 8), duration_s: 4200, level0_s: 3700, level1_s: 300, level2_s: 180, level3_s: 20, microsleeps: 1, distractions: 2, perclos_mean: .12, longest_closed_ms: 3200 },
    { trip_id: 35, boot: 35, started_at: ago(127, 3), duration_s: 1500, level0_s: 1480, level1_s: 20, level2_s: 0, level3_s: 0, microsleeps: 0, distractions: 0, perclos_mean: .04, longest_closed_ms: 500 },
    { trip_id: 30, boot: 30, started_at: null, duration_s: 610, level0_s: 600, level1_s: 10, level2_s: 0, level3_s: 0, microsleeps: 0, distractions: 0, perclos_mean: .05, longest_closed_ms: 400 },
  ];
  function series(id, b) {
    const t = trips.find(x => x.trip_id === id), n = Math.ceil(t.duration_s / b), out = [];
    for (let i = 0; i < n; i++) { const f = i / n; let lv = 0;
      if (t.level3_s && (Math.abs(f - .77) < .012 || Math.abs(f - .9) < .012)) lv = 3; else if (t.level2_s && (Math.abs(f - .76) < .04 || Math.abs(f - .89) < .03)) lv = 2; else if (t.level1_s && f > .4 && Math.abs(Math.sin(f * 40)) > .85) lv = 1;
      out.push({ t_s: i * b, level: lv, perclos: i < 2 ? null : Math.min(.55, .04 + lv * .09 + Math.abs(Math.sin(i * .7)) * .03), closed_ms: lv === 3 ? 3400 : 0, distraction: false }); }
    return out;
  }
  function events(id) {
    const t = trips.find(x => x.trip_id === id), e = [], d = t.duration_s * 1000;
    if (t.level1_s) e.push({ ms: d * .42, type: 0, from_level: 0, to_level: 1 });
    if (t.level2_s) e.push({ ms: d * .74, type: 0, from_level: 1, to_level: 2 });
    for (let k = 0; k < t.microsleeps; k++) e.push({ ms: d * (k ? .9 : .77), type: 1, value: 3800 - k * 400 });
    for (let k = 0; k < Math.min(t.distractions, 4); k++) e.push({ ms: d * (.1 + k * .2), type: 2 });
    if (t.level1_s) e.push({ ms: d * .95, type: 0, from_level: 2, to_level: 0 });
    return e.sort((a, b) => a.ms - b.ms);
  }
  const res = data => { const p = Promise.resolve({ data, error: null }); const c = { then: (a, b) => p.then(a, b) };
    ['select', 'order', 'limit', 'eq'].forEach(k => c[k] = () => c); return c; };
  const session = q.has('out') ? null : { user: { email: 'driver@example.com' } };
  window.supabase = { createClient: () => ({
    auth: { getSession: async () => ({ data: { session } }), onAuthStateChange: () => {}, signOut: async () => ({}),
      signInWithPassword: async () => ({ data: {}, error: { message: 'Invalid login credentials' } }), signUp: async () => ({ data: {}, error: null }) },
    rpc: async (name, a) => name === 'trip_series' ? { data: series(a.p_trip_id, a.p_bucket_s), error: null } : { data: null, error: null },
    from: tbl => {
      if (tbl === 'devices') return res(q.has('nodev') ? [] : [{ device_id: '28:84:85:65:63:44', name: 'รถของฉัน', last_seen: ago(0, 1) }]);
      if (tbl === 'trip_summary') return res(trips.map(({ upd, ...t }) => t));
      if (tbl === 'trips') return { select: () => res(trips.map(t => ({ id: t.trip_id, updated_at: t.upd || ago(1) }))), delete: () => ({ eq: () => ({ select: async () => ({ data: q.has('delok') ? [{ id: 40 }] : [], error: null }) }) }) };
      if (tbl === 'events') { let id; const c = res(null); c.eq = (_, v) => { id = v; return c; }; c.then = (a, b) => Promise.resolve({ data: events(id), error: null }).then(a, b); return c; }
    } }) };
})();
