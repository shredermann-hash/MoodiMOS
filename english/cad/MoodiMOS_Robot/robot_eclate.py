# MoodiMOS — exploded views of the head, the torso and the complete robot, for steps 9, 10 and 11 of the guide
# 
# Run from the project root, after robot_parts.py and robot_limbs.py:
#   blender -b -P cad/MoodiMOS_Robot/robot_eclate.py               (all views)
#   blender -b -P cad/MoodiMOS_Robot/robot_eclate.py -- robot      (a single one: head, torso or robot)
# Reuses the scene of robot_render.py (parts in place from stl/, colours, screens, studio), keeps only the head or the
# torso and moves the parts apart along y. The screens are flush with the front: the glass and its board come first,
# then bezel, plate and dome for the head, frame, front, body, speaker box and back for the torso.
# The modules that stl/ does not contain (camera, radar, PCF8574T, BH1750 collar, the two halves of the neck pogo)
# are modelled here with the dimensions and positions of encombrements() in ../MoodiMOS_Torso/MoodiMOS_Torso.py.
# Hidden in the body, they come out of it, then rise above it (offset (y, z): z comes after y). The assembly order
# stays that of the text of steps 9 and 10. The complete robot keeps the head and torso assembled, moves the limbs
# apart and places the pogo between the head and the torso. Writes to renders/:
#   eclate_<head|torso|robot>.png and _en.png: exploded view, part names in French or in English
#   eclate_<head|torso|robot>.gif: the parts separate, then come back together, without text
# Blender renders the images in a temporary folder; python3 (Pillow) adds the names, ffmpeg assembles the GIFs.

import json, math, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, "renders")

# scene object: y offset (mm, + towards the back) in the exploded view; the paint layers follow their part
GROUPES = {
    "head": {
        "decalage": {
            "glass_head": -95, 'Head display (screen)': -95,
            "head_bezel": -50, "paint_marks": -50, 'paint_trim_bezel': -50,
            "head_plate": 0, "paint_bail": 0,
            "head_back": 45, "paint_medallion": 45, "paint_strip_headphone_right": 45, "paint_strip_headphone_left": 45,
        },
        # reference object, French name, English name, French file, English file
        "noms": [
            ("head_bezel", "Cerclage", "Bezel", "head_bezel.stl", "head_bezel.stl"),
            ("glass_head", "Glass et carte 2.1", "Glass and 2.1 board", "", ""),
            ("head_plate", "Plate", "Plate", "head_plate.stl", "head_plate.stl"),
            ("head_back", 'Dome', "Dome", "head_back.stl", "head_back.stl"),
        ],
    },
    "torso": {
        "decalage": {
            "glass_torso": -80, 'Torso screen': -80,
            "torso_frame": -55,
            "torso_front": -30, "paint_screws": -30,
            "component_camera": (-14, 22), "component_radar": (-14, 22),
            "torso_body": 0, "torso_strip_right": 0, "torso_strip_left": 0,
            "component_pcf8574": (34, 25),
            "torso_speaker_box": 45,
            "component_bh1750": 40, "component_bh1750_collerette": 40,
            "torso_back": 95,
        },
        "noms": [
            ("torso_frame", "Cadre noir", "Black frame", "torso_frame.stl", "torso_frame.stl"),
            ("torso_front", 'Speaker grille (face panel)', "Front panel", "torso_front.stl", "torso_front.stl"),
            ("component_camera", 'OV5640 camera module', "OV5640 camera", "", ""),
            ("component_radar", "Radar LD2410C", "LD2410C radar", "", ""),
            ("torso_body", "Body", "Body", "torso_body.stl", "torso_body.stl"),
            ("component_pcf8574", "Expandeur PCF8574T", "PCF8574T expander", "", ""),
            ("glass_torso", "Carte 3.5-C", "3.5-C board", "", ""),
            ("torso_speaker_box", 'Speaker enclosure', "Speaker box", "torso_speaker_box.stl", "torso_speaker_box.stl"),
            ("torso_back", "Back", "Back", "torso_back.stl", "torso_back.stl"),
            ("component_bh1750", "BH1750 et sa boule", "BH1750 and its ball", "", ""),
        ],
    },
}

