# MoodiMOS Complete edition — Printable parts (personal robot, not Instructable)
# 
# Same electronics and component dimensions as the main CAD: dimensions are read from
# `../MoodiMOS_Torso/MoodiMOS_Torso.py` and `../MoodiMOS_Head/MoodiMOS_Head.py` (no copying).
# Only the design changes:
# 
# Head:
# - Black bezel with flat face (orange stripe at the edge; orange index marks at 12, 3, 6, 9 o’clock and diagonally),
# - Orange drop spot on the plate (light side painted in the bail; spherical back, dome with radius Rp centered on the plate’s rear, truncated flat: rear disc),
# - Lateral casques (head opens),
# - Conical speaker grille, light ring;
# - Screws for the PCB via 4 holes in the dome (M2×30).
# 
# Torso:
# - Main CAD shell + vertical light stripe on each side.
# 
# Arms:
# - Generator `../MoodiMOS_Organic/organic.py` + black articulation rings (slots/guides in ivory pieces, separate rings for rendering).
# 
# Run from project root:
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_parts.py
# Outputs (mm, torso reference: x right, y depth from façade, z up, origin at façade center):
# - `stl/<piece>.stl` → assembled parts (for rendering via `robot_render.py` and assembly),
# - `print_stl/<piece>.stl` → printed parts (z = 0 on the bed).

import ast, math, os, struct, sys
import numpy as np
from manifold3d import CrossSection, JoinType, Manifold

HERE = os.path.dirname(os.path.abspath(__file__))
CAO = os.path.join(HERE, "..")
OUT = os.path.join(HERE, "stl")
OUT_PRINT = os.path.join(HERE, "print_stl")
SEG = 128
from robot_style import TORSO_STYLE, LEG_VISIBLE, SHOE_TOP
TP_STYLE = TORSO_STYLE


