# MoodiMOS — Parametric head for Autodesk Fusion
# 
# Run: Utilities > Add-ons > Scripts and Add-ons > « + » (existing script)
# > Select the MoodiMOS_Head folder > Run.
# The script generates a new document with three bodies:
# 1. Front ring: tube Ø bezelD in steps (step1D → step2D on the face), glass pocket flush with a shoulder.
# 2. Central plate: droplet silhouette (bail), holds the round PCB, magnetic pogo connector below.
# 3. Curved back: closed shell, rear chamfer, LiPo 403040 (V2) cradle on the base (enclosure volume added via `encombrements()`; called by assembly).
# 
# Orientation (Fusion: Z up): Head top = +Z, screen face = -Y, front-to-back stacking = +Y.
# 
# Key dimensions: User parameters (Edit > Modify Parameters).
# Droplet shapes (tabD, tabR, plateExtra) loaded on launch; modify PARAMS below and rerun.
# 
# No camera in the head (decision 2026-09-28: XIAO in the torso).
# M2 PCB screws: Positions noted on Waveshare PCB (heights 20.36/61.06 from top edge); horizontal gaps estimated → recalibrate with a sliding caliper.
# 
# Full circular ring, no visible openings; thin-walled behind glass shoulder (lamage boreD) for USB-C, microSD reader, and PCB overhangs (USB-C → VBus via pogo at neck; 5V → OTA programming). PCB orientation: USB-C at bottom, no opening; wide spacers at top, narrow at bottom.
# 
# Assembly: 4 M2×20 screws from back → back columns → central plate → PCB spacers; ring clamped between glass and plate—no other fixes.

import adsk.core, adsk.fusion, traceback, math, os, struct

PARAMS = [
    # name, value, unit, comment
    ("bezelD",      82,   "mm", 'Outer diameter of the ring (3.5 visible around the glass)'),
    ("step1D",      79,   "mm", "gradin avant du bezel : Ø (paroi 1,8 autour de la poche de glass)"),
    ("step1T",      1.5,  "mm", "gradin avant : profondeur"),
    ("step2D",      80.5, "mm", "second gradin : Ø"),
    ("step2T",      3,    "mm", 'second grading depth from the façade'),
    ("backD",       78,   "mm", 'Curved back diameter (≥ 78 mm: screw columns for R 33.4 + wall)'),
    ("grooveD",     54,   "mm", 'Decorative groove on the back: Ø (dimension unspecified)'),
    ("grooveW",     1.2,  "mm", "rainure du back : largeur"),
    ("grooveT",     0.6,  "mm", "rainure du back : profondeur"),
    ("glassD",      75,   "mm", "Ø glass ronde (plan Waveshare 75,00 ±0,1)"),
    ("glassFit",    0.3,  "mm", "jeu radial autour de la glass (0,2 entrait en force en PLA+)"),
    ("glassT",      1.0,  "mm", "épaisseur glass seule (mesurée le 12/10/2026 : à peine 1 mm)"),
    ("ledgeD",      72.4, "mm", 'Internal diameter of the shoulder (LCD Ø 71.8 + radial clearance 0.3)'),
    ("frontT",      12,   "mm", 'front ring = glass face → end of M2 spacers (≈ 12, TO MEASURE)'),
    ("plateT",      9,    "mm", 'thickness of the central plate (pogo 4.2 + walls)'),
    ("backT",       11,   "mm", 'depth of the curved back (LiPo 403040: 4 mm, in its cradle)'),
    ("wallT",       2,    "mm", 'wall-thickness of the back (torso)'),
    ("backFillet",  9,    "mm", 'back curve (backT)'),
    ("plateOpenD",  54,   "mm", "ouverture centrale de la plate (LiPo 30 × 40 : diagonale 50)"),
    ("lipoW",       30,   "mm", "LiPo 403040 (≈ 500 mAh, V2) : largeur"),
    ("lipoL",       40,   "mm", "LiPo 403040 : longueur (hors fils)"),
    ("lipoT",       4,    "mm", 'LiPo 403040: thickness (inflation ≈ +0.3 mm at end of life)'),
    ("lipoFit",     0.4,  "mm", 'clearance around the LiPo in its cradle'),
    ("ribW",        1.2,  "mm", 'Cradle LiPo rib thickness (3 diameters)'),
    ("ribH",        3,    "mm", "nervures du berceau LiPo : hauteur depuis le fond du back"),
    ("lipoWireGap", 8,    "mm", 'notch for LiPo wires (bottom, near the neck; align facing the MX1.25 connector).'),
    ("pcbTop",      33,   "mm", 'glass center → top edge of PCB, reference for Waveshare dimensions (to MEASURE)'),
    ("sUpX",        29.8, "mm", 'interference plates (side) 20.36 mm: ½ pitch (measured pixel-wise on drawing, TO MEASURE)'),
    ("sLoX",        18.1, "mm", 'intermediate spacers (side 61.06): ½ pitch (measured pixel-wise on drawing, TO MEASURE)'),
    ("boardRot",    0,    "deg", 'Rotation of the PCB in the head (0: USB-C at bottom, image upright)'),
    ("boreD",       78,   "mm", 'rear lamination of the ring: Ø (USB-C at 38.3 mm from center, TF reader and its card, folded cable)'),
    ("boreD0",      3,    "mm", 'rear approach: start from the façade (window sill intact so far)'),
    ("screwD",      2.3,  "mm", "passage vis M2"),
    ("screwHeadD",  4.2,  "mm", 'Threading M2 machine screw head'),
    ("screwHeadT",  3,    "mm", 'depth of countersink at the back (M2×20 screw: 17 mm under head + 3 mm of thread)'),
    ("bossD",       7,    "mm", 'Back screw column'),
    ("holeD",       12,   "mm", 'ø bail hole'),
    ("flatH",       3,    "mm", 'Flat surface under the head (neck support pad)'),
    ("pogoL",       26.4, "mm", 'Internal magnetic pogo connector (5-pin, 2.5mm pitch) – ear canal (25.6mm too short)'),
    ("pogoBodyL",   21.4, "mm", "fente du nez dans la peau du méplat : longueur (20,6 trop court à la jauge du 13/10)"),
    ("pogoW",       4.6,  "mm", "largeur de la fente et du canal, bouts arrondis (jauge du 13/10 : 4,4 presque bon, un peu serré)"),
    ("pogoFaceT",   2.0,  "mm", 'flat skin height = nose magnet side height: facing flush, ears resting behind'),
    # launched (form)
    ("tabD",        52,   "mm", 'center-to-center head → center of the top of the bead (1.27 × R)'),
    ("tabR",        12.3, "mm", "rayon du haut de la goutte (0,30 R)"),
    ("plateExtra",  2,    "mm", 'Radial overhang of the plate on the flange (widest collar, rendered)'),
]

