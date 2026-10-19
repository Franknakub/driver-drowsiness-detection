"""กล่อง Driver Drowsiness Detection (PCB v2) — build123d

รัน:  <venv>/Scripts/python.exe enclosure.py      → out/*.step, out/*.stl

พิกัดในสคริปต์ = พิกัดบอร์ด (ดู ../DESIGN_INPUTS.md):
  x = มม. จากขอบซ้าย PCB, y = มม. จากขอบบน (ลูกศร MODULE USB ชี้ขึ้น), z = มม. จากใต้พื้นกล่อง
  ใน CAD แกน Y กลับทิศ (CAD Y = −y) เพื่อให้ขอบบนบอร์ดอยู่บน
ป้ายที่มาของค่า: MEAS = ผู้ใช้วัดจริง | DESIGN = Gerber/PnP v2 | ASSUMED = สมมติ ต้องวัด/ยืนยันก่อนสั่งปริ้นตัวจริง
"""
import math
from pathlib import Path
from build123d import (Align, Axis, Box, Cone, Cylinder, Plane, Pos, RegularPolygon, Rot, Text, export_step,
                       export_stl, extrude)

OUT = Path(__file__).parent / "out"

# ค่า MEAS ทั้งหมดจาก db ตารางวัดขนาด plan/measure v466 (กรอกล่าสุด 17 ต.ค. 2026) — ผลหลังคำนวณ (r)
# ---------------------------------------------------------------- PCB (DESIGN)
PCB_W, PCB_H = 58.55, 73.03
PCB_T = 1.6                      # MEAS pcb_t (กรอกใหม่ จากเดิม 1.69)
BOT_H = 2.69                     # MEAS bot_h ขาใต้บอร์ดยาวสุด
MH = [(4.0, 4.0), (54.55, 4.0), (4.0, 69.02), (54.55, 69.02)]   # DESIGN NPTH D3.2

IR = [(53.76, y) for y in (12.51, 26.51, 40.51, 54.51)]          # DESIGN
IR_D = 5.8                       # MEAS ir_d (ปีกโคน)
IR_H = 9.05                      # MEAS ir_h1–4
LDR = (5.59, 31.37)              # DESIGN
LDR_H = 2.1                      # MEAS ldr_h
LEDS = [(41.15, 58.04), (46.48, 57.92)]                          # DESIGN LED1, LED2
# LED1 อยู่ใต้มุมโมดูล (ปลายเสาอากาศ x ≤ 41.45, y ≤ 58.22) → แท่ง Ø3 เอียงไปทาง +y (ขอบล่างบอร์ด) 15°
# เอียงทาง +y อย่างเดียว เพื่อไม่เข้าใกล้ LED2 · ผู้ใช้เลือก 18 ต.ค.
LED_TILT = [15.0, 0.0]           # องศาจากแนวตั้ง
LED_ROD_Z0 = 0.5                 # ปลายล่างแท่งเหนือผิว PCB
USB1 = (3.68, 17.40)             # DESIGN
USB1_OVER = 0.95                 # MEAS usb1_over ยื่นเลยขอบซ้าย
USB1_H = 3.2                     # MEAS usb1_h
SW1 = (7.49, 62.49)              # DESIGN ก้านเลื่อนแนว x
SW_H = 8.3                       # MEAS sw_h ปลายก้าน
SW_TRAVEL = 3.76                 # MEAS sw_travel
SW_BODY = (8.55, 3.85, 4.3)      # MEAS sw_bw (แนวเลื่อน x) · sw_bl · sw_bh
SW_LEVER = (1.5, 1.5)            # MEAS sw_lw (แนวเลื่อน x) × sw_lt
SW_SOCKET_FIT = 0.3              # เผื่อรูสวมก้าน (แน่นกว่า FIT ทั่วไป)
SW_FLANGE_W, SW_FLANGE_T, SW_FLANGE_GAP = 5.4, 1.2, 0.4   # ปีกขังด้ามต่อใต้ฝา: กว้าง (แนว y) · หนา · ห่างผิวในฝา

