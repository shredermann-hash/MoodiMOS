# MoodiMOS — torse paramétrique pour Autodesk Fusion
#
# Lancer : Utilitaires > Compléments > Scripts et compléments > « + » (script existant)
#          > choisir ce dossier MoodiMOS_Torse > Exécuter.
# Le script crée un nouveau document avec trois corps :
#   1. Coque : façade + côtés + dessus + dessous, ouverte à l'arrière (impression face sur le plateau).
#      Vitre 3,5" affleurante en portrait (poche + épaulement), objectif caméra au-dessus,
#      collier du cou (connecteur pogo magnétique), logements d'épaules et de jambes, USB-C en bas, PWR à droite.
#      Intérieur : berceau du module caméra OV5640 et cadre du radar dans le bandeau haut, compartiment
#      du BME280 derrière les ouïes gauches.
#   2. Dos : plaque avec lèvre de centrage, 4 colonnes de vis, grilles du haut-parleur, bague et trou
#      de la boule BH1750.
# Carte : kit ESP32-S3-Touch-LCD-3.5-C sorti de son boîtier (même carte et même vitre que la 3.5).
# Sa caméra OV5640 regarde vers l'arrière du boîtier : son module est déporté derrière l'objectif
# du bandeau, par une rallonge FPC 24 broches 0,5 mm (carte adaptateur + nappe 150 mm).
#   3. Boîte HP : volume clos du haut-parleur, collé sur le dos (bague de centrage).
# encombrements() ajoute les volumes des composants (non imprimés) ; l'assemblage l'appelle.
# Assemblage : 4 vis M2×35 par le dos → colonnes → entretoises de la carte ; la carte plaque
# la vitre sur l'épaulement et le dos contre la coque, aucune autre fixation.
#
# Orientation (réglage Fusion « Z vers le haut ») : haut du torse = +Z, face écran vers -Y,
# profondeur u mesurée depuis la face avant. Origine au centre de la façade.
# Carte vue de face : plan Waveshare (vue de dos, paysage) retourné en miroir puis tourné
# pour mettre l'USB-C en bas → PWR / RESET / BOOT sur le flanc droit.
# Cotes lues au lancement : modifier PARAMS ci-dessous puis relancer.

import adsk.core, adsk.fusion, traceback, math, os, struct

