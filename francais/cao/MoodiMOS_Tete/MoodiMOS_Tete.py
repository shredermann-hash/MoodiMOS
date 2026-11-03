# MoodiMOS — tête paramétrique pour Autodesk Fusion
#
# Lancer : Utilitaires > Compléments > Scripts et compléments > « + » (script existant)
#          > choisir ce dossier MoodiMOS_Tete > Exécuter.
# Le script crée un nouveau document avec trois corps :
#   1. Cerclage avant  : tube Ø bezelD en gradins (Ø step1D puis step2D en façade), poche de vitre
#                        affleurante + épaulement
#   2. Plaque centrale : silhouette en goutte (bélière), porte la carte ronde, connecteur pogo magnétique dessous
#   3. Dos bombé       : coque fermée, congé arrière, berceau de la LiPo 403040 (V2) sur le fond
# encombrements() ajoute le volume de la LiPo (non imprimé) ; l'assemblage l'appelle.
# Orientation (réglage Fusion « Z vers le haut ») : haut de la tête = +Z, face écran vers -Y,
# empilement avant → arrière selon +Y.
#
# Les cotes principales sont des paramètres utilisateur : Modifier > Modifier les paramètres.
# Les formes de la goutte (tabD, tabR, plateExtra) sont lues au lancement :
# pour les changer, modifier PARAMS ci-dessous et relancer.
#
# Pas de caméra dans la tête (décision 2026-09-28 : la XIAO est dans le torse).
# Vis M2 de la carte : positions relevées sur le plan Waveshare (hauteurs 20,36 / 61,06
# depuis le bord haut du PCB), écarts horizontaux estimés → à recaler au pied à coulisse.
# Cerclage entièrement rond, sans aucune ouverture visible ; paroi amincie derrière l'épaulement de la vitre (lamage
# boreD) pour les USB-C, le lecteur microSD et la nappe qui débordent du PCB : recharge par le pogo du cou (5 V → VBus),
# programmation dos ouvert ou OTA. Carte dans son sens d'origine (image à l'endroit) :
# USB-C en bas, sans ouverture ; entretoises larges en haut, étroites en bas.
# Assemblage : 4 vis M2×20 par le dos → colonnes du dos → plaque → entretoises de la carte ;
# le cerclage est serré entre la vitre et la plaque, aucune autre fixation.

import adsk.core, adsk.fusion, traceback, math, os, struct

