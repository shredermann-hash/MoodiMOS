# MoodiMOS — vues éclatées de la tête, du torse et du robot complet, pour les étapes 9, 10 et 11 de la notice
#
# Lancer depuis la racine du projet, après robot_pieces.py et robot_membres.py :
#   blender -b -P cao/MoodiMOS_Robot/robot_eclate.py               (toutes les vues)
#   blender -b -P cao/MoodiMOS_Robot/robot_eclate.py -- robot      (une seule : tete, torse ou robot)
# Reprend la scène de robot_rendu.py (pièces en place de stl/, couleurs, écrans, studio), ne garde que la tête ou le
# torse et écarte les pièces le long de y. Les écrans affleurent à l'avant : la vitre et sa carte passent devant,
# puis cerclage, plaque et dôme pour la tête, cadre, façade, coque, boîte du haut-parleur et dos pour le torse.
# Les modules que stl/ ne contient pas (caméra, radar, PCF8574T, collerette du BH1750, les deux moitiés du pogo du
# cou) sont modélisés ici aux cotes et places de encombrements() de ../MoodiMOS_Torse/MoodiMOS_Torse.py. Cachés dans
# la coque, ils en sortent puis montent au-dessus (décalage (y, z) : le z vient après le y). L'ordre de montage reste
# celui du texte des étapes 9 et 10. Le robot complet garde la tête et le torse montés, écarte les membres et place le
# pogo entre la tête et le torse. Écrit dans rendus/ :
#   eclate_<tete|torse|robot>.png et _en.png : vue éclatée, noms des pièces en français ou en anglais
#   eclate_<tete|torse|robot>.gif : les pièces se séparent puis se remontent, sans texte
# Blender rend les images dans un dossier temporaire ; python3 (Pillow) pose les noms, ffmpeg assemble les GIF.

import json, math, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, "rendus")

# objet de la scène : décalage en y (mm, + vers l'arrière) en vue éclatée ; les peintures suivent leur pièce
GROUPES = {
    "tete": {
        "decalage": {
            "vitre_tete": -95, "Écran tête": -95,
            "tete_cerclage": -50, "peinture_index": -50, "peinture_liseré_lunette": -50,
            "tete_plaque": 0, "peinture_belier": 0,
            "tete_dos": 45, "peinture_medaillon": 45, "peinture_jonc_casque_droit": 45, "peinture_jonc_casque_gauche": 45,
        },
        # objet repère, nom français, nom anglais, fichier français, fichier anglais
        "noms": [
            ("tete_cerclage", "Cerclage", "Bezel", "tete_cerclage.stl", "head_bezel.stl"),
            ("vitre_tete", "Vitre et carte 2.1", "Glass and 2.1 board", "", ""),
            ("tete_plaque", "Plaque", "Plate", "tete_plaque.stl", "head_plate.stl"),
            ("tete_dos", "Dôme", "Dome", "tete_dos.stl", "head_back.stl"),
        ],
    },
    "torse": {
        "decalage": {
            "vitre_torse": -80, "Écran torse": -80,
            "torse_cadre": -55,
            "torse_facade": -30, "peinture_vis": -30,
            "compo_camera": (-14, 22), "compo_radar": (-14, 22),
            "torse_coque": 0, "torse_jonc_droit": 0, "torse_jonc_gauche": 0,
            "compo_pcf8574": (34, 25),
            "torse_boite_hp": 45,
            "compo_bh1750": 40, "compo_bh1750_collerette": 40,
            "torse_dos": 95,
        },
        "noms": [
            ("torse_cadre", "Cadre noir", "Black frame", "torse_cadre.stl", "torso_frame.stl"),
            ("torse_facade", "Façade", "Front panel", "torse_facade.stl", "torso_front.stl"),
            ("compo_camera", "Caméra OV5640", "OV5640 camera", "", ""),
            ("compo_radar", "Radar LD2410C", "LD2410C radar", "", ""),
            ("torse_coque", "Coque", "Body", "torse_coque.stl", "torso_body.stl"),
            ("compo_pcf8574", "Expandeur PCF8574T", "PCF8574T expander", "", ""),
            ("vitre_torse", "Carte 3.5-C", "3.5-C board", "", ""),
            ("torse_boite_hp", "Boîte du haut-parleur", "Speaker box", "torse_boite_hp.stl", "torso_speaker_box.stl"),
            ("torse_dos", "Dos", "Back", "torse_dos.stl", "torso_back.stl"),
            ("compo_bh1750", "BH1750 et sa boule", "BH1750 and its ball", "", ""),
        ],
    },
}