AZIMUT, ELEVATION = math.radians(50), math.radians(18)   # camera: 50° to the right of the front, 18° above
VUE = (1500, 1000, 64)          # still view: width, height, Cycles samples
GIF = (600, 400, 16, 18)        # GIF: width, height, samples, frames of the separation
FPS = 12
BANDE = 0.16                    # top and bottom of the still view kept for the names
COLONNE = 0.2                   # complete robot: left and right of the still view kept for the names
LOUPE = (520, 540, 64, math.radians(25))   # pogo close-up: width, height, samples, elevation

# Complete robot, for step 11: head and torso assembled (their parts are in the views above), the two halves
# of the neck pogo between them, each limb split into its printed parts. The sneakers stay on the ground, the rest
# rises and the limbs move outwards; everything moves together (offset (x, y, z), x for the right side).
TORSE_Z = 80
MEMBRES = [   # printed part, scene objects (without _right/_left), offset
    ("sneaker", ('sole', "trim", 'toe', 'edge_toe', 'upper', 'side', 'collar', 'laces', 'roundel', "vent_sneaker",
                'heel'), (0, 0, 0)),
    ("leg", ("leg", "ring_leg"), (0, 0, 50)),
    ("glove_cuff", ("glove", "cuff", 'wrist_glove', "trim_glove"), (62, 0, 40)),
    ("arm", ("arm", "ring_arm"), (62, 0, TORSE_Z)),
    ("shoulder", ("shoulder",), (28, 0, TORSE_Z)),
]


def groupe_robot():
    dec = {n: (0, 0, TORSE_Z) for n in GROUPES["torso"]["decalage"]}
    dec.update({n: (0, 0, TORSE_Z + 70) for n in GROUPES["head"]["decalage"]})
    dec.update({"component_pogo_torso": (0, 0, TORSE_Z + 22), "component_pogo_head": (0, 0, TORSE_Z + 46)})
    for _, objets, (dx, dy, dz) in MEMBRES:
        for cote, sens in (("right", 1), ("left", -1)):
            dec.update({o + "_" + cote: (sens * dx, dy, dz) for o in objets})
    pieces = {piece: objets for piece, objets, _ in MEMBRES}
    d = lambda piece: tuple(o + "_right" for o in pieces[piece])
    return {
        "decalage": dec, "azimut": math.radians(30), "elevation": math.radians(15),
        "vue": (1500, 1500, 64), "gif": (600, 600, 16, 18),
        # reference objects, names and files, column (g for left, d for right) and height of the leader line in the objects' box
        "noms": [
            (("head_plate", "head_back", "head_bezel"), 'Head', "Head", 'step 9', "step 9", "g", 0.5),
            (("torso_body", "torso_back", "torso_front"), "Torso", "Torso", 'step 10', "step 10", "g", 0.08),
            (d("shoulder"), "Shoulder", "Shoulder", "shoulder_right.stl", "shoulder_right.stl", "d", 0.5),
            (d("arm"), "Arm", "Arm", "arm_right.stl", "arm_right.stl", "d", 0.6),
            (d("glove_cuff"), "Glove et cuff", "Glove and cuff", "glove_cuff_right.stl",
             "glove_cuff_right.stl", "d", 0.5),
            (d("leg"), "Leg", "Leg", "leg_right.stl", "leg_right.stl", "d", 0.12),
            (d("sneaker"), "Sneaker", "Sneaker", "sneaker_right.stl", "sneaker_right.stl", "d", 0.5),
        ],
        # close-up of the neck pogo, on its own, at the top right of the still view; names at the top and bottom
        "loupe": [
            ("component_pogo_head", 'Pogo, pad half', "Pogo, pad half", 'in the head', "in the head"),
            ("component_pogo_torso", 'Pogo, spring half', "Pogo, spring half", 'in the torso', "in the torso"),
        ],
    }


