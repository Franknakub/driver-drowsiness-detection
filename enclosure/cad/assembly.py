"""ส่งออกชิ้นส่วนทั้งชุดเป็น STL แยกตามวัสดุ สำหรับหน้าดูภาพประกอบ 3D
รัน: <venv>/Scripts/python.exe assembly.py   → out/assembly/*.stl
ชิ้นอิเล็กทรอนิกส์เป็นหุ่นจำลองทรงง่ายจากค่าวัด ใช้ดูภาพรวม ไม่ใช่ตรวจการชน (ใช้ check.py)
"""
import math
from build123d import Align, Box, Compound, Cylinder, Plane, Pos, RegularPolygon, Rot, Solid, Sphere, export_stl, extrude
import enclosure as E
from enclosure import box, cyl

OUT = E.OUT / "assembly"
zp = E.Z_PCB


def group(*shapes):
    return Compound(children=list(shapes))


def make_groups():
    g = {}
    g["lid"] = E.make_lid()
    g["base"] = E.make_base()
    g["switch_knob"] = Pos(E.SW1[0], -E.SW1[1], zp + E.SW_H - 3.0) * E.make_sw_ext()

    g["pcb"] = box(0, E.PCB_W, 0, E.PCB_H, E.Z_PCB_BOT, zp)

    mod_bot = E.Z_MOD - 1.6
    headers = [box(13.08 - 1.27, 15.62 + 1.27, 2.29 - 1.27, 50.55 + 1.27, zp, zp + E.HDR_H),
               box(41.02 - 1.27, 43.56 + 1.27, 2.29 - 1.27, 50.55 + 1.27, zp, zp + E.HDR_H)]
    g["module"] = group(
        box(E.MOD_LEFT, E.MOD_LEFT + E.MOD_W, E.MOD_TOP_Y, E.MOD_BOT_Y - 6.1, mod_bot, E.Z_MOD),
        *headers,
        box(E.LENS[0] - 4.25, E.LENS[0] + 4.25, E.LENS[1] - 4.25, E.LENS[1] + 4.25, E.Z_PCB + E.ST_CAN,
            E.Z_LENS - 2.2),
        cyl(*E.LENS, E.LENS_D / 2, E.Z_LENS - 2.2, E.Z_LENS),
        box(E.SW1[0] - E.SW_BODY[0] / 2, E.SW1[0] + E.SW_BODY[0] / 2, E.SW1[1] - E.SW_BODY[1] / 2,
            E.SW1[1] + E.SW_BODY[1] / 2, zp, zp + E.SW_BODY[2]),
        box(5.33 - 1.27, 5.33 + 1.27, 38.23 - 1.27, 45.85 + 1.27, zp, zp + 2.5),
        box(4.32 - 1.27, 9.40 + 1.27, 9.02 - 1.27, 9.02 + 1.27, zp, zp + 2.5),
    )
    g["antenna"] = box(E.MOD_LEFT + 2, E.MOD_LEFT + E.MOD_W - 2, E.MOD_BOT_Y - 6.1, E.MOD_BOT_Y,
                       mod_bot, E.Z_MOD)
    g["metal"] = group(
        box(E.LENS[0] - 9, E.LENS[0] + 9, E.LENS[1] - 12, E.LENS[1] + 8, E.Z_MOD, E.Z_PCB + E.ST_CAN),
        *[box(x - E.USBM_PLUG[0] / 2 + 0.95, x + E.USBM_PLUG[0] / 2 - 0.95, E.MOD_TOP_Y, E.MOD_TOP_Y + 7.3,
              E.Z_MOD, E.Z_MOD + E.USBM_HH) for x in E.USBM_X],
        box(-E.USB1_OVER, 7.3, E.USB1[1] - 4.47, E.USB1[1] + 4.47, zp, zp + E.USB1_H),
    )
    g["nut"] = extrude(Plane.XY.offset(-E.MOUNT_PAD_T + E.MOUNT_SKIN) * Pos(E.MOUNT_14[0], -E.MOUNT_14[1]) *
                       RegularPolygon(E.NUT14_AF / 2 / 0.8660254, 6), E.NUT14_T)
    g["ir_led"] = group(*[cyl(x, y, E.IR_D / 2 - 0.4, zp, zp + E.IR_H - 2.5) for x, y in E.IR],
                        *[Pos(x, -y, zp + E.IR_H - 2.5) * Cylinder(2.5, 2.5, align=(Align.CENTER,) * 2 + (Align.MIN,))
                          for x, y in E.IR],
                        cyl(*E.LDR, 2.4, zp, zp + E.LDR_H))
    g["led_rods"] = group(*[E.led_rod(i, 1.5, zp + E.LED_ROD_Z0, E.Z_TOP) for i in range(len(E.LEDS))])
    oled_bottom = E.Z_LID_IN - E.OLED_RECESS - E.OLED_T
    g["oled_pcb"] = E.oled_box(0, E.OLED_W, 0, E.OLED_H, oled_bottom, oled_bottom + E.OLED_PCB_T)
    g["buzzer_pcb"] = box(E.BZ_X0, E.BZ_X0 + E.BZ_W, E.BZ_Y0, E.BZ_Y0 + E.BZ_L, E.BZ_Z0, E.BZ_Z0 + 1.6)
    g["oled_glass"] = E.oled_box(E.GLASS_DX, E.GLASS_DX + E.GLASS_W, E.GLASS_DY,
                          E.GLASS_DY + E.GLASS_H, oled_bottom + E.OLED_PCB_T,
                          E.Z_LID_IN - E.OLED_RECESS)
    g["buzzer"] = cyl(E.BZ_X0 + E.BZ_W / 2, E.BZ_Y0 + E.BZ_L - 8.5, 6.0, E.BZ_Z0 + 1.6, E.BZ_Z0 + E.BZ_HGT)

    # สายไฟ USB-C เสียบที่ USB1 (หัวพลาสติก usbc_w × usbc_t × usbc_l วัดจริง) + สายต่อลงล่าง (−x บอร์ด)
    pw, pt, pl = E.USBC_PLUG[0], E.USBC_PLUG[1], 21.6
    zc = zp + E.USB1_H / 2
    x_face = -E.USB1_OVER
    plug = box(x_face - pl, x_face, E.USB1[1] - pw / 2, E.USB1[1] + pw / 2, zc - pt / 2, zc + pt / 2)
    cable = Pos(x_face - pl, -E.USB1[1], zc) * Rot(0, -90, 0) * Cylinder(1.75, 35, align=(Align.CENTER,) * 2 + (Align.MIN,))
    g["usb_cable"] = group(plug, cable)
    # เคเบิลไทร์: ร้อยผ่านร่องบนหูฐาน รัดรอบหัว USB-C กับปลายหู (แถบกว้าง 2.5 หนา 1)
    z_back = -E.MOUNT_PAD_T
    tx0, tx1 = E.OX0 - 13.05, E.OX0 - 7.05             # ปลายหู → ขอบในร่อง
    tz0, tz1 = z_back - 1.0, zc + pt / 2 + 1.0
    ty0, ty1 = E.USB1[1] - 1.25, E.USB1[1] + 1.25
    g["zip_tie"] = box(tx0, tx1, ty0, ty1, tz0, tz1) - box(tx0 + 1, tx1 - 1, ty0 - 1, ty1 + 1, tz0 + 1, tz1 - 1)

    # สาย dupont เมีย–เมีย: H1 → จอ OLED (4 เส้น) · H2 → buzzer (3 เส้น) · เส้นทางเดินโดยประมาณ ไม่ได้ระบุสีตามขั้ว
    hz = zp + 2.5                                   # ยอดพลาสติก header ตัวผู้บนบอร์ด
    top = zp + E.DUP_L                              # ปลายหัว dupont ที่ H1/H2
    zr = E.Z_LID_IN - 2.6                           # ระดับเดินสายใต้ฝา
    housings, wires = [], []
    h1 = [38.23 + 2.54 * i for i in range(4)]
    oled_back = E.Z_LID_IN - E.OLED_RECESS - E.OLED_T
    oled_pin_x = E.OLED_TOP_X - 1.27
    oled_pins = [E.OLED_Y0 + E.OLED_W / 2 + (i - 1.5) * 2.54 for i in range(4)]
    o_bot = oled_back - 2.5 - 14.5
    for i, y in enumerate(h1):
        housings.append(box(5.33 - 1.25, 5.33 + 1.25, y - 1.25, y + 1.25, hz, top))
        yo = oled_pins[i]
        housings.append(box(oled_pin_x - 1.25, oled_pin_x + 1.25, yo - 1.25, yo + 1.25, o_bot, oled_back - 2.5))
        xr = 10.0 + 0.9 * i
        xa, yt, zl = 2.0 + 0.9 * i, 73.3 + 0.45 * i, o_bot - 2.0 - 0.9 * i   # อ้อมซ้ายของจอ แล้วลอดใต้จอ
        wires.append([(5.33, y, top), (5.33, y, zr), (xr, y, zr), (xr, yt, zr), (xa, yt, zr), (xa, yo, zr),
                      (xa, yo, zl), (oled_pin_x, yo, zl), (oled_pin_x, yo, o_bot)])
    h2 = [4.32, 6.86, 9.40]
    bz_pin_y = E.BZ_Y0
    bz_pins = [E.BZ_X0 + E.BZ_W / 2 + (i - 1) * 2.54 for i in range(3)]
    bz_z = E.BZ_Z0 + 1.6 + 1.25
    b_end = bz_pin_y - E.BZ_DUP
    for i, x in enumerate(h2):
        housings.append(box(x - 1.25, x + 1.25, 9.02 - 1.25, 9.02 + 1.25, hz, top))
        xb = bz_pins[2 - i]                         # สองขาริมสลับ (VCC↔GND) ตามประเด็นค้าง #1
        housings.append(box(xb - 1.25, xb + 1.25, b_end, bz_pin_y, bz_z - 1.25, bz_z + 1.25))
        xr = 13.6 + 0.9 * i
        yr = 68.0 + 0.9 * i
        wires.append([(x, 9.02, top), (x, 9.02, zr - 1.0), (xr - 3.0, 9.02, zr - 1.0), (xr - 3.0, yr, zr - 1.0),
                      (xb, yr, zr - 1.0), (xb, yr, bz_z), (xb, b_end, bz_z)])
    g["dupont_housing"] = group(*housings)
    g["dupont_wire"] = group(*[seg for path in wires for seg in tube_path(path, 0.65)])
    return g


def tube_path(pts, r):
    """ท่อกลมตามเส้นหักมุม (พิกัดบอร์ด) — ใช้วาดสายไฟ"""
    out = []
    for (x0, y0, z0), (x1, y1, z1) in zip(pts, pts[1:]):
        d = (x1 - x0, -(y1 - y0), z1 - z0)
        L = math.sqrt(sum(c * c for c in d))
        if L < 1e-6:
            continue
        out.append(Solid.make_cylinder(r, L, Plane(origin=(x0, -y0, z0), z_dir=d)))
        out.append(Pos(x1, -y1, z1) * Sphere(r))
    return out


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for name, part in make_groups().items():
        export_stl(part, str(OUT / f"{name}.stl"), tolerance=0.02 if name == "lid" else 0.05,
                   angular_tolerance=0.3)
        print(name, (OUT / f"{name}.stl").stat().st_size // 1024, "KB")