AZIMUT, ELEVATION = math.radians(50), math.radians(18)   # caméra : 50° à droite de la face, 18° au-dessus
VUE = (1500, 1000, 64)          # vue fixe : largeur, hauteur, échantillons Cycles
GIF = (600, 400, 16, 18)        # GIF : largeur, hauteur, échantillons, images de l'écartement
FPS = 12
BANDE = 0.16                    # haut et bas de la vue fixe réservés aux noms
COLONNE = 0.2                   # robot complet : gauche et droite de la vue fixe réservées aux noms
LOUPE = (520, 540, 64, math.radians(25))   # gros plan du pogo : largeur, hauteur, échantillons, élévation

# Robot complet, pour l'étape 11 : tête et torse montés (leurs pièces sont dans les vues ci-dessus), les deux moitiés
# du pogo du cou entre eux, chaque membre en ses pièces imprimées. Les baskets restent au sol, le reste monte et les
# membres s'écartent vers l'extérieur ; tout bouge ensemble (décalage (x, y, z), x pour le côté droit).
TORSE_Z = 80
MEMBRES = [   # pièce imprimée, objets de la scène (sans _droit/_gauche), décalage
    ("basket", ("semelle", "lisere", "bout", "bord_bout", "tige", "flanc", "col", "lacets", "pastille", "vent_basket",
                "talon"), (0, 0, 0)),
    ("jambe", ("jambe", "bague_jambe"), (0, 0, 50)),
    ("gant_bracelet", ("gant", "bracelet", "poignet_gant", "lisere_gant"), (62, 0, 40)),
    ("bras", ("bras", "bague_bras"), (62, 0, TORSE_Z)),
    ("epaule", ("epaule",), (28, 0, TORSE_Z)),
]


def groupe_robot():
    dec = {n: (0, 0, TORSE_Z) for n in GROUPES["torse"]["decalage"]}
    dec.update({n: (0, 0, TORSE_Z + 70) for n in GROUPES["tete"]["decalage"]})
    dec.update({"compo_pogo_torse": (0, 0, TORSE_Z + 22), "compo_pogo_tete": (0, 0, TORSE_Z + 46)})
    for _, objets, (dx, dy, dz) in MEMBRES:
        for cote, sens in (("droit", 1), ("gauche", -1)):
            dec.update({o + "_" + cote: (sens * dx, dy, dz) for o in objets})
    pieces = {piece: objets for piece, objets, _ in MEMBRES}
    d = lambda piece: tuple(o + "_droit" for o in pieces[piece])
    return {
        "decalage": dec, "azimut": math.radians(30), "elevation": math.radians(15),
        "vue": (1500, 1500, 64), "gif": (600, 600, 16, 18),
        # objets repères, noms et fichiers, colonne (g ou d) et hauteur du trait dans la boîte des objets
        "noms": [
            (("tete_plaque", "tete_dos", "tete_cerclage"), "Tête", "Head", "étape 9", "step 9", "g", 0.5),
            (("torse_coque", "torse_dos", "torse_facade"), "Torse", "Torso", "étape 10", "step 10", "g", 0.08),
            (d("epaule"), "Épaule", "Shoulder", "epaule_droit.stl", "shoulder_right.stl", "d", 0.5),
            (d("bras"), "Bras", "Arm", "bras_droit.stl", "arm_right.stl", "d", 0.6),
            (d("gant_bracelet"), "Gant et bracelet", "Glove and cuff", "gant_bracelet_droit.stl",
             "glove_cuff_right.stl", "d", 0.5),
            (d("jambe"), "Jambe", "Leg", "jambe_droit.stl", "leg_right.stl", "d", 0.12),
            (d("basket"), "Basket", "Sneaker", "basket_droit.stl", "sneaker_right.stl", "d", 0.5),
        ],
        # gros plan sur le pogo du cou, seul, en haut à droite de la vue fixe ; noms en haut et en bas
        "loupe": [
            ("compo_pogo_tete", "Pogo, moitié à plots", "Pogo, pad half", "dans la tête", "in the head"),
            ("compo_pogo_torse", "Pogo, moitié à ressorts", "Pogo, spring half", "dans le torse", "in the torso"),
        ],
    }