PARAMS = {
    # enveloppe
    "torsoW": 82,  "torsoH": 120.5, "torsoD": 48,  # 82 : tête et tronc de même largeur (rendu) ; 120,5 : bandeau intérieur
                                                   # de 18,6 au-dessus de la vitre pour la caméra et le radar
    "cornerR": 6,       # arrondi des arêtes verticales (vue de face)
    "frontFillet": 4,   # congé du pourtour de la façade (arêtes douces du rendu)
    "wallT": 2.5,       # parois de la coque
    "backT": 3,         # épaisseur du dos
    "backFillet": 2.5,  # congé du pourtour du dos
    # vitre 3,5" (plan Waveshare)
    "glassW": 61, "glassH": 92.44, "glassR": 6, "glassT": 1.1, "glassFit": 0.2,
    "glassBot": 6.78,   # façade sous la vitre ; le bandeau du haut reçoit la caméra
    "winW": 56, "winH": 82,  # ouverture derrière la vitre, dégage le module LCD (À MESURER)
    # fixation de la carte (trous M2 à 48,50 × 72,00 centrés sur la vitre)
    "standoffU": 11.5,  # face vitre → bout des entretoises (plan : 11,50 hors tout)
    "holeX": 24.25, "holeZ": 36,
    "bossD": 7, "screwD": 2.3, "screwHeadD": 4.2,
    "screwHeadT": 4.5,  # chambrage : vis M2×35 → 3 mm de filet dans l'entretoise
    # lèvre du dos
    "lipW": 1.5, "lipH": 1.5, "lipFit": 0.2,
    # module caméra OV5640 24 broches du kit (Waveshare ne publie pas ses cotes : valeurs usuelles de ces
    # modules, À MESURER) : bloc lentille camModW × camModH, lentille + capteur camModT, fût d'objectif
    # dans le trou camD. Glissé entre deux joues contre la façade, nappe vers le bas, point de colle.
    "camD": 8,          # trou d'objectif : fût + 0,5 à 1 (À MESURER)
    "camModW": 8.5, "camModH": 8.5, "camModT": 6.5, "camFit": 0.3, "cradleT": 1.6,
    "camOffZ": 0,       # objectif au-dessus (+) ou au-dessous du centre du bandeau
    # carte adaptateur de la rallonge FPC (sourcing map 24 broches : 20 × 24 × 3,5) : couchée au bas du
    # bandeau, à gauche de la caméra, juste au-dessus du bord de la carte ; adhésif double face
    "fpcL": 20, "fpcW": 24, "fpcT": 3.5, "fpcX": -18.3, "fpcU": 15,   # 20 en largeur, 24 en profondeur
    # extenseur PCF8574T (DollaTek 54 × 15, broches comprises ≈ 12) : debout contre le dos, dans le
    # bandeau entre les épaules, derrière le pogo du cou ; adhésif double face
    "pcfL": 54, "pcfH": 15, "pcfT": 12, "pcfU": 29.5,
    # radar HLK-LD2410C (22 × 16) : collé (adhésif double face) contre la façade, à droite de la caméra
    "radW": 22, "radH": 16, "radT": 3.5, "radFit": 0.3, "railT": 1.2,
    "radX0": 6.45,      # bord gauche du radar : sa joue est la paroi droite du berceau de la caméra (13,3 jusqu'au
                        # 16/10/2026 : décalé pour laisser la place au bossage de la vis de façade en haut à droite)
    # BME280 (≈ 13,5 × 10,5) : compartiment cloisonné derrière les ouïes du flanc gauche, loin des cartes chaudes
    "bmeW": 13.5, "bmeH": 10.5, "bmeT": 3, "bmeBoxX": 5, "bmeWallT": 1.2,
    # BH1750 en boule (mesuré 2026-10-02 : dôme 25, collerette 28) : dôme qui sort du haut du dos, collerette plaquée à l'intérieur dans une bague
    "domeD": 25, "domeBaseD": 28, "domeZ": 32, "domeRingH": 3,
    # haut-parleur récupéré (À MESURER) : dans une boîte close collée au dos, membrane face aux grilles
    "spkD": 40, "spkT": 16, "spkX": 11, "spkZ": -8, "spkVol": 4.5, "boxT": 1.6,   # décalé à droite : fiches Dupont à gauche
    # connecteur 2×16 de la carte 3,5" (photo Waveshare, à vérifier) : côté gauche vu de face, à mi-hauteur ;
    # fiches Dupont femelles enfichées → zone réservée jusqu'à hdrU derrière la façade
    "hdrX": -17, "hdrZ": -6.6, "hdrL": 41, "hdrW": 5.1, "hdrU": 29,
    # composants de bord de la carte
    "usbU": 9.1, "usbW": 13, "usbH": 7,          # USB-C : axe à 7,5 (dos PCB) + 1,6 (À MESURER)
    "pwrZ": 21.22, "pwrU": 9, "pwrD": 3.5,       # PWR : 10,22 + 11,00 au-dessus du bas de la vitre
    # cou (même implantation que sous la tête)
    "neckD": 30, "neckH": 6, "neckU": 24.5,      # tête Ø 82 × 32 centrée sur la profondeur du torse ; Ø 30 (et non 28) :
                                                 # 1,8 de paroi aux bouts du logement du pogo
    # connecteur pogo magnétique monté par l'intérieur (Sylen 5 broches 2,5 : 25 avec les oreilles, 20 sans, 4 de large,
    # nez de 2) : fente du nez dans une peau de 2 au sommet du collier, oreilles en appui dessous ; largeur À CONFIRMER
    "pogoL": 26.4, "pogoBodyL": 21.4, "pogoW": 4.6, "pogoFaceT": 2.0,   # jauge du 13/10 : 4,4 × 20,6 / 25,6 trop justes
    # épaules et jambes (pièces organiques : cao/MoodiMOS_Organique) : tenon Ø 9 aimanté,
    # aimant Ø 6 × 3 en bout de tenon face à un aimant collé côté intérieur derrière une peau de 0,6
    "shoulderDown": 18, "pegD": 9.4, "shoulderBossD": 16,   # trou Ø 9,4 : tenon Ø 9 pivotant et amovible
    "limbMagD": 6.2, "limbMagT": 3.2, "skinT": 0.6,
    "shHoleDepth": 4.7, "legHoleDepth": 5.5,     # profondeur des trous de tenon
    "socketD": 18, "socketT": 1.5,              # rotule d'épaule : cuvette où se loge la boule
    # façade séparée du corps (imprimée dos sur le plateau : épaulement de la vitre sans support), vissée par 4 vis M2 × 8
    # à tête bombée aux places des anciennes vis décoratives : chambrage de la tête, passage, bossage et avant-trou
    "screwDecoD": 4.4, "screwDecoT": 0.8,        # chambrage de la tête (ISO 7380 : Ø 3,5 × 1,3, dépasse de 0,5)
    "faceScrewD": 2.4, "facePilotD": 1.8, "faceBossD": 5.0, "faceBossL": 8.0,
    "ventN": 5, "ventL": 14, "ventW": 2, "ventZ": -30, "ventPitch": 5,  # ouïes des flancs (Nano ventilé)
    "legX": 23, "legBossD": 16,
    # grilles du haut-parleur (dos)
    "grilleW": 30, "grilleH": 2.4, "grilleN": 4, "grillePitch": 6,   # centrées sur le haut-parleur (spkZ)
}

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
        build(design.rootComponent, PARAMS)
        app.activeViewport.fit()
        supports = support_collier(design.rootComponent, dict(PARAMS))
        report = export_stl(design.rootComponent, PIECES)                     # supports : impression seulement,
        for f in reversed(supports):                                          # retirés ensuite de la conception
            f.deleteMe()
        ui.messageBox("Torse MoodiMOS créé : coque, dos et boîte du haut-parleur.\n"
                      "Cotes : modifier PARAMS dans le script puis relancer.\n\n"
                      "STL d'impression (orientés) dans {} :\n{}".format(os.path.normpath(STL_DIR), "\n".join(report)))
    except Exception:
        if ui:
            ui.messageBox("Erreur :\n{}".format(traceback.format_exc()))