VI = adsk.core.ValueInput.createByString
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
        root = design.rootComponent
        um = design.unitsManager

        up = design.userParameters
        for name, val, unit, comment in PARAMS:
            up.add(name, VI("{} {}".format(val, unit)), unit, comment)

        def mm(expr):
            return um.evaluateExpression(expr, "mm") * 10.0

        def rad(expr):
            return um.evaluateExpression(expr, "rad")

        build(root, mm, rad)
        app.activeViewport.fit()
        report = export_stl(root, PIECES)
        ui.messageBox('MoodiMOS Head created: front ring, central plate, curved back with LiPo cradle.\nDimensions: Modify > Modify parameters.\n\nSTL print (oriented) in `{}`:\n{}`'.format(os.path.normpath(STL_DIR), "\n".join(report)))
    except Exception:
        if ui:
            ui.messageBox('Error:\n{}'.format(traceback.format_exc()))


# ---
# tools
# ---

def offset_plane(root, base, value):
    inp = root.constructionPlanes.createInput()
    inp.setByOffset(base, value)
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


def extrude(root, profiles, distance, op, bodies=None):
    ex = root.features.extrudeFeatures
    inp = ex.createInput(profiles, op)
    inp.setDistanceExtent(False, distance)
    if bodies:
        inp.participantBodies = bodies
    return ex.add(inp)


def circle_at_origin(sketch, dia_expr, mm):
    r = mm(dia_expr) / 20.0
    c = sketch.sketchCurves.sketchCircles.addByCenterRadius(sketch.originPoint, r)
    dim = sketch.sketchDimensions.addDiameterDimension(c, adsk.core.Point3D.create(r * 0.7, r * 0.7, 0))
    dim.parameter.expression = dia_expr
    return c


def circle_at(sketch, x, y, z, dia_expr, mm):
    r = mm(dia_expr) / 20.0
    c = sketch.sketchCurves.sketchCircles.addByCenterRadius(sketch.modelToSketchSpace(P3(x, y, z)), r)
    dim = sketch.sketchDimensions.addDiameterDimension(c, sketch.modelToSketchSpace(P3(x + 5, y, z + 5)))
    dim.parameter.expression = dia_expr
    return c