GROUPES["robot"] = groupe_robot()


def douceur(t):
    return t * t * (3 - 2 * t)


# ── rendu (dans Blender) ──

def gpu(bpy, sc):
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        sc.cycles.device = "GPU"
    except Exception:            # pas de Metal : Cycles reste sur le processeur
        pass


def cotes_torse():
    """PARAMS de la CAO principale du torse, avec le style (robot_pieces.py fait de même)."""
    import ast
    from robot_style import TORSO_STYLE
    chemin = os.path.join(HERE, "..", "MoodiMOS_Torse", "MoodiMOS_Torse.py")
    for node in ast.parse(open(chemin, encoding="utf-8").read()).body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "PARAMS":
            return dict(ast.literal_eval(node.value), **TORSO_STYLE)
    raise RuntimeError("PARAMS introuvable dans " + chemin)


def module(bpy, nom, pieces):
    """Objet fait de pavés (x0, x1, y0, y1, z0, z1), de cylindres d'axe y (x, z, rayon, y0, y1) et d'axe z
    ("cylz" : x, y, rayon, z0, z1), chacun sa matière."""
    import bmesh
    from mathutils import Matrix
    me = bpy.data.meshes.new(nom)
    bm = bmesh.new()
    mats = []
    for forme, c, mat in pieces:
        if mat not in mats:
            mats.append(mat)
        avant = set(bm.faces)
        if forme == "pave":
            x0, x1, y0, y1, z0, z1 = c
            r = bmesh.ops.create_cube(bm, size=1.0)
            m = Matrix.Translation(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)) @ Matrix.Diagonal((x1 - x0, y1 - y0, z1 - z0, 1))
        elif forme == "cylz":
            x, y, rayon, z0, z1 = c
            r = bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=rayon, radius2=rayon, depth=z1 - z0)
            m = Matrix.Translation((x, y, (z0 + z1) / 2))
        else:
            x, z, rayon, y0, y1 = c
            r = bmesh.ops.create_cone(bm, cap_ends=True, segments=48, radius1=rayon, radius2=rayon, depth=y1 - y0)
            m = Matrix.Translation((x, (y0 + y1) / 2, z)) @ Matrix.Rotation(math.pi / 2, 4, "X")
        bmesh.ops.transform(bm, matrix=m, verts=r["verts"])
        for f in set(bm.faces) - avant:
            f.material_index = mats.index(mat)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(nom, me)
    bpy.context.scene.collection.objects.link(ob)
    for mat in mats:
        ob.data.materials.append(mat)
    arrondi = ob.modifiers.new("arrondi", "BEVEL")
    arrondi.width, arrondi.segments, arrondi.limit_method = 0.25, 2, "ANGLE"
    return ob