GROUPES["robot"] = groupe_robot()


def douceur(t):
    return t * t * (3 - 2 * t)


# ── rendering (in Blender) ──

def gpu(bpy, sc):
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        sc.cycles.device = "GPU"
    except Exception:            # no Metal: Cycles stays on the CPU
        pass


def dimensions_torso():
    "PARAMS of the main torso CAD, with this robot's style (robot_parts.py does the same)."
    import ast
    from robot_style import TORSO_STYLE
    chemin = os.path.join(HERE, "..", "MoodiMOS_Torso", "MoodiMOS_Torso.py")
    for node in ast.parse(open(chemin, encoding="utf-8").read()).body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "PARAMS":
            return dict(ast.literal_eval(node.value), **TORSO_STYLE)
    raise RuntimeError('PARAMS not found in:' + chemin)


def module(bpy, nom, pieces):
    'Object made of boxes (x0, x1, y0, y1, z0, z1), of y-axis cylinders (x, z, radius, y0, y1) and of z-axis cylinders\n    ("cylz": x, y, radius, z0, z1), each with its own material.'
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


def modules(bpy, robot_render):
    'Camera, radar, PCF8574T and BH1750 collar, with the dimensions of encombrements() (MoodiMOS_Torso.py).'
    p = dimensions_torso()
    H, wt = p["torsoH"], p["wallT"]
    shellD = p["torsoD"] - p["backT"]
    zG = -H / 2 + p["glassBot"] + p["glassH"] / 2
    zBand0, zCeil = zG + p["glassH"] / 2 + p["glassFit"], H / 2 - wt
    zX = (zBand0 + zCeil) / 2
    noir = robot_render.material("Module noir", (0.02, 0.02, 0.022), 0.4)
    vert = robot_render.material("Circuit du radar", (0.01, 0.05, 0.03), 0.35)
    bleu = robot_render.material("Circuit du PCF8574T", (0.01, 0.05, 0.22), 0.35)
    verre = robot_render.material("Objectif", (0.01, 0.015, 0.03), 0.05)
    blanc = robot_render.material("Collerette du BH1750", (0.86, 0.84, 0.80), 0.4)
    dore = robot_render.material('Gold', (0.85, 0.6, 0.2), 0.25)
    dore.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 1.0

    # camera: square module in the cradle of the band, lens in the hole of the front
    zc, a, b = zX + p["camOffZ"], p["camModW"] / 2, p["camModH"] / 2
    module(bpy, "component_camera", [
        ("pave", (-a, a, wt, wt + p["camModT"], zc - b, zc + b), noir),
        ("cyl", (0, zc, p["camD"] / 2 - 0.6, 0.6, wt), noir),
        ("cyl", (0, zc, p["camD"] / 2 - 1.6, 0.4, 0.6), verre),
    ])
    # radar: board against the front, etched antennas facing forward, components at the back
    x0, x1, z0, z1 = p["radX0"], p["radX0"] + p["radW"], zX - p["radH"] / 2, zX + p["radH"] / 2
    radar = [("pave", (x0, x1, wt + 0.1, wt + 1.7, z0, z1), vert),
             ("pave", (x0 + 2, x0 + 10, wt + 1.7, wt + p["radT"], z0 + 4, z1 - 4), noir)]
    radar += [("pave", (x0 + 12 + 2.3 * i, x0 + 13.2 + 2.3 * i, wt, wt + 0.1, z0 + 2, z1 - 2), dore) for i in range(4)]
    module(bpy, "component_radar", radar)
    # PCF8574T: upright against the back of the band, board at the far side, headers at both ends, pins facing forward
    L, h, u0, u1 = p["pcfL"], p["pcfH"], p["pcfU"], p["pcfU"] + p["pcfT"]
    zc = zCeil - 0.3 - h / 2
    pcf = [("pave", (-L / 2, L / 2, u1 - 1.6, u1, zc - h / 2, zc + h / 2), bleu),
           ("pave", (-5, 5, u1 - 3.1, u1 - 1.6, zc - 3, zc + 3), noir)]
    for x in (-L / 2 + 3.5, L / 2 - 3.5):
        pcf.append(("pave", (x - 1.25, x + 1.25, u1 - 4.1, u1 - 1.6, zc - 5.1, zc + 5.1), noir))
        pcf += [("pave", (x - 0.32, x + 0.32, u0, u1 - 4.1, zc + k - 0.32, zc + k + 0.32), dore) for k in (-3.81, -1.27, 1.27, 3.81)]
    module(bpy, "component_pcf8574", pcf)
    # BH1750: collar of the ball (the ball is in stl/), held inside the back
    module(bpy, "component_bh1750_collerette", [("cyl", (0, p["domeZ"], p["domeBaseD"] / 2, shellD - 1.6, shellD), blanc)])
    # neck pogo, a pair of 5 contacts at a 2.5 pitch (25 × 4 with the ears, 20 without, 4 high): 2 mm nose in the
    # slot at the top of the collar or of the head's flat, ears resting behind; spring half in the torso,
    # pad half in the head, a magnet at each end of the nose
    magnet = robot_render.material("Aimant", (0.62, 0.62, 0.64), 0.3)
    magnet.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 1.0
    zTop, u = H / 2 + p["neckH"], p["neckU"]

    def oblong(L, z0, z1):
        return [("pave", (-(L - 4) / 2, (L - 4) / 2, u - 2, u + 2, z0, z1), noir)] + \
               [("cylz", (s * (L - 4) / 2, u, 2, z0, z1), noir) for s in (1, -1)]

    for nom, s, contact, rayon in (("component_pogo_torso", -1, 0.8, 0.55), ("component_pogo_head", 1, 0.05, 0.8)):
        z = lambda a, b: (min(zTop + s * a, zTop + s * b), max(zTop + s * a, zTop + s * b))
        pogo = oblong(20, *z(0, 2)) + oblong(25, *z(2, 4))
        pogo += [("cylz", (2.5 * (i - 2), u, rayon, *z(-contact, 0.5)), dore) for i in range(5)]
        pogo += [("cylz", (x, u, 1.4, *z(-0.05, 0.5)), magnet) for x in (-8, 8)]
        module(bpy, nom, pogo)