def nearest_point(points, target):
    return min(points, key=lambda sp: sp.geometry.distanceTo(target))


def planar_faces_along_y(body):
    faces = []
    for f in body.faces:
        if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType:
            if abs(f.geometry.normal.y) > 0.99:
                faces.append(f)
    return faces


# ── construction ──────────────────────────────────────────────────────

def build(root, mm, rad):
    sk = root.sketches
    xz = root.xZConstructionPlane
    xy = root.xYConstructionPlane

    R = mm("bezelD") / 2.0
    Rp = R + mm("plateExtra")

    # 1. Front bezel encircling: tube ledgeD / ledgeD on frontT
    s1 = sk.add(xz)
    s1.name = "Cerclage avant"
    circle_at_origin(s1, "bezelD", mm)
    circle_at_origin(s1, "ledgeD", mm)
    bezel = extrude(root, profile_with_loops(s1, 2), VI("frontT"), NEW).bodies.item(0)
    bezel.name = "Cerclage avant"

    # glass-level flush pocket *(shoulder remains between `ledgeD` and `glassD`)*
    s2 = sk.add(xz)
    s2.name = "Poche glass"
    circle_at_origin(s2, "glassD + 2 * glassFit", mm)
    circle_at_origin(s2, "ledgeD", mm)
    extrude(root, profile_with_loops(s2, 2), VI("glassT"), CUT, [bezel])

    # front steps (rendering: stacked metal rings)
    for name, d_out, d_in, depth in (("Gradin 2", "bezelD + 2", "step2D", "step2T"),
                                     ("Gradin 1", "step2D", "step1D", "step1T")):
        sg = sk.add(xz)
        sg.name = name
        circle_at_origin(sg, d_out, mm)
        circle_at_origin(sg, d_in, mm)
        extrude(root, profile_with_loops(sg, 2), VI(depth), CUT, [bezel])

    # rear panel: thin-walled section behind the bezel for anything protruding from the PCB
    # *(two USB-C ports at the bottom, microSD reader with its card at the top-left when viewed from the front, screen cable folded at the bottom)*
    sL = sk.add(offset_plane(root, xz, VI("boreD0")))
    sL.name = 'Rear damage'
    circle_at_origin(sL, "boreD", mm)
    circle_at_origin(sL, "ledgeD", mm)
    extrude(root, profile_with_loops(sL, 2), VI("frontT - boreD0 + 1 mm"), CUT, [bezel])

    # 2. Central teardrop plate
    planeP = offset_plane(root, xz, VI("frontT"))
    yP = planeP.geometry.origin.y * 10.0
    s4 = sk.add(planeP)
    s4.name = "Plate goutte"
    d, rt = mm("tabD"), mm("tabR")
    sn = (Rp - rt) / d
    cs = math.sqrt(1.0 - sn * sn)
    W = lambda x, z: s4.modelToSketchSpace(P3(x, yP, z))
    arcs = s4.sketchCurves.sketchArcs
    big = arcs.addByThreePoints(W(Rp * cs, Rp * sn), W(0, -Rp), W(-Rp * cs, Rp * sn))
    top = arcs.addByThreePoints(W(-rt * cs, d + rt * sn), W(0, d + rt), W(rt * cs, d + rt * sn))
    pts = [big.startSketchPoint, big.endSketchPoint, top.startSketchPoint, top.endSketchPoint]
    lines = s4.sketchCurves.sketchLines
    for side in (1, -1):
        a = nearest_point(pts, W(side * Rp * cs, Rp * sn))
        b = nearest_point(pts, W(side * rt * cs, d + rt * sn))
        lines.addByTwoPoints(a, b)
    circle_at(s4, 0, yP, d, "holeD", mm)
    circle_at_origin(s4, "plateOpenD", mm)
    plate = extrude(root, profile_with_loops(s4, 3), VI("plateT"), NEW).bodies.item(0)
    plate.name = "Plate centrale"

    # M2 screws for the PCB: they pass through back + PCB and tighten the entire stack.
    # Positions in the Waveshare plane reference (front view), then rotation of the PCB.
    rot = rad("boardRot")
    sPos = []
    for sx, h in (("sUpX", 20.36), ("sLoX", 61.06)):
        for side in (1, -1):
            x, z = side * mm(sx), mm("pcbTop") - h
            sPos.append((x * math.cos(rot) - z * math.sin(rot), x * math.sin(rot) + z * math.cos(rot)))
    s4b = sk.add(planeP)
    s4b.name = "Vis carte"
    for x, z in sPos:
        circle_at(s4b, x, yP, z, "screwD", mm)
    extrude(root, all_profiles(s4b), VI("plateT"), CUT, [plate])

    # 3. Curved back
    planeB = offset_plane(root, xz, VI("frontT + plateT"))
    yB = planeB.geometry.origin.y * 10.0
    s5 = sk.add(planeB)
    s5.name = "Back"
    circle_at_origin(s5, "backD", mm)
    back = extrude(root, profile_with_loops(s5, 1), VI("backT"), NEW).bodies.item(0)
    back.name = 'Concave back'

    faces = planar_faces_along_y(back)
    rear = max(faces, key=lambda f: abs(f.centroid.y * 10.0 - yB))
    edges = adsk.core.ObjectCollection.create()
    edges.add(rear.edges.item(0))
    fillets = root.features.filletFeatures
    fi = fillets.createInput()
    try:
        fi.edgeSetInputs.addConstantRadiusEdgeSet(edges, VI("backFillet"), True)
    except AttributeError:
        fi.addConstantRadiusEdgeSet(edges, VI("backFillet"), True)
    fillets.add(fi)

    # Flat surface beneath the head (resting on the cou), passing through the three parts.
    zb = -Rp
    planeF = offset_plane(root, xy, VR(zb / 10.0))
    s6 = sk.add(planeF)
    s6.name = 'Flat end (male) coupling end'
    s6.sketchCurves.sketchLines.addTwoPointRectangle(
        s6.modelToSketchSpace(P3(-R - 10, -120, zb)), s6.modelToSketchSpace(P3(R + 10, 120, zb)))
    zTopFlat = zb + mm("flatH")
    touched = [b for b, r in ((bezel, R), (plate, Rp), (back, mm("backD") / 2.0)) if r > -zTopFlat]
    extrude(root, profile_with_loops(s6, 1), VI("flatH"), CUT, touched)

    # internal-pogo-connector assembly:
    # nose inserted into the flat-side groove (thickness = nose, facing flush),
    # ears pressed against the skin behind it;
    # oblong channel to the central opening for insertion, wired soldered.
    zf = zb + mm("flatH")
    yMid = (yP + yB) / 2.0
    L, Wd, t = mm("pogoL"), mm("pogoW"), mm("pogoFaceT")
    zC = -math.sqrt((mm("plateOpenD") / 2.0) ** 2 - (L / 2.0) ** 2) + 1.0      # top of the channel, in the opening
    for name, Lo, z0, depth in (("Pogo, fente du nez", mm("pogoBodyL"), zf, t), ("Pogo, canal", L, zf + t, zC - zf - t)):
        s7 = sk.add(offset_plane(root, xy, VR(z0 / 10.0)))
        s7.name = name
        Lr = Lo - Wd                                          # oblong: rectangle with two rounded ends
        s7.sketchCurves.sketchLines.addTwoPointRectangle(
            s7.modelToSketchSpace(P3(-Lr / 2, yMid - Wd / 2, z0)), s7.modelToSketchSpace(P3(Lr / 2, yMid + Wd / 2, z0)))
        for side in (1, -1):
            s7.sketchCurves.sketchCircles.addByCenterRadius(s7.modelToSketchSpace(P3(side * Lr / 2, yMid, z0)), Wd / 20.0)
        extrude(root, all_profiles(s7), VR(depth / 10.0), CUT, [plate])

    # Back: open casing on the PCB side
    faces = planar_faces_along_y(back)
    front_face = min(faces, key=lambda f: abs(f.centroid.y * 10.0 - yB))
    oc = adsk.core.ObjectCollection.create()
    oc.add(front_face)
    shells = root.features.shellFeatures
    si = shells.createInput(oc, False)
    si.insideThickness = VI("wallT")
    shells.add(si)

    # Screw columns in the back (after the casing, otherwise they would be hollowed out) →
    # Back mounting screw studs (post-shell, else they’d be recessed)
    s10 = sk.add(planeB)
    s10.name = "Posts vis"
    for x, z in sPos:
        circle_at(s10, x, yB, z, "bossD", mm)
    extrude(root, all_profiles(s10), VI("backT - wallT"), JOIN, [back])
    s11 = sk.add(planeB)
    s11.name = "Passage vis back"
    for x, z in sPos:
        circle_at(s11, x, yB, z, "screwD", mm)
    extrude(root, all_profiles(s11), VI("backT"), CUT, [back])
    planeR = offset_plane(root, xz, VI("frontT + plateT + backT"))
    yR = planeR.geometry.origin.y * 10.0
    s12 = sk.add(planeR)
    s12.name = "Chambrages vis"
    for x, z in sPos:
        circle_at(s12, x, yR, z, "screwHeadD", mm)
    extrude(root, all_profiles(s12), VI("-screwHeadT"), CUT, [back])

    # decorative groove on the back plate
    s13 = sk.add(planeR)
    s13.name = "Rainure back"
    circle_at_origin(s13, "grooveD", mm)
    circle_at_origin(s13, "grooveD - 2 * grooveW", mm)
    extrude(root, profile_with_loops(s13, 2), VI("-grooveT"), CUT, [back])

    # 4. LiPo Cradle (V2): rib-frame frame on the back bottom; slot at the bottom for wires.
    # The LiPo is glued (double-sided adhesive) to it; on the PCB side, 14 mm clearance remains for components.
    planeL = offset_plane(root, xz, VI("frontT + plateT + backT - wallT"))
    yL = planeL.geometry.origin.y * 10.0
    s14 = sk.add(planeL)
    s14.name = "Berceau LiPo"
    wi, li = mm("lipoW") / 2.0 + mm("lipoFit"), mm("lipoL") / 2.0 + mm("lipoFit")
    wo, lo = wi + mm("ribW"), li + mm("ribW")
    for w, l in ((wo, lo), (wi, li)):
        s14.sketchCurves.sketchLines.addTwoPointRectangle(
            s14.modelToSketchSpace(P3(-w, yL, -l)), s14.modelToSketchSpace(P3(w, yL, l)))
    extrude(root, profile_with_loops(s14, 2), VI("-ribH"), JOIN, [back])
    s15 = sk.add(planeL)
    s15.name = "Encoche fils LiPo"
    g = mm("lipoWireGap") / 2.0
    s15.sketchCurves.sketchLines.addTwoPointRectangle(
        s15.modelToSketchSpace(P3(-g, yL, -lo - 0.5)), s15.modelToSketchSpace(P3(g, yL, -li + 0.5)))
    extrude(root, profile_with_loops(s15, 1), VI("-ribH"), CUT, [back])


