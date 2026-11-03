# MoodiMOS Complete edition — the complete robot built in Autodesk Fusion
# 
#  Launch: Utilities > Add-ons > Scripts and Add-ons > + (existing script)
#           > Select the MoodiMOS_Robot folder > Run.
#  Prerequisites: Fusion Python generators (../robot_parts.py, ../robot_limbs.py) executed: this script reads
#  ../fusion_reference.json (dimensions, profiles, expected volumes) and ../stl/ (members).
# 
#  Built in TRUE FUSION SOLIDS (solid bodies, history functions):
#    - Torso: Main torso script (../../MoodiMOS_Torso) with settings (8° ray angles, legs at ±27.5°), plus illuminated flank strips, joints, and screen frame; integrated electronic components (PCB, camera, radar, BME280, speaker, BH1750…).
#    - Head: Black lens (orange strip chamfer, rear recess), advanced drop-plate, and painted guide groove for its ball-bearing, spherical dome (coating, bolt columns, M2×30 threaded wells, LiPo battery housing, painted medallion groove, conical mesh casings with painted joint groove—all in one piece), and glass.
#  Imported in MAILLAGES (sculpted shapes): ../stl/*.stl, already in place.
# 
#  One piece = one component: Head, Torso, Limbs; split via Animation > Automatic Explode, then Drawing > From Animation for exploded layout. Colors from ../robot_render.py.
#  Verification: At completion, Fusion volume of each piece is compared to robot_parts.py’s calculated volume (pre-verified); discrepancies appear in the final window.
# 
#  Coordinate System: x to the right (front view), y = depth from façade (backward), z upward; Fusion setting: Z Up. Head built around its glass center, then placed on the neck.

import adsk.core, adsk.fusion, traceback, os, ast, json, math, unicodedata, importlib.util

HERE = os.path.dirname(os.path.realpath(__file__))
ROBOT_DIR = os.path.normpath(os.path.join(HERE, ".."))
CAO = os.path.normpath(os.path.join(ROBOT_DIR, ".."))
STL_DIR = os.path.join(ROBOT_DIR, "stl")

VR = adsk.core.ValueInput.createByReal
VS = adsk.core.ValueInput.createByString
NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation
INTERSECT = adsk.fusion.FeatureOperations.IntersectFeatureOperation


def P3(x, y, z):
    'Millimeter point → Centimeter Point3D (Fusion internal units).'
    return adsk.core.Point3D.create(x / 10.0, y / 10.0, z / 10.0)


def nfc(s):
    return unicodedata.normalize("NFC", s)


# ════════════════════════════════════════════════════════════════════════
#  component construction tools
#  ════════════════════════════════════════════════════════════════════════

