"""
BrainRing Wireless - Admin: parametric enclosure (build123d), v2.

Coordinate system (same for every exported file):
  origin = centre of the PCB outline, X/Y in the PCB plane (KiCad top view: X right, Y+ = antenna end),
  Z = 0 at the PCB bottom (solder) side, Z+ = component side.
  The user looks at the component side (+Z): WS2812 indicator in the centre of the front.
  The solder side (-Z) lies in the palm: buzzer and the IP5305 battery LEDs look down.
  Seen from the front the button is on the LEFT edge (-X), level with the middle of the remote:
  index finger of the right hand / thumb of the left hand.

Parts:
  Case_BRw_admin  - deep shell with the front face: LED, side button, antenna SMA, slide switch, USB-C
  Cap_BRw_admin   - bottom cover (palm side): screws, buzzer grille, light pipes for the battery LEDs

Run:  python brw_admin_case.py [path/to/board.step]
"""
import sys
from pathlib import Path
import math
from build123d import *

OUT = Path(__file__).resolve().parent.parent / "print" / "v2"

# ---------------------------------------------------------------- parameters
PCB_T = 1.6
PCB_CX, PCB_CY = 206.0, 104.5                    # KiCad coordinates of the board centre

A_L, A_R = 38.3, 38.3        # outer half width (side walls 3.9 mm: room for the finger grooves)
B_TOP, B_BOT = 43.0, 43.0    # outer half height: antenna end / USB end
R_CORNER = 5.0               # outline corner radius
R_TOP, R_BOT = 4.0, 6.0      # edge rounding front / palm side
Z_BOT, Z_TOP = -6.6, 27.0    # outer faces (palm side / front)
Z_SPLIT = -1.0               # Case / Cap parting plane
WALL = 2.0
CAV_X = 34.4                 # cavity half width  (PCB 34.0 + 0.4)
CAV_Y0, CAV_Y1 = -39.9, 40.0 # cavity Y limits   (PCB +-39.5; top wall kept thin for the SMA)

# board holes (local)
HOLES = {"H1": (-30.0, 35.5), "H2": (-30.0, -35.5), "H3": (10.05, 36.0), "H4": (10.05, -23.5)}
# left side: M3 countersunk screws through Cap + PCB into brass heat-set inserts (OD 4.2 x 4) in case bosses
SCREW_HOLES = ("H1", "H2")
BOSS_R, INS_HOLE_D, INS_DEPTH = 4.25, 4.0, 4.8
# right side: H3/H4 sit in the 4.5 mm gap between the nRF module and the battery holder -> slim clamp posts
# (radius up to z, radius above; H3 narrows above the nRF board edge at z 12.7), no screws
SLIM_BOSSES = {"H3": (2.3, 12.0, 2.0), "H4": (2.3, 12.0, 2.3)}

# right side: hook latches. A rigid cap tongue rises into the case wall, its tooth points outwards (+X).
# The cap is hooked in first at ~12 deg and swung shut about the outer seam of the right wall.
LATCHES = ((30.0, 8.0), (3.2, 4.4), (-16.5, 5.5), (-35.5, 6.0))   # (Y, width) between the finger grooves
T_X0, T_X1, T_Z1 = 34.6, 35.8, 6.0                       # tongue: 0.6 mm off the PCB edge, top height
TOOTH, TOOTH_H = 1.0, 1.4
T_BEVEL = (0.45, 3.5)                                    # inner top bevel of the tongue: X, Z
POCKET_CLR = 0.15                                        # -> outer skin over the pocket A_R - T_X1 - TOOTH - POCKET_CLR

# antenna SMA (nRF24L01+PA+LNA), axis along +Y
SMA_X, SMA_Z = 0.0, 19.95
SMA_HOLE_D = 6.8
SMA_SEAT_Y = 41.2            # antenna base face (outer face of the thin wall)
SMA_CBORE_D = 11.5