# ---------------------------------------------------------------- โมดูล + กล้อง
HDR_H = 8.51                     # MEAS hdr_h (กรอกใหม่ จากเดิม 11.7)
MOD_W, MOD_L = 28.25, 57.0       # MEAS
MOD_LEFT = 13.2                  # MEAS mod_left (แบบกลาง header ให้ 14.20 — ใช้ค่าวัด)
MOD_TOP_Y = 0.65                 # MEAS mod_topedge ปลาย USB อยู่ในขอบบน PCB 0.65
MOD_BOT_Y = 58.22                # MEAS mod_bot ปลายเสาอากาศ (ยาวรวม 57.57 vs mod_all 57 ต่าง 0.57)
ST_MOD = 12.68                   # MEAS ผิวบนแผ่นโมดูล
ST_CAN = 15.85                   # MEAS ฝาโลหะ ESP32 (= ST_MOD + mod_top 3.27 ต่าง 0.1 ลงตัว)
ST_LENS = 20.77                  # MEAS ยอดเลนส์ (= ST_CAN + เลนส์เหนือฝา 4.95 ลงตัว) → กำหนดความสูงฝา
LENS = (28.79, 40.11)            # MEAS lens_x, lens_y
LENS_D = 7.1                     # MEAS
CAM_FOV = 120.0                  # ตกลงไว้แทนการวัดขั้น 8
USBM_X = [MOD_LEFT + 8.93, MOD_LEFT + 20.3]   # MEAS usbm2_x (UART) / usbm_x (OTG) จากขอบซ้ายโมดูล
USBM_HH = 2.1                    # MEAS ขอบบนพอร์ตเหนือผิวโมดูล (พอร์ตอยู่ด้านกล้อง)
BTN = [(MOD_LEFT + 6.07, MOD_TOP_Y + 12.78),  # BOOT  MEAS
       (MOD_LEFT + 23.24, MOD_TOP_Y + 12.78)] # RST
MOD_TOP = 3.27                   # MEAS mod_top ชิ้นสูงสุดบนโมดูล (ไม่นับกล้อง) — ปลอกปุ่มจบเหนือค่านี้

# ---------------------------------------------------------------- จอ OLED / buzzer / สาย
OLED_W, OLED_H, OLED_T = 28.9, 27.46, 3.21     # MEAS
OLED_PCB_T = 1.6                 # MEAS oled_pt
OLED_RECESS = 0.8                # ร่นจอถอยจากผิวในฝา ให้สกรูเกลียวปล่อย M2×5 เหลือเนื้อหน้าฝา 0.8 (ผู้ใช้เลือก 18 ต.ค.)
M2_SCREW_L = 5.0                 # HSHTS-M2x5 (ยาวไม่รวมหัว)
GLASS_W, GLASS_H = 26.6, 19.2    # MEAS
GLASS_DX, GLASS_DY = 0.72, 4.8   # MEAS glass_dxr (แปลงจากขอบขวา) / glass_dy (ขอบบน = ฝั่งขา)
OLED_HOLE_D = 2.1                # MEAS
OLED_CX, OLED_CY = 23.04, 23.59  # MEAS | ASSUMED ว่ารูอยู่กึ่งกลางแผ่น
VIEW_W, VIEW_H = 21.74, 10.86    # SPEC พื้นที่พิกเซลจอ 0.96" (ใช้แค่ตรวจว่าหน้าต่างครอบ)
VIEW_DX, VIEW_DY = 0.8, 3.9      # MEAS view_dx / view_dy จากขอบซ้าย / บนกระจก — dy ขัดกับรูปถ่าย (~1.1) ยังไม่ชัวร์
# หน้าต่างจอ: ครอบทั้งแผ่นดำของจอ เพื่อให้เห็นทุกพิกเซลไม่ว่า view_dy จะเป็นค่าไหน
WIN_LIP = 0.5                    # ขอบฝาทับกระจกด้านซ้าย/ขวา/บน
WIN_H = 15.5                     # สูงจากขอบบนกระจก — หยุดก่อนแถบสายแพดำด้านล่างกระจก (รูปถ่าย ~3.5 มม.)
OLED_HOLE_X = (1.2 + 4.55) / 2   # MEAS oled_hx (ขอบรูซ้าย/ขวา) = 2.875 จากขอบซ้ายแผ่น
OLED_HOLE_Y = (0.8 + 2.95) / 2    # MEAS oled_hy (ขอบรูบน/ล่าง วัดใหม่ 18 ต.ค.) = 1.875 จากขอบบนแผ่น · สูงรู 2.15 ≈ Ø2.1 ลงตัว
BZ_W, BZ_L, BZ_HGT = 12.95, 32.0, 11.0          # MEAS
BZ_HOLE = (6.36, 15.58)          # MEAS จากขอบซ้าย / ขอบฝั่งขา
DUP_L = 18.05                    # MEAS dup_l หัว dupont + จุดเริ่มงอ สูงจากผิว PCB ที่ H1
BZ_DUP = 17.45                   # MEAS bz_dup ขอบแผ่น buzzer ถึงปลายหัว dupont
BZ_PIN_ROOM = BZ_DUP + 3.0       # + ที่ให้สายงอ
USBC_PLUG = (11.0, 6.52)         # MEAS usbc_w × usbc_t สายไฟเข้า
USBM_PLUG = (11.0, 6.52)         # MEAS usbm_plug สายแฟลช
USB_TAB_GAP = 0.8                # หูรัดสายอยู่ใต้หัว USB-C ห่าง 0.8 (หัวสายเสียบได้ไม่ครูด)