class Builder:
    def __init__(self, comp):
        self.c, self.f, self.sk = comp, comp.features, comp.sketches
        self.xz, self.xy, self.yz = comp.xZConstructionPlane, comp.xYConstructionPlane, comp.yZConstructionPlane
        self.sY = 1.0 if self.xz.geometry.normal.y > 0 else -1.0     # Depth (d) → y = sY × d
        self.sZ = 1.0 if self.xy.geometry.normal.z > 0 else -1.0
        self.sX = 1.0 if self.yz.geometry.normal.x > 0 else -1.0

    # Points and planes
    def pt(self, x, d, z):
        return P3(x, self.sY * d, z)

    def offset(self, base, dist_mm):
        inp = self.c.constructionPlanes.createInput()
        inp.setByOffset(base, VR(dist_mm / 10.0))
        pl = self.c.constructionPlanes.add(inp)
        pl.isLightBulbOn = False
        return pl

    def dplane(self, d):          # Facade plane at depth d; positive extrusion = backward (+d)
        return self.xz if abs(d) < 1e-9 else self.offset(self.xz, d)

    def zplane(self, z):          # Horizontal plane at height z; positive extrusion = sZ direction
        return self.offset(self.xy, self.sZ * z)

    def xplane(self, x):          # Lateral plane at x-coordinate; positive extrusion = sX direction
        return self.offset(self.yz, self.sX * x)

    def sketch(self, plane, name):
        s = self.sk.add(plane)
        s.name = name
        return s

    def S(self, s, x, d, z):
        return s.modelToSketchSpace(self.pt(x, d, z))

    # Curves
    def circle(self, s, x, d, z, r):
        return s.sketchCurves.sketchCircles.addByCenterRadius(self.S(s, x, d, z), r / 10.0)

    def polyline(self, s, pts3):
        'Closed Polygon: List of (x, d, z) coordinates.'
        ln = s.sketchCurves.sketchLines
        first = prev = None
        for i, p in enumerate(pts3):
            q = self.S(s, *p)
            if prev is None:
                prev = q
                continue
            l = ln.addByTwoPoints(prev, q)
            if first is None:
                first = l.startSketchPoint
            prev = l.endSketchPoint
        ln.addByTwoPoints(prev, first)

    def rect(self, s, p0, p1):
        s.sketchCurves.sketchLines.addTwoPointRectangle(self.S(s, *p0), self.S(s, *p1))

    # profiles and functions
    @staticmethod
    def loops(s, n):
        for p in s.profiles:
            if p.profileLoops.count == n:
                return p
        raise RuntimeError('Sketch « {} »: no profile at {} contours (found: {})'.format(
            s.name, n, [p.profileLoops.count for p in s.profiles]))

    @staticmethod
    def every(s):
        oc = adsk.core.ObjectCollection.create()
        for p in s.profiles:
            oc.add(p)
        return oc

    def extrude(self, profiles, dist_mm, op, bodies=None):
        ex = self.f.extrudeFeatures
        inp = ex.createInput(profiles, op)
        inp.setDistanceExtent(False, VR(dist_mm / 10.0))
        if bodies:
            inp.participantBodies = bodies
        return ex.add(inp)

    def revolve(self, s, profile, axis, op=NEW):
        rv = self.f.revolveFeatures
        inp = rv.createInput(profile, axis, op)
        inp.setAngleExtent(False, VR(2 * math.pi))
        return rv.add(inp)

    def revolve_rd(self, name, segs, axis_d=(-300, 300)):
        'Revolutionary body around depth axis (x = 0, z = 0). Segments: starting point (r, d) then\n        ("L", (r, d)) or ("A", (r, d) start, (r, d) end); the contour closes back to the start.'
        s = self.sketch(self.xy, name)
        ln, ar = s.sketchCurves.sketchLines, s.sketchCurves.sketchArcs
        M = lambda rd: s.modelToSketchSpace(self.pt(rd[0], rd[1], 0))
        start = M(segs[0])
        cur, first = start, None
        for sg in segs[1:]:
            if sg[0] == "L":
                c = ln.addByTwoPoints(cur, M(sg[1]))
            else:
                c = ar.addByThreePoints(cur, M(sg[1]), M(sg[2]))
            if first is None:
                first = c.startSketchPoint
            cur = c.endSketchPoint
        ln.addByTwoPoints(cur, first)
        axis = ln.addByTwoPoints(M((0, axis_d[0])), M((0, axis_d[1])))
        axis.isConstruction = True
        body = self.revolve(s, s.profiles.item(0), axis).bodies.item(0)
        body.name = name
        return body

    def combine(self, target, tools, op, keep=True):
        oc = adsk.core.ObjectCollection.create()
        for t in tools:
            oc.add(t)
        cb = self.f.combineFeatures
        inp = cb.createInput(target, oc)
        inp.operation = op
        inp.isKeepToolBodies = keep
        return cb.add(inp)

    def remove(self, body):
        self.f.removeFeatures.add(body)

    def rotate_z(self, bodies, angle, center):
        'Rotation of body around a vertical axis (z) passing through center (x, d, z in mm).'
        oc = adsk.core.ObjectCollection.create()
        for b in bodies:
            oc.add(b)
        m = adsk.core.Matrix3D.create()
        m.setToRotation(angle, adsk.core.Vector3D.create(0, 0, 1), self.pt(*center))
        mf = self.f.moveFeatures
        try:
            inp = mf.createInput2(oc)
            inp.defineAsFreeMove(m)
        except AttributeError:
            inp = mf.createInput(oc, m)
        mf.add(inp)

    def by_name(self, name):
        for b in self.c.bRepBodies:
            if b.name == name:
                return b
        raise RuntimeError('Body « {} » not found'.format(name))


# ════════════════════════════════════════════════════════════════════════
#  HEAD
#  ════════════════════════════════════════════════════════════════════════