# ── outils ────────────────────────────────────────────────────────────

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
    raise RuntimeError("Esquisse « {} » : aucun profil à {} contours (trouvés : {})".format(sketch.name, n, counts))


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
    """Extrusion des deux côtés du plan : le sens de la normale est indifférent."""
    ex = root.features.extrudeFeatures
    inp = ex.createInput(profiles, op)
    inp.setSymmetricExtent(VR(half_mm / 10.0), False)
    inp.participantBodies = bodies
    return ex.add(inp)


def nearest_point(points, target):
    return min(points, key=lambda sp: sp.geometry.distanceTo(target))


def rounded_rect(s, M, ca, cb, w, h, r):
    """Rectangle arrondi centré en (ca, cb) ; M(a, b) → Point3D monde dans le plan de l'esquisse."""
    sp = lambda a, b: s.modelToSketchSpace(M(a, b))
    ax, bx = w / 2.0 - r, h / 2.0 - r
    k = r * math.sqrt(0.5)
    corners, ends = [], []
    for sa, sb in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        cx, cz = ca + sa * ax, cb + sb * bx
        pa, pb = (cx + sa * r, cz), (cx, cz + sb * r)   # sur le côté vertical / horizontal
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
    """Cercle Ø d en goutte : pointe à r·√2 sur l'axe b (la profondeur u), du côté sens = ±1, raccordée par deux
    tangentes à 45°. Les cylindres couchés à l'impression (coque façade en bas) s'impriment ainsi sans support :
    pointe vers la façade (sens -1) pour un bossage. Les trous des tenons restent ronds (le tenon y pivote, tenu par son
    aimant). Extruder all_profiles."""
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
    zG = -H / 2.0 + p["glassBot"] + p["glassH"] / 2.0          # centre vitre (et carte)
    # bandeau intérieur au-dessus de la vitre : module caméra centré, objectif décalé de camOffZ
    zBand0, zCeil = zG + p["glassH"] / 2.0 + p["glassFit"], H / 2.0 - wt
    zX = (zBand0 + zCeil) / 2.0
    zCam = zX + p["camOffZ"]
    uMid = D / 2.0

    def uplane(u):                                    # plan de façade à la profondeur u
        pl = xz if u == 0 else offset_plane(root, xz, u)
        return pl, (lambda a, b: P3(a, sY * u, b))     # (x, z)

    def zplane(z):                                    # plan horizontal à la hauteur z
        return offset_plane(root, xy, sZ * z), (lambda a, b: P3(a, sY * b, z))   # (x, u)

    def xplane(x):                                    # plan latéral à l'abscisse x
        return offset_plane(root, yz, sX * x), (lambda a, b: P3(x, sY * b, a))   # (z, u)

    def sketch(plane_M, name):
        s = sk.add(plane_M[0])
        s.name = name
        return s, plane_M[1]

    # 1. Coque : bloc arrondi, congé de façade, évidé côté dos
    s, M = sketch(uplane(0), "Coque")
    rounded_rect(s, M, 0, 0, W, H, p["cornerR"])
    shell = extrude(root, profile_with_loops(s, 1), shellD, NEW).bodies.item(0)
    shell.name = "Coque"
    front = min(planar_faces_along_y(shell), key=lambda f: abs(f.centroid.y))
    try:
        fillet_outer_loop(root, front, p["frontFillet"])
    except Exception:
        pass   # congé facultatif : la coque reste valable sans lui
    rear = max(planar_faces_along_y(shell), key=lambda f: abs(f.centroid.y))
    oc = adsk.core.ObjectCollection.create()
    oc.add(rear)
    si = root.features.shellFeatures.createInput(oc, False)
    si.insideThickness = VR(wt / 10.0)
    root.features.shellFeatures.add(si)

    # vitre affleurante : poche arrondie R6, puis ouverture de l'épaulement
    fit = p["glassFit"]
    s, M = sketch(uplane(0), "Poche vitre")
    rounded_rect(s, M, 0, zG, p["glassW"] + 2 * fit, p["glassH"] + 2 * fit, p["glassR"] + fit)
    extrude(root, profile_with_loops(s, 1), p["glassT"], CUT, [shell])
    s, M = sketch(uplane(0), "Ouverture LCD")
    rounded_rect(s, M, 0, zG, p["winW"], p["winH"], 2)
    extrude(root, profile_with_loops(s, 1), wt, CUT, [shell])

    # objectif caméra, centré dans le bandeau haut
    s, M = sketch(uplane(0), "Objectif caméra")
    circle(s, M, 0, zCam, p["camD"])
    extrude(root, profile_with_loops(s, 1), wt, CUT, [shell])

    # vis M2 de la façade aux quatre coins (anciennes vis décoratives) : chambrage de la tête, puis passage
    s, M = sketch(uplane(0), "Vis de façade")
    # dans les zones plates : entre le bord de la vitre et le début du congé de façade
    flat = W / 2.0 - p["frontFillet"]
    dx = (p["glassW"] / 2.0 + fit + flat) / 2.0
    zTopDeco = (zG + p["glassH"] / 2.0 + fit + H / 2.0 - p["frontFillet"]) / 2.0
    zBotDeco = zG - p["glassH"] / 2.0 + p["glassR"]           # à hauteur du début de l'arrondi de la vitre
    deco = [(sx * dx, zTopDeco) for sx in (1, -1)] + [(sx * dx, zBotDeco) for sx in (1, -1)]
    for x, z in deco:
        circle(s, M, x, z, p["screwDecoD"])
    extrude(root, all_profiles(s), p["screwDecoT"], CUT, [shell])
    s, M = sketch(uplane(0), "Passages vis de façade")
    for x, z in deco:
        circle(s, M, x, z, p["faceScrewD"])
    extrude(root, all_profiles(s), wt, CUT, [shell])

    # ouïes des flancs
    n = int(p["ventN"])
    for side in (1, -1):
        s, M = sketch(xplane(side * (W / 2.0 - wt / 2.0)), "Ouïes")
        for k in range(n):
            rect(s, M, p["ventZ"] + (k - (n - 1) / 2.0) * p["ventPitch"], uMid, p["ventW"], p["ventL"])
        extrude_sym(root, all_profiles(s), wt, CUT, [shell])

    # USB-C de recharge (bas) et poussoir PWR (flanc droit)
    s, M = sketch(zplane(-H / 2.0 + wt / 2.0), "USB-C")
    rect(s, M, 0, p["usbU"], p["usbW"], p["usbH"])
    extrude_sym(root, profile_with_loops(s, 1), wt, CUT, [shell])
    s, M = sketch(xplane(W / 2.0 - wt / 2.0), "PWR")
    circle(s, M, zG - p["glassH"] / 2.0 + p["pwrZ"], p["pwrU"], p["pwrD"])
    extrude_sym(root, profile_with_loops(s, 1), wt, CUT, [shell])

    # collier du cou : connecteur pogo magnétique monté par l'intérieur ; nez dans la fente de la peau du collier
    # (face au ras), oreilles en appui dessous, cavité ouverte vers l'intérieur du torse
    s, M = sketch(zplane(H / 2.0), "Collier cou")
    circle(s, M, 0, p["neckU"], p["neckD"])
    extrude(root, profile_with_loops(s, 1), sZ * p["neckH"], JOIN, [shell])
    zTop = H / 2.0 + p["neckH"]
    zS = zTop - p["pogoFaceT"]
    for name, Lo, z0, depth in (("Pogo, fente du nez", p["pogoBodyL"], zTop, p["pogoFaceT"]),
                                ("Pogo, cavité", p["pogoL"], zS, p["neckH"] - p["pogoFaceT"] + wt + 1)):
        s, M = sketch(zplane(z0), name)
        Lr = Lo - p["pogoW"]                               # oblong : bouts arrondis comme les oreilles
        rect(s, M, 0, p["neckU"], Lr, p["pogoW"])
        for side in (1, -1):
            circle(s, M, side * Lr / 2.0, p["neckU"], p["pogoW"])
        extrude(root, all_profiles(s), -sZ * depth, CUT, [shell])

    # épaules : cuvette de rotule, bossage intérieur, trou de tenon, logement d'aimant par l'intérieur
    zSh = H / 2.0 - p["shoulderDown"]
    for side in (1, -1):
        s, M = sketch(xplane(side * W / 2.0), "Rotule épaule")
        circle(s, M, zSh, uMid, p["socketD"])
        extrude(root, profile_with_loops(s, 1), -side * sX * p["socketT"], CUT, [shell])
        xFloor = W / 2.0 - p["socketT"]                        # fond de la cuvette
        xMag = xFloor - p["shHoleDepth"] - p["skinT"] - p["limbMagT"]   # face intérieure du bossage
        s, M = sketch(xplane(side * (W / 2.0 - wt)), "Épaule {}".format("droite" if side > 0 else "gauche"))
        teardrop(s, M, zSh, uMid, p["shoulderBossD"], -1)      # bossage en goutte : sans support à l'impression
        extrude(root, all_profiles(s), -side * sX * (W / 2.0 - wt - xMag), JOIN, [shell])
        s, M = sketch(xplane(side * xFloor), "Tenon épaule")
        circle(s, M, zSh, uMid, p["pegD"])                     # trou rond : le tenon pivote, tenu par son aimant
        extrude(root, profile_with_loops(s, 1), -side * sX * p["shHoleDepth"], CUT, [shell])
        s, M = sketch(xplane(side * xMag), "Aimant épaule")
        circle(s, M, zSh, uMid, p["limbMagD"])
        extrude(root, profile_with_loops(s, 1), side * sX * p["limbMagT"], CUT, [shell])

    # jambes : bossages sous le fond, trous borgnes de tenon, logements d'aimant par l'intérieur
    s, M = sketch(zplane(-H / 2.0 + wt), "Bossages jambes")
    for side in (1, -1):
        teardrop(s, M, side * p["legX"], uMid, p["legBossD"], -1)   # en goutte : sans support à l'impression
    legStack = p["legHoleDepth"] + p["skinT"] + p["limbMagT"]    # depuis le dessous du fond
    extrude(root, all_profiles(s), sZ * (legStack - wt), JOIN, [shell])
    s, M = sketch(zplane(-H / 2.0), "Tenons jambes")
    for side in (1, -1):
        circle(s, M, side * p["legX"], uMid, p["pegD"])            # trou rond : le tenon pivote
    extrude(root, all_profiles(s), sZ * p["legHoleDepth"], CUT, [shell])
    s, M = sketch(zplane(-H / 2.0 + legStack), "Aimants jambes")
    for side in (1, -1):
        circle(s, M, side * p["legX"], uMid, p["limbMagD"])
    extrude(root, all_profiles(s), -sZ * p["limbMagT"], CUT, [shell])

    def box(name, x0, x1, z0, z1, u0, u1, op, bodies):
        s, M = sketch(uplane(u0), name)
        rect(s, M, (x0 + x1) / 2.0, (z0 + z1) / 2.0, x1 - x0, z1 - z0)
        return extrude(root, profile_with_loops(s, 1), u1 - u0, op, bodies)

    # ── intérieur de la coque (imprimé avec elle, façade sur le plateau : tout pousse vers l'arrière) ──
    # berceau caméra : deux joues contre la façade et le plafond, le module se glisse objectif dans le trou,
    # nappe vers le bas (ni appui ni doigt : un point de colle le tient)
    xw = p["camModW"] / 2.0 + p["camFit"]
    uC1 = wt + p["camModT"] + p["camFit"]                    # dos du module
    for side in (1, -1):
        x0, x1 = sorted((side * xw, side * (xw + p["cradleT"])))
        box("Berceau caméra", x0, x1, zBand0 + 0.2, zCeil + 0.5, wt - 0.5, uC1, JOIN, [shell])
    # radar : cadre de pose (joue gauche, rail droit, appui bas), plafond au-dessus
    xR0 = p["radX0"]
    xR1 = xR0 + p["radW"]
    box("Joue radar", xR0 - p["radFit"] - p["cradleT"], xR0 - p["radFit"], zBand0 + 0.2, zCeil + 0.5, wt - 0.5, wt + 2.0,
        JOIN, [shell])
    zR0 = zX - p["radH"] / 2.0
    uR = wt + 2.0
    box("Rail radar", xR1 + p["radFit"], xR1 + p["radFit"] + p["railT"], zBand0 + 0.2, zCeil + 0.5, wt - 0.5, uR, JOIN, [shell])
    box("Appui radar", xR0 - p["radFit"] - p["cradleT"], xR1 + p["radFit"] + p["railT"], zBand0 + 0.2, zR0 - p["radFit"], wt - 0.5, uR, JOIN, [shell])
    # BME280 : boîte ouverte vers le dos, plaquée sur les ouïes du flanc gauche
    n = int(p["ventN"])
    zv = (n - 1) / 2.0 * p["ventPitch"] + p["ventW"] / 2.0 + 0.5
    xi, bw = -(W / 2.0 - wt), p["bmeWallT"]
    uB0, uB1 = uMid - p["ventL"] / 2.0 - 1.0, uMid + p["ventL"] / 2.0 + 1.0
    box("Compartiment BME280", xi - 0.5, xi + p["bmeBoxX"] + bw, p["ventZ"] - zv - bw, p["ventZ"] + zv + bw,
        uB0 - bw, uB1, JOIN, [shell])
    box("Logement BME280", xi, xi + p["bmeBoxX"], p["ventZ"] - zv, p["ventZ"] + zv, uB0, uB1 + 1, CUT, [shell])

    # bossages des vis de façade (côté corps), reliés au flanc par une nervure, avant-trou M2
    s, M = sketch(uplane(wt), "Bossages vis de façade")
    for x, z in deco:
        circle(s, M, x, z, p["faceBossD"])
        sx = 1.0 if x > 0 else -1.0
        x0, x1 = sorted((x, sx * (W / 2.0 - wt + 0.5)))
        rect(s, M, (x0 + x1) / 2.0, z, x1 - x0, 3.0)
    extrude(root, all_profiles(s), p["faceBossL"], JOIN, [shell])
    s, M = sketch(uplane(wt), "Avant-trous vis de façade")
    for x, z in deco:
        circle(s, M, x, z, p["facePilotD"])
    extrude(root, all_profiles(s), p["faceBossL"] - 1.0, CUT, [shell])

    # façade séparée du corps au plan u = wt (joint sur l'arrondi du bord, même contour) : imprimée dos sur le plateau,
    # l'épaulement de la vitre se construit sans support ; le corps s'imprime avant en bas
    sb = root.features.splitBodyFeatures
    split = sb.add(sb.createInput(shell, offset_plane(root, xz, wt), True))
    parts = sorted((split.bodies.item(i) for i in range(split.bodies.count)),
                   key=lambda b: abs(b.physicalProperties.centerOfMass.y))
    parts[0].name, parts[-1].name = "Façade", "Coque"

    # 2. Dos : plaque, congé arrière, lèvre de centrage
    s, M = sketch(uplane(shellD), "Dos")
    rounded_rect(s, M, 0, 0, W, H, p["cornerR"])
    back = extrude(root, profile_with_loops(s, 1), p["backT"], NEW).bodies.item(0)
    back.name = "Dos"
    rearB = max(planar_faces_along_y(back), key=lambda f: abs(f.centroid.y))
    try:
        fillet_outer_loop(root, rearB, p["backFillet"])
    except Exception:
        pass
    lf, lw = p["lipFit"], p["lipW"]
    ri = p["cornerR"] - wt - lf
    s, M = sketch(uplane(shellD), "Lèvre")
    rounded_rect(s, M, 0, 0, W - 2 * (wt + lf), H - 2 * (wt + lf), ri)
    rounded_rect(s, M, 0, 0, W - 2 * (wt + lf + lw), H - 2 * (wt + lf + lw), max(ri - lw, 0.5))
    extrude(root, profile_with_loops(s, 2), -p["lipH"], JOIN, [back])

    # colonnes de vis : du dos jusqu'aux entretoises de la carte
    holes = [(sx * p["holeX"], zG + sz * p["holeZ"]) for sx in (1, -1) for sz in (1, -1)]
    s, M = sketch(uplane(p["standoffU"]), "Colonnes vis")
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

    # BH1750 : trou du dôme et bague de la collerette
    s, M = sketch(uplane(shellD), "Bague BH1750")
    circle(s, M, 0, p["domeZ"], p["domeBaseD"] + 0.4)
    circle(s, M, 0, p["domeZ"], p["domeBaseD"] + 0.4 + 2 * 1.2)
    extrude(root, profile_with_loops(s, 2), -p["domeRingH"], JOIN, [back])
    s, M = sketch(uplane(shellD), "Trou BH1750")
    circle(s, M, 0, p["domeZ"], p["domeD"] + 0.4)
    extrude(root, profile_with_loops(s, 1), p["backT"], CUT, [back])

    # haut-parleur : bague de centrage de la boîte sur le dos, puis la boîte close (pièce à part, collée)
    spkIn = p["spkD"] + 0.4
    boxOD = spkIn + 2 * p["boxT"]
    s, M = sketch(uplane(shellD), "Bague boîte HP")
    circle(s, M, p["spkX"], p["spkZ"], boxOD + 0.4)
    circle(s, M, p["spkX"], p["spkZ"], boxOD + 0.4 + 2 * 1.2)
    extrude(root, profile_with_loops(s, 2), -2.0, JOIN, [back])
    depth = p["spkT"] + p["spkVol"]
    s, M = sketch(uplane(shellD), "Boîte HP")
    circle(s, M, p["spkX"], p["spkZ"], boxOD)
    spk = extrude(root, profile_with_loops(s, 1), -(depth + p["boxT"]), NEW).bodies.item(0)
    spk.name = "Boîte HP"
    s, M = sketch(uplane(shellD), "Volume HP")
    circle(s, M, p["spkX"], p["spkZ"], spkIn)
    extrude(root, profile_with_loops(s, 1), -depth, CUT, [spk])
    s, M = sketch(xplane(p["spkX"] + boxOD / 2.0), "Passage câble HP")   # côté prise SPK de la carte          # fil du HP, trou rebouché à la colle
    circle(s, M, p["spkZ"], shellD - depth + 3, 3)
    extrude_sym(root, profile_with_loops(s, 1), p["boxT"] + 1, CUT, [spk])

    # grilles du haut-parleur
    s, M = sketch(uplane(shellD), "Grilles HP")
    n = int(p["grilleN"])
    for k in range(n):
        z = p["spkZ"] + (k - (n - 1) / 2.0) * p["grillePitch"]
        rect(s, M, p["spkX"], z, p["grilleW"], p["grilleH"])
    extrude(root, all_profiles(s), p["backT"], CUT, [back])