# side button 20 mm (body D16, flange D19, panel <= 2 mm); body passes over the LGT module (top ~7.2 mm)
BTN_Y, BTN_Z = 22.0, 15.8      # moved towards the antenna end (index finger); H1 boss limits it to ~22.8
BTN_HOLE_D = 17.3
BTN_EAR_W, BTN_EAR_R = 4.2, 9.4  # notches for the snap ears (3.6 wide, R10.06): only ~0.65 mm of snap left
BTN_SPOT_D, BTN_WALL = 24.0, 2.0

# slide switch SS12D11 knob and USB-C opening (bottom end, Y-)
SW_X0, SW_X1, SW_Z0, SW_Z1 = -2.6, 4.9, 2.4, 7.4
SW_SLOT_R = 1.2
SW_DISH, SW_DISH_DEPTH, SW_DISH_Z = (9.5, 4.0, 7.0), 1.6, 5.9   # ellipsoid semi-axes X/Y/Z, depth, centre height
USB_CX, USB_CZ, USB_W, USB_H = -18.0, -1.76, 10.0, 4.4   # receptacle 8.94 x 3.2

# WS2812B indicator (round PCB) in the centre of the front
LED_D, LED_H = 10.0, 2.2     # module diameter / height (PCB 1.0 + 5050 LED), Elements_admin/WS2812b_1_round_pcb.STEP
LED_WIN, LED_ROOF = 5.0, 0.6   # square window: only the 5050 package (4.8 x 4.8) shows
LED_RING_D = 15.0

# IP5305 battery LEDs D2..D5 (solder side) -> light pipes through the bottom cover
LP_X, LP_YS = -30.0, (-28.5, -23.5, -18.5, -13.5)
LP_HOLE, LP_OD, LP_TOP = 2.0, 3.6, -1.45
BUZZER = (22.0, -3.5)

# finger grooves: +X side, and -X side below the button (the button is the index-finger spot there)
GRIP_YS_R, GRIP_YS_L, GRIP_R, GRIP_DEPTH = (-26.0, -7.0, 12.0), (-28.0, -10.0), 11.0, 2.0

# front artwork (variant A4 + frame): multi-colour inlay, flush with the front, printed face down in the first layers.
# AMS-friendly: no thin details, every colour in large areas, letters of the top plate are cut out (= white case)
ART_DEPTH = 0.6                         # 3 layers x 0.2
FONT_DIR = "C:/Windows/Fonts/"
TITLE_FONT, SUB_FONT = "impact.ttf", "ariblk.ttf"
FRAME_INSET, FRAME_W = 1.5, 2.0         # frame inside the flat part of the front (the R_TOP rounding is left white)

CLR = 0.15
UP = (Align.CENTER, Align.CENTER, Align.MIN)


def rrect_prism(x0, x1, y0, y1, r, z0, z1):
    return extrude(Pos((x0 + x1) / 2, (y0 + y1) / 2, z0) * RectangleRounded(x1 - x0, y1 - y0, r), amount=z1 - z0)


def fillet_z(part, z, r):
    edges = [e for e in part.edges() if abs(e.center().Z - z) < 1e-3 and abs(e.tangent_at(0).Z) < 1e-3]
    return fillet(edges, r)


# ---------------------------------------------------------------- shell
outer = rrect_prism(-A_L, A_R, -B_BOT, B_TOP, R_CORNER, Z_BOT, Z_TOP)
outer = fillet_z(outer, Z_TOP, R_TOP)
outer = fillet_z(outer, Z_BOT, R_BOT)

cav_z0, cav_z1 = Z_BOT + WALL, Z_TOP - WALL
cavity = rrect_prism(-CAV_X, CAV_X, CAV_Y0, CAV_Y1, 1.0, cav_z0, cav_z1)
cavity = fillet_z(cavity, cav_z1, 1.5)
cavity = fillet_z(cavity, cav_z0, 3.0)