def build_head(comp, ref):
    b = Builder(comp)
    h, STYLE, T = ref["h"], ref["STYLE"], ref["head"]
    R, Rp, zP, zB, zR, flatY, zMid = T["R"], T["Rp"], T["zP"], T["zB"], T["zR"], T["flatY"], T["zMid"]
    frontT, w = h["frontT"], h["wallT"]
    screws = T["screws"]
    rs = R - STYLE["lunStep"]
    lv = STYLE["lunVis"]
    sph = lambda d, rad=Rp: math.sqrt(max(rad * rad - (d - zB) ** 2, 0.0))
    cF = ref["cF"]                                                  # Front sphere: passes through the front edge of the lens
    sphf = lambda d: math.sqrt(max(Rp * Rp - (d - cF) ** 2, 0.0))

    def meplat(bodies, name):
        s = b.sketch(b.zplane(flatY), 'Flat face' + name)
        b.rect(s, (-200, -200, flatY), (200, 200, flatY))
        b.extrude(b.loops(s, 1), -60 * b.sZ, CUT, bodies)

    # 1. Black lens: rotational profile (glass pocket, shoulder, rear recess), painted rim groove
    rg, rl, rb, d0 = h["glassD"] / 2 + h["glassFit"], h["ledgeD"] / 2, h["boreD"] / 2, h["boreD0"]
    # Lens flank on front sphere: continuity from lens → plate → dome; rear hammering (thinned wall
    # behind the lens shoulder) for USB-C, microSD reader, and PCB cable that overhangs
    lun = b.revolve_rd("head_bezel", [(rb, frontT), ("L", (rb, d0)), ("L", (rl, d0)), ("L", (rl, h["glassT"])),
                                         ("L", (rg, h["glassT"])), ("L", (rg, 0)),
                                         ("L", (sphf(0), 0)), ("A", (sphf(lv / 2), lv / 2), (sphf(lv), lv)),
                                         ("L", (rs, lv)), ("L", (rs, frontT))])
    s = b.sketch(b.dplane(0), 'Painted edge groove (rim)')
    b.circle(s, 0, 0, 0, STYLE["rimR0"] + STYLE["rimGrooveW"] / 2)
    b.circle(s, 0, 0, 0, STYLE["rimR0"] - STYLE["rimGrooveW"] / 2)
    b.extrude(b.loops(s, 2), STYLE["rimGrooveT"], CUT, [lun])
    meplat([lun], "bezel")
    # 2. orange stripe: painted on the outer band of the face (aspect shown in *colorized view*).

    # 3. Drop plate: original component (zP → zB) + extended to lunVis, enveloping the rear of the lens
    def goutte(s, d, e=0.0, hole=True):
        'Drop contour (circle of the head + rounded bushing + parallel tangents), offset by e outward (e < 0: inward). Same centers, radii + e, parallel tangents. Smooth arcs and straight edges.'
        rt0, dt = h["tabR"], h["tabD"]
        sn = (Rp - rt0) / dt
        cs = math.sqrt(1.0 - sn * sn)
        rb, rt = Rp + e, rt0 + e
        M = lambda x, z: b.S(s, x, d, z)
        ar = s.sketchCurves.sketchArcs
        big = ar.addByThreePoints(M(rb * cs, rb * sn), M(0, -rb), M(-rb * cs, rb * sn))
        top = ar.addByThreePoints(M(-rt * cs, dt + rt * sn), M(0, dt + rt), M(rt * cs, dt + rt * sn))
        ends = [big.startSketchPoint, big.endSketchPoint, top.startSketchPoint, top.endSketchPoint]
        near = lambda target: min(ends, key=lambda sp: sp.geometry.distanceTo(target))
        for side in (1, -1):
            s.sketchCurves.sketchLines.addByTwoPoints(near(M(side * rb * cs, rb * sn)), near(M(side * rt * cs, dt + rt * sn)))
        if hole:
            b.circle(s, 0, d, dt, h["holeD"] / 2)

    def by_area(s, target_mm2, far=False):
        'Sketch profile whose area is closest to the zone calculated by `robot_parts.py`. far: Only profiles where the barycenter is distant from the head axis (> 25 mm) — the bushing, not the thin crescent cut by the large circle at the bottom of the head (nearby area).'
        cands = list(s.profiles)
        if far:
            def dist(p):
                c = p.areaProperties().centroid
                return math.sqrt(c.x * c.x + c.y * c.y + c.z * c.z) * 10.0
            cands = [p for p in cands if dist(p) > 25.0] or cands
        return min(cands, key=lambda p: abs(p.areaProperties().area * 100.0 - target_mm2))

    s = b.sketch(b.dplane(zP), 'Drop Plate')
    goutte(s, zP)
    b.circle(s, 0, zP, 0, h["plateOpenD"] / 2)
    plate = b.extrude(b.loops(s, 3), zB - zP, NEW).bodies.item(0)
    plate.name = "head_plate"
    s = b.sketch(b.dplane(STYLE["lunVis"]), 'Advance of the drop plate')
    goutte(s, STYLE["lunVis"])
    b.circle(s, 0, STYLE["lunVis"], 0, rs + STYLE["lunFit"])
    b.extrude(b.loops(s, 3), zP - STYLE["lunVis"], JOIN, [plate])
    # Drop plate advance on the front sphere; the bushing (and its base) retains its flat face: intersection of the plate with (sphere ∪ bushing)
    tool = b.revolve_rd('Front-sphere tool', [(0, lv - 1), ("L", (sphf(lv - 1), lv - 1)),
                                               ("A", (sphf((lv - 1 + cF) / 2), (lv - 1 + cF) / 2), (Rp, cF)),
                                               ("L", (Rp, zB + 1)), ("L", (0, zB + 1))])
    s = b.sketch(b.dplane(lv - 1), 'Bushing tool')
    goutte(s, lv - 1, e=1.0, hole=False)
    b.circle(s, 0, lv - 1, 0, sphf(lv) - 0.3)
    yt = Rp * (Rp - h["tabR"]) / h["tabD"]                          # Tangency point heights
    s.sketchCurves.sketchLines.addByTwoPoints(b.S(s, -70, lv - 1, yt), b.S(s, 70, lv - 1, yt))
    tab = b.extrude(by_area(s, zone_area(ref["tab_front_zone"]), far=True), zB + 1 - (lv - 1), NEW).bodies.item(0)
    b.combine(tool, [tab], JOIN, keep=False)
    b.combine(plate, [tool], INTERSECT, keep=False)
    plate = b.by_name("head_plate")
    s = b.sketch(b.dplane(zP), "Vis de la carte (plate)")
    for x, z in screws:
        b.circle(s, x, zP, z, h["screwD"] / 2)
    b.extrude(b.every(s), zB - zP, CUT, [plate])
    meplat([plate], "plate")
    dt = STYLE["lunVis"] + STYLE["tabThick"]                                # thin shank: no backing behind its rear face
    s = b.sketch(b.dplane(dt), 'thin shank')
    goutte(s, dt, e=1.0, hole=False)                                # swollen drop 1 mm (outside of sides)…
    b.circle(s, 0, dt, 0, Rp + 0.05)                                # ... minus the sphere: ring + shank
    b.extrude(by_area(s, zone_area(ref["tab_zone"])), zB - dt + 1, CUT, [plate])
    # guide groove on the painted face of the shank: the recessed zone, then the painted face refined to the plate
    for ti, key, op in ((STYLE["tabInset"], "bail_zone", CUT), (STYLE["tabInset"] + STYLE["tabGrooveW"], "bail_peint", JOIN)):
        s = b.sketch(b.dplane(STYLE["lunVis"]), 'Painted groove of the shank' if op == CUT else 'Painted face of the shank')
        goutte(s, STYLE["lunVis"], e=-ti, hole=False)
        b.circle(s, 0, STYLE["lunVis"], 0, Rp + ti)
        b.circle(s, 0, STYLE["lunVis"], h["tabD"], h["holeD"] / 2 + ti)
        b.extrude(by_area(s, zone_area(ref[key]), far=True), STYLE["tabGrooveT"], op, [plate])
    # pogo connector mounted from the inside (mounted from the flat side): tip in the slot of the flat side skin
    #  (thickness = tip, facing flush), ears in contact behind; elongated channel to central opening
    up = lambda dz: dz * b.sZ
    L, Wd, t = h["pogoL"], h["pogoW"], h["pogoFaceT"]
    yC = -math.sqrt((h["plateOpenD"] / 2) ** 2 - (L / 2) ** 2) + 1.0
    for name, Lo, z0, depth in (("Pogo, fente du nez", h["pogoBodyL"], flatY, t), ("Pogo, canal", L, flatY + t, yC - flatY - t)):
        s = b.sketch(b.zplane(z0), name)
        Lr = Lo - Wd                                     # elongated: rounded ends like the connector ears
        b.rect(s, (-Lr / 2, zMid - Wd / 2, z0), (Lr / 2, zMid + Wd / 2, z0))
        for side in (1, -1):
            b.circle(s, side * Lr / 2, zMid, z0, Wd / 2)
        b.extrude(b.every(s), up(depth), CUT, [plate])

    # 4. shank: painted face (surface finish by colorizing), additional bonded insert

    # 5. spherical dome: full revolution shell, columns (within shell), screws, well, LiPo battery cradle, LiPo battery support beam
    dm = (zB + zR) / 2
    dome = b.revolve_rd("head_back", [(0, zB), ("L", (sph(zB), zB)), ("A", (sph(dm), dm), (sph(zR), zR)), ("L", (0, zR))])
    faces = [f for f in dome.faces if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType]
    front = min(faces, key=lambda f: abs(f.centroid.y * 10.0 - b.sY * zB))
    oc = adsk.core.ObjectCollection.create()
    oc.add(front)
    si = b.f.shellFeatures.createInput(oc, False)
    si.insideThickness = VR(w / 10.0)
    b.f.shellFeatures.add(si)
    s = b.sketch(b.dplane(zB), "Posts de vis")
    for x, z in screws:
        b.circle(s, x, zB, z, h["bossD"] / 2)
    cols = list(b.extrude(b.every(s), zR - w - zB, NEW).bodies)
    ri = Rp - w + 0.5                                              # columns terminate in the wall
    clip = b.revolve_rd("outil colonnes", [(0, zB - 1), ("L", (sph(zB - 1, ri), zB - 1)),
                                           ("A", (sph((zB + zR) / 2, ri), (zB + zR) / 2), (sph(zR - w + 0.5, ri), zR - w + 0.5)),
                                           ("L", (0, zR - w + 0.5))])
    for c in cols:
        b.combine(c, [clip], INTERSECT, keep=True)
    b.remove(clip)
    cols = [x for x in comp.bRepBodies if x.name not in ("head_bezel", "head_plate", "head_back")]
    b.combine(dome, cols, JOIN, keep=False)
    s = b.sketch(b.dplane(zB), 'Screw passage (dome)')
    for x, z in screws:
        b.circle(s, x, zB, z, h["screwD"] / 2)
    b.extrude(b.every(s), zR - zB + 1, CUT, [dome])
    s = b.sketch(b.dplane(zR), 'Screw well for M2×30 screws')
    for x, z in screws:
        b.circle(s, x, zR, z, STYLE["headScrewWellD"] / 2)
    b.extrude(b.every(s), -STYLE["headScrewWell"], CUT, [dome])
    s = b.sketch(b.dplane(zR), 'Groove in painted medallion')
    b.circle(s, 0, zR, 0, STYLE["medD"] / 2 + STYLE["medGrooveW"] / 2)
    b.circle(s, 0, zR, 0, STYLE["medD"] / 2 - STYLE["medGrooveW"] / 2)
    b.extrude(b.loops(s, 2), -STYLE["medGrooveT"], CUT, [dome])
    s = b.sketch(b.dplane(zR - w), "Logement de la LiPo")
    wi, li = h["lipoW"] / 2 + h["lipoFit"], h["lipoL"] / 2 + h["lipoFit"]
    wo, lo = wi + h["ribW"], li + h["ribW"]
    b.rect(s, (-wo, zR - w, -lo), (wo, zR - w, lo))
    b.rect(s, (-wi, zR - w, -li), (wi, zR - w, li))
    b.extrude(b.loops(s, 2), -STYLE["lipoWallH"], JOIN, [dome])      # walls along the entire height of the battery
    d0 = zR - w - STYLE["lipoWallH"]                                   # opening in the housing (side of PCB)
    s = b.sketch(b.dplane(d0), "Ergots de retenue de la LiPo")
    for sx in (1, -1):
        b.rect(s, (sx * (wi - STYLE["lipoLipIn"]), d0, -STYLE["lipoLipL"] / 2), (sx * (wi + 0.5), d0, STYLE["lipoLipL"] / 2))
    b.extrude(b.every(s), STYLE["lipoLipT"], JOIN, [dome])
    s = b.sketch(b.dplane(zR - w), 'LiPo battery cable notches')
    g = h["lipoWireGap"] / 2
    b.rect(s, (-g, zR - w, -lo - 0.5), (g, zR - w, -li + 0.5))
    b.extrude(b.loops(s, 1), -STYLE["lipoWallH"] - 0.5, CUT, [dome])
    meplat([dome], 'dome')

    # 6. medallion: part of the dome; the disk surrounding the groove is highlighted in gray (front view, colorized)

    # 7. headphones: right-side-aligned (X-axis) from the dome’s center, engraved, turned with earTilt facing backward, then attached to the dome (single printed piece).
    a0, ao, dd, rf = STYLE["earRoot"], Rp + STYLE["earOut"], STYLE["earD"], STYLE["earRound"]
    rad = dd / 2
    ro = rad - rf - STYLE["earRingInset"]
    ri_ = ro - STYLE["earRingW"]
    c45 = math.sqrt(0.5)
    holes = [(0.0, 0.0)]
    rr = pitch = STYLE["earHolePitch"]
    while rr <= ri_ - 0.8 + 0.01:
        n = max(6, int(round(2 * math.pi * rr / pitch)))
        holes += [(rr * math.cos(2 * math.pi * j / n), rr * math.sin(2 * math.pi * j / n)) for j in range(n)]
        rr += pitch
    ears = []
    for side, label in ((1, "right"), (-1, "left")):
        s = b.sketch(b.xy, "Headphone " + label)
        M = lambda a, r: s.modelToSketchSpace(b.pt(side * a, zB + r, 0))
        ln, ar = s.sketchCurves.sketchLines, s.sketchCurves.sketchArcs
        l1 = ln.addByTwoPoints(M(a0, 0), M(a0, rad))
        l2 = ln.addByTwoPoints(l1.endSketchPoint, M(ao - rf, rad))
        a1 = ar.addByThreePoints(l2.endSketchPoint, M(ao - rf + rf * c45, rad - rf + rf * c45), M(ao, rad - rf))
        l3 = ln.addByTwoPoints(a1.endSketchPoint, M(ao, 0))
        axis = ln.addByTwoPoints(l3.endSketchPoint, l1.startSketchPoint)
        ear = b.revolve(s, s.profiles.item(0), axis).bodies.item(0)
        ear.name = "head_headphone_" + label
        # outer face (x = ±ao): slight groove on the key painted, conical mesh; ‘inside’ = −side in x
        inward = -side * b.sX                                       # sign of extrusion distance inward
        face = b.xplane(side * ao)
        so = b.sketch(face, "Strip du headphone " + label)
        b.circle(so, side * ao, zB, 0, ro)
        b.circle(so, side * ao, zB, 0, ri_)
        b.extrude(b.loops(so, 2), inward * STYLE["earRingT"], CUT, [ear])   # groove bottom highlighted in orange (colorize)
        sg = b.sketch(face, "Grille du headphone " + label)
        for dy, dz in holes:
            b.circle(sg, side * ao, zB + dy, dz, STYLE["earHoleR"])
        ex = b.f.extrudeFeatures
        inp = ex.createInput(b.every(sg), CUT)
        inp.participantBodies = [ear]
        try:
            # cones tapering inward: Fusion calculates taper relative to the sketch normal; its sign reverses when extrusion is negative (observed: right helmet flared otherwise)
            neg = inward < 0
            inp.setOneSideExtent(adsk.fusion.DistanceExtentDefinition.create(VR(STYLE["earHoleDepth"] / 10.0)),
                                 adsk.fusion.ExtentDirections.NegativeExtentDirection if neg
                                 else adsk.fusion.ExtentDirections.PositiveExtentDirection,
                                 VS("{}{} deg".format("" if neg else "-", STYLE["earHoleTaper"])))
            ex.add(inp)
        except Exception:
            b.extrude(b.every(sg), inward * STYLE["earHoleDepth"], CUT, [ear])   # default: straight holes
        b.rotate_z([ear], side * b.sY * math.radians(STYLE["earTilt"]), (0, zB, 0))
        ears.append("head_headphone_" + label)
    # helmet piece with dome: retain the part outside the cavity and behind the plate, then join it to the dome; its axis (dome center → face center) is used for black painting (colorize)
    centre = b.pt(0, zB, 0)
    for name in ears:
        planes = [f for f in b.by_name(name).faces if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType]
        c = max(planes, key=lambda f: f.area).centroid
        headphone_axes.append(((centre.x, centre.y, centre.z), (c.x, c.y, c.z)))
    di = zR - w
    cav = b.revolve_rd('dome cavity tool', [(0, zB - 20), ("L", (Rp + 20, zB - 20)), ("L", (Rp + 20, zB)),
                                                ("L", (sph(zB, Rp - w), zB)),
                                                ("A", (sph((zB + di) / 2, Rp - w), (zB + di) / 2), (sph(di, Rp - w), di)),
                                                ("L", (0, di))])
    for name in ears:
        b.combine(b.by_name(name), [cav], CUT, keep=True)
    b.remove(cav)
    b.combine(b.by_name("head_back"), [b.by_name(n) for n in ears], JOIN, keep=False)

    # 8. non-printed glass (V2) and LiPo battery (enclosure size)
    s = b.sketch(b.dplane(0.0), 'Head glass')
    b.circle(s, 0, 0, 0, h["glassD"] / 2 - 0.05)
    v = b.extrude(b.loops(s, 1), h["glassT"] - 0.05, NEW).bodies.item(0)
    v.name = "glass_head"
    s = b.sketch(b.dplane(zR - w), 'LiPo battery head (V2)')
    b.rect(s, (-h["lipoW"] / 2, zR - w, -h["lipoL"] / 2), (h["lipoW"] / 2, zR - w, h["lipoL"] / 2))
    lp = b.extrude(b.loops(s, 1), -h["lipoT"], NEW).bodies.item(0)
    lp.name = 'LiPo battery head (V2)'