def boites(sc, noms):
    from mathutils import Vector
    return {n: [sc.objects[n].matrix_world @ Vector(c) for c in sc.objects[n].bound_box] for n in noms}


def cadrer(bpy, sc, cam, pts, largeur, hauteur, azimut=AZIMUT, elevation=ELEVATION):
    'Places the camera on the azimuth/elevation axis so that pts fit in width × height (fractions of the frame).'
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
        else:                    # then recentre by shifting the lens
            cam.data.shift_x += (max(xs) + min(xs)) / 2 - 0.5
            cam.data.shift_y += ((max(ys) + min(ys)) / 2 - 0.5) * ratio


def bord(bpy, sc, cam, objets, colonne, fz):
    "Point on the outline of the objects, seen from the camera, on the side of their name's column (g or d), at the\n    height fz of their box: the image is swept from that edge to the first ray that hits one of them, then moved in a little."
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
    raise RuntimeError('no ray hits %s' % (objets,))


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
            if colonnes:                     # complete robot: x, y and z together
                d = Vector(v) * douceur(t)
            else:                            # y, then z once y is done
                dy, dz = v if isinstance(v, tuple) else (v, 0)
                d = Vector((0, dy * douceur(t), dz * douceur(max(0.0, 2 * t - 1))))
            sc.objects[n].location = base[n] + d
        bpy.context.view_layer.update()

    # still view, exploded, with room for the names at the top and bottom, or left and right for the complete robot
    w, h, samples = g.get("vue", VUE)
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.cycles.samples = samples
    poser(1)
    frame = (1 - 2 * COLONNE, 0.94) if colonnes else (0.9, 1 - 2 * BANDE - 0.04)
    cadrer(bpy, sc, cam, sum(boites(sc, dec).values(), []), *frame, g.get("azimut", AZIMUT), g.get("elevation", ELEVATION))
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

    if "loupe" in g:     # close-up: the objects alone, same azimuth, seen from slightly higher; area marked on the view
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

    # GIF: same framing (that of the exploded view), parts from 0 (in place) to 1 (moved apart)
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
    import robot_render
    robot_render.COTES.update(json.load(open(os.path.join(HERE, "stl", "dimensions.json"))))
    sc = robot_render.build_scene()
    sc.cycles.use_denoising = True
    sc.render.use_persistent_data = True     # keeps the scene loaded from one GIF frame to the next
    gpu(bpy, sc)
    modules(bpy, robot_render)
    cam = bpy.data.objects.new('Exploded camera', bpy.data.cameras.new('Exploded camera'))
    cam.data.lens = 85
    cam.data.clip_start, cam.data.clip_end = 10, 10000
    sc.collection.objects.link(cam)
    sc.camera = cam
    folder = tempfile.mkdtemp(prefix="eclate_")
    manifest = {}
    for nom, g in GROUPES.items():
        if noms and nom not in noms:
            continue
        print('exploded view:', nom, flush=True)
        manifest[nom] = render_groupe(bpy, sc, cam, nom, g, folder)
    with open(os.path.join(folder, "eclate.json"), "w") as f:
        json.dump(manifest, f)
    try:
        subprocess.run(["python3", os.path.abspath(__file__), "--assembler", folder], check=True)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