# ---------------------------------------------------------------- กล่อง
WALL = 2.0
LID_T = 2.0
FLOOR = 2.0
GAP = 0.5                        # ช่องว่างบอร์ด–ผนัง
FIT = 0.45                       # เผื่อสวม 0.4–0.5 (หน้าเว็บ: ผู้ใช้เลือกสั่งตัวจริงเลย ไม่ปริ้นทดลอง)
STANDOFF = BOT_H + 1.4           # ใต้ PCB ถึงพื้นใน
Z_PCB_BOT = FLOOR + STANDOFF
Z_PCB = Z_PCB_BOT + PCB_T        # ผิวบน PCB
Z_LENS = Z_PCB + ST_LENS
Z_LID_IN = Z_LENS + 0.3          # เลนส์ต่ำกว่าผิวในฝา 0.3 (ตัวกล้องไม่ชนฝา)
Z_TOP = Z_LID_IN + LID_T
Z_MOD = Z_PCB + ST_MOD           # ผิวบนแผ่นโมดูล

LED_GUIDE_R = 2.8
BZ_Z0 = Z_PCB + 1.0                              # ยกแผ่น buzzer ให้หัว dupont ข้ามเหนือขอบ PCB
_tilt_dy = lambda i, z: max(0.0, z - (Z_PCB + LED_ROD_Z0)) * math.tan(math.radians(LED_TILT[i]))
BZ_Y0_MIN = max(y + _tilt_dy(i, BZ_Z0 + 3.0) for i, (_, y) in enumerate(LEDS)) + LED_GUIDE_R / math.cos(
    math.radians(max(LED_TILT))) + BZ_PIN_ROOM + 0.5   # หัว dupont ของ buzzer ต้องไม่ชนท่อ LED
ZONE_B0 = PCB_H + 1.0
ZONE_B = (ZONE_B0, max(ZONE_B0 + max(OLED_H, BZ_L) + 4.0, BZ_Y0_MIN + BZ_L + 1.0))   # ส่วนต่อขยาย y
IX0, IX1 = -USB1_OVER - GAP - 0.05, PCB_W + GAP                   # ขอบใน x
IY0, IY1 = min(0.0, MOD_TOP_Y) - GAP, ZONE_B[1]                   # ขอบใน y
OX0, OX1, OY0, OY1 = IX0 - WALL, IX1 + WALL, IY0 - WALL, IY1 + WALL
CORNER_R = 3.0

LABEL_X, LABEL_Y = 45.0, 24.0                    # กึ่งกลางป้ายสลัก "SW1 OFF before USB" (อ่านแนวนอนเมื่อ IR อยู่บน)
# จอ OLED หมุน 90° (ผู้ใช้เลือก 18 ต.ค.): ตอนติดตั้งแนวนอน (IR บน) ขาจออยู่บน ภาพแนวนอน 128×64 ปกติ
# พิกัดในจอ: u = ซ้าย→ขวาตามที่อ่าน (ด้านกว้าง 28.9), v = จากขอบบน (ฝั่งขา) ลงล่าง
# บอร์ด: ขอบบนจอ (v = 0) อยู่ที่ x = OLED_TOP_X หันไปฝั่ง IR · u เพิ่มตาม +y (ขวามือคนขับ)
OLED_TOP_X, OLED_Y0 = 33.0, ZONE_B[0] + 1.5