def encombrements(root, p):
    """Volumes des composants (non imprimés) pour vérifier l'implantation dans l'assemblage."""
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
    cbox("Caméra OV5640", 0, zX + p["camOffZ"], p["camModW"], p["camModH"], wt, wt + p["camModT"])
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
        pass   # dôme laissé cylindrique si le congé échoue
    cbox("Pogo", 0, H / 2.0 + p["neckH"] - 2.0, p["pogoL"], 4.0, p["neckU"] - p["pogoW"] / 2.0, p["neckU"] + p["pogoW"] / 2.0)   # connecteur de 4

# ── export STL automatique, pièces orientées pour l'impression ──────────
# Maillage calculé par Fusion (qualité très haute), rotation autour de x pour poser la pièce,
# écriture STL binaire en mm. Python pur : aucun module externe dans Fusion.
STL_DIR = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "MoodiMOS_Organique",
                       "stl_impression", "fusion")


def support_collier(root, p):
    """Impression seulement : support détachable sous le collier du cou (couché à l'impression, sa face avant porte à
    faux ; le corps s'imprime avant en bas, joint avec la façade sur le plateau). Trois ailettes de 0,8 sur une semelle de
    1, à 0,2 sous le cylindre là où sa pente dépasse 60° (|x| ≤ 9), à 0,4 du dessus du torse. Renvoie les fonctions, à
    supprimer après l'export."""
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
    s.name = "Support collier, semelle (impression)"
    rect(s, lambda a, b: P3(a, sY * b, z0), 0, wt + 0.5, 2 * xs, 1.0)
    feats.append(extrude(root, profile_with_loops(s, 1), sZ * (z1 - z0), JOIN, [body]))
    return feats