shell = outer - cavity
for y in GRIP_YS_R:
    shell -= Pos(A_R + GRIP_R - GRIP_DEPTH, y, 0) * Cylinder(GRIP_R, 100)
for y in GRIP_YS_L:
    shell -= Pos(-A_L - GRIP_R + GRIP_DEPTH, y, 0) * Cylinder(GRIP_R, 100)

case = shell & (Pos(0, 0, Z_SPLIT + 50) * Box(200, 200, 100))
cap = shell & (Pos(0, 0, Z_SPLIT - 50) * Box(200, 200, 100))

# ---------------------------------------------------------------- CASE features
for name in SCREW_HOLES:
    x, y = HOLES[name]
    case += (Pos(x, y, PCB_T) * Cylinder(BOSS_R, cav_z1 + 0.5 - PCB_T, align=UP)) & outer
    case -= Pos(x, y, PCB_T) * Cylinder(INS_HOLE_D / 2, INS_DEPTH, align=UP)       # heat-set insert
    case -= Pos(x, y, PCB_T) * Cylinder(1.7, INS_DEPTH + 2.5, align=UP)             # screw tip run-out
for name, (r0, z_step, r1) in SLIM_BOSSES.items():
    x, y = HOLES[name]
    case += Pos(x, y, PCB_T) * Cylinder(r0, z_step - PCB_T, align=UP)
    case += (Pos(x, y, z_step) * Cylinder(r1, cav_z1 + 0.5 - z_step, align=UP)) & outer

# latch slots (open to the cavity) + pockets = tooth swept along the closing arc
LATCH_PIVOT = (A_R, Z_SPLIT)
for y, w in LATCHES:
    case -= Pos((CAV_X - 1 + T_X1 + 0.2) / 2, y, (Z_SPLIT - 1 + T_Z1 + 0.3) / 2) * Box(T_X1 + 0.2 - CAV_X + 1, w + 0.4, T_Z1 + 0.3 - Z_SPLIT + 1)
    px0, px1 = T_X1 - 0.2, T_X1 + TOOTH + POCKET_CLR
    pocket = Pos((px0 + px1) / 2, y, T_Z1 + 0.2 - (TOOTH_H + 0.4) / 2) * Box(px1 - px0, w + 0.4, TOOTH_H + 0.4)
    for a in range(0, 20, 2):
        case -= Pos(LATCH_PIVOT[0], 0, LATCH_PIVOT[1]) * Rot(0, -a, 0) * Pos(-LATCH_PIVOT[0], 0, -LATCH_PIVOT[1]) * pocket

# ribs just above the battery (keep the cell and the right half of the board in place)
for y in (-20.0, 20.0):
    case += Pos(23.0, y, (23.3 + cav_z1 + 0.5) / 2) * Box(18.0, 1.5, cav_z1 + 0.5 - 23.3)
# post above the nRF module (stops it from walking off its header)
case += Pos(0, 18.0, 16.0) * Cylinder(2.0, cav_z1 + 0.5 - 16.0, align=UP)

# WS2812 indicator: ring boss under the front, pocket for the round module, window
led_back = Z_TOP - LED_ROOF - LED_H
case += Pos(0, 0, led_back) * Cylinder(LED_RING_D / 2, cav_z1 + 0.5 - led_back, align=UP)
case -= Pos(0, 0, led_back - 1) * Cylinder(LED_D / 2 + 0.2, LED_H + 1, align=UP)
led_win = Pos(0, 0, Z_TOP - 2) * Box(LED_WIN, LED_WIN, 3, align=UP)
led_win += Pos(0, 0, Z_TOP - 0.3) * extrude(Rectangle(LED_WIN, LED_WIN), amount=0.31, taper=-45)   # 0.3 mm chamfer
case -= led_win
case -= Pos(-LED_RING_D / 4 - 1, -2.0, led_back + 0.6) * Box(LED_RING_D / 2, 3.0, 1.4)   # wire exit towards J1

