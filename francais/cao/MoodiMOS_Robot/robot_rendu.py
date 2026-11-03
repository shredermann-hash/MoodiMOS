# MoodiMOS — rendu Blender des quatre vues de la fiche (face, 3/4, profil, dos)
#
# Lancer depuis la racine du projet, après robot_pieces.py et robot_membres.py :
#   blender -b -P cao/MoodiMOS_Robot/robot_rendu.py                 (toutes les vues)
#   blender -b -P cao/MoodiMOS_Robot/robot_rendu.py -- face 34      (vues choisies : face, 34, profil, dos, tete)
# Importe stl/*.stl (mm, repère du torse : x à droite vu de face, y vers l'arrière, z en haut), pose les couleurs
# de la planche (orange, ivoire, noir, gris clair), les écrans du firmware (visage, tableau de bord) en émission,
# et écrit rendus/<vue>.png. Garde aussi la scène dans rendus/moodimos_robot.blend pour la retoucher dans Blender.

import bpy, json, math, os, sys, unicodedata
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
STL = os.path.join(HERE, "stl")
OUTDIR = os.path.join(HERE, "rendus")
SCREENS = os.path.join(HERE, "..", "MoodiMOS_Rendu", "ecrans")

# ── couleurs (fiche technique) ──
COL = {
    "orange": ((0.95, 0.075, 0.004), 0.22),
    "ivory": ((0.82, 0.72, 0.60), 0.28),
    "black": ((0.012, 0.012, 0.014), 0.30),
    "grey": ((0.62, 0.60, 0.56), 0.32),
    "white": ((0.86, 0.84, 0.80), 0.40),
}
PIECE_COL = {
    "torse_cadre": "black", "torse_coque": "ivory", "torse_facade": "ivory", "torse_dos": "ivory", "torse_boite_hp": "ivory",
    "tete_cerclage": "black", "tete_plaque": "orange", "tete_dos": "orange",
    "peinture_belier": "grey", "peinture_medaillon": "grey", "compo_bh1750": "white", "peinture_vis": "black",
    "epaule": "black", "bras": "ivory", "bague_bras": "black", "bracelet": "orange", "gant": "ivory",
    "jambe": "ivory", "bague_jambe": "black",
    "lisere": "black", "semelle": "ivory", "bout": "orange", "tige": "ivory", "flanc": "black", "col": "ivory",
    "talon": "orange", "poignet_gant": "black",
    "bord_bout": "black", "vent_basket": "black", "lacets": "orange", "pastille": "orange",
    "socle_corps": "ivory", "socle_bandeau": "black", "socle_plaque": "black", "socle_filet": "orange",
}
SOCLE = os.environ.get("ROBOT_SOCLE", "")      # ROBOT_SOCLE=1 : robot posé sur son socle (robot_socle.py → socle/*.stl)
GLOW = ("torse_jonc", "peinture_jonc_casque", "peinture_liseré_lunette", "peinture_index", "pastille")   # joncs lumineux : orange émissif


def material(name, rgb, rough, emit=0.0, coat=0.35):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Roughness"].default_value = rough
    for key in ("Coat Weight", "Clearcoat"):          # vernis du plastique jouet
        if key in b.inputs:
            b.inputs[key].default_value = coat
    if emit:
        b.inputs["Emission Color"].default_value = (*rgb, 1)
        b.inputs["Emission Strength"].default_value = emit
    return m


