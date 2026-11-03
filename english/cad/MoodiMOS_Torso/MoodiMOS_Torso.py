# MoodiMOS — parametric torso for Autodesk Fusion.
# 
# Run through Utilities > Add-Ins > Scripts and Add-Ins > + (existing script), select the
# MoodiMOS_Torso folder and click Run.
# The current design contains the body, separate front panel, back and speaker box:
# - Body and front: flush portrait 3.5-inch glass pocket and ledge, camera aperture above,
#   magnetic pogo neck collar, shoulder/leg sockets, lower USB-C opening and right-side PWR access.
#   Internally: OV5640 camera cradle, radar frame in the upper band and a separate BME280 bay
#   behind the left-side vents.
# - Back: centring lip, four screw columns, speaker grilles, and the BH1750 diffuser collar/opening.
# - Speaker box: closed speaker enclosure, glued to the back using its centring ring.
# The ESP32-S3-Touch-LCD-3.5-C board and glass are removed from their supplied case.
# Its OV5640 camera is relocated behind the upper-band aperture using a 24-contact, 0.5 mm-pitch
# FPC extension, adapter board and approximately 150 mm ribbon.
# encombrements() adds non-printed component envelopes for assembly inspection.
# Four M2×35 screws enter through the back and columns into the board standoffs; the board holds
# the glass on its ledge. The current separate front panel has its own mounting screws.
# 
# Coordinates with Z up: torso top is +Z, screen faces -Y, depth u starts at the front panel,
# and the origin is the front panel centre. The rear-view landscape Waveshare drawing is mirrored
# and rotated to put USB-C below and PWR / RESET / BOOT on the right when viewed from the front.
# Dimensions are read at launch: edit PARAMS below and rerun the script.

import adsk.core, adsk.fusion, traceback, math, os, struct