def modules(bpy, robot_rendu):
    """Caméra, radar, PCF8574T et collerette du BH1750, aux cotes de encombrements() (MoodiMOS_Torse.py)."""
    p = cotes_torse()
    H, wt = p["torsoH"], p["wallT"]
    shellD = p["torsoD"] - p["backT"]
    zG = -H / 2 + p["glassBot"] + p["glassH"] / 2
    zBand0, zCeil = zG + p["glassH"] / 2 + p["glassFit"], H / 2 - wt
    zX = (zBand0 + zCeil) / 2
    noir = robot_rendu.material("Module noir", (0.02, 0.02, 0.022), 0.4)
    vert = robot_rendu.material("Circuit du radar", (0.01, 0.05, 0.03), 0.35)
    bleu = robot_rendu.material("Circuit du PCF8574T", (0.01, 0.05, 0.22), 0.35)
    verre = robot_rendu.material("Objectif", (0.01, 0.015, 0.03), 0.05)
    blanc = robot_rendu.material("Collerette du BH1750", (0.86, 0.84, 0.80), 0.4)
    dore = robot_rendu.material("Doré", (0.85, 0.6, 0.2), 0.25)
    dore.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 1.0

    # caméra : module carré dans le berceau du bandeau, objectif dans le trou de la façade
    zc, a, b = zX + p["camOffZ"], p["camModW"] / 2, p["camModH"] / 2
    module(bpy, "compo_camera", [
        ("pave", (-a, a, wt, wt + p["camModT"], zc - b, zc + b), noir),
        ("cyl", (0, zc, p["camD"] / 2 - 0.6, 0.6, wt), noir),
        ("cyl", (0, zc, p["camD"] / 2 - 1.6, 0.4, 0.6), verre),
    ])
    # radar : circuit contre la façade, antennes gravées vers l'avant, composants au dos
    x0, x1, z0, z1 = p["radX0"], p["radX0"] + p["radW"], zX - p["radH"] / 2, zX + p["radH"] / 2
    radar = [("pave", (x0, x1, wt + 0.1, wt + 1.7, z0, z1), vert),
             ("pave", (x0 + 2, x0 + 10, wt + 1.7, wt + p["radT"], z0 + 4, z1 - 4), noir)]
    radar += [("pave", (x0 + 12 + 2.3 * i, x0 + 13.2 + 2.3 * i, wt, wt + 0.1, z0 + 2, z1 - 2), dore) for i in range(4)]
    module(bpy, "compo_radar", radar)
    # PCF8574T : debout contre l'arrière du bandeau, circuit au fond, barrettes aux deux bouts, broches vers l'avant
    L, h, u0, u1 = p["pcfL"], p["pcfH"], p["pcfU"], p["pcfU"] + p["pcfT"]
    zc = zCeil - 0.3 - h / 2
    pcf = [("pave", (-L / 2, L / 2, u1 - 1.6, u1, zc - h / 2, zc + h / 2), bleu),
           ("pave", (-5, 5, u1 - 3.1, u1 - 1.6, zc - 3, zc + 3), noir)]
    for x in (-L / 2 + 3.5, L / 2 - 3.5):
        pcf.append(("pave", (x - 1.25, x + 1.25, u1 - 4.1, u1 - 1.6, zc - 5.1, zc + 5.1), noir))
        pcf += [("pave", (x - 0.32, x + 0.32, u0, u1 - 4.1, zc + k - 0.32, zc + k + 0.32), dore) for k in (-3.81, -1.27, 1.27, 3.81)]
    module(bpy, "compo_pcf8574", pcf)
    # BH1750 : collerette de la boule (la boule est dans stl/), retenue à l'intérieur du dos
    module(bpy, "compo_bh1750_collerette", [("cyl", (0, p["domeZ"], p["domeBaseD"] / 2, shellD - 1.6, shellD), blanc)])
    # pogo du cou, une paire 5 contacts au pas de 2,5 (25 × 4 avec les oreilles, 20 sans, 4 de haut) : nez de 2 dans la
    # fente au sommet du collier ou du méplat de la tête, oreilles en appui derrière ; moitié à ressorts dans le torse,
    # moitié à plots dans la tête, un aimant à chaque bout du nez
    aimant = robot_rendu.material("Aimant", (0.62, 0.62, 0.64), 0.3)
    aimant.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 1.0
    zTop, u = H / 2 + p["neckH"], p["neckU"]

    def oblong(L, z0, z1):
        return [("pave", (-(L - 4) / 2, (L - 4) / 2, u - 2, u + 2, z0, z1), noir)] + \
               [("cylz", (s * (L - 4) / 2, u, 2, z0, z1), noir) for s in (1, -1)]

    for nom, s, contact, rayon in (("compo_pogo_torse", -1, 0.8, 0.55), ("compo_pogo_tete", 1, 0.05, 0.8)):
        z = lambda a, b: (min(zTop + s * a, zTop + s * b), max(zTop + s * a, zTop + s * b))
        pogo = oblong(20, *z(0, 2)) + oblong(25, *z(2, 4))
        pogo += [("cylz", (2.5 * (i - 2), u, rayon, *z(-contact, 0.5)), dore) for i in range(5)]
        pogo += [("cylz", (x, u, 1.4, *z(-0.05, 0.5)), aimant) for x in (-8, 8)]
        module(bpy, nom, pogo)


