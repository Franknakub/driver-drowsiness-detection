"""ตรวจการชนระหว่างกล่องกับหุ่นจำลองชิ้นส่วน (ทรงกล่อง/ทรงกระบอกอย่างหยาบ) + ส่ง SVG มุมมองบน
รัน: <venv>/Scripts/python.exe check.py
"""
from build123d import ExportSVG, LineType, Pos, Rot, Unit
import enclosure as E
from enclosure import box, cyl

zp = E.Z_PCB
dummies = {
    "PCB": box(0, E.PCB_W, 0, E.PCB_H, E.Z_PCB_BOT, zp),
    "module": box(E.MOD_LEFT, E.MOD_LEFT + E.MOD_W, E.MOD_TOP_Y, E.MOD_BOT_Y, E.Z_MOD - 1.6, E.Z_MOD),
    "module parts (mod_top 3.27, excl. lens)": box(E.MOD_LEFT + 1, E.MOD_LEFT + E.MOD_W - 1, E.MOD_TOP_Y + 1, E.MOD_BOT_Y - 7, E.Z_MOD, E.Z_MOD + E.MOD_TOP),
    "camera body 8.5": box(E.LENS[0] - 4.25, E.LENS[0] + 4.25, E.LENS[1] - 4.25, E.LENS[1] + 4.25, E.Z_PCB + E.ST_CAN, E.Z_LENS - 2.0),
    "lens": cyl(*E.LENS, E.LENS_D / 2, E.Z_MOD, E.Z_LENS),
    "header U6": box(13.08 - 1.27, 15.62 + 1.27, 2.29 - 1.27, 50.55 + 1.27, zp, zp + E.HDR_H),
    "header U5": box(41.02 - 1.27, 43.56 + 1.27, 2.29 - 1.27, 50.55 + 1.27, zp, zp + E.HDR_H),
    "SW1 body": box(E.SW1[0] - E.SW_BODY[0] / 2, E.SW1[0] + E.SW_BODY[0] / 2, E.SW1[1] - E.SW_BODY[1] / 2, E.SW1[1] + E.SW_BODY[1] / 2, zp, zp + E.SW_BODY[2]),
    "USB1": box(-E.USB1_OVER, 7.3, E.USB1[1] - 4.47, E.USB1[1] + 4.47, zp, zp + E.USB1_H),
    "LDR": cyl(*E.LDR, 2.55, zp, zp + E.LDR_H),
    "H1 + dupont (dup_l)": box(5.33 - 1.27, 5.33 + 1.27, 38.23 - 1.27, 45.85 + 1.27, zp, zp + E.DUP_L),
    "H2 + dupont (dup_l)": box(4.32 - 1.27, 9.40 + 1.27, 9.02 - 1.27, 9.02 + 1.27, zp, zp + E.DUP_L),
    "C7 (assumed h 6)": cyl(4.95, 54.99, 2.65, zp, zp + 6),
    "OLED pcb": E.oled_box(0, E.OLED_W, 0, E.OLED_H,
                    E.Z_LID_IN - E.OLED_RECESS - E.OLED_T, E.Z_LID_IN - E.OLED_RECESS - E.OLED_T + E.OLED_PCB_T),
    "OLED glass": E.oled_box(E.GLASS_DX, E.GLASS_DX + E.GLASS_W, E.GLASS_DY,
                      E.GLASS_DY + E.GLASS_H, E.Z_LID_IN - E.OLED_RECESS - E.OLED_T + E.OLED_PCB_T, E.Z_LID_IN - E.OLED_RECESS - 0.01),
    "OLED dupont (dup_l behind pcb)": E.oled_box(9, 20, 0, 2.6,
                      E.Z_LID_IN - E.OLED_RECESS - E.OLED_T - E.DUP_L, E.Z_LID_IN - E.OLED_RECESS - E.OLED_T),
    "buzzer": box(E.BZ_X0, E.BZ_X0 + E.BZ_W, E.BZ_Y0, E.BZ_Y0 + E.BZ_L, E.BZ_Z0, E.BZ_Z0 + E.BZ_HGT),
    "buzzer dupont room": box(E.BZ_X0, E.BZ_X0 + E.BZ_W, E.BZ_Y0 - E.BZ_PIN_ROOM, E.BZ_Y0, E.BZ_Z0, E.BZ_Z0 + 3),
}
for i, (x, y) in enumerate(E.IR, 1):
    dummies[f"IR{i}"] = cyl(x, y, E.IR_D / 2, zp, zp + E.IR_H)
