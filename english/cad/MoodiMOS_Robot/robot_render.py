# MoodiMOS — Blender rendering of the four reference views (front, 3/4, profile, back).
# 
# Run from the project root after robot_parts.py and robot_limbs.py:
#   blender -b -P cad/MoodiMOS_Robot/robot_render.py                 (all views)
#   blender -b -P cad/MoodiMOS_Robot/robot_render.py -- face 34      (selected views: face, 34, profil, back, head)
# Imports stl/*.stl in millimetres, in torso coordinates: x rightwards viewed from the front,
# y towards the back, z upwards. Applies the orange, ivory, black and light-grey palette and the
# firmware face/dashboard textures as emissive materials, then writes renders/<view>.png.
# Also saves renders/moodimos_robot.blend for further editing in Blender.

import bpy, json, math, os, sys, unicodedata
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
STL = os.path.join(HERE, "stl")
OUTDIR = os.path.join(HERE, "renders")
SCREENS = os.path.join(HERE, "..", "MoodiMOS_Rendu", "screens")

# Technical sheet (STYLE) – colors
COL = {
    "orange": ((0.95, 0.075, 0.004), 0.22),
    "ivory": ((0.82, 0.72, 0.60), 0.28),
    "black": ((0.012, 0.012, 0.014), 0.30),
    "grey": ((0.62, 0.60, 0.56), 0.32),
    "white": ((0.86, 0.84, 0.80), 0.40),
}
PIECE_COL = {
    "torso_frame": "black", "torso_body": "ivory", "torso_front": "ivory", "torso_back": "ivory", "torso_speaker_box": "ivory",
    "head_bezel": "black", "head_plate": "orange", "head_back": "orange",
    "paint_bail": "grey", "paint_medallion": "grey", "component_bh1750": "white", "paint_screws": "black",
    "shoulder": "black", "arm": "ivory", "ring_arm": "black", "cuff": "orange", "glove": "ivory",
    "leg": "ivory", "ring_leg": "black",
    "trim": "black", 'sole': "ivory", 'toe': "orange", 'upper': "ivory", 'side': "black", 'collar': "ivory",
    'heel': "orange", 'wrist_glove': "black",
    'edge_toe': "black", "vent_sneaker": "black", 'laces': "orange", 'roundel': "orange",
    "base_corps": "ivory", "base_bandeau": "black", "base_plate": "black", "base_filet": "orange",
}
SOCLE = os.environ.get("ROBOT_SOCLE", "")      # ROBOT_SOCLE=1 : robot mounted on its base (robot_base.py → **base/*.stl**)
GLOW = ("torso_strip", "paint_strip_headphone", 'paint_trim_bezel', "paint_marks", 'roundel')   # Lightweight joints: orange emitter


def material(name, rgb, rough, emit=0.0, coat=0.35):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Roughness"].default_value = rough
    for key in ("Coat Weight", "Clearcoat"):          # plastic toy varnish
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
    mix = nt.nodes.new("ShaderNodeMixShader")           # discrete reflection: screen remains readable
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
    'Headphones piece with dome: their faces (outside the sphere, within their clearance) are black.'
    ob.data.materials.append(mat)
    for cq in COTES["casques"]:
        c, a = Vector(cq["centre"]), Vector(cq["axe"])
        for poly in ob.data.polygons:
            v = Vector(poly.center) - c
            t = v.dot(a)
            if t > 0 and (v - t * a).length < cq["rayon"] and v.length > cq["rmin"]:
                poly.material_index = 1


