# MoodiMOS — GIF tournant des STL à imprimer, pour la notice
#
# Lancer depuis la racine du projet :
#   blender -b -P cao/MoodiMOS_Robot/robot_gif_stl.py
# Rend chaque fichier de stl_impression/ (et la jauge pogo) posé comme sur le plateau, qui fait un tour sur lui-même,
# les paires gauche/droite côte à côte, aux couleurs de la planche. Blender écrit les images dans un dossier
# temporaire, puis python3 (Pillow) y inscrit le nom des fichiers et ffmpeg assemble rendus/stl_tournant.gif
# (noms français) et rendus/stl_tournant_en.gif (noms de la version anglaise).

import json, math, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, "rendus")
STL_DIRS = (os.path.join(HERE, "stl_impression"), os.path.join(HERE, "..", "MoodiMOS_Organique", "stl_impression"))

# une scène par pièce, ou par paire gauche/droite ; ordre de la notice : tête, torse, membres, socle, outils
SCENES = [
    ["tete_plaque"], ["tete_cerclage"], ["tete_dos"],
    ["torse_coque"], ["torse_facade"], ["torse_dos"], ["torse_cadre"], ["torse_boite_hp"],
    ["epaule_gauche", "epaule_droit"], ["bras_gauche", "bras_droit"], ["gant_bracelet_gauche", "gant_bracelet_droit"],
    ["jambe_gauche", "jambe_droit"], ["basket_gauche", "basket_droit"],
    ["socle"],
    ["gabarit_aimant_basket_gauche", "gabarit_aimant_basket_droit"], ["pochoir_index_lunette"],
    ["cache_flanc_lunette"], ["jauge_pogo"],
]
# noms des fichiers dans la version anglaise (preparer_version_anglaise.py)
EN = {
    "tete_plaque": "head_plate", "tete_cerclage": "head_bezel", "tete_dos": "head_back",
    "torse_coque": "torso_body", "torse_facade": "torso_front", "torse_dos": "torso_back", "torse_cadre": "torso_frame",
    "torse_boite_hp": "torso_speaker_box", "epaule": "shoulder", "bras": "arm", "gant_bracelet": "glove_cuff",
    "jambe": "leg", "basket": "sneaker", "socle": "base", "gabarit_aimant_basket": "sneaker_magnet_jig",
    "pochoir_index_lunette": "bezel_marks_stencil", "cache_flanc_lunette": "bezel_side_mask", "jauge_pogo": "pogo_gauge",
}
SIDE = {"gauche": "left", "droit": "right"}

# couleurs de la planche (robot_rendu.py) ; le noir est un peu relevé pour se lire sur le fond
COL = {"orange": (0.95, 0.075, 0.004), "ivory": (0.82, 0.72, 0.60), "black": (0.03, 0.03, 0.034), "grey": (0.62, 0.60, 0.56)}
PIECE_COL = {"tete_plaque": "orange", "tete_dos": "orange", "tete_cerclage": "black", "torse_cadre": "black",
             "epaule": "black", "gabarit": "grey", "pochoir": "grey", "cache": "grey", "jauge": "grey"}
BACKGROUND = (0.10, 0.11, 0.13)

RES = (576, 432)
FRAMES = 24            # images par tour
FPS = 12               # un tour en 2 s ; taille, images et couleurs gardent le GIF sous les 10 Mo d'Instructables
ELEVATION = math.radians(25)
LABEL_BAND = 0.12      # bas de l'image réservé aux noms


def en_name(stem):
    for side_fr, side_en in SIDE.items():
        if stem.endswith("_" + side_fr):
            return EN[stem[: -len(side_fr) - 1]] + "_" + side_en
    return EN[stem]


def colour(stem):
    for key, col in PIECE_COL.items():
        if stem.startswith(key):
            return COL[col]
    return COL["ivory"]


def stl_path(stem):
    for d in STL_DIRS:
        p = os.path.join(d, stem + ".stl")
        if os.path.exists(p):
            return p
    raise FileNotFoundError(stem + ".stl")


# ── rendu (dans Blender) ──

def setup(bpy):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x, sc.render.resolution_y = RES
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.display.render_aa = "16"
    sc.view_settings.view_transform = "Standard"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_cavity = True
    sh.cavity_type = "BOTH"
    sh.show_specular_highlight = True
    if sc.world is None:
        sc.world = bpy.data.worlds.new("fond")
    sc.world.color = BACKGROUND
    cam = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    cam.data.type = "ORTHO"
    sc.collection.objects.link(cam)
    sc.camera = cam
    return sc, cam


def load(bpy, stem):
    from mathutils import Matrix, Vector
    bpy.ops.wm.stl_import(filepath=stl_path(stem))
    ob = bpy.context.selected_objects[0]
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    # axe de rotation au centre de la pièce, posée sur le plateau (z = 0)
    ob.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))))
    ob.location = ob.rotation_euler = (0, 0, 0)
    r = max(math.hypot(v.co.x, v.co.y) for v in ob.data.vertices)
    mat = bpy.data.materials.new(stem)
    mat.diffuse_color = (*colour(stem), 1)
    mat.roughness = 0.35
    ob.data.materials.append(mat)
    return ob, r, hi.z - lo.z