# ════════════════════════════════════════════════════════════════════════
#  TORSO
#  ════════════════════════════════════════════════════════════════════════

def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_torso(comp, ref, electronics=True):
    torso = load_module("MoodiMOS_Torso_robot", os.path.join(CAO, "MoodiMOS_Torso", "MoodiMOS_Torso.py"))
    p = dict(torso.PARAMS)
    p.update(ref["tp"])                                            # Settings (robot_style.py) included in the reference documentation
    torso.build(comp, p)                                           # Speaker enclosure, back panel, top housing
    if electronics:
        torso.encombrements(comp, p)
    b = Builder(comp)
    STYLE = ref["STYLE"]
    for old, new in (("Body", "torso_body"), ('Speaker grille (face panel)', "torso_front"), ("Back", "torso_back"), ('Speaker enclosure (HP)', "torso_speaker_box")):
        b.by_name(old).name = new
    body = b.by_name("torso_body")
    W, H = p["torsoW"], p["torsoH"]
    # Luminescent edge strips on sides: vertical groove, orange gasket filling it
    for side, label in ((1, "right"), (-1, "left")):
        x_in = side * (W / 2 - STYLE["ledT"])
        out = side * b.sX
        s = b.sketch(b.xplane(x_in), 'Edge strip groove' + label)
        b.rect(s, (x_in, STYLE["ledU"] - STYLE["ledW"] / 2, STYLE["ledZ0"]), (x_in, STYLE["ledU"] + STYLE["ledW"] / 2, STYLE["ledZ1"]))
        b.extrude(b.loops(s, 1), out * (STYLE["ledT"] + 1), CUT, [body])
        s = b.sketch(b.xplane(x_in), 'Edge strip gasket' + label)
        wj = STYLE["ledW"] / 2 - 0.1
        b.rect(s, (x_in, STYLE["ledU"] - wj, STYLE["ledZ0"] + 0.1), (x_in, STYLE["ledU"] + wj, STYLE["ledZ1"] - 0.1))
        j = b.extrude(b.loops(s, 1), out * STYLE["ledT"], NEW).bodies.item(0)
        j.name = "torso_strip_" + label
    # Black screen frame placed on the façade
    zG = -H / 2 + p["glassBot"] + p["glassH"] / 2
    zBand0, zCeil = zG + p["glassH"] / 2 + p["glassFit"], H / 2 - p["wallT"]
    zCam = (zBand0 + zCeil) / 2 + p["camOffZ"]
    s = b.sketch(b.xz, 'Screen frame')
    M = lambda a, c: b.pt(a, 0, c)
    torso.rounded_rect(s, M, 0, 0, p["glassW"] + 5, H - 8, 7)
    torso.rounded_rect(s, M, 0, zG, p["glassW"] + 0.4, p["glassH"] + 0.4, p["glassR"] + 0.2)
    b.circle(s, 0, 0, zCam, p["camD"] / 2)
    fr = b.extrude(b.loops(s, 3), -0.85, NEW).bodies.item(0)
    fr.name = "torso_frame"
    # Non-printed glass (tempered/laminated)
    s = b.sketch(b.dplane(0.05), 'Torso glass')
    M = lambda a, c: b.pt(a, 0.05, c)
    torso.rounded_rect(s, M, 0, zG, p["glassW"] - 0.1, p["glassH"] - 0.1, p["glassR"])
    v = b.extrude(b.loops(s, 1), p["glassT"] - 0.05, NEW).bodies.item(0)
    v.name = "glass_torso"