PARAMS = [
    # nom,          valeur, unité, commentaire
    ("bezelD",      82,   "mm", "Ø extérieur du cerclage (3,5 visibles autour de la vitre)"),
    ("step1D",      79,   "mm", "gradin avant du cerclage : Ø (paroi 1,8 autour de la poche de vitre)"),
    ("step1T",      1.5,  "mm", "gradin avant : profondeur"),
    ("step2D",      80.5, "mm", "second gradin : Ø"),
    ("step2T",      3,    "mm", "second gradin : profondeur depuis la façade"),
    ("backD",       78,   "mm", "Ø du dos bombé (≥ 78 : colonnes de vis à r 33,4 + paroi)"),
    ("grooveD",     54,   "mm", "rainure décorative du dos : Ø"),
    ("grooveW",     1.2,  "mm", "rainure du dos : largeur"),
    ("grooveT",     0.6,  "mm", "rainure du dos : profondeur"),
    ("glassD",      75,   "mm", "Ø vitre ronde (plan Waveshare 75,00 ±0,1)"),
    ("glassFit",    0.3,  "mm", "jeu radial autour de la vitre (0,2 entrait en force en PLA+)"),
    ("glassT",      1.0,  "mm", "épaisseur vitre seule (mesurée le 12/10/2026 : à peine 1 mm)"),
    ("ledgeD",      72.4, "mm", "Ø intérieur de l'épaulement (LCD Ø 71,8 + jeu radial 0,3)"),
    ("frontT",      12,   "mm", "cerclage avant = face vitre → bout des entretoises M2 (≈ 12, À MESURER)"),
    ("plateT",      9,    "mm", "épaisseur de la plaque centrale (pogo 4,2 + parois)"),
    ("backT",       11,   "mm", "profondeur du dos bombé (LiPo 403040 : 4 mm, dans son berceau)"),
    ("wallT",       2,    "mm", "épaisseur de paroi du dos"),
    ("backFillet",  9,    "mm", "congé arrière du dos bombé (< backT)"),
    ("plateOpenD",  54,   "mm", "ouverture centrale de la plaque (LiPo 30 × 40 : diagonale 50)"),
    ("lipoW",       30,   "mm", "LiPo 403040 (≈ 500 mAh, V2) : largeur"),
    ("lipoL",       40,   "mm", "LiPo 403040 : longueur (hors fils)"),
    ("lipoT",       4,    "mm", "LiPo 403040 : épaisseur (gonfle ≈ +0,3 en fin de vie)"),
    ("lipoFit",     0.4,  "mm", "jeu autour de la LiPo dans son berceau"),
    ("ribW",        1.2,  "mm", "nervures du berceau LiPo : épaisseur (3 périmètres)"),
    ("ribH",        3,    "mm", "nervures du berceau LiPo : hauteur depuis le fond du dos"),
    ("lipoWireGap", 8,    "mm", "encoche des fils de la LiPo (en bas, côté cou ; à placer face à la prise MX1.25)"),
    ("pcbTop",      33,   "mm", "centre vitre → bord haut du PCB, repère des cotes Waveshare (À MESURER)"),
    ("sUpX",        29.8, "mm", "entretoises côté 20,36 : ½ entraxe (relevé au pixel sur plan, À MESURER)"),
    ("sLoX",        18.1, "mm", "entretoises côté 61,06 : ½ entraxe (relevé au pixel sur plan, À MESURER)"),
    ("boardRot",    0,    "deg", "rotation de la carte dans la tête (0 : USB-C en bas, image à l'endroit)"),
    ("boreD",       78,   "mm", "lamage arrière du cerclage : Ø (USB-C à 38,3 du centre, lecteur TF et sa carte, nappe pliée)"),
    ("boreD0",      3,    "mm", "lamage arrière : départ depuis la façade (épaulement de la vitre intact jusque-là)"),
    ("screwD",      2.3,  "mm", "passage vis M2"),
    ("screwHeadD",  4.2,  "mm", "chambrage tête de vis M2"),
    ("screwHeadT",  3,    "mm", "profondeur du chambrage au dos (vis M2×20 : 17 sous tête + 3 de filet)"),
    ("bossD",       7,    "mm", "colonnes des vis dans le dos"),
    ("holeD",       12,   "mm", "Ø trou de bélière"),
    ("flatH",       3,    "mm", "méplat sous la tête (assise du cou)"),
    ("pogoL",       26.4, "mm", "connecteur pogo magnétique monté par l'intérieur (Sylen 5 broches 2,5) : canal des oreilles (25,6 trop court)"),
    ("pogoBodyL",   21.4, "mm", "fente du nez dans la peau du méplat : longueur (20,6 trop court à la jauge du 13/10)"),
    ("pogoW",       4.6,  "mm", "largeur de la fente et du canal, bouts arrondis (jauge du 13/10 : 4,4 presque bon, un peu serré)"),
    ("pogoFaceT",   2.0,  "mm", "peau du méplat = hauteur du nez côté aimants : face au ras, oreilles en appui derrière"),
    # lus au lancement (forme)
    ("tabD",        52,   "mm", "entraxe centre tête → centre du haut de la goutte (1,27 R)"),
    ("tabR",        12.3, "mm", "rayon du haut de la goutte (0,30 R)"),
    ("plateExtra",  2,    "mm", "débord radial de la plaque sur le cerclage (collerette la plus large, rendu)"),
]

VI = adsk.core.ValueInput.createByString
VR = adsk.core.ValueInput.createByReal
NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def P3(x, y, z):
    """Point monde en mm → Point3D en cm (unités internes Fusion)."""
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
        ui.messageBox("Tête MoodiMOS créée : cerclage avant, plaque centrale, dos bombé avec berceau de LiPo.\n"
                      "Cotes : Modifier > Modifier les paramètres.\n\n"
                      "STL d'impression (orientés) dans {} :\n{}".format(os.path.normpath(STL_DIR), "\n".join(report)))
    except Exception:
        if ui:
            ui.messageBox("Erreur :\n{}".format(traceback.format_exc()))