def oled_pt(u, v):
    return OLED_TOP_X - v, OLED_Y0 + u


def oled_box(u0, u1, v0, v1, z0, z1):
    return box(OLED_TOP_X - v1, OLED_TOP_X - v0, OLED_Y0 + u0, OLED_Y0 + u1, z0, z1)

BZ_X0, BZ_Y0 = 38.0, max(ZONE_B[0] + 3.0, BZ_Y0_MIN)           # แผ่น buzzer แนวตั้ง ขาหันเข้าหาบอร์ด
ZB_POSTS = [(IX0 + 3.5, IY1 - 3.5), (IX1 - 3.5, IY1 - 3.5)]
# ขาดูดกระจก (ผู้ใช้ 18 ต.ค.): เกลียวตัวผู้ 1/4"-20 (MEAS Ø 6.3–6.4) บนแป้นกลม Ø30.36 (MEAS) · หลังกล่องหันกระจก
# เกลียวยื่นพ้นหน้าแป้น 10.16 (MEAS) — ต้องอยู่ในช่วงที่ report() พิมพ์ (5.8–11.5)
# จุดขัน: กลางแนวยาวโดยประมาณ เยื้องไปฝั่ง IR — ห่างขอบโมดูล/เสาอากาศ ~5.5 มม. แนวราบ และต่ำกว่า ~20 มม.
# y 47.5 = กึ่งกลางระหว่างขา IR3/IR4 ใต้บอร์ด · x 48 = ก้านเกลียวพ้นขา header U5 (x 43.56) ใต้บอร์ด
MOUNT_14 = (48.0, 47.5)
MOUNT_PAD_T = 6.0                # หลังกล่องหนาเพิ่มทั้งแผ่น (ไม่ทำเป็นแป้นนูน เพราะแป้นนูนต้องใช้ support ตอนปริ้น)
                                 # → น็อตจมอยู่ใต้ผิวพื้นในทั้งตัว ไม่ชนขา THT ใต้บอร์ด · หลังเรียบ แป้น Ø30.36 วางได้เต็มหน้า
MOUNT_SKIN = 2.0                 # เนื้อระหว่างผิวหน้าแป้นกับน็อต (รับแรงดึงตอนล็อกแหวน)

# heat-set insert ที่ผู้ใช้จะซื้อ (18 ต.ค.): formflex M3 × 5.7 · D1 4.6 · ตารางผู้ผลิต: รูเจาะ 4.0, ลึกอย่างน้อย L + 1, ผนังอย่างน้อย 1.6
M3_CLR, M3_HEAD, M3_INSERT = 3.4, 6.2, 4.0
INSERT_DEPTH = 5.7 + 1.0
INSERT_POST_R = M3_INSERT / 2 + 1.6               # ผนังรอบ insert 1.6 (เสา MH1 ชิด header H2 เหลือ ~0.15)
ZB_POST_R = 3.8
M2_PILOT = 1.6
NUT14_AF, NUT14_T, BOLT14_D = 11.11, 5.56, 6.6  # 1/4"-20 hex nut 7/16" AF


# ---------------------------------------------------------------- helpers
def box(x0, x1, y0, y1, z0, z1):
    """กล่องจากช่วงพิกัดบอร์ด (y ลงล่าง)"""
    return Pos(x0, -y1, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=(Align.MIN,) * 3)


def cyl(x, y, r, z0, z1):
    return Pos(x, -y, z0) * Cylinder(r, z1 - z0, align=(Align.CENTER, Align.CENTER, Align.MIN))


def cone(x, y, r0, r1, z0, z1):
    return Pos(x, -y, z0) * Cone(r0, r1, z1 - z0, align=(Align.CENTER, Align.CENTER, Align.MIN))