# Members (mesh breakdown), components, colors, and control specifications

LIMB_SKIP = ("head_", "torso_", "glass_", "component_")


def import_limbs(comp):
    identity = adsk.core.Matrix3D.create()
    files = sorted(f for f in os.listdir(STL_DIR) if f.lower().endswith(".stl") and not nfc(f).startswith(LIMB_SKIP))
    for f in files:
        stem = nfc(f[:-4])
        occ = comp.occurrences.addNewComponent(identity)
        occ.component.name = label(stem)
        base = occ.component.features.baseFeatures.add()
        base.startEdit()
        try:
            bodies = occ.component.meshBodies.add(os.path.join(STL_DIR, f), adsk.fusion.MeshUnits.MillimeterMeshUnit, base)
            for k in range(bodies.count):
                bodies.item(k).name = stem
        finally:
            base.finishEdit()
    return len(files)


NAMES = {
    "head_bezel": ("Bezel", "f"), "head_plate": ('Drop Plate', "f"), "head_back": ('Dome', "m"),
    "paint_bail": ('Painted Bearing', "f"), "paint_medallion": ('Painted medallion', "m"),
    'paint_trim_bezel': ('Painted bezel of the lens', "m"),
    "paint_strip_headphone": ("Strip peint du headphone", "m"),
    "torso_body": ('Torso body', "m"), "torso_front": ('Torso front panel', "f"), "torso_back": ('Torso back panel', "m"),
    "torso_frame": ('Screen frame', "m"),
    "torso_speaker_box": ('Speaker enclosure', "f"), "torso_strip": ('Luminescent bezel', "m"),
    "glass_head": ('Head glass', "f"), "glass_torso": ('Torso glass', "f"),
    "paint_marks": ("Index peints de la bezel", "mp"), "paint_screws": ('Painted head bolt heads', "fp"),
    "shoulder": ("Shoulder", "f"), "arm": ("Arm", "m"), "ring_arm": ("Ring de coude", "f"),
    "cuff": ("Bracelet", "m"), "glove": ("Poing", "m"), "trim_glove": ('Finger rings', "mp"),
    'wrist_glove': ('Ring de wrist', "f"), "leg": ("Leg", "f"), "ring_leg": ("Bagues de leg", "fp"),
    "trim": ("Semelle noire", "f"), 'sole': ("Intercalaire", "m"), 'toe': ("Bout", "m"),
    'upper': ("Empeigne", "f"), 'side': ('Rear panel', "m"), 'heel': ("Talon", "m"),
    'collar': ("Collerette", "f"), 'laces': ("Bride", "f"), 'roundel': ("Anneau lumineux", "m"),
    "vent_sneaker": ('Sneaker vents', "fp"), 'edge_toe': ('Trait du toe', "m"),
}
SIDE = {("right", "m"): "right", ("right", "f"): "right", ("right", "mp"): "droits", ("right", "fp"): "droites",
        ("left", "m"): "left", ("left", "f"): "left", ("left", "mp"): "gauches", ("left", "fp"): "gauches"}