def boites(sc, noms):
    from mathutils import Vector
    return {n: [sc.objects[n].matrix_world @ Vector(c) for c in sc.objects[n].bound_box] for n in noms}


def cadrer(bpy, sc, cam, pts, largeur, hauteur, azimut=AZIMUT, elevation=ELEVATION):
    """Place la caméra sur l'axe azimut/elevation pour que pts tiennent dans largeur × hauteur (fractions du cadre)."""
    from bpy_extras.object_utils import world_to_camera_view
    from mathutils import Vector
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    centre = (lo + hi) / 2
    axe = Vector((math.sin(azimut) * math.cos(elevation), -math.cos(azimut) * math.cos(elevation), math.sin(elevation)))
    cam.rotation_euler = (-axe).to_track_quat("-Z", "Y").to_euler()
    cam.data.shift_x = cam.data.shift_y = 0
    d = 800
    ratio = sc.render.resolution_y / sc.render.resolution_x
    for i in range(8):
        cam.location = centre + axe * d
        bpy.context.view_layer.update()
        q = [world_to_camera_view(sc, cam, p) for p in pts]
        xs, ys = [p.x for p in q], [p.y for p in q]
        if i < 5:
            d *= max((max(xs) - min(xs)) / largeur, (max(ys) - min(ys)) / hauteur)
        else:                    # puis recentrer par décentrement de l'objectif
            cam.data.shift_x += (max(xs) + min(xs)) / 2 - 0.5
            cam.data.shift_y += ((max(ys) + min(ys)) / 2 - 0.5) * ratio


def bord(bpy, sc, cam, objets, colonne, fz):
    """Point du contour des objets, vu de la caméra, du côté de la colonne de leur nom (g ou d), à la hauteur fz de
    leur boîte : on balaie l'image depuis ce bord jusqu'au premier rayon qui touche l'un d'eux, puis on rentre un peu."""
    from bpy_extras.object_utils import world_to_camera_view
    from mathutils import Vector
    b = sum(boites(sc, objets).values(), [])
    lo = Vector([min(p[i] for p in b) for i in range(3)])
    hi = Vector([max(p[i] for p in b) for i in range(3)])
    sy = world_to_camera_view(sc, cam, Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z + fz * (hi.z - lo.z)))).y
    tr, br, bl, tl = [cam.matrix_world @ c for c in cam.data.view_frame(scene=sc)]
    graphe = bpy.context.evaluated_depsgraph_get()
    pas = -0.001 if colonne == "d" else 0.001
    sx = 1.0 if colonne == "d" else 0.0
    while 0 <= sx <= 1:
        cible = bl + (br - bl) * sx + (tl - bl) * sy
        touche, _, _, _, ob, _ = sc.ray_cast(graphe, cam.location, (cible - cam.location).normalized())
        if touche and ob.name in objets:
            return [sx + 6 * pas, 1 - sy]
        sx += pas
    raise RuntimeError("aucun rayon ne touche %s" % (objets,))