# antenna SMA through the top wall (slotted towards the board for easy fitting) + seat for the antenna base
case -= Pos(SMA_X, CAV_Y1 - 1, SMA_Z - 0.75) * (Rot(-90, 0, 0) * extrude(SlotCenterToCenter(1.5, SMA_HOLE_D, rotation=90), amount=12))
case -= Pos(SMA_X, SMA_SEAT_Y, SMA_Z) * (Rot(-90, 0, 0) * Cylinder(SMA_CBORE_D / 2, 10, align=UP))

# side button: hole + flat spot-face (finger locator, leaves a 2 mm panel for the clips)
to_minus_x = Rot(0, -90, 0)
case -= Pos(-CAV_X + 1, BTN_Y, BTN_Z) * (to_minus_x * Cylinder(BTN_HOLE_D / 2, 10, align=UP))
case -= Pos(-CAV_X - BTN_WALL, BTN_Y, BTN_Z) * (to_minus_x * Cylinder(BTN_SPOT_D / 2, 10, align=UP))
case -= Pos(-CAV_X - BTN_WALL / 2, BTN_Y, BTN_Z) * Box(BTN_WALL + 1, 2 * BTN_EAR_R, BTN_EAR_W)   # ear notches along Y

# slide switch: slot open to the parting plane (the cap closes it below the knob), rounded top corners
sw_cx = (SW_X0 + SW_X1) / 2
sw_z0 = Z_SPLIT - 1 - 2 * SW_SLOT_R      # lower rounded corners end up below the case, only the top ones remain
sw_slot = extrude(Plane.XZ * Pos(0, (sw_z0 + SW_Z1) / 2) * RectangleRounded(SW_X1 - SW_X0, SW_Z1 - sw_z0, SW_SLOT_R), amount=B_BOT + 2)
case -= Pos(sw_cx, CAV_Y0 + 0.5, 0) * sw_slot

# finger dish around the switch knob (the knob tip is only 0.6 mm proud of the 3 mm wall):
# shallow ellipsoid, fades out above the parting plane -> no thin edges at the joint
sw_recess = Pos(sw_cx, -B_BOT - SW_DISH[1] + SW_DISH_DEPTH, SW_DISH_Z) * scale(Sphere(1.0), by=SW_DISH)
case -= sw_recess

# USB-C opening (rounded, in both halves)
usb_cut = Pos(USB_CX, CAV_Y0 + 2.0, USB_CZ) * (Rot(90, 0, 0) * extrude(RectangleRounded(USB_W, USB_H, USB_H / 2 - 0.01), amount=12))
case -= usb_cut

# ---------------------------------------------------------------- front artwork
def art(face):
    return extrude(Pos(0, 0, Z_TOP) * face, amount=-ART_DEPTH)


def text_at(txt, size, font, x, y):  # centred on its real outline
    t = Text(txt, size, font_path=FONT_DIR + font)
    c = t.bounding_box().center()
    return Pos(x - c.X, y - c.Y) * t


front = RectangleRounded(A_L + A_R, B_BOT + B_TOP, R_CORNER)
for y in GRIP_YS_R:
    front -= Pos(A_R + GRIP_R - GRIP_DEPTH, y) * Circle(GRIP_R)
for y in GRIP_YS_L:
    front -= Pos(-A_L - GRIP_R + GRIP_DEPTH, y) * Circle(GRIP_R)
flat = offset(front, -R_TOP)
frame = offset(flat, -FRAME_INSET) - offset(flat, -FRAME_INSET - FRAME_W)

STAR_N, STAR_RO, STAR_RI, STAR_EDGE, STAR_CORE = 24, 15.0, 10.5, 1.6, 5.0
star = Polygon(*[((STAR_RO if i % 2 == 0 else STAR_RI) * math.cos(2 * math.pi * i / STAR_N + 0.1),
                  (STAR_RO if i % 2 == 0 else STAR_RI) * math.sin(2 * math.pi * i / STAR_N + 0.1)) for i in range(STAR_N)])