def label(stem):
    kind, side = stem, None
    for sd in ("right", "left"):
        if stem.endswith("_" + sd):
            kind, side = stem[: -len(sd) - 1], sd
    name, g = NAMES.get(kind, (kind.replace("_", " ")[:1].upper() + kind.replace("_", " ")[1:], "m"))
    return name if side is None else "{} {}".format(name, SIDE[(side, g)])


def split_bodies(comp):
    'Each solid body in the component is automatically split into sub-components by name'
    identity = adsk.core.Matrix3D.create()
    for body in list(comp.bRepBodies):
        occ = comp.occurrences.addNewComponent(identity)
        occ.component.name = label(body.name)
        body.moveToComponent(occ)


def read_render_colors():
    tree = ast.parse(open(os.path.join(ROBOT_DIR, "robot_render.py"), encoding="utf-8").read())
    found = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") in ("COL", "PIECE_COL", "GLOW"):
            found[node.targets[0].id] = ast.literal_eval(node.value)
    return found.get("COL", {}), found.get("PIECE_COL", {}), found.get("GLOW", ())


def srgb(c):
    c = max(0.0, min(1.0, c))
    return int(round(255 * (12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055)))


def color_key(name, piece_col, glow):
    name = nfc(name)
    if name.startswith(glow):
        return "glow"
    if name.startswith("glass"):
        return "glass"
    for k in sorted(piece_col, key=len, reverse=True):
        if name == k or name.startswith(k + "_"):
            return piece_col[k]
    return "pcb"                                                   # Electronic components (enclosure dimensions)