def render_groupe(bpy, sc, cam, nom, g, folder):
    from bpy_extras.object_utils import world_to_camera_view
    from mathutils import Vector
    dec = g["decalage"]
    for ob in sc.objects:
        if ob.type == "MESH" and ob.name != "Sol et fond":
            ob.hide_render = ob.name not in dec
    base = {n: sc.objects[n].location.copy() for n in dec}
    colonnes = "azimut" in g

    def poser(t):
        for n, v in dec.items():
            if colonnes:                     # robot complet : x, y et z ensemble
                d = Vector(v) * douceur(t)
            else:                            # y, puis z une fois le y fait
                dy, dz = v if isinstance(v, tuple) else (v, 0)
                d = Vector((0, dy * douceur(t), dz * douceur(max(0.0, 2 * t - 1))))
            sc.objects[n].location = base[n] + d
        bpy.context.view_layer.update()

    # vue fixe, éclatée, avec la place des noms en haut et en bas, ou à gauche et à droite pour le robot complet
    w, h, samples = g.get("vue", VUE)
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.cycles.samples = samples
    poser(1)
    cadre = (1 - 2 * COLONNE, 0.94) if colonnes else (0.9, 1 - 2 * BANDE - 0.04)
    cadrer(bpy, sc, cam, sum(boites(sc, dec).values(), []), *cadre, g.get("azimut", AZIMUT), g.get("elevation", ELEVATION))
    sc.render.filepath = os.path.join(folder, nom + "_vue.png")
    bpy.ops.render.render(write_still=True)

    def reperes(noms, bandes):
        liste = []
        for n, fr, en, fichier_fr, fichier_en, *place in noms:
            r = {"fr": [fr, fichier_fr], "en": [en, fichier_en]}
            if not bandes:
                r["colonne"], r["ancre"] = place[0], bord(bpy, sc, cam, n, *place)
            else:
                b = boites(sc, [n])[n]
                lo = Vector([min(p[i] for p in b) for i in range(3)])
                hi = Vector([max(p[i] for p in b) for i in range(3)])
                haut = world_to_camera_view(sc, cam, Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, hi.z)))
                bas = world_to_camera_view(sc, cam, Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z)))
                r.update({"haut": [haut.x, 1 - haut.y], "bas": [bas.x, 1 - bas.y],
                          "leve": isinstance(dec[n], tuple) and len(dec[n]) == 2})
            liste.append(r)
        return liste

    resultat = {"reperes": reperes(g["noms"], not colonnes), "colonnes": colonnes}

    if "loupe" in g:     # gros plan : les objets seuls, même azimut, vus d'un peu plus haut ; zone repérée sur la vue
        objets = [n for n, *_ in g["loupe"]]
        pts = sum(boites(sc, objets).values(), [])
        q = [world_to_camera_view(sc, cam, p) for p in pts]
        resultat["zone"] = [min(p.x for p in q), 1 - max(p.y for p in q), max(p.x for p in q), 1 - min(p.y for p in q)]
        garde = cam.location.copy(), cam.rotation_euler.copy(), cam.data.shift_x, cam.data.shift_y
        for ob in sc.objects:
            if ob.type == "MESH" and ob.name != "Sol et fond":
                ob.hide_render = ob.name not in objets
        w, h, samples, elevation = LOUPE
        sc.render.resolution_x, sc.render.resolution_y = w, h
        sc.cycles.samples = samples
        cadrer(bpy, sc, cam, pts, 0.9, 1 - 2 * BANDE - 0.04, g["azimut"], elevation)
        sc.render.filepath = os.path.join(folder, nom + "_loupe.png")
        bpy.ops.render.render(write_still=True)
        resultat["loupe"] = reperes(g["loupe"], True)
        cam.location, cam.rotation_euler, cam.data.shift_x, cam.data.shift_y = garde
        for ob in sc.objects:
            if ob.type == "MESH" and ob.name != "Sol et fond":
                ob.hide_render = ob.name not in dec

    # GIF : même cadrage (celui de la vue éclatée), pièces de 0 (en place) à 1 (écartées)
    w, h, samples, n_images = g.get("gif", GIF)
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.cycles.samples = samples
    for k in range(n_images + 1):
        poser(k / n_images)
        sc.render.filepath = os.path.join(folder, "%s_%02d.png" % (nom, k))
        bpy.ops.render.render(write_still=True)
    poser(0)
    resultat["images"] = n_images
    return resultat


def render_all(noms=None):
    import bpy
    sys.path.insert(0, HERE)
    import robot_rendu
    robot_rendu.COTES.update(json.load(open(os.path.join(HERE, "stl", "cotes.json"))))
    sc = robot_rendu.build_scene()
    sc.cycles.use_denoising = True
    sc.render.use_persistent_data = True     # garde la scène chargée d'une image du GIF à l'autre
    gpu(bpy, sc)
    modules(bpy, robot_rendu)
    cam = bpy.data.objects.new("Caméra éclaté", bpy.data.cameras.new("Caméra éclaté"))
    cam.data.lens = 85
    cam.data.clip_start, cam.data.clip_end = 10, 10000
    sc.collection.objects.link(cam)
    sc.camera = cam
    folder = tempfile.mkdtemp(prefix="eclate_")
    manifest = {}
    for nom, g in GROUPES.items():
        if noms and nom not in noms:
            continue
        print("vue éclatée :", nom, flush=True)
        manifest[nom] = render_groupe(bpy, sc, cam, nom, g, folder)
    with open(os.path.join(folder, "eclate.json"), "w") as f:
        json.dump(manifest, f)
    try:
        subprocess.run(["python3", os.path.abspath(__file__), "--assembler", folder], check=True)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