PARAMS = {
    # envelope
    "torsoW": 82,  "torsoH": 120.5, "torsoD": 48,  # 82: head and torso have the same width in the rendering; 120.5: internal upper band
                                                   # From 18.6 mm above the glass for the camera and radar.
    "cornerR": 6,       # rounded vertical edges (front view)
    "frontFillet": 4,   # edge recess of the façade (smooth render edges)
    "wallT": 2.5,       # shell walls
    "backT": 3,         # thickness of the back (torso)
    "backFillet": 2.5,  # cutting of the perimeter of the backside
    # 3.5" (Plan WaveShare) glass
    "glassW": 61, "glassH": 92.44, "glassR": 6, "glassT": 1.1, "glassFit": 0.2,
    "glassBot": 6.78,   # Facade beneath the glass; the top strip accommodates the camera.
    "winW": 56, "winH": 82,  # Remove the panel behind the glass, then measure and unmount the LCD module.
    # Mounting of the PCB (M2 holes at 48.50 × 72.00 mm centered on the glass)
    "standoffU": 11.5,  # glass face → end of spacers (plan: 11.50 overall)
    "holeX": 24.25, "holeZ": 36,
    "bossD": 7, "screwD": 2.3, "screwHeadD": 4.2,
    "screwHeadT": 4.5,  # threading: M2×35 bolt → 3 mm of thread in the spacer
    # rear lip
    "lipW": 1.5, "lipH": 1.5, "lipFit": 0.2,
    # Camera module OV5640 (24-pin kit):
    # - Lens assembly camModW × camModH, lens + sensor camModT inserted into camD housing.
    # - Placed between two mounting flanges, lens-side facing outward, adhesive-side down.
    "camD": 8,          # objective hole: barrel + 0.5 to 1 (to measure)
    "camModW": 8.5, "camModH": 8.5, "camModT": 6.5, "camFit": 0.3, "cradleT": 1.6,
    "camOffZ": 0,       # objective above (+) or below the center of the visor
    # adaptor PCB extension (sourcing map 24-pin: 20 × 24 × 3.5 mm): placed at the bottom of the band, left of the camera, just above the edge of the board; double-sided adhesive
    "fpcL": 20, "fpcW": 24, "fpcT": 3.5, "fpcX": -18.3, "fpcU": 15,   # 20 cm wide, 24 cm deep
    # PCF8574T I²C extender (DollaTek 54 × 15 pins, ~12 included):
    # Mounted backward on the torso, between the shoulders, behind the peg of the neck; double-sided adhesive tape.
    "pcfL": 54, "pcfH": 15, "pcfT": 12, "pcfU": 29.5,
    # Radar HLK-LD2410C (22 × 16) glued (double-sided adhesive) onto the façade, to the right of the camera.
    "radW": 22, "radH": 16, "radT": 3.5, "radFit": 0.3, "railT": 1.2,
    "radX0": 6.45,      # Left side of the radar: its joystick is the right wall of the camera cradle (13.3 mm up to the
                        # 16/10/2026: delayed to accommodate the faceplate screw bossing at top-right
                        # BME280 (≈ 13.5 × 10.5 mm): enclosed compartment behind the left flank vents, away from hot boards
    "bmeW": 13.5, "bmeH": 10.5, "bmeT": 3, "bmeBoxX": 5, "bmeWallT": 1.2,
    # BH1750 spherical (measured 2026-10-02: dome 25, bezel 28): dome protruding from the top of the torso, bezel pressed into an inner ring.
    "domeD": 25, "domeBaseD": 28, "domeZ": 32, "domeRingH": 3,
    # recovered speaker (to be measured): in a sealed box attached to the back, diaphragm facing the grilles.
    "spkD": 40, "spkT": 16, "spkX": 11, "spkZ": -8, "spkVol": 4.5, "boxT": 1.6,   # Shifted to the right: DuPont connectors on the left.
    # Connector 2×16 from the 3.5" board (photo Waveshare, verify): front view, mid-height, left side → Dupont female pins plugged in → reserved zone up to `hdrU` behind the faceplate.
    "hdrX": -17, "hdrZ": -6.6, "hdrL": 41, "hdrW": 5.1, "hdrU": 29,
    # Edge components of the PCB
    "usbU": 9.1, "usbW": 13, "usbH": 7,          # USB-C: 7.5 mm (back PCB) + 1.6 mm (to be measured)
    "pwrZ": 21.22, "pwrU": 9, "pwrD": 3.5,       # Power: 10.22" + 11.00" above the bottom of the window.
    # cou (same mounting as under the head)
    "neckD": 30, "neckH": 6, "neckU": 24.5,      # Head Ø 82 × 32 centered on the depth of the torso; Ø 30 (not 28).
                                                 # 1.8 mm wall thickness at the pogo connector ends
                                                 # magnetic pogo connector (5-pin Sylen, 2.5 mm pitch: 25 with ears, 20 without, 4 mm wide, 2 mm nose length):
                                                 # nose notch in a 2 mm skin at the top of the collar; ears in contact below; width to confirm.
    "pogoL": 26.4, "pogoBodyL": 21.4, "pogoW": 4.6, "pogoFaceT": 2.0,   # Measurement dated 13/10: 4.4 × 20.6 / 25.6 too tight
    # shoulders and legs (organic parts: cad/MoodiMOS_Organic):
    # - peg Ø 9 (magnetic)
    # - magnetic core Ø 6 × 3 (attached to the end of the peg, facing a magnet glued to the inner side of a 0.6mm skin)
    "shoulderDown": 18, "pegD": 9.4, "shoulderBossD": 16,   # hole Ø 9.4: peg Ø 9 pivoting and removable
    "limbMagD": 6.2, "limbMagT": 3.2, "skinT": 0.6,
    "shHoleDepth": 4.7, "legHoleDepth": 5.5,     # peg-hole depth
    "socketD": 18, "socketT": 1.5,              # shoulder joint: socket that houses the ball.
    # Separate front panel (printed back onto the base plate: shoulder of the glass without support), secured by 4 M2 × 8 threaded screws with domed heads (replacing old decorative screws): counterbore, passage hole, boss, and pre-drill.
    "screwDecoD": 4.4, "screwDecoT": 0.8,        # Fitting of the head (ISO 7380: Ø 3.5 × 1.3, protruding by 0.5)
    "faceScrewD": 2.4, "facePilotD": 1.8, "faceBossD": 5.0, "faceBossL": 8.0,
    "ventN": 5, "ventL": 14, "ventW": 2, "ventZ": -30, "ventPitch": 5,  # ears on the sides (Nano ventilated)
    "legX": 23, "legBossD": 16,
    # Speaker grille (back panel)
    "grilleW": 30, "grilleH": 2.4, "grilleN": 4, "grillePitch": 6,   # centered on the speaker (spkZ)
}

VR = adsk.core.ValueInput.createByReal
NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def P3(x, y, z):
    'World point (mm) → Fusion internal units (cm) → Point3D'
    return adsk.core.Point3D.create(x / 10.0, y / 10.0, z / 10.0)


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        build(design.rootComponent, PARAMS)
        app.activeViewport.fit()
        supports = support_collier(design.rootComponent, dict(PARAMS))
        report = export_stl(design.rootComponent, PIECES)                     # supports: only printing.
        for f in reversed(supports):                                          # Removed subsequently from the design.
            f.deleteMe()
        ui.messageBox('MoodiMOS torso created: body, back and speaker box.\nDimensions: edit PARAMS in the script, then rerun.\n\nPrint STLs (oriented) in {}:\n{}'.format(os.path.normpath(STL_DIR), "\n".join(report)))
    except Exception:
        if ui:
            ui.messageBox('Error:\n{}'.format(traceback.format_exc()))


# ---
# tools
# ---

def offset_plane(root, base, dist_mm):
    inp = root.constructionPlanes.createInput()
    inp.setByOffset(base, VR(dist_mm / 10.0))
    pl = root.constructionPlanes.add(inp)
    pl.isLightBulbOn = False
    return pl


def profile_with_loops(sketch, n):
    for p in sketch.profiles:
        if p.profileLoops.count == n:
            return p
    counts = [p.profileLoops.count for p in sketch.profiles]
    raise RuntimeError('Sketch « {} »: no profile at {} contours (found: {})'.format(sketch.name, n, counts))


def all_profiles(sketch):
    oc = adsk.core.ObjectCollection.create()
    for p in sketch.profiles:
        oc.add(p)
    return oc