def render_scene(bpy, sc, cam, stems, folder, index):
    for ob in [o for o in sc.objects if o.type == "MESH"]:
        bpy.data.objects.remove(ob, do_unlink=True)
    parts = [load(bpy, s) for s in stems]
    gap = 0.15 * max(r for _, r, _ in parts)
    xs = [0.0] if len(parts) == 1 else [-(parts[0][1] + gap / 2), parts[1][1] + gap / 2]
    for (ob, _, _), x in zip(parts, xs):
        ob.location.x = x
    top = max(h for _, _, h in parts)
    zc = top / 2
    ce, se = math.cos(ELEVATION), math.sin(ELEVATION)
    width = 2 * max(abs(x) + r for x, (_, r, _) in zip(xs, parts))
    height = max(top * ce + 2 * r * se for _, r, _ in parts)
    aspect = RES[0] / RES[1]
    scale = max(width / 0.92, height * aspect / (1 - LABEL_BAND - 0.08))
    dist = 4 * scale
    cam.data.ortho_scale = scale
    cam.data.clip_end = 10 * dist
    cam.data.shift_y = -LABEL_BAND / 2 / aspect
    cam.location = (0, -dist * ce, zc + dist * se)
    cam.rotation_euler = (math.pi / 2 - ELEVATION, 0, 0)
    for k in range(FRAMES):
        a = 2 * math.pi * k / FRAMES
        for i, (ob, _, _) in enumerate(parts):
            ob.rotation_euler.z = a if i == 0 else -a     # paire : tours en miroir
        sc.render.filepath = os.path.join(folder, "%02d_%02d.png" % (index, k))
        bpy.ops.render.render(write_still=True)
    return [0.5 + x / scale for x in xs]


def render_all():
    import bpy
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc, cam = setup(bpy)
    folder = tempfile.mkdtemp(prefix="stl_tournant_")
    manifest = []
    for i, stems in enumerate(SCENES):
        print("pièce %d/%d : %s" % (i + 1, len(SCENES), " ".join(stems)), flush=True)
        us = render_scene(bpy, sc, cam, stems, folder, i)
        manifest.append({"u": us, "fr": [s + ".stl" for s in stems], "en": [en_name(s) + ".stl" for s in stems]})
    with open(os.path.join(folder, "scenes.json"), "w") as f:
        json.dump(manifest, f)
    try:
        subprocess.run(["python3", os.path.abspath(__file__), "--assembler", folder], check=True)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


# ── noms et GIF (python3 avec Pillow, hors de Blender) ──

def font(size):
    from PIL import ImageFont
    for p in ("/System/Library/Fonts/Helvetica.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default(size)


def assemble(folder):
    from PIL import Image, ImageDraw
    with open(os.path.join(folder, "scenes.json")) as f:
        manifest = json.load(f)
    name_font, count_font = font(15), font(12)
    os.makedirs(OUTDIR, exist_ok=True)
    for lang, out in (("fr", "stl_tournant.gif"), ("en", "stl_tournant_en.gif")):
        frames = os.path.join(folder, lang)
        os.makedirs(frames, exist_ok=True)
        n = 0
        for i, scene in enumerate(manifest):
            for k in range(FRAMES):
                im = Image.open(os.path.join(folder, "%02d_%02d.png" % (i, k))).convert("RGB")
                d = ImageDraw.Draw(im)
                y = RES[1] * (1 - LABEL_BAND / 2)
                xs = [u * RES[0] for u in scene["u"]]
                if len(xs) == 2:          # pièces étroites : les deux noms s'écartent pour ne pas se toucher
                    need = sum(d.textlength(t, font=name_font) for t in scene[lang]) / 2 + 30
                    if xs[1] - xs[0] < need:
                        mid = (xs[0] + xs[1]) / 2
                        xs = [mid - need / 2, mid + need / 2]
                for x, name in zip(xs, scene[lang]):
                    d.text((x, y), name, font=name_font, fill=(235, 232, 226), anchor="mm")
                d.text((RES[0] - 12, 12), "%d/%d" % (i + 1, len(manifest)), font=count_font, fill=(150, 152, 158), anchor="ra")
                im.save(os.path.join(frames, "%04d.png" % n))
                n += 1
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", os.path.join(frames, "%04d.png"),
                        "-vf", "split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];"
                               "[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle",
                        "-loop", "0", os.path.join(OUTDIR, out)], check=True)
        print("écrit rendus/%s (%.1f Mo)" % (out, os.path.getsize(os.path.join(OUTDIR, out)) / 1e6))


if __name__ == "__main__":
    if "--assembler" in sys.argv:
        assemble(sys.argv[sys.argv.index("--assembler") + 1])
    else:
        render_all()