# ── noms et GIF (python3 avec Pillow, hors de Blender) ──

def font(size, gras=False):
    from PIL import ImageFont
    if os.path.exists("/System/Library/Fonts/Helvetica.ttc"):
        return ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", size, index=1 if gras else 0)
    return ImageFont.load_default(size)


def nommer(vue, reperes, langue):
    """Noms en bande haute et basse, reliés à la pièce par un trait : chaque pièce prend la bande la plus proche de son
    bord, les modules levés au-dessus de la coque la bande haute, puis les bandes s'équilibrent à un nom près."""
    from PIL import ImageDraw
    im = vue.copy()
    d = ImageDraw.Draw(im)
    W, H = im.size
    f_nom, f_fichier = font(24, True), font(16)
    marge, ecart = 24, 28
    reps = sorted(reperes, key=lambda r: r["haut"][0])

    def preference(r):                   # < 0 : la bande haute est la plus proche
        return (r["haut"][1] - BANDE) - ((1 - BANDE) - r["bas"][1])

    haut = [r["leve"] or preference(r) < 0 for r in reps]
    while True:
        n = sum(haut)
        if n > len(reps) - n + 1 and any(h and not r["leve"] for h, r in zip(haut, reps)):
            i = max((i for i, r in enumerate(reps) if haut[i] and not r["leve"]), key=lambda i: preference(reps[i]))
        elif len(reps) - n > n + 1:
            i = min((i for i in range(len(reps)) if not haut[i]), key=lambda i: preference(reps[i]))
        else:
            break
        haut[i] = not haut[i]
    etiquettes = []
    for h, r in zip(haut, reps):
        nom, fichier = r[langue]
        largeur = max(d.textlength(nom, font=f_nom), d.textlength(fichier, font=f_fichier))
        ax, ay = r["haut"] if h else r["bas"]
        etiquettes.append({"haut": h, "x": ax * W, "ancre": (ax * W, ay * H), "w": largeur, "nom": nom, "fichier": fichier})
    for bande in (True, False):          # écarter les noms d'une bande, les centrer sur leurs pièces, rester dans l'image
        ligne = [e for e in etiquettes if e["haut"] == bande]
        if not ligne:
            continue
        for a, b in zip(ligne, ligne[1:]):
            b["x"] = max(b["x"], a["x"] + (a["w"] + b["w"]) / 2 + ecart)
        decal = sum(e["ancre"][0] - e["x"] for e in ligne) / len(ligne)
        decal = min(decal, W - marge - ligne[-1]["x"] - ligne[-1]["w"] / 2)
        decal = max(decal, marge - ligne[0]["x"] + ligne[0]["w"] / 2)
        for e in ligne:
            e["x"] += decal
    for e in etiquettes:
        y_nom = H * BANDE * 0.42 if e["haut"] else H * (1 - BANDE * 0.62)
        y_bout = y_nom + ((48 if e["fichier"] else 22) if e["haut"] else -14)
        d.line([(e["x"], y_bout), e["ancre"]], fill=(138, 144, 153), width=2)
        ax, ay = e["ancre"]
        d.ellipse([ax - 5, ay - 5, ax + 5, ay + 5], fill=(232, 115, 12))
        d.text((e["x"], y_nom), e["nom"], font=f_nom, fill=(244, 241, 234), anchor="mm")
        if e["fichier"]:
            d.text((e["x"], y_nom + 26), e["fichier"], font=f_fichier, fill=(169, 173, 179), anchor="mm")
    return im