core = Circle(STAR_CORE)
title = Pos(0, 26.5) * RectangleRounded(56.0, 13.0, 3.0)        # blue plate, "BRAIN RING" cut out
badge = Pos(0, -29.0) * RectangleRounded(40.0, 9.0, 3.0)        # black plate, yellow "ADMIN"
badge_txt = art(text_at("ADMIN", 6.0, SUB_FONT, 0, -29.0))

blue = art(title) - art(text_at("BRAIN RING", 10.0, TITLE_FONT, 0, 26.5))
yellow = art(star) - art(core) + badge_txt
black = art(frame) + art(offset(star, STAR_EDGE, kind=Kind.ARC)) - art(star) + art(core) + art(badge) - badge_txt
art_parts = {"blue": blue - led_win, "yellow": yellow - led_win, "black": black - led_win}
for part in art_parts.values():
    case -= part

# ---------------------------------------------------------------- CAP features
for name, (x, y) in HOLES.items():
    cap += (Pos(x, y, cav_z0 - 0.5) * Cylinder(3.0, -cav_z0 + 0.5, align=UP)) & outer
for name in SCREW_HOLES:
    x, y = HOLES[name]
    cap -= Pos(x, y, Z_BOT - 1) * Cylinder(1.7, 20, align=UP)
    cap -= Pos(x, y, Z_BOT) * Cone(3.1, 1.7, 1.4, align=UP)              # 90 deg countersink, M3 flat head
    cap -= Pos(x, y, Z_BOT - 1) * Cylinder(3.1, 1.0, align=UP)
for name in SLIM_BOSSES:  # pins that locate the board in its H3/H4 holes (2.2 mm)
    x, y = HOLES[name]
    cap += Pos(x, y, 0) * Cylinder(0.95, PCB_T - 0.2, align=UP)

for y in LP_YS:  # light pipes for the charge / battery-level LEDs
    cap += Pos(LP_X, y, cav_z0 - 0.5) * Cylinder(LP_OD / 2, LP_TOP - cav_z0 + 0.5, align=UP)
    cap -= Pos(LP_X, y, Z_BOT - 1) * Cylinder(LP_HOLE / 2, 20, align=UP)

bx, by = BUZZER  # buzzer grille
for dx, dy in [(0, 0)] + [(3.2 * c, 3.2 * s) for c, s in ((1, 0), (0.5, 0.866), (-0.5, 0.866), (-1, 0), (-0.5, -0.866), (0.5, -0.866))]:
    cap -= Pos(bx + dx, by + dy, Z_BOT - 1) * Cylinder(0.9, WALL + 2, align=UP)

# alignment lip that slips inside the case wall (stays below the PCB)
lip = rrect_prism(-CAV_X - 0.5, CAV_X + 0.5, CAV_Y0 - 0.5, CAV_Y1 + 0.5, 1.0, Z_SPLIT - 1.5, Z_SPLIT)
lip += rrect_prism(-CAV_X + CLR, CAV_X - CLR, CAV_Y0 + CLR, CAV_Y1 - CLR, 1.0, Z_SPLIT - 0.1, Z_SPLIT + 0.9)
lip -= rrect_prism(-CAV_X + CLR + 1.2, CAV_X - CLR - 1.2, CAV_Y0 + CLR + 1.2, CAV_Y1 - CLR - 1.2, 0.5, Z_SPLIT - 2, Z_SPLIT + 2)
lip -= Pos(USB_CX, CAV_Y0, Z_SPLIT) * Box(USB_W + 3, 6, 6)
lip -= Pos(1.0, CAV_Y0, Z_SPLIT) * Box(17.0, 6, 6)  # slide switch + its THT mounting legs
cap += lip