def encombrements(root, mm):
    'Non-printed LiPo battery volume in its cradle; assembly calls for (rendered, visual inspection).'
    planeL = offset_plane(root, root.xZConstructionPlane, VI("frontT + plateT + backT - wallT"))
    yL = planeL.geometry.origin.y * 10.0
    s = root.sketches.add(planeL)
    s.name = 'LiPo battery head (V2)'
    w, l = mm("lipoW") / 2.0, mm("lipoL") / 2.0
    s.sketchCurves.sketchLines.addTwoPointRectangle(s.modelToSketchSpace(P3(-w, yL, -l)), s.modelToSketchSpace(P3(w, yL, l)))
    body = extrude(root, profile_with_loops(s, 1), VI("-lipoT"), NEW).bodies.item(0)
    body.name = 'LiPo battery head (V2)'
    return body

# Auto-export STL, oriented parts for printing —
# Mesh calculated in Fusion (extremely high quality), rotation around X to position the piece,
# binary STL file in mm. Pure Python: no external Fusion modules.
STL_DIR = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "MoodiMOS_Organic",
                       "print_stl", "fusion")


def export_stl(root, pieces):
    'pieces: `[(body_name, file_name, "top")]`; "top" = depth *u* (backward direction)\n    Mounts on the platform (front face down), otherwise it descends (rear face down).\n    Returns the report lines.'
    sY = 1.0 if root.xZConstructionPlane.geometry.normal.y > 0 else -1.0   # u = sY × y
    os.makedirs(STL_DIR, exist_ok=True)
    bodies = {b.name: b for b in root.bRepBodies}
    report = []
    for body_name, file_name, u_up in pieces:
        body = bodies.get(body_name)
        if body is None:
            report.append('`{}: body "{}"` not found'.format(file_name, body_name))
            continue
        calc = body.meshManager.createMeshCalculator()
        calc.setQuality(adsk.fusion.TriangleMeshQualityOptions.VeryHighQualityTriangleMesh)
        mesh = calc.calculate()
        c, idx = mesh.nodeCoordinatesAsDouble, mesh.nodeIndices
        s = sY if u_up else -sY           # rotation by s · 90° about x: (0, sY, 0) → ±z
        pts = []
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


# circumference: top face; plate: flat; back: opening onto the base plate
PIECES = [("Cerclage avant", "head_bezel.stl", False), ("Plate centrale", "head_plate.stl", False), ('Concave back', "head_back.stl", True)]