# ── outils ────────────────────────────────────────────────────────────

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
    raise RuntimeError("Esquisse « {} » : aucun profil à {} contours (trouvés : {})".format(sketch.name, n, counts))


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

    # 1. Cerclage avant : tube bezelD / ledgeD sur frontT
    s1 = sk.add(xz)
    s1.name = "Cerclage avant"
    circle_at_origin(s1, "bezelD", mm)
    circle_at_origin(s1, "ledgeD", mm)
    bezel = extrude(root, profile_with_loops(s1, 2), VI("frontT"), NEW).bodies.item(0)
    bezel.name = "Cerclage avant"

    # poche de vitre affleurante (l'épaulement reste entre ledgeD et glassD)
    s2 = sk.add(xz)
    s2.name = "Poche vitre"
    circle_at_origin(s2, "glassD + 2 * glassFit", mm)
    circle_at_origin(s2, "ledgeD", mm)
    extrude(root, profile_with_loops(s2, 2), VI("glassT"), CUT, [bezel])

    # gradins de façade du cerclage (rendu : anneaux métalliques étagés)
    for name, d_out, d_in, depth in (("Gradin 2", "bezelD + 2", "step2D", "step2T"),
                                     ("Gradin 1", "step2D", "step1D", "step1T")):
        sg = sk.add(xz)
        sg.name = name
        circle_at_origin(sg, d_out, mm)
        circle_at_origin(sg, d_in, mm)
        extrude(root, profile_with_loops(sg, 2), VI(depth), CUT, [bezel])

    # lamage arrière : paroi amincie derrière l'épaulement de la vitre, pour tout ce qui déborde du PCB
    # (deux USB-C en bas, lecteur microSD et sa carte en haut à gauche vu de face, nappe de l'écran pliée en bas)
    sL = sk.add(offset_plane(root, xz, VI("boreD0")))
    sL.name = "Lamage arrière"
    circle_at_origin(sL, "boreD", mm)
    circle_at_origin(sL, "ledgeD", mm)
    extrude(root, profile_with_loops(sL, 2), VI("frontT - boreD0 + 1 mm"), CUT, [bezel])

    # 2. Plaque centrale en goutte
    planeP = offset_plane(root, xz, VI("frontT"))
    yP = planeP.geometry.origin.y * 10.0
    s4 = sk.add(planeP)
    s4.name = "Plaque goutte"
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
    plate.name = "Plaque centrale"

    # vis M2 de la carte : elles traversent dos + plaque et serrent tout l'empilement
    # positions dans le repère du plan Waveshare (vue de face), puis rotation de la carte
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

    # 3. Dos bombé
    planeB = offset_plane(root, xz, VI("frontT + plateT"))
    yB = planeB.geometry.origin.y * 10.0
    s5 = sk.add(planeB)
    s5.name = "Dos"
    circle_at_origin(s5, "backD", mm)
    back = extrude(root, profile_with_loops(s5, 1), VI("backT"), NEW).bodies.item(0)
    back.name = "Dos bombé"

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

    # méplat sous la tête (assise sur le cou), à travers les trois pièces
    zb = -Rp
    planeF = offset_plane(root, xy, VR(zb / 10.0))
    s6 = sk.add(planeF)
    s6.name = "Méplat cou"
    s6.sketchCurves.sketchLines.addTwoPointRectangle(
        s6.modelToSketchSpace(P3(-R - 10, -120, zb)), s6.modelToSketchSpace(P3(R + 10, 120, zb)))
    zTopFlat = zb + mm("flatH")
    touched = [b for b, r in ((bezel, R), (plate, Rp), (back, mm("backD") / 2.0)) if r > -zTopFlat]
    extrude(root, profile_with_loops(s6, 1), VI("flatH"), CUT, touched)

    # connecteur pogo monté par l'intérieur : nez dans la fente de la peau du méplat (épaisseur = nez, face au ras),
    # oreilles en appui derrière la peau ; canal oblong jusqu'à l'ouverture centrale pour l'y glisser, fils soudés
    zf = zb + mm("flatH")
    yMid = (yP + yB) / 2.0
    L, Wd, t = mm("pogoL"), mm("pogoW"), mm("pogoFaceT")
    zC = -math.sqrt((mm("plateOpenD") / 2.0) ** 2 - (L / 2.0) ** 2) + 1.0      # haut du canal, dans l'ouverture
    for name, Lo, z0, depth in (("Pogo, fente du nez", mm("pogoBodyL"), zf, t), ("Pogo, canal", L, zf + t, zC - zf - t)):
        s7 = sk.add(offset_plane(root, xy, VR(z0 / 10.0)))
        s7.name = name
        Lr = Lo - Wd                                          # oblong : rectangle + deux bouts arrondis
        s7.sketchCurves.sketchLines.addTwoPointRectangle(
            s7.modelToSketchSpace(P3(-Lr / 2, yMid - Wd / 2, z0)), s7.modelToSketchSpace(P3(Lr / 2, yMid + Wd / 2, z0)))
        for side in (1, -1):
            s7.sketchCurves.sketchCircles.addByCenterRadius(s7.modelToSketchSpace(P3(side * Lr / 2, yMid, z0)), Wd / 20.0)
        extrude(root, all_profiles(s7), VR(depth / 10.0), CUT, [plate])

    # dos : coque ouverte côté plaque
    faces = planar_faces_along_y(back)
    front_face = min(faces, key=lambda f: abs(f.centroid.y * 10.0 - yB))
    oc = adsk.core.ObjectCollection.create()
    oc.add(front_face)
    shells = root.features.shellFeatures
    si = shells.createInput(oc, False)
    si.insideThickness = VI("wallT")
    shells.add(si)

    # colonnes de vis dans le dos (après la coque, sinon elles seraient évidées)
    s10 = sk.add(planeB)
    s10.name = "Colonnes vis"
    for x, z in sPos:
        circle_at(s10, x, yB, z, "bossD", mm)
    extrude(root, all_profiles(s10), VI("backT - wallT"), JOIN, [back])
    s11 = sk.add(planeB)
    s11.name = "Passage vis dos"
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

    # rainure décorative sur le plat du dos
    s13 = sk.add(planeR)
    s13.name = "Rainure dos"
    circle_at_origin(s13, "grooveD", mm)
    circle_at_origin(s13, "grooveD - 2 * grooveW", mm)
    extrude(root, profile_with_loops(s13, 2), VI("-grooveT"), CUT, [back])

    # 4. Berceau de la LiPo (V2) : cadre de nervures sur le fond du dos, encoche en bas pour les fils.
    # La LiPo y est collée (adhésif double face) ; côté carte, 14 mm restent libres pour ses composants.
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
    """Volume de la LiPo (non imprimée) dans son berceau ; l'assemblage l'appelle (rendu, contrôle visuel)."""
    planeL = offset_plane(root, root.xZConstructionPlane, VI("frontT + plateT + backT - wallT"))
    yL = planeL.geometry.origin.y * 10.0
    s = root.sketches.add(planeL)
    s.name = "LiPo tête (V2)"
    w, l = mm("lipoW") / 2.0, mm("lipoL") / 2.0
    s.sketchCurves.sketchLines.addTwoPointRectangle(s.modelToSketchSpace(P3(-w, yL, -l)), s.modelToSketchSpace(P3(w, yL, l)))
    body = extrude(root, profile_with_loops(s, 1), VI("-lipoT"), NEW).bodies.item(0)
    body.name = "LiPo tête (V2)"
    return body