def zone_area(polys):
    'Area (mm²) of a given zone defined by its contours (outermost contour, followed by holes)'
    a = [abs(sum(x0 * z1 - x1 * z0 for (x0, z0), (x1, z1) in zip(pl, pl[1:] + pl[:1]))) / 2 for pl in polys]
    return a[0] - sum(a[1:])


medallion_r = [27.0]   # Painted disk radius (retrieved via execution from reference)
ear_ring = [0.0, 0.0]  # Outer/inner radii of the groove bottom of the painted cup (headphone)
bezel_band = [0.0, 0.0]   # Painted outer band of the lens face (outer/inner radii)
bail_area = [0.0]         # Clear face painted area of the bevel (mm²)
headphone_axes = []            # Per helmet: (dome center, face center), in cm, for black painting
headphone_limits = [46.0, 14.3]  # Dome sphere radius (+ margin) and helmet footprint radius (mm)


def in_headphone(p):
    'Point (cm) on a helmet: outside the dome sphere and within the helmet’s envelope around its axis.'
    for c, f in headphone_axes:
        v = [(p.x - c[0]) * 10.0, (p.y - c[1]) * 10.0, (p.z - c[2]) * 10.0]
        a = [f[i] - c[i] for i in range(3)]
        na = math.sqrt(sum(x * x for x in a))
        a = [x / na for x in a]
        t = sum(v[i] * a[i] for i in range(3))
        rho = math.sqrt(max(sum(x * x for x in v) - t * t, 0.0))
        if t > 0 and rho < headphone_limits[1] and math.sqrt(sum(x * x for x in v)) > headphone_limits[0]:
            return True
    return False