def led_rod(i, r, z0, z1):
    """ทรงกระบอกตามแนวแท่งนำแสง LED i (เอียงไปทาง +y บอร์ด = −Y CAD) ระหว่างความสูง z0–z1"""
    x, y = LEDS[i]
    t = math.radians(LED_TILT[i])
    za = Z_PCB + LED_ROD_Z0
    y0 = y + (z0 - za) * math.tan(t)
    return Pos(x, -y0, z0) * Rot(math.degrees(t), 0, 0) * Cylinder(
        r, (z1 - z0) / math.cos(t), align=(Align.CENTER, Align.CENTER, Align.MIN))


def tube(x, y, r_out, r_in, z0, z1):
    return cyl(x, y, r_out, z0, z1) - cyl(x, y, r_in, z0 - 1, z1 + 1)


def outer_block(z0, z1):
    b = box(OX0, OX1, OY0, OY1, z0, z1)
    return b.fillet(CORNER_R, b.edges().filter_by(Axis.Z))


# ---------------------------------------------------------------- ฝาหน้า
def make_lid():
    lid = outer_block(FLOOR, Z_TOP) - box(IX0, IX1, IY0, IY1, FLOOR - 1, Z_LID_IN)

    # เสาหนีบบอร์ดที่ MH1–MH4 (insert M3 จากด้านล่าง)
    for x, y in MH:
        lid += cyl(x, y, INSERT_POST_R, Z_PCB, Z_LID_IN + 0.1)
    # เสาปิดส่วนต่อขยาย ลงถึงพื้น
    for x, y in ZB_POSTS:
        lid += cyl(x, y, ZB_POST_R, FLOOR, Z_LID_IN + 0.1)

    # ท่อกั้นแสง IR (ลงไปเหนือ PCB 1.6 ให้พ้น 0805 ข้างๆ)
    ir_r = (IR_D + 0.4) / 2
    for x, y in IR:
        lid += tube(x, y, ir_r + 2.0, ir_r, Z_PCB + 1.6, Z_LID_IN + 0.1)
    # ท่อ LDR
    lid += tube(*LDR, 3.5, 2.5, Z_PCB + LDR_H + 0.5, Z_LID_IN + 0.1)

    # ท่อนำแท่งอะคริลิก D3 ของ LED — ตัดส่วนที่ชนโมดูลออก
    keepout = box(MOD_LEFT - GAP, MOD_LEFT + MOD_W + GAP, MOD_TOP_Y - GAP, MOD_BOT_Y + GAP,
                  Z_PCB + HDR_H, Z_LID_IN)
    for i in range(len(LEDS)):
        lid += (led_rod(i, LED_GUIDE_R, Z_PCB + 1.0, Z_LID_IN + 0.1)
                - led_rod(i, 1.6, Z_PCB, Z_TOP + 1)) - keepout

    # ปลอกนำคลิปกดปุ่ม BOOT/RST
    for x, y in BTN:
        lid += tube(x, y, 2.5, 1.5, Z_MOD + MOD_TOP + 0.5, Z_LID_IN + 0.1)

    # เสายึดจอ OLED (สกรู M2 จากหลังจอ) — เว้นช่องกระจก
    oled_holes = [oled_pt(OLED_HOLE_X + du, OLED_HOLE_Y + dv) for du in (0, OLED_CX) for dv in (0, OLED_CY)]
    boss_h = OLED_T - OLED_PCB_T + OLED_RECESS
    assert M2_SCREW_L - OLED_PCB_T <= boss_h + 1.2, "สกรูจอยาวเกินรูนำ"
    glass = oled_box(GLASS_DX - FIT, GLASS_DX + GLASS_W + FIT, GLASS_DY - FIT, GLASS_DY + GLASS_H + FIT,
                     Z_LID_IN - boss_h - 0.1, Z_LID_IN + 0.01)
    for x, y in oled_holes:
        lid += cyl(x, y, 1.9, Z_LID_IN - boss_h, Z_LID_IN + 0.1) - glass
    for x, y in oled_holes:
        lid -= cyl(x, y, M2_PILOT / 2, Z_LID_IN - boss_h - 0.1, Z_LID_IN + 1.2)

    # ---- รูทะลุฝา
    # กล้อง: กรวย 120° เริ่มที่ยอดเลนส์
    lr = (LENS_D + 0.6) / 2
    tan = math.tan(math.radians(CAM_FOV / 2))
    lid -= cyl(*LENS, lr, Z_LID_IN - 1, Z_LENS)
    lid -= cone(*LENS, lr, lr + (Z_TOP + 0.1 - Z_LENS) * tan, Z_LENS, Z_TOP + 0.1)
    for x, y in IR:
        lid -= cyl(x, y, ir_r, Z_LID_IN - 1, Z_TOP + 1)
    lid -= cyl(*LDR, 2.5, Z_LID_IN - 1, Z_TOP + 1)
    lid -= cone(*LDR, 2.5, 4.0, Z_TOP - 1.5, Z_TOP + 0.01)
    for i in range(len(LEDS)):
        lid -= led_rod(i, 1.6, Z_LID_IN - 1, Z_TOP + 1)
    for x, y in BTN:
        lid -= cyl(x, y, 1.5, Z_LID_IN - 1, Z_TOP + 1)
    # SW1 ร่องเลื่อน (แนว x)
    sw_len = SW_TRAVEL + 3.0 + FIT * 2
    lid -= box(SW1[0] - sw_len / 2, SW1[0] + sw_len / 2, SW1[1] - 1.5 - FIT, SW1[1] + 1.5 + FIT,
               Z_LID_IN - 1, Z_TOP + 1)
    # หน้าต่าง OLED (บาก 45° ออกด้านนอก)
    wu0, wu1 = GLASS_DX + WIN_LIP, GLASS_DX + GLASS_W - WIN_LIP      # หน้าต่างในพิกัดจอ (u, v)
    wv0, wv1 = GLASS_DY + WIN_LIP, GLASS_DY + WIN_H
    for du, dv in ((VIEW_DX, VIEW_DY), (VIEW_DX, 1.1)):     # พื้นที่พิกเซลต้องอยู่ในหน้าต่างทั้งสองกรณี
        pu, pv = GLASS_DX + du, GLASS_DY + dv
        assert wu0 <= pu and pu + VIEW_W <= wu1 and wv0 <= pv and pv + VIEW_H <= wv1
    lid -= oled_box(wu0, wu1, wv0, wv1, Z_LID_IN - 1, Z_TOP + 1)
    for i in range(4):   # บากเป็นขั้น 0.5 มม.
        d = (i + 1) * 0.5
        lid -= oled_box(wu0 - d, wu1 + d, wv0 - d, wv1 + d, Z_LID_IN + d, Z_TOP + 1)
    # รูเสียง buzzer (กริด Ø2 ทั่วครึ่งล่างของแผ่น — ตำแหน่งตัว buzzer ASSUMED)
    for i in range(3):
        for j in range(4):
            lid -= cyl(BZ_X0 + 2.5 + i * 4.0, BZ_Y0 + 14 + j * 4.5, 1.0, Z_LID_IN - 1, Z_TOP + 1)

    # ---- ช่องที่ผนัง (ร่องเปิดด้านล่างตรงรอยต่อ ปริ้นได้ไม่ต้องมี support)
    usbc_w, usbc_t = USBC_PLUG[0] + 2 * FIT, USBC_PLUG[1] + 2 * FIT
    zc = Z_PCB + USB1_H / 2
    lid -= box(OX0 - 1, IX0 + 1, USB1[1] - usbc_w / 2, USB1[1] + usbc_w / 2, FLOOR - 1, zc + usbc_t / 2)
    # USB ของโมดูล (2 พอร์ตชิดกัน รวมเป็นช่องเดียว)
    plug_w, plug_t = USBM_PLUG[0] + 2 * FIT, USBM_PLUG[1] + 2 * FIT
    zu = Z_MOD + USBM_HH / 2
    x0 = min(USBM_X) - plug_w / 2
    x1 = max(USBM_X) + plug_w / 2
    lid -= box(x0, x1, OY0 - 1, IY0 + 1, zu - plug_t / 2, min(zu + plug_t / 2, Z_LID_IN - 0.4))
    # ร่องระบาย ผนังซ้าย (เลี่ยง USB1) และผนังขวาช่วงส่วนต่อขยาย
    for k in range(9):
        y = 36 + k * 4
        lid -= box(OX0 - 1, IX0 + 1, y, y + 1.5, Z_PCB + 4, Z_LID_IN - 3)
    for k in range(7):
        y = ZONE_B[0] + 3 + k * 4
        lid -= box(IX1 - 1, OX1 + 1, y, y + 1.5, FLOOR + 6, Z_LID_IN - 3)

    # insert M3 ในเสา
    for x, y in MH:
        lid -= cyl(x, y, M3_INSERT / 2, Z_PCB - 0.1, Z_PCB + INSERT_DEPTH)
    for x, y in ZB_POSTS:
        lid -= cyl(x, y, M3_INSERT / 2, FLOOR - 0.1, FLOOR + INSERT_DEPTH)

    # ป้ายเตือนข้างช่อง USB โมดูล (สลักลึก 0.6 บนหน้าฝา)
    # ติดตั้งแนวนอน แถว IR อยู่บน (ผู้ใช้เลือก 18 ต.ค.) → ซ้าย→ขวาที่คนขับเห็น = +y บอร์ด, ขึ้น = +x บอร์ด
    # ป้ายวางในแถบว่างระหว่างรู RST/กล้อง กับรู IR · เริ่มจากฝั่งช่อง USB โมดูล (ซ้ายมือคนขับ)
    label = Plane.XY.offset(Z_TOP - 0.6) * Pos(LABEL_X, -LABEL_Y) * Rot(0, 0, -90) * \
        Text("SW1 OFF before USB", 3.2, align=(Align.CENTER, Align.CENTER))
    lid -= extrude(label, 1.0)
    return lid