def screen_material(name, image_path):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image_path)
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 1.1
    glossy = nt.nodes.new("ShaderNodeBsdfGlossy")
    glossy.inputs["Roughness"].default_value = 0.08
    mix = nt.nodes.new("ShaderNodeMixShader")           # reflet discret : l'écran reste lisible
    mix.inputs[0].default_value = 0.06
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(tex.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs[0], mix.inputs[1])
    nt.links.new(glossy.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    return m


def kind_of(stem):
    for k in sorted(PIECE_COL, key=len, reverse=True):
        if stem == k or stem.startswith(k + "_"):
            return PIECE_COL[k]
    return None


def paint_casques(ob, mat):
    """Casques d'une pièce avec le dôme : leurs faces (hors de la sphère, dans leur emprise) passent en noir."""
    ob.data.materials.append(mat)
    for cq in COTES["casques"]:
        c, a = Vector(cq["centre"]), Vector(cq["axe"])
        for poly in ob.data.polygons:
            v = Vector(poly.center) - c
            t = v.dot(a)
            if t > 0 and (v - t * a).length < cq["rayon"] and v.length > cq["rmin"]:
                poly.material_index = 1


def screen(name, outline, w, h, y, zc, mat):
    """Écran : polygone (x, z) dans le plan y (devant la vitre), coordonnées de texture = image w × h centrée."""
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    verts = [bm.verts.new((x, y, zc + z)) for x, z in outline]
    face = bm.faces.new(verts)
    uv = bm.loops.layers.uv.new()
    for loop in face.loops:
        x, _, z = loop.vert.co
        loop[uv].uv = (x / w + 0.5, (z - zc) / h + 0.5)
    if face.normal.y > 0:                     # face vers -y (vers la caméra de face)
        face.normal_flip()
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    return ob


def disc(r, n=160):
    return [(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def rounded(w, h, r, n=12):
    pts = []
    for sx, sz, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        cx, cz = sx * (w / 2 - r), sz * (h / 2 - r)
        pts += [(cx + r * math.cos(math.radians(a)), cz + r * math.sin(math.radians(a))) for a in
                [a0 + 90 * i / (n - 1) for i in range(n)]]
    return pts


def build_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.001
    mats = {k: material("Robot " + k, rgb, r) for k, (rgb, r) in COL.items()}
    glow = material("Robot jonc", (1.0, 0.15, 0.006), 0.3, emit=1.5)
    glove = material("Robot gant et poignet", COL["ivory"][0], COL["ivory"][1])
    nodes, links = glove.node_tree.nodes, glove.node_tree.links
    geometry = nodes.new("ShaderNodeNewGeometry")
    split = nodes.new("ShaderNodeSeparateXYZ")
    gate = nodes.new("ShaderNodeMath")
    gate.operation = "GREATER_THAN"
    gate.inputs[1].default_value = COTES["torso_h"] / 2 - 18 - 86 - 5.0
    mix = nodes.new("ShaderNodeMixRGB")
    mix.inputs[1].default_value = (*COL["ivory"][0], 1)
    mix.inputs[2].default_value = (*COL["black"][0], 1)
    links.new(geometry.outputs["Position"], split.inputs[0])
    links.new(split.outputs["Z"], gate.inputs[0])
    links.new(gate.outputs[0], mix.inputs[0])
    links.new(mix.outputs[0], nodes["Principled BSDF"].inputs["Base Color"])
    glass = material("Robot vitre", (0.005, 0.005, 0.006), 0.04)

    detail = os.environ.get("ROBOT_DETAIL_ONLY", "")
    families = {
        "main": ("bras", "bague_bras", "bracelet", "gant", "lisere_gant"),
        "chaussures": ("jambe", "bague_jambe", "semelle", "lisere", "bout", "tige", "flanc", "col", "lacets", "pastille", "vent_basket", "bord_bout"),
    }
    if detail and detail not in families:
        raise ValueError("ROBOT_DETAIL_ONLY doit être main ou chaussures")
    files = [(STL, f) for f in sorted(os.listdir(STL))]
    if SOCLE and not detail:
        files += [(os.path.join(HERE, "socle"), f) for f in sorted(os.listdir(os.path.join(HERE, "socle")))]
    for folder, f in files:
        if not f.endswith(".stl"):
            continue
        stem = unicodedata.normalize("NFC", f[:-4])
        if detail and not any(stem == k or stem.startswith(k + "_") for k in families[detail]):
            continue
        if detail == "chaussures" and stem.startswith("lisere_gant"):
            continue
        bpy.ops.wm.stl_import(filepath=os.path.join(folder, f))
        ob = bpy.context.selected_objects[0]
        ob.name = stem
        if stem.startswith(GLOW):
            mat = glow
        elif stem.startswith("gant_"):
            mat = glove
        elif stem.startswith("vitre"):
            mat = glass
        else:
            mat = mats.get(kind_of(stem) or "white")
        ob.data.materials.append(mat)
        if stem == "tete_dos":
            paint_casques(ob, mats["black"])
        bpy.ops.object.shade_auto_smooth(angle=math.radians(35))

    # écrans : visage (tête, image 75 × 75 sur la vitre Ø 75) et tableau de bord (torse, image 61 × 92,44)
    face = os.path.join(HERE, "ecrans", "visage.png")      # écrans (robot_ecrans.py)
    dash = os.path.join(HERE, "ecrans", "torse.png")
    if not detail:
        screen("Écran tête", disc(COTES["head_glass_d"] / 2 - 0.05), COTES["head_glass_d"], COTES["head_glass_d"], COTES["head_glass_y"] - 0.03, COTES["head_center_z"],
               screen_material("Écran tête", face))
        screen("Écran torse", rounded(COTES["torso_glass_w"] - 0.1, COTES["torso_glass_h"] - 0.1, COTES["torso_glass_r"]), COTES["torso_glass_w"], COTES["torso_glass_h"], -0.03, COTES["torso_glass_z"],
               screen_material("Écran torse", dash))

    # sol et studio
    # Fond cyclorama : sol et fond continus, sans horizon coupant le robot. Profil pour les vues de face (fond
    # en Y = 500…850, derrière le robot) ; main() le tourne d'un demi-tour pour les vues arrière (dos, …).
    ground = COTES["ground_z"]
    if SOCLE and not detail:                       # le sol passe sous le socle
        ground -= json.load(open(os.path.join(HERE, "socle", "socle.json")))["hauteur"]
    profile = [(-5000, ground), (500, ground)]
    profile += [(500 + 350 * math.sin(t), ground + 350 * (1 - math.cos(t)))
                for t in [math.pi / 2 * i / 48 for i in range(1, 49)]]
    profile.append((850, 2000))
    vertices = [(x, y, z) for x in (-5000, 5000) for y, z in profile]
    n = len(profile)
    faces = [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)]
    mesh = bpy.data.meshes.new("Cyclorama")
    mesh.from_pydata(vertices, [], faces)
    mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))   # lissé : pas de marches dans le raccord
    mesh.update()
    floor = bpy.data.objects.new("Sol et fond", mesh)
    sc.collection.objects.link(floor)
    floor.data.materials.append(material("Robot sol", (0.028, 0.027, 0.03), 0.65, coat=0.0))   # sans vernis : pas de reflets nets des panneaux
    world = bpy.data.worlds.new("Studio")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.03, 0.031, 0.036, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    sc.world = world
    for name, loc, energy, size, color in (
            ("Clé", (-300, -460, 420), 3.2e6, 280, (1.0, 0.93, 0.85)),
            ("Remplissage", (460, -380, 80), 1.1e6, 320, (0.85, 0.9, 1.0)),
            ("Contre orange", (-380, 420, 260), 2.2e6, 200, (1.0, 0.55, 0.22)),
            ("Contre", (380, 400, 330), 1.0e6, 220, (1.0, 0.9, 0.8)),
            ("Dessus", (0, 0, 700), 0.5e6, 400, (1.0, 1.0, 1.0))):
        bpy.ops.object.light_add(type="AREA", location=loc)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size
        L.data.color = color
        L.visible_camera = False                       # les panneaux lumineux éclairent sans apparaître dans l'image
        L.rotation_euler = (Vector((0, 0, COTES["mid_z"])) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()

    sc.render.engine = "CYCLES"
    sc.cycles.samples = int(os.environ.get("ROBOT_SAMPLES", "64"))
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = 900, 1300
    sc.render.resolution_percentage = int(os.environ.get("ROBOT_RESOLUTION", "100"))
    sc.render.image_settings.file_format = "PNG"
    try:
        sc.view_settings.view_transform = "Standard"
    except TypeError:
        sc.view_settings.view_transform = "Filmic"
    return sc


def camera(sc, name, loc, target, lens=85):
    cam = bpy.data.cameras.new(name)
    cam.lens = lens
    cam.clip_start, cam.clip_end = 10, 10000
    ob = bpy.data.objects.new(name, cam)
    sc.collection.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return ob


VIEWS = {   # caméra : position, cible (mm)
    "face": ((0, -960, 35), (0, 0, 14)),
    "34": ((620, -740, 115), (0, 10, 14)),
    "profil": ((1080, 24, 40), (0, 24, 17)),
    "dos": ((0, 1100, 40), (0, 24, 17)),
    "tete": ((300, -560, 190), (0, 20, 120)),
    "casque": ((330, -150, 150), (30, 24, 108)),
    "lunette": ((-230, -330, 150), (5, 15, 108)),
    "main": ((180, -240, -35), (71, 12, -67)),
    "main2": ((150, -160, -70), (66, 12, -52)),
    "profil_technique": ((600, 16, -35), (40, 16, -41)),
    "chaussure_profil": ((420, 94, -112), (34, 12, -112)),
    "chaussures": ((260, -260, -45), (0, 24, -112)),
    "main3": ((200, -60, -95), (66, 14, -55)),
    "chaussures_dos": ((-60, 420, -60), (0, 30, -112)),
}


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    views = args or ["face", "34", "profil", "dos"]
    sc = build_scene()
    os.makedirs(OUTDIR, exist_ok=True)
    for v in views:
        for obj in sc.objects:
            if obj.name.endswith("_gauche"):
                obj.hide_render = v in ("chaussure_profil", "profil_technique")
        sc.render.resolution_x, sc.render.resolution_y = (800, 1600) if v == "profil_technique" else (900, 1300)
        loc, tgt = VIEWS[v]
        if SOCLE:                                  # recul et visée un peu plus bas : le socle entre dans le cadre
            tgt = (tgt[0], tgt[1], tgt[2] - 12)
            loc = tuple(t + 1.1 * (l - t) for l, t in zip(loc, tgt))
        # le fond du cyclorama se place toujours derrière le robot vu de la caméra (vues arrière : demi-tour)
        sc.objects["Sol et fond"].rotation_euler.z = math.pi if loc[1] > tgt[1] else 0.0
        sc.camera = camera(sc, "Caméra " + v, loc, tgt)
        sc.render.filepath = os.path.join(OUTDIR, v + ("_detail" if os.environ.get("ROBOT_DETAIL_ONLY") else "")
                                          + ("_socle" if SOCLE else "") + ".png")
        bpy.ops.render.render(write_still=True)
        print("rendu", sc.render.filepath)
    if os.environ.get("ROBOT_DETAIL_ONLY"):
        return
    for obj in sc.objects:
        obj.hide_render = False
    sc.objects["Sol et fond"].rotation_euler.z = 0.0
    sc.render.resolution_x, sc.render.resolution_y = 900, 1300
    if "Caméra 34" in sc.objects:
        sc.camera = sc.objects["Caméra 34"]
    for img in bpy.data.images:
        if img.source == "FILE":
            img.pack()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUTDIR, "moodimos_robot_socle.blend" if SOCLE else "moodimos_robot.blend"))


COTES = {}

if __name__ == "__main__":
    import json
    COTES.update(json.load(open(os.path.join(HERE, "stl", "cotes.json"))))
    main()