def extrude(root, profiles, dist_mm, op, bodies=None):
    ex = root.features.extrudeFeatures
    inp = ex.createInput(profiles, op)
    inp.setDistanceExtent(False, VR(dist_mm / 10.0))
    if bodies:
        inp.participantBodies = bodies
    return ex.add(inp)


def extrude_sym(root, profiles, half_mm, op, bodies):
    'Extrusion on both sides of the plane: the direction of the normal is irrelevant.'
    ex = root.features.extrudeFeatures
    inp = ex.createInput(profiles, op)
    inp.setSymmetricExtent(VR(half_mm / 10.0), False)
    inp.participantBodies = bodies
    return ex.add(inp)


def nearest_point(points, target):
    return min(points, key=lambda sp: sp.geometry.distanceTo(target))


def rounded_rect(s, M, ca, cb, w, h, r):
    'Rounded rectangle centred on (ca, cb); M(a, b) returns a world Point3D in the sketch plane.'
    sp = lambda a, b: s.modelToSketchSpace(M(a, b))
    ax, bx = w / 2.0 - r, h / 2.0 - r
    k = r * math.sqrt(0.5)
    corners, ends = [], []
    for sa, sb in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        cx, cz = ca + sa * ax, cb + sb * bx
        pa, pb = (cx + sa * r, cz), (cx, cz + sb * r)   # vertical / horizontal side
        arc = s.sketchCurves.sketchArcs.addByThreePoints(sp(*pa), sp(cx + sa * k, cz + sb * k), sp(*pb))
        ends += [arc.startSketchPoint, arc.endSketchPoint]
        corners.append((pa, pb))
    lines = s.sketchCurves.sketchLines
    for i, j, side in ((0, 1, 1), (1, 2, 0), (2, 3, 1), (3, 0, 0)):
        lines.addByTwoPoints(nearest_point(ends, sp(*corners[i][side])),
                             nearest_point(ends, sp(*corners[j][side])))


def rect(s, M, a, b, w, h):
    sp = lambda a_, b_: s.modelToSketchSpace(M(a_, b_))
    s.sketchCurves.sketchLines.addTwoPointRectangle(sp(a - w / 2.0, b - h / 2.0), sp(a + w / 2.0, b + h / 2.0))


def circle(s, M, a, b, d):
    s.sketchCurves.sketchCircles.addByCenterRadius(s.modelToSketchSpace(M(a, b)), d / 20.0)


def teardrop(s, M, a, b, d, sens):
    'Drip circle Ø diameter d: point at r·√2 along axis b (depth u), on the side sens = ±1, connected by two 45° tangents. The lying cylinders (front shell printed downward) are printed without support:\n- Point toward the façade (sens -1) for a boss.\n- Tenon holes remain round (tenon pivots, held by its magnet).\nExtrude all_profiles.'
    r = d / 2.0
    circle(s, M, a, b, d)
    sp = lambda a_, b_: s.modelToSketchSpace(M(a_, b_))
    tip = sp(a, b + sens * r * math.sqrt(2.0))
    ln = s.sketchCurves.sketchLines
    for side in (1, -1):
        ln.addByTwoPoints(tip, sp(a + side * r / math.sqrt(2.0), b + sens * r / math.sqrt(2.0)))


def planar_faces_along_y(body):
    return [f for f in body.faces
            if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType and abs(f.geometry.normal.y) > 0.99]


def fillet_outer_loop(root, face, radius_mm):
    edges = adsk.core.ObjectCollection.create()
    for loop in face.loops:
        if loop.isOuter:
            for e in loop.edges:
                edges.add(e)
    fi = root.features.filletFeatures.createInput()
    try:
        fi.edgeSetInputs.addConstantRadiusEdgeSet(edges, VR(radius_mm / 10.0), True)
    except AttributeError:
        fi.addConstantRadiusEdgeSet(edges, VR(radius_mm / 10.0), True)
    root.features.filletFeatures.add(fi)


# ── construction ──────────────────────────────────────────────────────