# ── names and GIF (python3 with Pillow, outside Blender) ──

def font(size, gras=False):
    from PIL import ImageFont
    if os.path.exists("/System/Library/Fonts/Helvetica.ttc"):
        return ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", size, index=1 if gras else 0)
    return ImageFont.load_default(size)


def nommer(vue, reperes, langue):
    'Names in a top and a bottom band, linked to the part by a line: each part takes the band nearest its edge,\n    the modules lifted above the body take the top band, then the bands are balanced to within one name.'
    from PIL import ImageDraw
    im = vue.copy()
    d = ImageDraw.Draw(im)
    W, H = im.size
    f_nom, f_fichier = font(24, True), font(16)
    marge, ecart = 24, 28
    reps = sorted(reperes, key=lambda r: r["haut"][0])

    def preference(r):                   # < 0: the top band is the nearest
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
    for bande in (True, False):          # spread the names of a band, centre them on their parts, stay inside the image
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
    'Complete robot: names in columns on the left and right, each at the height of its part, spaced by one step and\n    recentred on their parts, linked to the outline of the part by a line.'
    from PIL import ImageDraw
    im = vue.copy()
    d = ImageDraw.Draw(im)
    W, H = im.size
    f_nom, f_fichier = font(24, True), font(16)
    marge, pas = 28, 66
    for colonne in ("g", "d"):
        ligne = sorted((r for r in reperes if r["colonne"] == colonne), key=lambda r: r["ancre"][1])
        ys = [r["ancre"][1] * H - 13 for r in ligne]          # the line starts from the middle of the name and its file
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
    'Close-up at the top right, outlined, linked by a line to the frame of its area on the view.'
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
            print("écrit renders/%s" % os.path.basename(chemin))
        # in place 0.7 s, separation, apart 1.2 s, reassembly
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
        print("écrit renders/%s (%.1f Mo)" % (os.path.basename(gif), os.path.getsize(gif) / 1e6))


if __name__ == "__main__":
    if "--assembler" in sys.argv:
        assemble(sys.argv[sys.argv.index("--assembler") + 1])
    else:                                # after "--": the views to render (head, torso, robot), otherwise all of them
        render_all(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else None)