def export_stl(root, pieces):
    """pieces : [(nom du corps ou tuple de noms réunis dans un même fichier, nom du fichier, « haut »)] ; « haut » = la
    profondeur u (vers l'arrière) monte sur le plateau (face avant en bas), sinon elle descend (face arrière en bas).
    Renvoie les lignes du compte rendu."""
    sY = 1.0 if root.xZConstructionPlane.geometry.normal.y > 0 else -1.0   # u = sY · y
    os.makedirs(STL_DIR, exist_ok=True)
    bodies = {b.name: b for b in root.bRepBodies}
    report = []
    for body_names, file_name, u_up in pieces:
        names = body_names if isinstance(body_names, tuple) else (body_names,)
        missing = [n for n in names if n not in bodies]
        if missing:
            report.append("{} : corps « {} » introuvable".format(file_name, ", ".join(missing)))
            continue
        s = sY if u_up else -sY           # rotation de s · 90° autour de x : (0, sY, 0) → ±z
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


# coque : façade sur le plateau ; dos : face extérieure sur le plateau ; boîte HP : fond fermé en bas
PIECES = [(("Coque", "Support collier"), "torse_coque.stl", True), ("Façade", "torse_facade.stl", False),
          ("Dos", "torse_dos.stl", False),
          ("Boîte HP", "torse_boite_hp.stl", True)]