# ---------------------------------------------------------------- ฐาน
def make_base():
    z_back = -MOUNT_PAD_T
    base = outer_block(z_back, FLOOR)
    # ขอบนำศูนย์ (สวมในผนังฝา)
    lip = box(IX0 + FIT, IX1 - FIT, IY0 + FIT, IY1 - FIT, FLOOR, FLOOR + 3) - \
        box(IX0 + FIT + 1.2, IX1 - FIT - 1.2, IY0 + FIT + 1.2, IY1 - FIT - 1.2, FLOOR - 1, FLOOR + 4)
    lip -= box(IX0 - 1, IX0 + 3, USB1[1] - 7, USB1[1] + 7, FLOOR - 1, FLOOR + 4)   # ช่อง USB1
    for x, y in ZB_POSTS:
        lip -= cyl(x, y, ZB_POST_R + FIT, FLOOR - 1, FLOOR + 4)
    base += lip
    # เสารองบอร์ด
    for x, y in MH:
        base += cyl(x, y, 3.5, FLOOR - 0.1, Z_PCB_BOT)
        base -= cyl(x, y, M3_CLR / 2, z_back - 1, Z_PCB_BOT + 1)
        base -= cyl(x, y, M3_HEAD / 2, z_back - 1, 3.0)  # หัวน็อตจมใต้ฐาน (ที่นั่งหัวน็อต z 3 → M3x10)
    for x, y in ZB_POSTS:
        base -= cyl(x, y, M3_CLR / 2, z_back - 1, FLOOR + 1)
        base -= cyl(x, y, M3_HEAD / 2, z_back - 1, 1.0)
    # เสา buzzer (สกรูเกลียวปล่อย M3)
    bx, by = BZ_X0 + BZ_HOLE[0], BZ_Y0 + BZ_HOLE[1]
    base += cyl(bx, by, 3.5, FLOOR - 0.1, BZ_Z0)
    base -= cyl(bx, by, 1.25, FLOOR + 0.6, BZ_Z0 + 1)
    # น็อต 1/4"-20 ฝัง (ใส่น็อตจากด้านในก่อนวาง PCB · แรงล็อกแหวนดึงน็อตเข้าหาเนื้อ MOUNT_SKIN)
    mx, my = MOUNT_14
    z_face = z_back
    base -= cyl(mx, my, BOLT14_D / 2, z_face - 1, FLOOR + 1)
    hex_r = NUT14_AF / 2 / 0.8660254 + FIT
    z_nut = z_face + MOUNT_SKIN
    base -= extrude(Plane.XY.offset(z_nut) * Pos(mx, -my) * RegularPolygon(hex_r, 6), FLOOR + 0.1 - z_nut)
    # หูรัดสาย USB1 (เคเบิลไทร์) ยื่นออกผนังซ้าย
    # 18 ต.ค. ยกหูขึ้นมาถึงใต้หัวสาย USB-C (เดิมหนา 2 มม. ที่ระดับหลังกล่อง ห่างหัวสาย ~1 ซม.)
    # เป็นก้อนตันจากหลังกล่องขึ้นไป จึงปริ้นได้ไม่ต้องมี support · เหนือ z = FLOOR อยู่นอกผนังฝาพอดี
    tab_top = Z_PCB + USB1_H / 2 - USBC_PLUG[1] / 2 - USB_TAB_GAP
    tab = box(OX0 - 12, OX0 + 1, USB1[1] - 8, USB1[1] + 8, z_back, FLOOR) + \
        box(OX0 - 12, OX0, USB1[1] - 8, USB1[1] + 8, FLOOR - 0.1, tab_top)
    tab -= box(OX0 - 9, OX0 - 7, USB1[1] - 5, USB1[1] + 5, z_back - 1, tab_top + 1)
    base += tab
    return base