def build(root, p):
    sk = root.sketches
    xz, xy, yz = root.xZConstructionPlane, root.xYConstructionPlane, root.yZConstructionPlane
    sY = 1.0 if xz.geometry.normal.y > 0 else -1.0
    sZ = 1.0 if xy.geometry.normal.z > 0 else -1.0
    sX = 1.0 if yz.geometry.normal.x > 0 else -1.0

    W, H, D = p["torsoW"], p["torsoH"], p["torsoD"]
    wt = p["wallT"]
    shellD = D - p["backT"]
    zG = -H / 2.0 + p["glassBot"] + p["glassH"] / 2.0          # Glass center (and PCB)
    # Internal headband above the glass: central camera module, objective offset by `camOffZ`
    zBand0, zCeil = zG + p["glassH"] / 2.0 + p["glassFit"], H / 2.0 - wt
    zX = (zBand0 + zCeil) / 2.0
    zCam = zX + p["camOffZ"]
    uMid = D / 2.0

    def uplane(u):                                    # front-panel plane at depth u
        pl = xz if u == 0 else offset_plane(root, xz, u)
        return pl, (lambda a, b: P3(a, sY * u, b))     # (x, z)

    def zplane(z):                                    # Horizontal plane at height z
        return offset_plane(root, xy, sZ * z), (lambda a, b: P3(a, sY * b, z))   # (x, u)

    def xplane(x):                                    # Side view at abscissa x
        return offset_plane(root, yz, sX * x), (lambda a, b: P3(x, sY * b, a))   # (z, u)

    def sketch(plane_M, name):
        s = sk.add(plane_M[0])
        s.name = name
        return s, plane_M[1]

    # 1. Shell: rounded block, face bevel, hollowed back side.
    s, M = sketch(uplane(0), "Body")
    rounded_rect(s, M, 0, 0, W, H, p["cornerR"])
    shell = extrude(root, profile_with_loops(s, 1), shellD, NEW).bodies.item(0)
    shell.name = "Body"
    front = min(planar_faces_along_y(shell), key=lambda f: abs(f.centroid.y))
    try:
        fillet_outer_loop(root, front, p["frontFillet"])
    except Exception:
        pass   # Optional leave: the casing remains valid without it.
    rear = max(planar_faces_along_y(shell), key=lambda f: abs(f.centroid.y))
    oc = adsk.core.ObjectCollection.create()
    oc.add(rear)
    si = root.features.shellFeatures.createInput(oc, False)
    si.insideThickness = VR(wt / 10.0)
    root.features.shellFeatures.add(si)

    # floating glass: rounded pocket R6, then shoulder cutout.
    fit = p["glassFit"]
    s, M = sketch(uplane(0), "Poche glass")
    rounded_rect(s, M, 0, zG, p["glassW"] + 2 * fit, p["glassH"] + 2 * fit, p["glassR"] + fit)
    extrude(root, profile_with_loops(s, 1), p["glassT"], CUT, [shell])
    s, M = sketch(uplane(0), "Ouverture LCD")
    rounded_rect(s, M, 0, zG, p["winW"], p["winH"], 2)
    extrude(root, profile_with_loops(s, 1), wt, CUT, [shell])

    # camera lens, centered in the top band/frame
    s, M = sketch(uplane(0), 'Camera objective')
    circle(s, M, 0, zCam, p["camD"])
    extrude(root, profile_with_loops(s, 1), wt, CUT, [shell])

    # M2 screws from the façade to the four corners (old decorative screws): counterbore the head, then proceed.
    s, M = sketch(uplane(0), 'Facade screw')
    # in flat zones: between the glass edge and the start of the façade recess.
    flat = W / 2.0 - p["frontFillet"]
    dx = (p["glassW"] / 2.0 + fit + flat) / 2.0
    zTopDeco = (zG + p["glassH"] / 2.0 + fit + H / 2.0 - p["frontFillet"]) / 2.0
    zBotDeco = zG - p["glassH"] / 2.0 + p["glassR"]           # at the start of the rounded edge of the glass.
    deco = [(sx * dx, zTopDeco) for sx in (1, -1)] + [(sx * dx, zBotDeco) for sx in (1, -1)]
    for x, z in deco:
        circle(s, M, x, z, p["screwDecoD"])
    extrude(root, all_profiles(s), p["screwDecoT"], CUT, [shell])
    s, M = sketch(uplane(0), 'Facade rivets')
    for x, z in deco:
        circle(s, M, x, z, p["faceScrewD"])
    extrude(root, all_profiles(s), wt, CUT, [shell])

    # ears on the sides
    n = int(p["ventN"])
    for side in (1, -1):
        s, M = sketch(xplane(side * (W / 2.0 - wt / 2.0)), 'Ears')
        for k in range(n):
            rect(s, M, p["ventZ"] + (k - (n - 1) / 2.0) * p["ventPitch"], uMid, p["ventW"], p["ventL"])
        extrude_sym(root, all_profiles(s), wt, CUT, [shell])

    # USB-C recharge port (bottom) and power push button (right side)
    s, M = sketch(zplane(-H / 2.0 + wt / 2.0), "USB-C")
    rect(s, M, 0, p["usbU"], p["usbW"], p["usbH"])
    extrude_sym(root, profile_with_loops(s, 1), wt, CUT, [shell])
    s, M = sketch(xplane(W / 2.0 - wt / 2.0), "PWR")
    circle(s, M, zG - p["glassH"] / 2.0 + p["pwrZ"], p["pwrU"], p["pwrD"])
    extrude_sym(root, profile_with_loops(s, 1), wt, CUT, [shell])

    # Neck collar: magnetic pogo connector installed from the inside; nose in the skin slot (facing outward), ears resting below; cavity opens inward toward the torso.
    s, M = sketch(zplane(H / 2.0), "Collier cou")
    circle(s, M, 0, p["neckU"], p["neckD"])
    extrude(root, profile_with_loops(s, 1), sZ * p["neckH"], JOIN, [shell])
    zTop = H / 2.0 + p["neckH"]
    zS = zTop - p["pogoFaceT"]
    for name, Lo, z0, depth in (("Pogo, fente du nez", p["pogoBodyL"], zTop, p["pogoFaceT"]),
                                ('Pogo pin, cavity', p["pogoL"], zS, p["neckH"] - p["pogoFaceT"] + wt + 1)):
        s, M = sketch(zplane(z0), name)
        Lr = Lo - p["pogoW"]                               # oblong: rounded ends like ears
        rect(s, M, 0, p["neckU"], Lr, p["pogoW"])
        for side in (1, -1):
            circle(s, M, side * Lr / 2.0, p["neckU"], p["pogoW"])
        extrude(root, all_profiles(s), -sZ * depth, CUT, [shell])

    # shoulders:
    # - socket for ball joint
    # - internal boss (protrusion)
    # - tenon (peg) hole
    # - internal magnet housing
    zSh = H / 2.0 - p["shoulderDown"]
    for side in (1, -1):
        s, M = sketch(xplane(side * W / 2.0), 'Shoulder ball joint')
        circle(s, M, zSh, uMid, p["socketD"])
        extrude(root, profile_with_loops(s, 1), -side * sX * p["socketT"], CUT, [shell])
        xFloor = W / 2.0 - p["socketT"]                        # bottom of the well
        xMag = xFloor - p["shHoleDepth"] - p["skinT"] - p["limbMagT"]   # inner face of the bossing
        s, M = sketch(xplane(side * (W / 2.0 - wt)), 'Shoulder {}'.format("right" if side > 0 else "left"))
        teardrop(s, M, zSh, uMid, p["shoulderBossD"], -1)      # Dripping bossing: without support during printing
        extrude(root, all_profiles(s), -side * sX * (W / 2.0 - wt - xMag), JOIN, [shell])
        s, M = sketch(xplane(side * xFloor), 'Shoulder tenon')
        circle(s, M, zSh, uMid, p["pegD"])                     # Round hole: the peg rotates, held by its magnet.
        extrude(root, profile_with_loops(s, 1), -side * sX * p["shHoleDepth"], CUT, [shell])
        s, M = sketch(xplane(side * xMag), 'Shoulder magnet')
        circle(s, M, zSh, uMid, p["limbMagD"])
        extrude(root, profile_with_loops(s, 1), side * sX * p["limbMagT"], CUT, [shell])

    # legs:
    # - bosses under the base,
    # - peg holes (male connectors),
    # - internal magnet housings.
    s, M = sketch(zplane(-H / 2.0 + wt), "Bossages jambes")
    for side in (1, -1):
        teardrop(s, M, side * p["legX"], uMid, p["legBossD"], -1)   # drop: no support material for printing
    legStack = p["legHoleDepth"] + p["skinT"] + p["limbMagT"]    # from the underside of the base
    extrude(root, all_profiles(s), sZ * (legStack - wt), JOIN, [shell])
    s, M = sketch(zplane(-H / 2.0), "Tenons jambes")
    for side in (1, -1):
        circle(s, M, side * p["legX"], uMid, p["pegD"])            # round hole: the peg can rotate
    extrude(root, all_profiles(s), sZ * p["legHoleDepth"], CUT, [shell])
    s, M = sketch(zplane(-H / 2.0 + legStack), "Aimants jambes")
    for side in (1, -1):
        circle(s, M, side * p["legX"], uMid, p["limbMagD"])
    extrude(root, all_profiles(s), -sZ * p["limbMagT"], CUT, [shell])

    def box(name, x0, x1, z0, z1, u0, u1, op, bodies):
        s, M = sketch(uplane(u0), name)
        rect(s, M, (x0 + x1) / 2.0, (z0 + z1) / 2.0, x1 - x0, z1 - z0)
        return extrude(root, profile_with_loops(s, 1), u1 - u0, op, bodies)

    # Interior of the shell (printed with it, facing the platform: everything pushes backward)
    # Camera cradle: two cheeks against the façade and ceiling, module slides objective into the hole,
    # napkin downward (no support or finger: adhesive holds it in place)
    xw = p["camModW"] / 2.0 + p["camFit"]
    uC1 = wt + p["camModT"] + p["camFit"]                    # backside of the module
    for side in (1, -1):
        x0, x1 = sorted((side * xw, side * (xw + p["cradleT"])))
        box('Camera cradle', x0, x1, zBand0 + 0.2, zCeil + 0.5, wt - 0.5, uC1, JOIN, [shell])
    # radar frame (left clearance, right rail, lower support), ceiling above
    xR0 = p["radX0"]
    xR1 = xR0 + p["radW"]
    box("Joue radar", xR0 - p["radFit"] - p["cradleT"], xR0 - p["radFit"], zBand0 + 0.2, zCeil + 0.5, wt - 0.5, wt + 2.0,
        JOIN, [shell])
    zR0 = zX - p["radH"] / 2.0
    uR = wt + 2.0
    box("Rail radar", xR1 + p["radFit"], xR1 + p["radFit"] + p["railT"], zBand0 + 0.2, zCeil + 0.5, wt - 0.5, uR, JOIN, [shell])
    box("Appui radar", xR0 - p["radFit"] - p["cradleT"], xR1 + p["radFit"] + p["railT"], zBand0 + 0.2, zR0 - p["radFit"], wt - 0.5, uR, JOIN, [shell])
    # BME280: open-faced toward the back, mounted on the left flank sensors.
    n = int(p["ventN"])
    zv = (n - 1) / 2.0 * p["ventPitch"] + p["ventW"] / 2.0 + 0.5
    xi, bw = -(W / 2.0 - wt), p["bmeWallT"]
    uB0, uB1 = uMid - p["ventL"] / 2.0 - 1.0, uMid + p["ventL"] / 2.0 + 1.0
    box("Compartiment BME280", xi - 0.5, xi + p["bmeBoxX"] + bw, p["ventZ"] - zv - bw, p["ventZ"] + zv + bw,
        uB0 - bw, uB1, JOIN, [shell])
    box("Logement BME280", xi, xi + p["bmeBoxX"], p["ventZ"] - zv, p["ventZ"] + zv, uB0, uB1 + 1, CUT, [shell])

    # fastening of face screws (body side), connected to the side via a rib, pre-drill M2 hole.
    s, M = sketch(uplane(wt), 'Facade bossing (blind rivet heads)')
    for x, z in deco:
        circle(s, M, x, z, p["faceBossD"])
        sx = 1.0 if x > 0 else -1.0
        x0, x1 = sorted((x, sx * (W / 2.0 - wt + 0.5)))
        rect(s, M, (x0 + x1) / 2.0, z, x1 - x0, 3.0)
    extrude(root, all_profiles(s), p["faceBossL"], JOIN, [shell])
    s, M = sketch(uplane(wt), 'Front bolt holes')
    for x, z in deco:
        circle(s, M, x, z, p["facePilotD"])
    extrude(root, all_profiles(s), p["faceBossL"] - 1.0, CUT, [shell])

    # Separate façade from the body at plane u = wt (joint at the rounded edge, same contour): printed backwards on the base plate;
    # the window ledge is constructed without support; the body is printed bottom-first.
    sb = root.features.splitBodyFeatures
    split = sb.add(sb.createInput(shell, offset_plane(root, xz, wt), True))
    parts = sorted((split.bodies.item(i) for i in range(split.bodies.count)),
                   key=lambda b: abs(b.physicalProperties.centerOfMass.y))
    parts[0].name, parts[-1].name = 'Speaker grille (face panel)', "Body"

    # 2. Back:
    # - plate, rear chamfer, centering lip
    s, M = sketch(uplane(shellD), "Back")
    rounded_rect(s, M, 0, 0, W, H, p["cornerR"])
    back = extrude(root, profile_with_loops(s, 1), p["backT"], NEW).bodies.item(0)
    back.name = "Back"
    rearB = max(planar_faces_along_y(back), key=lambda f: abs(f.centroid.y))
    try:
        fillet_outer_loop(root, rearB, p["backFillet"])
    except Exception:
        pass
    lf, lw = p["lipFit"], p["lipW"]
    ri = p["cornerR"] - wt - lf
    s, M = sketch(uplane(shellD), 'Lip')
    rounded_rect(s, M, 0, 0, W - 2 * (wt + lf), H - 2 * (wt + lf), ri)
    rounded_rect(s, M, 0, 0, W - 2 * (wt + lf + lw), H - 2 * (wt + lf + lw), max(ri - lw, 0.5))
    extrude(root, profile_with_loops(s, 2), -p["lipH"], JOIN, [back])

    # Screw columns: from the back to the interstice (spacer) of the PCB.
    holes = [(sx * p["holeX"], zG + sz * p["holeZ"]) for sx in (1, -1) for sz in (1, -1)]
    s, M = sketch(uplane(p["standoffU"]), "Posts vis")
    for x, z in holes:
        circle(s, M, x, z, p["bossD"])
    extrude(root, all_profiles(s), shellD - p["standoffU"], JOIN, [back])
    s, M = sketch(uplane(p["standoffU"]), "Passage vis")
    for x, z in holes:
        circle(s, M, x, z, p["screwD"])
    extrude(root, all_profiles(s), D - p["standoffU"], CUT, [back])
    s, M = sketch(uplane(D), "Chambrages vis")
    for x, z in holes:
        circle(s, M, x, z, p["screwHeadD"])
    extrude(root, all_profiles(s), -p["screwHeadT"], CUT, [back])

    # BH1750 dome hole and collar bezel
    s, M = sketch(uplane(shellD), "Ring BH1750")
    circle(s, M, 0, p["domeZ"], p["domeBaseD"] + 0.4)
    circle(s, M, 0, p["domeZ"], p["domeBaseD"] + 0.4 + 2 * 1.2)
    extrude(root, profile_with_loops(s, 2), -p["domeRingH"], JOIN, [back])
    s, M = sketch(uplane(shellD), "Trou BH1750")
    circle(s, M, 0, p["domeZ"], p["domeD"] + 0.4)
    extrude(root, profile_with_loops(s, 1), p["backT"], CUT, [back])

    # speaker: centering ring for mounting the casing on the back, then closing the casing (separate piece, glued)
    spkIn = p["spkD"] + 0.4
    boxOD = spkIn + 2 * p["boxT"]
    s, M = sketch(uplane(shellD), 'HP Enclosure Ring')
    circle(s, M, p["spkX"], p["spkZ"], boxOD + 0.4)
    circle(s, M, p["spkX"], p["spkZ"], boxOD + 0.4 + 2 * 1.2)
    extrude(root, profile_with_loops(s, 2), -2.0, JOIN, [back])
    depth = p["spkT"] + p["spkVol"]
    s, M = sketch(uplane(shellD), 'Speaker enclosure (HP)')
    circle(s, M, p["spkX"], p["spkZ"], boxOD)
    spk = extrude(root, profile_with_loops(s, 1), -(depth + p["boxT"]), NEW).bodies.item(0)
    spk.name = 'Speaker enclosure (HP)'
    s, M = sketch(uplane(shellD), "Volume HP")
    circle(s, M, p["spkX"], p["spkZ"], spkIn)
    extrude(root, profile_with_loops(s, 1), -depth, CUT, [spk])
    s, M = sketch(xplane(p["spkX"] + boxOD / 2.0), 'High-power cable passage')   # Side of the SPK connector on the PCB *(speaker wire, hole filled with glue)*
    circle(s, M, p["spkZ"], shellD - depth + 3, 3)
    extrude_sym(root, profile_with_loops(s, 1), p["boxT"] + 1, CUT, [spk])

    # speaker grilles
    s, M = sketch(uplane(shellD), "Grilles HP")
    n = int(p["grilleN"])
    for k in range(n):
        z = p["spkZ"] + (k - (n - 1) / 2.0) * p["grillePitch"]
        rect(s, M, p["spkX"], z, p["grilleW"], p["grilleH"])
    extrude(root, all_profiles(s), p["backT"], CUT, [back])