def read_assign(path, name):
    tree = ast.parse(open(path, encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == name:
            return ast.literal_eval(node.value)
    raise RuntimeError('{} not found in {}'.format(name, path))


TP = read_assign(os.path.join(CAO, "MoodiMOS_Torso", "MoodiMOS_Torso.py"), "PARAMS")
TP = dict(TP, **TP_STYLE)
HP = {n: v for n, v, _, _ in read_assign(os.path.join(CAO, "MoodiMOS_Head", "MoodiMOS_Head.py"), "PARAMS")}

# ── style (does not affect components) ──
STYLE = {
    "plateExtra": 3,    # Overboard of the plate on the Ø 82 ring: head sphere Ø 92, larger than the torso (spec sheet)
    "domeT": 32,        # rear dome of the head: depth behind the plate (rear disk Ø ≈ 57)
    "headScrewWell": 11, "headScrewWellD": 5,   # thread holes in the dome; screw head on the column (M2×30)
    "earD": 28, "earOut": 0.9, "earRound": 1.5, "earTilt": 22, "earRoot": 30,   # Headphones: flat disc, one-piece with the dome.
    # (ear outlet beyond the sphere, along the axis; ≈ 6 at the edge), clearance, angle
    "earRingW": 1.0, "earRingT": 0.4, "earRingInset": 0.5,   # orange nylon bezel painted with a light guide groove along the edge of the face.
    "earHoleR": 0.4, "earHoleDepth": 0.6, "earHoleTaper": 15, "earHolePitch": 1.6,   # conical grid
    # Bezel (black ring): face plate with an orange border stripe; orange PEINTS (render only, no geometry).
    "idxR": 40.2, "idxW": 1.4, "idxBig": 7.0, "idxSmall": 2.5,   # radius, radial width, lengths (bars / labels)
    # Orange PE (rimR0 → rimR1 = rim edge) painted stripe on the outer face of the bezel; a slight groove
    # (rimGrooveW × rimGrooveT, centered on rimR0) separates the black face from the orange stripe and guides the brush.
    # 
    # Bezel: only the first mm of the lunVis are black and visible; behind, the bezel steps back by lunStep and the orange plate encases it with a collar (jeu lunFit), with a 12 mm deep screen stack depth.
    "lunVis": 6.0, "lunStep": 2.0, "lunFit": 0.1,
    "rimR0": 41.4, "rimR1": 43.0, "rimGrooveW": 0.8, "rimGrooveT": 0.4,
    # LiPo 403040 (V2) housing, molded into the dome: walls along the entire battery height (+ swelling),
    # two retention lugs on the long sides, at the edge of the opening: the battery clips in, no adhesive, removable.
    "lipoWallH": 5.4, "lipoLipIn": 1.2, "lipoLipL": 16.0, "lipoLipT": 1.0,   # wall 5.4 = LiPo 4 + swelling 0.4 + peg 1
    "tabThick": 6.0,    # bail thickness outside the sphere; front face aligned with the plate (lunVis)
    "tabInset": 0.6, "tabGrooveW": 0.8, "tabGrooveT": 0.4,   # Clear orange border, guide groove on the painted PE head bail.
    # Grey medallion (rear plug disk): part of the dome, painted; a slight groove marks the edge.
    "medD": 54, "medGrooveW": 0.8, "medGrooveT": 0.4,
    "ledW": 3, "ledZ0": -22, "ledZ1": 28, "ledU": 15, "ledT": 1.0,   # luminescent stripe on the sides of the torso
}


def seg(r):
    return max(32, int(SEG * min(1.0, r / 20.0 + 0.25)))


# ---
# primitives in the torso reference frame (x, y = depth, z)
# ---

def box(x0, x1, y0, y1, z0, z1):
    if not (x1 > x0 and y1 > y0 and z1 > z0):
        raise ValueError('Empty or inverted dimension box:' + str((x0, x1, y0, y1, z0, z1)))
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl_y(r, x, z, y0, y1, r1=None):
    'Cylinder (or cone if r1) along y-axis, from y0 (radius r) to y1 (radius r1).'
    c = Manifold.cylinder(y1 - y0, r, r if r1 is None else r1, seg(max(r, r1 or 0)))   # Z-axis, from 0 to h
    return c.rotate((-90, 0, 0)).translate((x, y0, z))                                 # z → +y


def cyl_x(r, y, z, x0, x1):
    return Manifold.cylinder(x1 - x0, r, r, seg(r)).rotate((0, 90, 0)).translate((x0, y, z))


def cyl_z(r, x, y, z0, z1):
    return Manifold.cylinder(z1 - z0, r, r, seg(r)).translate((x, y, z0))


def section_goutte(r, sens):
    '**Drip section (circle with radius *r* centered, tip at *r·√2* along axis *b*, direction ±1):**\nThe cylinders (lying flat during printing—front shell at the bottom, vertical print orientation = *y*-depth) are printed unsupported, tip-facing the façade for a protrusion. The tenon holes remain round; the tenon pivots inside them, held by its magnet, with friction in a round hole securing the limb in its chosen pose.'
    n = seg(r)
    a0 = math.atan2(sens, 0.0)                                   # Direction of the tip in the plane (a, b)
    pts = [(r * math.cos(a0 + t), r * math.sin(a0 + t)) for t in np.linspace(math.pi / 4, 7 * math.pi / 4, n)]
    return CrossSection([[(0.0, sens * r * math.sqrt(2))] + pts])


def goutte_x(r, y, z, x0, x1, sens):
    'Cylindrical component along X-axis in drop form: cross-section (Y, Z), tip oriented along Y.'
    m = Manifold.extrude(section_goutte(r, sens), x1 - x0)                # local reference (a, b, h), pointing along b
    M = np.array([[0, 0, 1, x0], [0, 1, 0, y], [-1, 0, 0, z]], dtype=float)  # rotation: x = h, y = b, z = -a
    return m.transform(M)


def goutte_z(r, x, y, z0, z1, sens):
    'Cylindrical component with Z-axis drop-in fit: cross-section (x, y), tip aligned along y.'
    return Manifold.extrude(section_goutte(r, sens), z1 - z0).translate((x, y, z0))


def rrect(w, h, r, cx=0.0, cz=0.0):
    'Circular arc in the (x, z) plane.'
    r = max(min(r, w / 2 - 0.01, h / 2 - 0.01), 0.05)
    pts = []
    for (sx, sz, a0) in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        ccx, ccz = cx + sx * (w / 2 - r), cz + sz * (h / 2 - r)
        for a in np.linspace(math.radians(a0), math.radians(a0 + 90), 12):
            pts.append((ccx + r * math.cos(a), ccz + r * math.sin(a)))
    return CrossSection([pts])


def slab(cs, y0, y1):
    'Extruded section along x, z axis between y₀ and y₁.'
    return Manifold.extrude(cs, y1 - y0).rotate((90, 0, 0)).mirror((0, 1, 0)).translate((0, y0, 0))


def rounded_block(w, h, r, y0, y1, f_front=0.0, f_back=0.0, n=12):
    'Rectangular-section rounded block, chamfered edges f_front / f_back on the front/back perimeter (convex envelope).'
    parts = []
    for f, ys in ((f_front, y0), (f_back, y1)):
        if f <= 0:
            parts.append(slab(rrect(w, h, r), y0 if ys == y0 else y1 - 0.01, y0 + 0.01 if ys == y0 else y1))
            continue
        for t in np.linspace(0, math.pi / 2, n):
            ins = f * (1 - math.cos(t))
            dy = f - f * math.sin(t)
            yy = ys + dy if ys == y0 else ys - dy
            cs = rrect(w - 2 * ins, h - 2 * ins, r - ins)
            parts.append(slab(cs, yy, yy + 0.01) if ys == y0 else slab(cs, yy - 0.01, yy))
    return Manifold.batch_hull(parts)


# TORSO (CAD dimensions; added: luminous edge strips)

p = TP
W, H, D = p["torsoW"], p["torsoH"], p["torsoD"]
wt = p["wallT"]
shellD = D - p["backT"]
zG = -H / 2 + p["glassBot"] + p["glassH"] / 2
zBand0, zCeil = zG + p["glassH"] / 2 + p["glassFit"], H / 2 - wt
zX = (zBand0 + zCeil) / 2
zCam = zX + p["camOffZ"]
uMid = D / 2
zSh = H / 2 - p["shoulderDown"]
HOLES = [(sx * p["holeX"], zG + sz * p["holeZ"]) for sx in (1, -1) for sz in (1, -1)]


def support_collier():
    'Only print: detachable base under the neck collar. The torso body is printed bottom-first (joining the façade on the platform):\n- Neck collar (Ø neckD, 6mm overhang) is laid flat, with its front face overhanging.\n- Three 0.8mm-wide ribs on a 1mm base, 0.2mm below the cylinder where the slope exceeds 60° (|x| ≤ 9); 0.4mm above the torso.'
    r, g, xs = p["neckD"] / 2, 0.2, 9.0
    xs_ = np.linspace(-xs, xs, 37)
    prof = [(-xs, wt), (xs, wt)] + [(x, p["neckU"] - math.sqrt(r * r - x * x) - g) for x in xs_[::-1]]
    cs = CrossSection([prof])                                        # plane (x, y): y = depth = vertical print direction
    z0, z1 = H / 2 + 0.4, H / 2 + p["neckH"]
    fins = Manifold()
    for zf in np.linspace(z0, z1 - 0.8, 3):
        fins += Manifold.extrude(cs, 0.8).translate((0, 0, zf))
    return fins + box(-xs, xs, wt, wt + 1.0, z0, z1)                # sole on the base plate (joint diagram)


SUPPORTS_IMPRESSION = {"torso_body": (support_collier,)}     # detachable body parts, STL files for standalone printing only


def print_supports(name):
    return [f() for f in SUPPORTS_IMPRESSION.get(name, ())]


def vis_front():
    'Positions of the 4 M2 screws on the façade (old decorative screws): midway between the glass pocket and the rounded edge.'
    fit = p["glassFit"]
    dx = (p["glassW"] / 2 + fit + W / 2 - p["frontFillet"]) / 2
    zTop = (zG + p["glassH"] / 2 + fit + H / 2 - p["frontFillet"]) / 2
    zBot = zG - p["glassH"] / 2 + p["glassR"]
    return [(sx * dx, zTop) for sx in (1, -1)] + [(sx * dx, zBot) for sx in (1, -1)]


def torso_complet():
    outer = rounded_block(W, H, p["cornerR"], 0, shellD, f_front=p["frontFillet"])
    inner = slab(rrect(W - 2 * wt, H - 2 * wt, p["cornerR"] - wt), wt, shellD + 1)
    shell = outer - inner
    fit = p["glassFit"]
    cut = slab(rrect(p["glassW"] + 2 * fit, p["glassH"] + 2 * fit, p["glassR"] + fit, 0, zG), -1, p["glassT"])
    cut += slab(rrect(p["winW"], p["winH"], 2, 0, zG), -1, wt + 0.1)
    cut += cyl_y(p["camD"] / 2, 0, zCam, -1, wt + 0.1)
    # M2 screws for the façade: chamfering of the head and passage through the façade
    for x, z in vis_front():
        cut += cyl_y(p["screwDecoD"] / 2, x, z, -1, p["screwDecoT"])
        cut += cyl_y(p["faceScrewD"] / 2, x, z, -1, wt + 0.01)
    # ears on the sides (BME280 on the left) → left-side side-ears (BME280 unit)
    n = int(p["ventN"])
    for side in (1, -1):
        for k in range(n):
            z = p["ventZ"] + (k - (n - 1) / 2) * p["ventPitch"]
            x0, x1 = (W / 2 - wt - 1, W / 2 + 1) if side > 0 else (-W / 2 - 1, -W / 2 + wt + 1)
            cut += box(x0, x1, uMid - p["ventL"] / 2, uMid + p["ventL"] / 2, z - p["ventW"] / 2, z + p["ventW"] / 2)
    # Vertical luminous stripe on each side (groove; orange rod as exception)
    for side in (1, -1):
        cut += trim(side, grow=0.0)
    # USB-C (bottom) and PWR (right side)
    cut += box(-p["usbW"] / 2, p["usbW"] / 2, p["usbU"] - p["usbH"] / 2, p["usbU"] + p["usbH"] / 2, -H / 2 - 1, -H / 2 + wt + 1)
    cut += cyl_x(p["pwrD"] / 2, p["pwrU"], zG - p["glassH"] / 2 + p["pwrZ"], W / 2 - wt - 1, W / 2 + 1)
    shell = shell - cut

    # neck collar: magnetic pogo connector inside the torso, nose inserted into the skin slot (thickness = nose, facing flush); ears resting below; elongated cavity opening inward.
    zTop = H / 2 + p["neckH"]
    shell += cyl_z(p["neckD"] / 2, 0, p["neckU"], H / 2 - 1, zTop)
    zS = zTop - p["pogoFaceT"]
    for Lo, z0, z1 in ((p["pogoBodyL"], zS - 0.01, zTop + 1), (p["pogoL"], H / 2 - wt - 1, zS)):
        Lr = Lo - p["pogoW"]
        shell -= box(-Lr / 2, Lr / 2, p["neckU"] - p["pogoW"] / 2, p["neckU"] + p["pogoW"] / 2, z0, z1)
        for side in (1, -1):
            shell -= cyl_z(p["pogoW"] / 2, side * Lr / 2, p["neckU"], z0, z1)

    # shoulders:
    # - socket,
    # - internal boss,
    # - tenon hole,
    # - magnet housing.
    xFloor = W / 2 - p["socketT"]
    xMag = xFloor - p["shHoleDepth"] - p["skinT"] - p["limbMagT"]
    for side in (1, -1):
        sg = lambda a, b: (min(side * a, side * b), max(side * a, side * b))
        shell -= cyl_x(p["socketD"] / 2, uMid, zSh, *sg(xFloor, W / 2 + 1))
        shell += goutte_x(p["shoulderBossD"] / 2, uMid, zSh, *sg(xMag, W / 2 - wt + 0.5), -1)   # points at the façade
        shell -= cyl_x(p["pegD"] / 2, uMid, zSh, *sg(xFloor - p["shHoleDepth"], xFloor + 0.1))   # tenon rotates
        shell -= cyl_x(p["limbMagD"] / 2, uMid, zSh, *sg(xMag - 0.1, xMag + p["limbMagT"]))

    # legs: knurling, peg-hole, magnets
    legStack = p["legHoleDepth"] + p["skinT"] + p["limbMagT"]
    for side in (1, -1):
        x = side * p["legX"]
        shell += goutte_z(p["legBossD"] / 2, x, uMid, -H / 2 + wt - 0.5, -H / 2 + legStack, -1)   # points at the façade
        shell -= cyl_z(p["pegD"] / 2, x, uMid, -H / 2 - 1, -H / 2 + p["legHoleDepth"])          # tenon rotates
        shell -= cyl_z(p["limbMagD"] / 2, x, uMid, -H / 2 + legStack - p["limbMagT"], -H / 2 + legStack + 0.1)

    # Interior:
    # - camera cradle,
    # - radar frame,
    # - BME280 sensor compartment.
    xw = p["camModW"] / 2 + p["camFit"]
    uC1 = wt + p["camModT"] + p["camFit"]
    for side in (1, -1):
        x0, x1 = sorted((side * xw, side * (xw + p["cradleT"])))
        shell += box(x0, x1, wt - 0.5, uC1, zBand0 + 0.2, zCeil + 0.5)
    xR0 = p["radX0"]
    xR1 = xR0 + p["radW"]
    zR0 = zX - p["radH"] / 2
    uR = wt + 2.0
    shell += box(xR0 - p["radFit"] - p["cradleT"], xR0 - p["radFit"], wt - 0.5, wt + 2.0, zBand0 + 0.2, zCeil + 0.5)
    shell += box(xR1 + p["radFit"], xR1 + p["radFit"] + p["railT"], wt - 0.5, uR, zBand0 + 0.2, zCeil + 0.5)
    shell += box(xR0 - p["radFit"] - p["cradleT"], xR1 + p["radFit"] + p["railT"], wt - 0.5, uR, zBand0 + 0.2, zR0 - p["radFit"])
    zv = (n - 1) / 2 * p["ventPitch"] + p["ventW"] / 2 + 0.5
    xi, bw = -(W / 2 - wt), p["bmeWallT"]
    uB0, uB1 = uMid - p["ventL"] / 2 - 1, uMid + p["ventL"] / 2 + 1
    shell += box(xi - 0.5, xi + p["bmeBoxX"] + bw, uB0 - bw, uB1, p["ventZ"] - zv - bw, p["ventZ"] + zv + bw)
    shell -= box(xi, xi + p["bmeBoxX"], uB0, uB1 + 1, p["ventZ"] - zv, p["ventZ"] + zv)
    # fastening of face screws, body side, connected to the rib by a flange; pre-drill M2 hole.
    rb, lb = p["faceBossD"] / 2, p["faceBossL"]
    for x, z in vis_front():
        sx = 1 if x > 0 else -1
        x0, x1 = sorted((x, sx * (W / 2 - wt + 0.5)))
        shell += cyl_y(rb, x, z, wt, wt + lb) + box(x0, x1, wt, wt + lb, z - 1.5, z + 1.5)
        shell -= cyl_y(p["facePilotD"] / 2, x, z, wt - 0.01, wt + lb - 1.0)
    return shell


_COMPLET = []


def _complet():
    if not _COMPLET:
        _COMPLET.append(torso_complet())
    return _COMPLET[0]


def torso_front():
    'Torso façade: front panel (0 to wt), including the window pocket, shoulder step, screen window, camera hole, and threading for the 4 screws. Printed backwards on the platform: the shoulder step is built unsupported.'
    return _complet() ^ box(-BIG, BIG, -BIG, wt, -BIG, BIG)


def torso_body():
    'Torso body: everything behind the façade (joint at wt, on the rounded edge, same contour). Printed bottom-up without Cura support (bosses for limbs in drop mode, fins under the collar).'
    return _complet() ^ box(-BIG, BIG, wt, BIG, -BIG, BIG)


def torso_frame():
    'Black thin frame affixed to the façade around the glass and camera.\nSame aperture as the glass: no fictitious extension of the screen area.'
    frame = slab(rrect(p["glassW"] + 5, H - 8, 7), -0.85, 0.0)        # Placed on the façade (y = 0), without sinking in.
    frame -= slab(rrect(p["glassW"] + 0.4, p["glassH"] + 0.4, p["glassR"] + 0.2, 0, zG), -1, 1)
    frame -= cyl_y(p["camD"] / 2, 0, zCam, -1, 1)
    return frame


def paint_screws():
    'Only rendered (never printed): black heads of the 4 M2 screws on the front façade, at the bottom of their countersink.'
    m = None
    for x, z in vis_front():
        c = cyl_y(p["screwDecoD"] / 2 - 0.05, x, z, p["screwDecoT"] - 0.1, p["screwDecoT"] - 0.02)
        m = c if m is None else m + c
    return m


def trim(side, grow=0.0):
    'Vertical groove on the side (grow = 0); the orange tenon filling it is of the same shape.'
    x0, x1 = (W / 2 - STYLE["ledT"], W / 2 + 1) if side > 0 else (-W / 2 - 1, -W / 2 + STYLE["ledT"])
    w = STYLE["ledW"] / 2 + grow
    return box(x0, x1, STYLE["ledU"] - w, STYLE["ledU"] + w, STYLE["ledZ0"], STYLE["ledZ1"])


def torso_strip(side):
    'Light rod from the side (orange translucent piece glued into the groove): flush-mounted.'
    x0, x1 = (W / 2 - STYLE["ledT"], W / 2) if side > 0 else (-W / 2, -W / 2 + STYLE["ledT"])
    w = STYLE["ledW"] / 2 - 0.1
    return box(x0, x1, STYLE["ledU"] - w, STYLE["ledU"] + w, STYLE["ledZ0"] + 0.1, STYLE["ledZ1"] - 0.1)


def torso_back():
    back = rounded_block(W, H, p["cornerR"], shellD, D, f_back=p["backFillet"])
    lf, lw = p["lipFit"], p["lipW"]
    ri = p["cornerR"] - wt - lf
    lip = slab(rrect(W - 2 * (wt + lf), H - 2 * (wt + lf), ri), shellD - p["lipH"], shellD + 0.01) \
        - slab(rrect(W - 2 * (wt + lf + lw), H - 2 * (wt + lf + lw), max(ri - lw, 0.5)), shellD - p["lipH"] - 1, shellD + 1)
    back += lip
    for x, z in HOLES:
        back += cyl_y(p["bossD"] / 2, x, z, p["standoffU"], shellD + 0.01)
    for x, z in HOLES:
        back -= cyl_y(p["screwD"] / 2, x, z, p["standoffU"] - 1, D + 1)
        back -= cyl_y(p["screwHeadD"] / 2, x, z, D - p["screwHeadT"], D + 1)
    # BH1750
    rb = (p["domeBaseD"] + 0.4) / 2
    back += cyl_y(rb + 1.2, 0, p["domeZ"], shellD - p["domeRingH"], shellD + 0.01) - cyl_y(rb, 0, p["domeZ"], shellD - p["domeRingH"] - 1, shellD + 1)
    back -= cyl_y((p["domeD"] + 0.4) / 2, 0, p["domeZ"], shellD - 1, D + 1)
    # HP case ring
    spkIn = p["spkD"] + 0.4
    boxOD = spkIn + 2 * p["boxT"]
    r0 = (boxOD + 0.4) / 2
    back += cyl_y(r0 + 1.2, p["spkX"], p["spkZ"], shellD - 2, shellD + 0.01) - cyl_y(r0, p["spkX"], p["spkZ"], shellD - 3, shellD + 1)
    # High-power filter grids
    n = int(p["grilleN"])
    for k in range(n):
        z = p["spkZ"] + (k - (n - 1) / 2) * p["grillePitch"]
        back -= box(p["spkX"] - p["grilleW"] / 2, p["spkX"] + p["grilleW"] / 2, shellD - 1, D + 1, z - p["grilleH"] / 2, z + p["grilleH"] / 2)
    return back


def torso_speaker_box():
    spkIn = p["spkD"] + 0.4
    boxOD = spkIn + 2 * p["boxT"]
    depth = p["spkT"] + p["spkVol"]
    b = cyl_y(boxOD / 2, p["spkX"], p["spkZ"], shellD - (depth + p["boxT"]), shellD)
    b -= cyl_y(spkIn / 2, p["spkX"], p["spkZ"], shellD - depth, shellD + 1)
    b -= cyl_x(1.5, shellD - depth + 3, p["spkZ"], p["spkX"] + boxOD / 2 - p["boxT"] - 1, p["spkX"] + boxOD / 2 + 1)
    return b


# HEAD (reference point for the head: X to the right, Y upward, Z = depth from the window)

h = dict(HP, bezelD=86, plateExtra=STYLE["plateExtra"])     # Dimensions of the head (head), overhang plate suitable for complete edition assembly.
R = h["bezelD"] / 2
Rp = R + h["plateExtra"]
frontT, plateT = h["frontT"], h["plateT"]
zP, zB = frontT, frontT + plateT
zR = zB + STYLE["domeT"]
flatY = -Rp + h["flatH"]
zMid = (zP + zB) / 2
BIG = 300


cF = math.sqrt(Rp * Rp - R * R)        # center of the sphere front (radius Rp): it passes through the front edge of the bezel (R, 0).


def sphf(d):
    'Front sphere radius to depth d (bezel → plate, tangent to equatorial cylinder at cF).'
    return math.sqrt(max(Rp * Rp - (d - cF) ** 2, 0.0))


def sphf_arc(d0, d1, n=48):
    return [(sphf(d), d) for d in np.linspace(d0, d1, n)]


def sph(d, rad=Rp):
    'Back dome radius to depth d (sphere of radius rad centered on the rear plate).'
    return math.sqrt(max(rad * rad - (d - zB) ** 2, 0.0))


def sph_arc(d0, d1, rad=Rp, n=64):
    return [(sph(d, rad), d) for d in np.linspace(d0, d1, n)]


def revolve(profile, n=SEG * 2):
    return Manifold.revolve(CrossSection([profile]), n)


def hcircle(r, cx=0.0, cy=0.0):
    return CrossSection.circle(r, seg(r)).translate((cx, cy))


def hprism(cs, z0, z1):
    return Manifold.extrude(cs, z1 - z0).translate((0, 0, z0))


def hbox(x0, x1, y0, y1, z0, z1):
    if not (x1 > x0 and y1 > y0 and z1 > z0):
        raise ValueError('Empty or inverted dimension box:' + str((x0, x1, y0, y1, z0, z1)))
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def rod_y(r, x, z, y0, y1):
    return Manifold.cylinder(y1 - y0, r, r, seg(r)).rotate((-90, 0, 0)).translate((x, y0, z))


def screw_positions():
    rot = math.radians(h["boardRot"])
    out = []
    for sx, hh in ((h["sUpX"], 20.36), (h["sLoX"], 61.06)):
        for side in (1, -1):
            x, y = side * sx, h["pcbTop"] - hh
            out.append((x * math.cos(rot) - y * math.sin(rot), x * math.sin(rot) + y * math.cos(rot)))
    return out


SCREWS = screw_positions()


def meplat():
    return hbox(-BIG, BIG, -BIG, flatY, -BIG, BIG)


def head_bezel():
    'Black bezel (complete edition): original face plate, glass pocket, and original shoulder; orange border groove.\nIndex marks (12, 3, 6, 9 h, small diagonal notches) are painted: no geometry applied.'
    rg = h["glassD"] / 2 + h["glassFit"]
    rl = h["ledgeD"] / 2
    rs = R - STYLE["lunStep"]                                            # rear recess, concealed by the bezel of the plate.
    rb = h["boreD"] / 2                                              # rear panel: USB-C, microSD card reader, cable
    prof = [(rb, frontT), (rb, h["boreD0"]), (rl, h["boreD0"]), (rl, h["glassT"]), (rg, h["glassT"]), (rg, 0)] + \
           sphf_arc(0, STYLE["lunVis"]) + \
           [(rs, STYLE["lunVis"]), (rs, frontT)]                        # Front side on the spherical part: continuity with the plate.
    gw = STYLE["rimGrooveW"] / 2
    groove = hprism(hcircle(STYLE["rimR0"] + gw) - hcircle(STYLE["rimR0"] - gw), -1, STYLE["rimGrooveT"])   # groove for painted edge guide
    return revolve(prof) - groove - meplat()


def paint_trim_bezel():
    'Only rendered (never printed): the orange stripe painted on the outer band of the bezel face.'
    return hprism(hcircle(STYLE["rimR1"] - 0.02) - hcircle(STYLE["rimR0"] + STYLE["rimGrooveW"] / 2), -0.03, 0.0) - meplat()


def paint_marks():
    'Only rendered (never printed): painted index markings on the bezel, 0.05 mm thick front film.'
    m = None
    for k in range(8):
        L = STYLE["idxBig"] if k % 2 == 0 else STYLE["idxSmall"]
        bar = hbox(-L / 2, L / 2, STYLE["idxR"] - STYLE["idxW"] / 2, STYLE["idxR"] + STYLE["idxW"] / 2, -0.05, 0.0).rotate((0, 0, 90 - 45 * k))
        m = bar if m is None else m + bar
    return m


def goutte():
    d, rt = h["tabD"], h["tabR"]
    a = math.asin((Rp - rt) / d)
    pts = [(Rp * math.cos(t), Rp * math.sin(t)) for t in np.linspace(math.pi - a, 2 * math.pi + a, 200)]
    pts += [(rt * math.cos(t), d + rt * math.sin(t)) for t in np.linspace(a, math.pi - a, 80)]
    return CrossSection([pts])


def front_envelope():
    'Front head volume: front sphere (from bezel lunVis to cF), cylinder from equator to rear plate.'
    return revolve([(0, STYLE["lunVis"] - 1)] + sphf_arc(STYLE["lunVis"] - 1, cF) + [(Rp, zB + 1), (0, zB + 1)])


def tab_front_zone():
    'The bail and its base: a drop (swollen by 1 mm, outside the flanks) above the tangency points, outside the front sphere.'
    d, rt = h["tabD"], h["tabR"]
    yt = Rp * (Rp - rt) / d                                            # Tangency point heights
    above = CrossSection.square((400, 400)).translate((-200, yt))
    return (goutte().offset(1.0, JoinType.Round, 2.0, 64) - hcircle(sphf(STYLE["lunVis"]) - 0.3)) ^ above


def tab_zone():
    'Cutting zone of the bail: the drop outside the sphere (beyond Rp), widened by 1 mm outward to avoid alignment with the plate’s flanks (overlapping faces → zero-thickness chips).'
    return goutte().offset(1.0, JoinType.Round, 2.0, 64) - hcircle(Rp + 0.05)


def bail_zone():
    'Bail outside orange border: the recessed bead from tab inset, outside the head disk, and around the hole.'
    cs = goutte().offset(-STYLE["tabInset"], JoinType.Round, 2.0, 64)
    return cs - hcircle(Rp + STYLE["tabInset"]) - hcircle(h["holeD"] / 2 + STYLE["tabInset"], 0, h["tabD"])


def bail_peint():
    'Clear painted area of the bail: the zone minus the width of its guide groove.'
    return bail_zone().offset(-STYLE["tabGrooveW"], JoinType.Round, 2.0, 64)


def head_plate():
    cs = goutte() - hcircle(h["holeD"] / 2, 0, h["tabD"]) - hcircle(h["plateOpenD"] / 2)
    for x, y in SCREWS:
        cs = cs - hcircle(h["screwD"] / 2, x, y)
    plate = hprism(cs, zP, zB)
    # The entire plate (drop and bail) advances to the lunVis behind the façade: it envelops the rear in a recess of the bezel; the black bezel protrudes directly from the orange face (black façade visible = lunVis, no orange strip).
    front = goutte() - hcircle(h["holeD"] / 2, 0, h["tabD"]) - hcircle(R - STYLE["lunStep"] + STYLE["lunFit"])
    plate += hprism(front, STYLE["lunVis"], zP + 0.01)
    # Place the bezel onto the front sphere (continuity bezel → plate → dome); the bail retains its flat face.
    plate ^= front_envelope() + hprism(tab_front_zone(), STYLE["lunVis"] - 1, zB + 1)
    plate -= meplat()
    plate -= hprism(tab_zone(), STYLE["lunVis"] + STYLE["tabThick"], zB + 1)        # thin bail (rear: none)
    plate -= hprism(bail_zone() - bail_peint(), STYLE["lunVis"] - 1, STYLE["lunVis"] + STYLE["tabGrooveT"])   # guide-channel
    # internal-pogo-connector assembly:
    # nose inserted into the flat-side groove (thickness = nose, facing flush),
    # ears pressed against the skin behind it;
    # oblong channel to the central opening for insertion, wired soldered.
    L, Wd, t = h["pogoL"], h["pogoW"], h["pogoFaceT"]
    yC = -math.sqrt((h["plateOpenD"] / 2) ** 2 - (L / 2) ** 2) + 1.0       # top of the channel, in the central opening
    for Lo, y0, y1 in ((h["pogoBodyL"], flatY - 1, flatY + t + 0.01), (L, flatY + t, yC)):
        plate -= hbox(-(Lo - Wd) / 2, (Lo - Wd) / 2, y0, y1, zMid - Wd / 2, zMid + Wd / 2)
        for side in (1, -1):
            plate -= rod_y(Wd / 2, side * (Lo - Wd) / 2, zMid, y0, y1)
    return plate


def paint_bail():
    'Only rendered (never printed): the clear painted bail in its guide groove.'
    return hprism(bail_peint(), STYLE["lunVis"] - 0.03, STYLE["lunVis"])


def head_back():
    'Dome: Spherical shell (Rp) centered on the rear plate, truncated at zR; wallT shell casing; fully threaded columns down to the well’s base (M2×35 screw head fits there), then tubes down to the base (attached to the wall and base); LiPo cradle on the flat bottom.'
    w = h["wallT"]
    outer = [(0, zB)] + sph_arc(zB, zR) + [(0, zR)]
    inner = [(0, zB - 1)] + sph_arc(zB - 1, zR - w, Rp - w) + [(0, zR - w)]
    inner_full = [(0, zB - 1)] + sph_arc(zB - 1, zR - w + 0.5, Rp - w + 0.5) + [(0, zR - w + 0.5)]   # columns: inside the housing
    back = revolve(outer) - revolve(inner)
    zTopCol = zR - STYLE["headScrewWell"]
    for x, y in SCREWS:
        back += hprism(hcircle(h["bossD"] / 2, x, y), zB, zR - w + 0.01) ^ revolve(inner_full)
    for x, y in SCREWS:
        back -= hprism(hcircle(h["screwD"] / 2, x, y), zB - 1, zR + 1)
        back -= hprism(hcircle(STYLE["headScrewWellD"] / 2, x, y), zTopCol, zR + 1)
    rm, gw = STYLE["medD"] / 2, STYLE["medGrooveW"] / 2                      # groove guide for the painted medallion (outer diameter medD)
    back -= hprism(hcircle(rm + gw) - hcircle(rm - gw), zR - STYLE["medGrooveT"], zR + 1)
    wi, li = h["lipoW"] / 2 + h["lipoFit"], h["lipoL"] / 2 + h["lipoFit"]
    wo, lo = wi + h["ribW"], li + h["ribW"]
    z0 = zR - w - STYLE["lipoWallH"]                                       # opening in the housing (side of PCB)
    frame = hprism(CrossSection.square((2 * wo, 2 * lo), True) - CrossSection.square((2 * wi, 2 * li), True), z0, zR - w + 0.01)
    for sx in (1, -1):                                                 # retention pegs, at the edge of the opening
        xa, xb = sorted((sx * (wi - STYLE["lipoLipIn"]), sx * (wi + 0.5)))
        frame += hbox(xa, xb, -STYLE["lipoLipL"] / 2, STYLE["lipoLipL"] / 2, z0, z0 + STYLE["lipoLipT"])
    g = h["lipoWireGap"] / 2
    frame -= hbox(-g, g, -lo - 0.5, -li + 0.5, z0 - 1, zR)
    return (back + frame + casques_dome()) - meplat()


def paint_medallion():
    'Only rendered (never printed): the grey enamel medallion on the rear disk of the dome, in its groove.'
    return hprism(hcircle(STYLE["medD"] / 2 - STYLE["medGrooveW"] / 2), zR, zR + 0.03)


def headphone_local():
    'Head +X axis from origin (center of dome) before tilt: casing, groove for peg, conical grille.\nReturns: (casing, peg)'
    a0, ao, d, rf = STYLE["earRoot"], Rp + STYLE["earOut"], STYLE["earD"], STYLE["earRound"]
    rad = d / 2
    prof = [(0, 0), (rad, 0)] + [(rad - rf + rf * math.cos(t), (ao - a0) - rf + rf * math.sin(t))
                                  for t in np.linspace(0, math.pi / 2, 16)] + [(0, ao - a0)]
    pod = revolve(prof).rotate((0, 90, 0)).translate((a0, 0, 0))
    ro = rad - rf - STYLE["earRingInset"]
    ri = ro - STYLE["earRingW"]
    ring = Manifold.cylinder(STYLE["earRingT"] + 1, ro, ro, seg(ro)) - Manifold.cylinder(STYLE["earRingT"] + 1, ri, ri, seg(ri))
    cut = ring.translate((0, 0, -STYLE["earRingT"]))
    k = math.tan(math.radians(STYLE["earHoleTaper"]))
    pitch, rmax = STYLE["earHolePitch"], ri - 0.8
    pts, r = [(0.0, 0.0)], pitch
    while r <= rmax + 0.01:
        n = max(6, int(round(2 * math.pi * r / pitch)))
        pts += [(r * math.cos(2 * math.pi * j / n), r * math.sin(2 * math.pi * j / n)) for j in range(n)]
        r += pitch
    hr, hd = STYLE["earHoleR"], STYLE["earHoleDepth"]
    for u, v in pts:
        cut += Manifold.cylinder(hd + 0.4, max(hr - hd * k, 0.02), hr + 0.4 * k, 12).translate((u, v, -hd))
    to_face = lambda m: m.rotate((0, 90, 0)).translate((ao, 0, 0))     # local reference (z outward) → headphone face
    pod = pod - to_face(cut)
    # peel-coated (rendered only): orange film in the groove, part of the headphones.
    strip = to_face((Manifold.cylinder(0.03, ro - 0.02, ro - 0.02, seg(ro))
                    - Manifold.cylinder(0.03, ri + 0.02, ri + 0.02, seg(ri))).translate((0, 0, -STYLE["earRingT"])))
    return pod, strip


def headphone_tilt(m):
    return m.rotate((0, -STYLE["earTilt"], 0)).translate((0, 0, zB))   # +X → backward, dome center


def casques_dome():
    'Headphones piece with dome: the part protruding from the sphere and its mounting in the wall, without anything in the cavity or in front of the dome (side of the plate).'
    pod, _ = headphone_local()
    w = h["wallT"]
    cav = revolve([(0, zB - 1)] + sph_arc(zB - 1, zR - w, Rp - w) + [(0, zR - w)])
    front = hprism(CrossSection.square((4 * Rp, 4 * Rp), True), zB - 20, zB)
    m = headphone_tilt(pod) - cav - front
    return m + m.mirror((1, 0, 0))


def casques_rendu():
    'Black headphones painted with dome center, axis, and coverage radius of each headphone, in the torso reference frame.'
    def to_torso(p):
        b = head_to_torso(Manifold.cube((0.02, 0.02, 0.02), True).translate(p)).bounding_box()
        return [round((b[i] + b[i + 3]) / 2, 4) for i in range(3)]
    out = []
    for side in (1, -1):
        c = to_torso((0, 0, zB))
        a = math.radians(STYLE["earTilt"])
        f = to_torso((side * Rp * math.cos(a), 0, zB + Rp * math.sin(a)))
        n = math.dist(c, f)
        out.append({"centre": c, "axe": [(f[i] - c[i]) / n for i in range(3)], "rayon": STYLE["earD"] / 2 + 0.3, "rmin": Rp + 0.02})
    return out


def joncs_casques():
    'Painted peg joints for headphones (render only) in the head coordinate system: `[(name, manifold)]`.'
    _, strip = headphone_local()
    joncR = headphone_tilt(strip)
    return [("paint_strip_headphone_right", joncR), ("paint_strip_headphone_left", joncR.mirror((1, 0, 0)))]


# Head reference → torso reference: flat on collar, pogo pin above pogo pin
HEAD_DZ = TP["torsoH"] / 2 + TP["neckH"] - flatY
HEAD_DY = TP["neckU"] - zMid


def head_to_torso(m):
    return m.rotate((90, 0, 0)).mirror((0, 1, 0)).translate((0, HEAD_DY, HEAD_DZ))   # (X, Y, Z) → (X, Z, Y) + offset


# Only rendered: unprinted glass (head)
# 
# ---
# Handmade Robot Specs:
# - head = head (glass only, non-printed)
# - torso = torso (not specified)
# - bezel = bezel (not rendered)
# - casques = headphones (not rendered)
# - bélière = bail (not rendered)
# - tenon = peg (not rendered)
# - pas = pitch (not specified)
# - jeu = clearance (not rendered)

def component_bh1750():
    'BH1750 sensor ball (component): dome Ø domeD protruding from the back of the torso.'
    r = p["domeD"] / 2
    return cyl_y(r, 0, p["domeZ"], shellD, D) + Manifold.sphere(r, seg(r) * 2).translate((0, D, p["domeZ"])) \
        - box(-BIG, BIG, -BIG, shellD, -BIG, BIG)


def glass_head():
    return revolve([(0, 0), (h["glassD"] / 2 - 0.05, 0), (h["glassD"] / 2 - 0.05, h["glassT"] - 0.05), (0, h["glassT"] - 0.05)])


def glass_torso():
    return slab(rrect(p["glassW"] - 0.1, p["glassH"] - 0.1, p["glassR"], 0, zG), 0.05, p["glassT"])


# ════════════════════════════════════════════════════════════════════════
#  file output
#  ════════════════════════════════════════════════════════════════════════

def write_stl(path, m):
    m = m.simplify(0.001)                       # Remove null-area triangles left by boolean operations (1 µm)
    if m.status().name != "NoError" or m.is_empty():
        raise ValueError('Refusal of export: empty or invalid part for' + path)
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"], rec["v"] = nrm, tri
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS".ljust(80, b" "))
        fh.write(struct.pack("<I", len(t)))
        fh.write(rec.tobytes())


def on_plate(m):
    b = m.bounding_box()
    return m.translate((-(b[0] + b[3]) / 2, -(b[1] + b[4]) / 2, -b[2]))


PIECES_ATTENDUES = {"paint_marks": 8, "paint_screws": 4}   # Only rendered: 8 index marks on the bezel


def report(name, m, extra=0):
    b = m.bounding_box()
    n = PIECES_ATTENDUES.get(name, 1) + extra
    ok = m.status().name == "NoError" and len(m.decompose()) == n
    print("  {:<22} {:6.1f} × {:6.1f} × {:6.1f} mm {:7.1f} cm³  {}".format(
        name, b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000,
        'waterproof, {} part{}'.format(n, "s" if n > 1 else "") if ok else 'REVIEW'))


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_PRINT, exist_ok=True)
    placed = {  # reference of the torso
        "torso_frame": torso_frame(), "torso_body": torso_body(), "torso_front": torso_front(), "torso_back": torso_back(),
        "torso_speaker_box": torso_speaker_box(),
        "torso_strip_right": torso_strip(1), "torso_strip_left": torso_strip(-1), "paint_screws": paint_screws(),
        "head_bezel": head_to_torso(head_bezel()), "head_plate": head_to_torso(head_plate()),
        "head_back": head_to_torso(head_back()),
        "paint_medallion": head_to_torso(paint_medallion()), "paint_bail": head_to_torso(paint_bail()),
        'paint_trim_bezel': head_to_torso(paint_trim_bezel()), "paint_marks": head_to_torso(paint_marks()),
        "glass_head": head_to_torso(glass_head()), "glass_torso": glass_torso(), "component_bh1750": component_bh1750(),
    }
    for name, m in joncs_casques():
        placed[name] = head_to_torso(m)
    org = os.path.join(CAO, "MoodiMOS_Organic", "organic.py")
    ground = -H / 2 - LEG_VISIBLE - SHOE_TOP
    top = HEAD_DZ + h["tabD"] + h["tabR"]
    dimensions = {"head_glass_y": HEAD_DY, "head_center_z": HEAD_DZ, "torso_glass_z": zG, "ground_z": ground,
             "top_z": top, "mid_z": (ground + top) / 2,
             "torso_w": W, "torso_h": H, "torso_d": D,
             "head_bezel_d": h["bezelD"], "head_glass_d": h["glassD"],
             "torso_glass_w": p["glassW"], "torso_glass_h": p["glassH"],
             "torso_glass_r": p["glassR"], "casques": casques_rendu()}
    import json
    json.dump(dimensions, open(os.path.join(OUT, "dimensions.json"), "w"), indent=1)
    # Reference for the Fusion script (MoodiMOS_Robot): dimensions, precomputed profiles, expected volumes.
    polys = lambda cs: [[list(map(float, pt)) for pt in poly] for poly in cs.to_polygons()]
    ref = {"tp": TP, "h": h, "STYLE": STYLE,
           "head": {"R": R, "Rp": Rp, "zP": zP, "zB": zB, "zR": zR, "flatY": flatY, "zMid": zMid,
                    "head_dy": HEAD_DY, "head_dz": HEAD_DZ, "screws": SCREWS},
           "bail_zone": polys(bail_zone()), "tab_zone": polys(tab_zone()),
           "tab_front_zone": polys(tab_front_zone()), "cF": cF,
           "bail_peint": polys(bail_peint()),
           "volumes_mm3": {k: float(m.volume()) for k, m in placed.items()}}
    json.dump(ref, open(os.path.join(HERE, "fusion_reference.json"), "w"), indent=1, ensure_ascii=False)
    print('Total height: {:.1f} mm (base: {:.1f} mm, bail top: {:.1f} mm)'.format(top - ground, ground, top))
    print('Parts in assembly coordinates ({}) :'.format(os.path.relpath(OUT)))
    for name, m in placed.items():
        write_stl(os.path.join(OUT, name + ".stl"), m)
        report(name, m)

    # printing: front panel shell bottom-up, back exterior face bottom-up, HP box rear bottom-up,
    # rear encasing bottom-up, flat base plate, dome (with its casques) bottom-up.
    up_y = lambda m: m.rotate((90, 0, 0))       # (x, y, z) → (x, -z, y) : y = 0 at bottom
    down_y = lambda m: m.rotate((-90, 0, 0))    # (x, z, -y) → (x, y, z) : y-axis inverted (negative pitch for z-axis)
    printed = {
        "torso_frame": on_plate(up_y(placed["torso_frame"])),
        "torso_body": on_plate(up_y(sum(print_supports("torso_body"), placed["torso_body"]))),   # torso, lower joint
        "torso_front": on_plate(down_y(placed["torso_front"])),     # Backside of the façade on the platform
        "torso_back": on_plate(down_y(placed["torso_back"])),
        "torso_speaker_box": on_plate(up_y(placed["torso_speaker_box"])),
        "head_bezel": on_plate(head_bezel().rotate((0, 180, 0))),
        "head_plate": on_plate(head_plate().rotate((0, 180, 0))),   # Back face on the base plate, collar at the top
        "head_back": on_plate(head_back().rotate((0, 180, 0))),          # medallion on the bed: 3 250 mm² contact area (650 opening
                                                                       # below, no support beneath the flat base in the dome.
    }
    supports = {k: {"volume_cm3": sum(m.volume() for m in print_supports(k)) / 1000,
                    "corps": sum(len(m.decompose()) for m in print_supports(k))} for k in SUPPORTS_IMPRESSION}
    json.dump(supports, open(os.path.join(OUT, "print_supports.json"), "w"), indent=1)   # Read by the controls
    print('Printed Parts ({})'.format(os.path.relpath(OUT_PRINT)))
    for name, m in printed.items():
        write_stl(os.path.join(OUT_PRINT, name + ".stl"), m)
        report(name, m, supports.get(name, {}).get("corps", 0))


if __name__ == "__main__":
    main()