def nommer_colonnes(vue, reperes, langue):
    """Robot complet : noms en colonnes à gauche et à droite, chacun à la hauteur de sa pièce, écartés d'un pas et
    recentrés sur leurs pièces, reliés au contour de la pièce par un trait."""
    from PIL import ImageDraw
    im = vue.copy()
    d = ImageDraw.Draw(im)
    W, H = im.size
    f_nom, f_fichier = font(24, True), font(16)
    marge, pas = 28, 66
    for colonne in ("g", "d"):
        ligne = sorted((r for r in reperes if r["colonne"] == colonne), key=lambda r: r["ancre"][1])
        ys = [r["ancre"][1] * H - 13 for r in ligne]          # le trait part du milieu du nom et de son fichier
        for i in range(1, len(ys)):
            ys[i] = max(ys[i], ys[i - 1] + pas)
        decal = sum(r["ancre"][1] * H - 13 - y for r, y in zip(ligne, ys)) / len(ys)
        decal = max(min(decal, H - marge - 40 - ys[-1]), marge + 14 - ys[0])
        for r, y in zip(ligne, ys):
            y += decal
            nom, fichier = r[langue]
            largeur = max(d.textlength(nom, font=f_nom), d.textlength(fichier, font=f_fichier))
            x, ancre_texte, x_trait = (marge, "lm", marge + largeur + 14) if colonne == "g" else \
                                      (W - marge, "rm", W - marge - largeur - 14)
            ax, ay = r["ancre"][0] * W, r["ancre"][1] * H
            d.line([(x_trait, y + 13), (ax, ay)], fill=(138, 144, 153), width=2)
            d.ellipse([ax - 5, ay - 5, ax + 5, ay + 5], fill=(232, 115, 12))
            d.text((x, y), nom, font=f_nom, fill=(244, 241, 234), anchor=ancre_texte)
            d.text((x, y + 26), fichier, font=f_fichier, fill=(169, 173, 179), anchor=ancre_texte)
    return im


def poser_loupe(im, loupe, zone):
    """Gros plan en haut à droite, cerné, relié par un trait au cadre de sa zone sur la vue."""
    from PIL import ImageDraw
    d = ImageDraw.Draw(im)
    W, H = im.size
    gris, marge, bord = (138, 144, 153), 28, 14
    x0, y0 = W - marge - loupe.width, marge
    im.paste(loupe, (x0, y0))
    d.rounded_rectangle([x0, y0, x0 + loupe.width, y0 + loupe.height], radius=10, outline=gris, width=2)
    z = [zone[0] * W - bord, zone[1] * H - bord, zone[2] * W + bord, zone[3] * H + bord]
    d.rounded_rectangle(z, radius=10, outline=gris, width=2)
    d.line([(z[2], (z[1] + z[3]) / 2), (x0, y0 + loupe.height / 2)], fill=gris, width=2)


def assemble(folder):
    from PIL import Image
    with open(os.path.join(folder, "eclate.json")) as f:
        manifest = json.load(f)
    os.makedirs(OUTDIR, exist_ok=True)
    for nom, m in manifest.items():
        vue = Image.open(os.path.join(folder, nom + "_vue.png")).convert("RGB")
        for langue, suffixe in (("fr", ""), ("en", "_en")):
            chemin = os.path.join(OUTDIR, "eclate_%s%s.png" % (nom, suffixe))
            im = (nommer_colonnes if m["colonnes"] else nommer)(vue, m["reperes"], langue)
            if "loupe" in m:
                loupe = Image.open(os.path.join(folder, nom + "_loupe.png")).convert("RGB")
                poser_loupe(im, nommer(loupe, m["loupe"], langue), m["zone"])
            im.save(chemin, optimize=True)
            print("écrit rendus/%s" % os.path.basename(chemin))
        # en place 0,7 s, écartement, écarté 1,2 s, remontage
        n = m["images"]
        ordre = [0] * 8 + list(range(n + 1)) + [n] * 14 + list(range(n, -1, -1))
        images = os.path.join(folder, nom + "_gif")
        os.makedirs(images, exist_ok=True)
        for i, k in enumerate(ordre):
            shutil.copy(os.path.join(folder, "%s_%02d.png" % (nom, k)), os.path.join(images, "%04d.png" % i))
        gif = os.path.join(OUTDIR, "eclate_%s.gif" % nom)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", os.path.join(images, "%04d.png"),
                        "-vf", "split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];"
                               "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
                        "-loop", "0", gif], check=True)
        print("écrit rendus/%s (%.1f Mo)" % (os.path.basename(gif), os.path.getsize(gif) / 1e6))


if __name__ == "__main__":
    if "--assembler" in sys.argv:
        assemble(sys.argv[sys.argv.index("--assembler") + 1])
    else:                                # après « -- » : les vues à rendre (tete, torse, robot), toutes sinon
        render_all(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else None)