for i in range(len(E.LEDS)):
    dummies[f"LED{i + 1} rod D3"] = E.led_rod(i, 1.5, zp + E.LED_ROD_Z0, E.Z_TOP)

# ขา THT ที่โผล่ใต้บอร์ด (bot_h) — DESIGN จาก Drill_PTH
zl0, zl1 = E.Z_PCB_BOT - E.BOT_H, E.Z_PCB_BOT
leads = {
    "U6 leads": box(13.08 - 0.5, 15.62 + 0.5, 2.29 - 0.5, 50.55 + 0.5, zl0, zl1),
    "U5 leads": box(41.02 - 0.5, 43.56 + 0.5, 2.29 - 0.5, 50.55 + 0.5, zl0, zl1),
}
for i, (x, y) in enumerate(E.IR, 1):
    leads[f"IR{i} leads"] = box(52.49 - 0.5, 55.03 + 0.5, y - 0.5, y + 0.5, zl0, zl1)
dummies.update(leads)
mx, my = E.MOUNT_14
screw_max = cyl(mx, my, 6.35 / 2, -E.MOUNT_PAD_T, E.Z_PCB_BOT - 0.5)

lid, base = E.make_lid(), E.make_base()
for name, d in leads.items():
    if (d & screw_max).volume > 0.01:
        print(f"  CLASH mount screw (max length) x {name}")
print(f"lid solids {len(lid.solids())} valid {lid.is_valid} | base solids {len(base.solids())} valid {base.is_valid}")
bad = 0
for name, d in dummies.items():
    for pname, part in (("lid", lid), ("base", base)):
        v = (d & part).volume
        if v > 0.01:
            bad += 1
            print(f"  CLASH {name} x {pname}: {v:.2f} mm3")
print("no clashes" if not bad else f"{bad} clashes")

# มุมมองบน (มองฝาจากด้านคนขับ) + ฐานจากด้านล่าง
for name, part in (("lid_top", lid), ("base_bottom", Rot(180, 0, 0) * base)):
    svg = ExportSVG(unit=Unit.MM, line_weight=0.15)
    svg.add_layer("v", line_type=LineType.CONTINUOUS)
    svg.add_layer("h", line_color=(170, 170, 170), line_type=LineType.HIDDEN)
    vis, hid = part.project_to_viewport((0, 0, 500), viewport_up=(0, 1, 0))
    svg.add_shape(hid, layer="h")
    svg.add_shape(vis, layer="v")
    svg.write(str(E.OUT / f"{name}.svg"))
print("svg written")

# ชิ้นที่ต้องไม่ชนกันเอง (ไม่เกี่ยวกับกล่อง)
for a, b in (("LED1 rod D3", "module"), ("LED2 rod D3", "module"), ("LED1 rod D3", "module parts (mod_top 3.27, excl. lens)")):
    for k in (a, b):
        assert k in dummies, k
    v = (dummies[a] & dummies[b]).volume
    print(f"  {'CLASH' if v > 0.01 else 'ok   '} {a} x {b}: {v:.2f} mm3")

# ด้ามต่อ SW1 ตลอดระยะเลื่อน ต้องไม่ชนฝา (ปีกอยู่ใต้ฝา หัวโผล่ผ่านร่อง)
ext = E.make_sw_ext()
for dx in (-E.SW_TRAVEL / 2, 0, E.SW_TRAVEL / 2):
    placed = Pos(E.SW1[0] + dx, -E.SW1[1], E.Z_PCB + E.SW_H - 3.0) * ext
    v = (placed & lid).volume if (placed & lid) is not None else 0.0
    print(f"  {'CLASH' if v > 0.01 else 'ok   '} SW1 extender at {dx:+.2f} x lid: {v:.2f} mm3")

# หัวสาย USB-C (ขนาดวัดจริง) เสียบที่ USB1 ต้องไม่ชนฐาน/ฝา
zc = E.Z_PCB + E.USB1_H / 2
plug = box(-E.USB1_OVER - 21.6, -E.USB1_OVER, E.USB1[1] - E.USBC_PLUG[0] / 2, E.USB1[1] + E.USBC_PLUG[0] / 2,
           zc - E.USBC_PLUG[1] / 2, zc + E.USBC_PLUG[1] / 2)
for pname, part in (("lid", lid), ("base", base)):
    r = plug & part
    v = r.volume if r is not None else 0.0
    print(f"  {'CLASH' if v > 0.01 else 'ok   '} USB-C plug x {pname}: {v:.2f} mm3")