# ---------------------------------------------------------------- ด้ามต่อ SW1
def make_sw_ext():
    z0 = Z_PCB + SW_H - 3.0          # สวมก้านลึก 3
    z1 = Z_TOP + 2.5                 # โผล่พ้นฝา 2.5
    c = (Align.CENTER, Align.CENTER, Align.MIN)
    ext = Box(3.0, 3.0, z1 - z0, align=c)
    # รูสวมก้านเผื่อแค่ 0.3 ให้จับก้านแน่น (ผู้ใช้เลือก 18 ต.ค.)
    ext -= Box(SW_LEVER[0] + SW_SOCKET_FIT, SW_LEVER[1] + SW_SOCKET_FIT, 3.0, align=c)
    ext += Pos(0, 0, z1 - z0 - 1.5) * Box(5.0, 3.0, 1.5, align=c)
    # ปีกใต้ฝา กว้างกว่าร่องในแนว y → ด้ามต่อถูกขังระหว่างฝากับก้าน หลุดออกหน้ากล่องไม่ได้
    zf = Z_LID_IN - SW_FLANGE_GAP - SW_FLANGE_T - z0
    ext += Pos(0, 0, zf) * Box(3.0, SW_FLANGE_W, SW_FLANGE_T, align=c)
    return ext


def report():
    print(f"outer  {OX1 - OX0:.1f} x {OY1 - OY0:.1f} x {Z_TOP:.1f} mm  (x | y | z)")
    print(f"z: PCB bottom {Z_PCB_BOT:.2f} | PCB top {Z_PCB:.2f} | module {Z_MOD:.2f} | lens {Z_LENS:.2f} | "
          f"lid inner {Z_LID_IN:.2f} | top {Z_TOP:.2f}")
    print(f"zone B y {ZONE_B[0]:.1f}–{ZONE_B[1]:.1f} | 1/4-20 at {MOUNT_14} | total depth incl. pad "
          f"{Z_TOP + MOUNT_PAD_T:.1f}")
    print(f"mount screw length (from pad face): >= {MOUNT_SKIN + 3 * 1.27:.1f} for 3 threads, "
          f"full nut at {MOUNT_SKIN + NUT14_T:.1f}, max {MOUNT_PAD_T + Z_PCB_BOT - 0.5:.1f} (PCB bottom)")
    print(f"screws: M3x10 x4 (MH) | M3x6 x2 (zone B) | LED rod D3 length ~ " + " / ".join(f"{(Z_TOP - Z_PCB - LED_ROD_Z0) / math.cos(math.radians(t)):.1f}" for t in LED_TILT) + f"")


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    report()
    parts = {"lid": make_lid(), "base": make_base(), "sw1_extender": make_sw_ext()}
    for name, p in parts.items():
        export_step(p, str(OUT / f"{name}.step"))
        export_stl(p, str(OUT / f"{name}.stl"))
        bb = p.bounding_box()
        print(f"{name}: volume {p.volume:.0f} mm3 | bbox {bb.size.X:.1f} x {bb.size.Y:.1f} x {bb.size.Z:.1f}")