# ── export STL automatique, pièces orientées pour l'impression ──────────
# Maillage calculé par Fusion (qualité très haute), rotation autour de x pour poser la pièce,
# écriture STL binaire en mm. Python pur : aucun module externe dans Fusion.
STL_DIR = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "MoodiMOS_Organique",
                       "stl_impression", "fusion")


def export_stl(root, pieces):
    """pieces : [(nom du corps, nom du fichier, « haut »)] ; « haut » = la profondeur u (vers l'arrière)
    monte sur le plateau (face avant en bas), sinon elle descend (face arrière en bas).
    Renvoie les lignes du compte rendu."""
    sY = 1.0 if root.xZConstructionPlane.geometry.normal.y > 0 else -1.0   # u = sY · y
    os.makedirs(STL_DIR, exist_ok=True)
    bodies = {b.name: b for b in root.bRepBodies}
    report = []
    for body_name, file_name, u_up in pieces:
        body = bodies.get(body_name)
        if body is None:
            report.append("{} : corps « {} » introuvable".format(file_name, body_name))
            continue
        calc = body.meshManager.createMeshCalculator()
        calc.setQuality(adsk.fusion.TriangleMeshQualityOptions.VeryHighQualityTriangleMesh)
        mesh = calc.calculate()
        c, idx = mesh.nodeCoordinatesAsDouble, mesh.nodeIndices
        s = sY if u_up else -sY           # rotation de s · 90° autour de x : (0, sY, 0) → ±z
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


# cerclage : façade en haut ; plaque : à plat ; dos : ouverture sur le plateau
PIECES = [("Cerclage avant", "tete_cerclage.stl", False), ("Plaque centrale", "tete_plaque.stl", False), ("Dos bombé", "tete_dos.stl", True)]