def colorize(app, design):
    col, piece_col, glow = read_render_colors()
    base = None
    for lib in app.materialLibraries:
        try:
            aps = lib.appearances
        except Exception:
            continue
        for i in range(aps.count):
            if "Plastic" in aps.item(i).name or "Plastique" in aps.item(i).name:
                base = aps.item(i)
                break
        if base:
            break
    if base is None:
        return 'No plastic features: no applied colors'
    rgb = {k: tuple(srgb(c) for c in v[0]) for k, v in col.items()}
    rgb.update(glow=(255, 110, 20), glass=(12, 12, 14), pcb=(30, 34, 40))
    made = {}
    for key, (r, g, bl) in rgb.items():
        name = "MoodiMOS " + key
        ap = design.appearances.itemByName(name) or design.appearances.addByCopy(base, name)
        for prop in ap.appearanceProperties:
            if prop.objectType == adsk.core.ColorProperty.classType():
                try:
                    prop.value = adsk.core.Color.create(r, g, bl, 255)
                except Exception:
                    pass
        made[key] = ap
    n = 0
    for occ in design.rootComponent.allOccurrences:
        for body in list(occ.component.bRepBodies) + list(occ.component.meshBodies):
            try:
                body.appearance = made[color_key(body.name, piece_col, glow)]
                n += 1
            except Exception:
                pass
            if body.name == "head_back" and "black" in made and "glow" in made:   # Helmet made from a single dome piece
                ro, ri = ear_ring
                ring = math.pi * (ro * ro - ri * ri)
                for f in body.faces:
                    if not in_headphone(f.centroid):
                        continue
                    flat = f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType
                    key = "glow" if flat and abs(f.area * 100.0 - ring) < 0.05 * ring else "black"   # Painted groove: base of the slot in a spline (pas) assembly
                    try:
                        f.appearance = made[key]
                    except Exception:
                        pass
            if body.name == "head_bezel" and "glow" in made:          # Painted rim: flat outer band of the face
                ro, ri = bezel_band
                band = math.pi * (ro * ro - ri * ri)
                for f in body.faces:
                    if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType and abs(f.area * 100.0 - band) < 0.08 * band:
                        try:
                            f.appearance = made["glow"]
                        except Exception:
                            pass
            if body.name == "head_plate" and "grey" in made:           # Painted pinion: flat face within its slot
                for f in body.faces:
                    if (f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType
                            and abs(f.area * 100.0 - bail_area[0]) < 0.03 * bail_area[0]):
                        try:
                            f.appearance = made["grey"]
                        except Exception:
                            pass
            if body.name == "head_back" and "grey" in made:              # Painted medallion: flat disk within the slot
                for f in body.faces:
                    if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType:
                        r = medallion_r[0]
                        if abs(f.area * 100.0 - math.pi * r * r) < 0.03 * math.pi * r * r:
                            try:
                                f.appearance = made["grey"]
                            except Exception:
                                pass
    return 'Colors applied to {} bodies'.format(n)


def check_volumes(design, ref):
    'Fusion Volume of each constructed part / volume calculated by `robot_parts.py` (already verified).'
    expected = {nfc(k): v for k, v in ref["volumes_mm3"].items()}
    rows, bad = [], 0
    for occ in design.rootComponent.allOccurrences:
        for body in occ.component.bRepBodies:
            name = nfc(body.name)
            if name in expected and expected[name] > 0:
                v = body.volume * 1000.0
                e = (v - expected[name]) / expected[name] * 100.0
                ok = abs(e) <= 1.5
                bad += not ok
                rows.append("{} {} : {:.0f} mm³ (attendu {:.0f}, {:+.1f} %)".format("✓" if ok else "✗", label(name), v, expected[name], e))
    return bad, rows


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        ref_path = os.path.join(ROBOT_DIR, "fusion_reference.json")
        if not os.path.exists(ref_path) or not os.path.isdir(STL_DIR):
            ui.messageBox('Run Python generators (`robot_parts.py`, `robot_limbs.py`) first:\n{} or {} missing.'.format(ref_path, STL_DIR))
            return
        ref = json.load(open(ref_path, encoding="utf-8"))
        medallion_r[0] = ref["STYLE"]["medD"] / 2 - ref["STYLE"]["medGrooveW"] / 2
        style_ = ref["STYLE"]
        ear_ring[0] = style_["earD"] / 2 - style_["earRound"] - style_["earRingInset"]
        ear_ring[1] = ear_ring[0] - style_["earRingW"]
        bezel_band[0], bezel_band[1] = style_["rimR1"], style_["rimR0"] + style_["rimGrooveW"] / 2
        bail_area[0] = zone_area(ref["bail_peint"])
        headphone_axes.clear()
        headphone_limits[0], headphone_limits[1] = ref["head"]["Rp"] + 0.02, style_["earD"] / 2 + 0.3

        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        identity = adsk.core.Matrix3D.create()
        steps = []

        occT = root.occurrences.addNewComponent(identity)
        occT.component.name = 'Torso'
        build_torso(occT.component, ref)
        steps.append("torso")

        occH = root.occurrences.addNewComponent(identity)
        occH.component.name = 'Head'
        build_head(occH.component, ref)
        steps.append('head')

        occM = root.occurrences.addNewComponent(identity)
        occM.component.name = "Limbs"
        n_limbs = import_limbs(occM.component)
        steps.append("{} membres".format(n_limbs))

        bad, rows = check_volumes(design, ref)                     # before component separation (even if single body)
        for c in (occH.component, occT.component):
            split_bodies(c)

        # head placed on neck: center of the window at (0, head_dy, head_dz)
        sY = 1.0 if root.xZConstructionPlane.geometry.normal.y > 0 else -1.0
        m = adsk.core.Matrix3D.create()
        m.translation = adsk.core.Vector3D.create(0, sY * ref["head"]["head_dy"] / 10.0, ref["head"]["head_dz"] / 10.0)
        occH.transform = m
        design.snapshots.add()
        colors = colorize(app, design)
        app.activeViewport.fit()
        ui.messageBox('MoodiMOS built: {}\n{}\n\nVolume control (Fusion pieces / robot_parts.py): {}\n{}'.format(
            ", ".join(steps), colors, "tout concorde" if not bad else '{} clearance(s) > 1.5%'.format(bad), "\n".join(rows)))
    except Exception:
        if ui:
            ui.messageBox('Error:\n{}'.format(traceback.format_exc()))