def encombrements(root, p):
    'Components (non-printed) volume data for verifying placement in assembly.'
    xz = root.xZConstructionPlane
    sY = 1.0 if xz.geometry.normal.y > 0 else -1.0
    W, H, wt = p["torsoW"], p["torsoH"], p["wallT"]
    shellD = p["torsoD"] - p["backT"]
    zG = -H / 2.0 + p["glassBot"] + p["glassH"] / 2.0
    zBand0, zCeil = zG + p["glassH"] / 2.0 + p["glassFit"], H / 2.0 - wt
    zX = (zBand0 + zCeil) / 2.0
    uMid = p["torsoD"] / 2.0

    def plane(u):
        if u == 0:
            return xz
        return offset_plane(root, xz, u)

    def body(name, u0, u1, draw):
        s = root.sketches.add(plane(u0))
        s.name = name
        draw(s, lambda a, b: P3(a, sY * u0, b))
        b = extrude(root, profile_with_loops(s, 1), u1 - u0, NEW).bodies.item(0)
        b.name = name
        return b

    def cbox(name, xc, zc, w, h, u0, u1):
        return body(name, u0, u1, lambda s, M: rect(s, M, xc, zc, w, h))

    def ccyl(name, xc, zc, d, u0, u1):
        return body(name, u0, u1, lambda s, M: circle(s, M, xc, zc, d))

    cbox("Carte 3,5\"", 0, zG, p["glassW"] - 1, p["glassH"] - 1, p["glassT"], p["standoffU"])
    cbox('OV5640 camera module', 0, zX + p["camOffZ"], p["camModW"], p["camModH"], wt, wt + p["camModT"])
    cbox("Adaptateur FPC", p["fpcX"], zBand0 + 0.2 + p["fpcT"] / 2.0, p["fpcL"], p["fpcT"],
         p["fpcU"] - p["fpcW"] / 2.0, p["fpcU"] + p["fpcW"] / 2.0)
    cbox("PCF8574", 0, zCeil - 0.3 - p["pcfH"] / 2.0, p["pcfL"], p["pcfH"], p["pcfU"], p["pcfU"] + p["pcfT"])
    cbox("Radar LD2410C", p["radX0"] + p["radW"] / 2.0, zX, p["radW"], p["radH"], wt, wt + p["radT"])
    cbox("BME280", -(W / 2.0 - wt) + 0.3 + p["bmeT"] / 2.0, p["ventZ"], p["bmeT"], p["bmeW"],
         uMid - p["bmeH"] / 2.0, uMid + p["bmeH"] / 2.0)
    ccyl("Haut-parleur", p["spkX"], p["spkZ"], p["spkD"], shellD - p["spkT"], shellD)
    cbox("Fiches Dupont", p["hdrX"], p["hdrZ"], p["hdrW"] + 2, p["hdrL"], p["standoffU"] - 4, p["hdrU"])
    ccyl("BH1750 collerette", 0, p["domeZ"], p["domeBaseD"], shellD - 1.6, shellD)
    dome = ccyl("BH1750 boule", 0, p["domeZ"], p["domeD"], shellD, p["torsoD"] + p["domeD"] / 2.0)
    try:
        far = max(planar_faces_along_y(dome), key=lambda f: abs(f.centroid.y))
        fillet_outer_loop(root, far, p["domeD"] / 2.0 - 0.2)
    except Exception:
        pass   # Cylindrical dome left if chamfer fails
    cbox("Pogo", 0, H / 2.0 + p["neckH"] - 2.0, p["pogoL"], 4.0, p["neckU"] - p["pogoW"] / 2.0, p["neckU"] + p["pogoW"] / 2.0)   # 4-pin connector