def screen(name, outline, w, h, y, zc, mat):
    'Display: (x, z) polygon in the y-plane (in front of the glass), texture coordinates = centered image width × height.'
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    verts = [bm.verts.new((x, y, zc + z)) for x, z in outline]
    face = bm.faces.new(verts)
    uv = bm.loops.layers.uv.new()
    for loop in face.loops:
        x, _, z = loop.vert.co
        loop[uv].uv = (x / w + 0.5, (z - zc) / h + 0.5)
    if face.normal.y > 0:                     # Facing -Y (toward the front camera)
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
    glow = material("Robot strip", (1.0, 0.15, 0.006), 0.3, emit=1.5)
    glove = material('Robot glove et wrist', COL["ivory"][0], COL["ivory"][1])
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
    glass = material("Robot glass", (0.005, 0.005, 0.006), 0.04)

    detail = os.environ.get("ROBOT_DETAIL_ONLY", "")
    families = {
        "main": ("arm", "ring_arm", "cuff", "glove", "trim_glove"),
        "chaussures": ("leg", "ring_leg", 'sole', "trim", 'toe', 'upper', 'side', 'collar', 'laces', 'roundel', "vent_sneaker", 'edge_toe'),
    }
    if detail and detail not in families:
        raise ValueError('DETAIL_ONLY must be sole or footwear')
    files = [(STL, f) for f in sorted(os.listdir(STL))]
    if SOCLE and not detail:
        files += [(os.path.join(HERE, "base"), f) for f in sorted(os.listdir(os.path.join(HERE, "base")))]
    for folder, f in files:
        if not f.endswith(".stl"):
            continue
        stem = unicodedata.normalize("NFC", f[:-4])
        if detail and not any(stem == k or stem.startswith(k + "_") for k in families[detail]):
            continue
        if detail == "chaussures" and stem.startswith("trim_glove"):
            continue
        bpy.ops.wm.stl_import(filepath=os.path.join(folder, f))
        ob = bpy.context.selected_objects[0]
        ob.name = stem
        if stem.startswith(GLOW):
            mat = glow
        elif stem.startswith("glove_"):
            mat = glove
        elif stem.startswith("glass"):
            mat = glass
        else:
            mat = mats.get(kind_of(stem) or "white")
        ob.data.materials.append(mat)
        if stem == "head_back":
            paint_casques(ob, mats["black"])
        bpy.ops.object.shade_auto_smooth(angle=math.radians(35))

    # screens: face (head, 75 × 75 image on Ø75 glass) and dashboard (torso, 61 × 92.44 image)
    face = os.path.join(HERE, "screens", "face.png")      # robot_screens.py
    dash = os.path.join(HERE, "screens", "torso.png")
    if not detail:
        screen('Head display (screen)', disc(COTES["head_glass_d"] / 2 - 0.05), COTES["head_glass_d"], COTES["head_glass_d"], COTES["head_glass_y"] - 0.03, COTES["head_center_z"],
               screen_material('Head display (screen)', face))
        screen('Torso screen', rounded(COTES["torso_glass_w"] - 0.1, COTES["torso_glass_h"] - 0.1, COTES["torso_glass_r"]), COTES["torso_glass_w"], COTES["torso_glass_h"], -0.03, COTES["torso_glass_z"],
               screen_material('Torso screen', dash))

    # Studio and set design:
    # Cyclorama background: Solid floor and continuous background without horizon cutting the robot. Profile for front views (background at Y = 500…850, behind the robot). `main()` rotates it by half a turn for rear views (back, ...).
    ground = COTES["ground_z"]
    if SOCLE and not detail:                       # The ground passes beneath the base.
        ground -= json.load(open(os.path.join(HERE, "base", "base.json")))["hauteur"]
    profile = [(-5000, ground), (500, ground)]
    profile += [(500 + 350 * math.sin(t), ground + 350 * (1 - math.cos(t)))
                for t in [math.pi / 2 * i / 48 for i in range(1, 49)]]
    profile.append((850, 2000))
    vertices = [(x, y, z) for x in (-5000, 5000) for y, z in profile]
    n = len(profile)
    faces = [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)]
    mesh = bpy.data.meshes.new("Cyclorama")
    mesh.from_pydata(vertices, [], faces)
    mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))   # smooth: no step in the joint
    mesh.update()
    floor = bpy.data.objects.new("Sol et fond", mesh)
    sc.collection.objects.link(floor)
    floor.data.materials.append(material("Robot sol", (0.028, 0.027, 0.03), 0.65, coat=0.0))   # no varnish: no clear reflections on panels
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
        L.visible_camera = False                       # The light panels illuminate without appearing in the image.
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


VIEWS = {   # camera: position, target (mm)
    "face": ((0, -960, 35), (0, 0, 14)),
    "34": ((620, -740, 115), (0, 10, 14)),
    "profil": ((1080, 24, 40), (0, 24, 17)),
    "back": ((0, 1100, 40), (0, 24, 17)),
    "head": ((300, -560, 190), (0, 20, 120)),
    "headphone": ((330, -150, 150), (30, 24, 108)),
    "bezel": ((-230, -330, 150), (5, 15, 108)),
    "main": ((180, -240, -35), (71, 12, -67)),
    "main2": ((150, -160, -70), (66, 12, -52)),
    "profil_technique": ((600, 16, -35), (40, 16, -41)),
    "chaussure_profil": ((420, 94, -112), (34, 12, -112)),
    "chaussures": ((260, -260, -45), (0, 24, -112)),
    "main3": ((200, -60, -95), (66, 14, -55)),
    "chaussures_back": ((-60, 420, -60), (0, 30, -112)),
}


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    views = args or ["face", "34", "profil", "back"]
    sc = build_scene()
    os.makedirs(OUTDIR, exist_ok=True)
    for v in views:
        for obj in sc.objects:
            if obj.name.endswith("_left"):
                obj.hide_render = v in ("chaussure_profil", "profil_technique")
        sc.render.resolution_x, sc.render.resolution_y = (800, 1600) if v == "profil_technique" else (900, 1300)
        loc, tgt = VIEWS[v]
        if SOCLE:                                  # Recess and aim slightly lower: the base fits into the frame.
            tgt = (tgt[0], tgt[1], tgt[2] - 12)
            loc = tuple(t + 1.1 * (l - t) for l, t in zip(loc, tgt))
        # the cyclorama stays behind the robot relative to the camera; reverse it for rear views
        sc.objects["Sol et fond"].rotation_euler.z = math.pi if loc[1] > tgt[1] else 0.0
        sc.camera = camera(sc, 'Camera' + v, loc, tgt)
        sc.render.filepath = os.path.join(OUTDIR, v + ("_detail" if os.environ.get("ROBOT_DETAIL_ONLY") else "")
                                          + ("_base" if SOCLE else "") + ".png")
        bpy.ops.render.render(write_still=True)
        print("rendu", sc.render.filepath)
    if os.environ.get("ROBOT_DETAIL_ONLY"):
        return
    for obj in sc.objects:
        obj.hide_render = False
    sc.objects["Sol et fond"].rotation_euler.z = 0.0
    sc.render.resolution_x, sc.render.resolution_y = 900, 1300
    if 'Camera 34' in sc.objects:
        sc.camera = sc.objects['Camera 34']
    for img in bpy.data.images:
        if img.source == "FILE":
            img.pack()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUTDIR, "moodimos_robot_base.blend" if SOCLE else "moodimos_robot.blend"))


COTES = {}

if __name__ == "__main__":
    import json
    COTES.update(json.load(open(os.path.join(HERE, "stl", "dimensions.json"))))
    main()