# tab that closes the switch slot below the knob
tab_y0, tab_y1 = -B_BOT - 0.5, CAV_Y0 - 0.2          # flush with the outer wall (trimmed by `outer`)
cap += Pos(sw_cx, (tab_y0 + tab_y1) / 2, (Z_SPLIT - 0.5 + SW_Z0 - CLR) / 2) * Box(SW_X1 - SW_X0 - 2 * CLR, tab_y1 - tab_y0, SW_Z0 - CLR - Z_SPLIT + 0.5)
cap = cap & outer

for y, w in LATCHES:  # tongues are added after the trim: they live inside the case wall
    # profile in XZ; the inner top corner is bevelled -> more swing before the tongue meets the battery holder (x 33.4)
    prof = Polygon((T_X0, Z_SPLIT - 0.5), (T_X1, Z_SPLIT - 0.5), (T_X1, T_Z1), (T_X0 + T_BEVEL[0], T_Z1),
                   (T_X0, T_Z1 - T_BEVEL[1]), align=None)
    tongue = Pos(0, y, 0) * extrude(Plane.XZ * prof, amount=w / 2, both=True)
    tooth = Pos(T_X1 + TOOTH / 2, y, T_Z1 - TOOTH_H / 2) * Box(TOOTH, w, TOOTH_H)
    tooth -= Pos(T_X1 + TOOTH, y, T_Z1) * (Rot(0, 45, 0) * Box(0.8, w + 1, 0.8))       # lead-in chamfer
    cap += tongue + tooth
cap -= sw_recess
cap -= usb_cut


# ---------------------------------------------------------------- export
def main():
    OUT.mkdir(parents=True, exist_ok=True)
    case.label, cap.label = "Case_BRw_admin", "Cap_BRw_admin"
    for part in (case, cap):
        export_step(part, str(OUT / f"{part.label}.step"))
        export_stl(part, str(OUT / f"{part.label}.stl"), tolerance=0.02, angular_tolerance=0.15)
        print(part.label, "volume %.1f cm3" % (part.volume / 1000), "bbox", part.bounding_box().size)

    # colour inlays of the front: load together with Case_BRw_admin.stl as ONE object with several parts
    for colour, part in art_parts.items():
        part.label = f"Case_art_{colour}"
        export_stl(part, str(OUT / f"Case_BRw_admin_art_{colour}.stl"), tolerance=0.01, angular_tolerance=0.1)
        print(part.label, "volume %.3f cm3" % (part.volume / 1000))
    export_step(Compound([case] + list(art_parts.values()), label="Case_BRw_admin_color"), str(OUT / "Case_BRw_admin_color.step"))

    if len(sys.argv) > 1:  # assembly with the KiCad board for fit checks / SolidWorks
        pcb = Pos(-PCB_CX, PCB_CY, 0) * import_step(sys.argv[1])
        pcb.label = "PCB_BRw_admin"
        parts = [case, cap, pcb]
        elements = Path(__file__).resolve().parent.parent / "Elements_admin"
        if (elements / "Push_Button_20mm.STEP").exists():  # flange seated on the spot-face, clips along Y
            btn = Pos(-CAV_X - BTN_WALL, BTN_Y, BTN_Z) * (Rot(0, -90, 0) * import_step(str(elements / "Push_Button_20mm.STEP")))
            btn.label = "Push_Button_20mm"
            parts.append(btn)
        if (elements / "WS2812b_1_round_pcb.STEP").exists():  # LED face against the window roof
            led = Pos(0, 0, Z_TOP - LED_ROOF - LED_H) * import_step(str(elements / "WS2812b_1_round_pcb.STEP"))
            led.label = "WS2812b_1_round_pcb"
            parts.append(led)
        export_step(Compound(parts, label="BRw_admin_assembly"), str(OUT / "BRw_admin_assembly.step"))


if __name__ == "__main__":
    main()