# Auto-export STL, oriented parts for printing —
# Mesh calculated in Fusion (extremely high quality), rotation around X to position the piece,
# binary STL file in mm. Pure Python: no external Fusion modules.
STL_DIR = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "MoodiMOS_Organic",
                       "print_stl", "fusion")


def support_collier(root, p):
    'Print only: detachable base under the neck ring (laid flat; front face has a false edge; the body is printed bottom-first, joined with the façade on the platform). Three rib flanges of 0.8 mm on a 1 mm base, 0.2 mm below the cylinder where its slope exceeds 60° (|x| ≤ 9), 0.4 mm above the torso. Return functions to delete after export.'
    xy = root.xYConstructionPlane
    sZ = 1.0 if xy.geometry.normal.z > 0 else -1.0
    sY = 1.0 if root.xZConstructionPlane.geometry.normal.y > 0 else -1.0
    r, g, xs, wt = p["neckD"] / 2.0, 0.2, 9.0, p["wallT"]
    z0, z1 = p["torsoH"] / 2.0 + 0.4, p["torsoH"] / 2.0 + p["neckH"]
    feats, body = [], None

    def plane(z):
        return offset_plane(root, xy, sZ * z)

    for k in range(3):
        zf = z0 + k * (z1 - 0.8 - z0) / 2.0
        s = root.sketches.add(plane(zf))
        s.name = "Support collier (impression)"
        M = lambda a, b: P3(a, sY * b, zf)
        pts = [(-xs, wt), (xs, wt)] + [(x, p["neckU"] - math.sqrt(r * r - x * x) - g)
                                         for x in [xs - i * 0.5 for i in range(int(2 * xs / 0.5) + 1)]]
        ln = s.sketchCurves.sketchLines
        sp = [s.modelToSketchSpace(M(a, b)) for a, b in pts]
        for i in range(len(sp)):
            ln.addByTwoPoints(sp[i], sp[(i + 1) % len(sp)])
        f = extrude(root, profile_with_loops(s, 1), sZ * 0.8, NEW if body is None else JOIN, None if body is None else [body])
        if body is None:
            body = f.bodies.item(0)
            body.name = "Support collier"
        feats.append(f)
    s = root.sketches.add(plane(z0))
    s.name = 'Support collier, sole (impression)'
    rect(s, lambda a, b: P3(a, sY * b, z0), 0, wt + 0.5, 2 * xs, 1.0)
    feats.append(extrude(root, profile_with_loops(s, 1), sZ * (z1 - z0), JOIN, [body]))
    return feats


def export_stl(root, pieces):
    'pieces: `[(body_part_or_tuple_of_names_in_same_file, filename, "top")]`; "top" = depth u (rearward) increases on the baseplate (front face down), otherwise it decreases (back face down).\nReturns the report lines.'
    sY = 1.0 if root.xZConstructionPlane.geometry.normal.y > 0 else -1.0   # u = sY × y
    os.makedirs(STL_DIR, exist_ok=True)
    bodies = {b.name: b for b in root.bRepBodies}
    report = []
    for body_names, file_name, u_up in pieces:
        names = body_names if isinstance(body_names, tuple) else (body_names,)
        missing = [n for n in names if n not in bodies]
        if missing:
            report.append('`{}: body "{}"` not found'.format(file_name, ", ".join(missing)))
            continue
        s = sY if u_up else -sY           # rotation by s · 90° about x: (0, sY, 0) → ±z
        pts, idx = [], []
        for name in names:
            calc = bodies[name].meshManager.createMeshCalculator()
            calc.setQuality(adsk.fusion.TriangleMeshQualityOptions.VeryHighQualityTriangleMesh)
            mesh = calc.calculate()
            c, base = mesh.nodeCoordinatesAsDouble, len(pts)
            idx += [base + i for i in mesh.nodeIndices]
            for i in range(0, len(c), 3):
                x, y, z = c[i] * 10.0, c[i + 1] * 10.0, c[i + 2] * 10.0
                pts.append((x, -s * z, s * y))
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        off = ((lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0, lo[2])
        pts = [(p[0] - off[0], p[1] - off[1], p[2] - off[2]) for p in pts]
        n_tri = len(idx) // 3
        with open(os.path.join(STL_DIR, file_name), "wb") as fh:
            fh.write(b"MoodiMOS Fusion".ljust(80, b" "))
            fh.write(struct.pack("<I", n_tri))
            for t in range(n_tri):
                a, b, d = pts[idx[3 * t]], pts[idx[3 * t + 1]], pts[idx[3 * t + 2]]
                ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
                vx, vy, vz = d[0] - a[0], d[1] - a[1], d[2] - a[2]
                nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
                ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                fh.write(struct.pack("<12fH", nx / ln, ny / ln, nz / ln, *a, *b, *d, 0))
        report.append("{} : {:.1f} × {:.1f} × {:.1f} mm, {} triangles".format(
            file_name, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], n_tri))
    return report


# shell:
# - front: face on the platform;
# - back: exterior face on the platform;
# - HP enclosure: closed bottom panel.
PIECES = [(("Body", "Support collier"), "torso_body.stl", True), ('Speaker grille (face panel)', "torso_front.stl", False),
          ("Back", "torso_back.stl", False),
          ('Speaker enclosure (HP)', "torso_speaker_box.stl", True)]
