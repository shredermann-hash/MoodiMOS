# MoodiMOS — assemblage dans Autodesk Fusion
#
# Lancer : Utilitaires > Compléments > Scripts et compléments > « + » (script existant)
#          > choisir ce dossier MoodiMOS_Assemblage > Exécuter.
# Importe les pièces de l'édition complète telles que les génèrent les scripts Python du dossier parent
# (robot_pieces.py, robot_membres.py) : ../stl/*.stl, en mm, déjà en place dans le repère du torse
# (x à droite vu de face, y vers l'arrière, z en haut). Rien n'est recalculé ici : Fusion montre
# exactement les pièces que l'on imprime et que rend Blender.
#
# Une pièce = un composant (atelier Animation › Éclatement automatique, puis Dessin), rangés en
# Tête, Torse, Membres et « Rendu (non imprimé) » (vitres, peintures, boule du BH1750).
# Couleurs : celles de robot_rendu.py (lues dans ce fichier, converties en sRGB), pour que Fusion et
# Blender restent identiques.
#
# Les pièces sont des maillages (formes sculptées des membres) : les cotes se modifient dans les
# scripts Python, puis on relance ce script. Pour usiner une pièce dans Fusion : clic droit sur le
# maillage > Convertir le maillage (pièces simples de la tête et du torse).
# Avant de lancer : générer les STL (voir ../LISEZMOI.md).

import adsk.core, adsk.fusion, traceback, os, ast, unicodedata

HERE = os.path.dirname(os.path.realpath(__file__))
ROBOT_DIR = os.path.normpath(os.path.join(HERE, ".."))
STL_DIR = os.path.join(ROBOT_DIR, "stl")

GROUPS = (("Tête", ("tete_",)), ("Torse", ("torse_",)),
          ("Rendu (non imprimé)", ("vitre_", "peinture_", "compo_")))
DEFAULT_GROUP = "Membres"


def read_render_colors():
    """COL, PIECE_COL et GLOW de robot_rendu.py (littéraux Python), sans exécuter Blender."""
    tree = ast.parse(open(os.path.join(ROBOT_DIR, "robot_rendu.py"), encoding="utf-8").read())
    found = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") in ("COL", "PIECE_COL", "GLOW"):
            found[node.targets[0].id] = ast.literal_eval(node.value)
    return found.get("COL", {}), found.get("PIECE_COL", {}), found.get("GLOW", ())


def srgb(c):
    """Couleur linéaire (Blender) → 0-255 sRGB (Fusion)."""
    c = max(0.0, min(1.0, c))
    v = 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
    return int(round(v * 255))


def nfc(s):
    return unicodedata.normalize("NFC", s)   # noms de fichiers macOS (accents décomposés)


def color_key(stem, piece_col, glow):
    if stem.startswith(glow):
        return "glow"
    if stem.startswith("vitre"):
        return "glass"
    for k in sorted(piece_col, key=len, reverse=True):
        if stem == k or stem.startswith(k + "_"):
            return piece_col[k]
    return "white"


# noms des composants : (nom, genre) pour accorder « droit / droite » ; sinon nom tiré du fichier
NAMES = {
    "tete_cerclage": ("Lunette", "f"), "tete_plaque": ("Plaque à goutte", "f"), "tete_dos": ("Dôme", "m"),
    "peinture_belier": ("Bélière peinte", "f"), "peinture_medaillon": ("Médaillon peint", "m"),
    "peinture_liseré_lunette": ("Liseré peint de la lunette", "m"),
    "peinture_jonc_casque": ("Jonc peint du casque", "m"),
    "torse_coque": ("Corps du torse", "m"), "torse_facade": ("Façade du torse", "f"), "torse_dos": ("Dos du torse", "m"),
    "torse_cadre": ("Cadre d'écran", "m"),
    "torse_boite_hp": ("Boîte du haut-parleur", "f"), "torse_jonc": ("Liseré lumineux", "m"),
    "vitre_tete": ("Vitre de la tête", "f"), "vitre_torse": ("Vitre du torse", "f"),
    "peinture_index": ("Index peints de la lunette", "mp"), "peinture_vis": ("Têtes de vis peintes", "fp"),
    "compo_bh1750": ("Boule du BH1750", "f"),
    "epaule": ("Épaule", "f"), "bras": ("Bras", "m"), "bague_bras": ("Bague de coude", "f"),
    "bracelet": ("Bracelet", "m"), "gant": ("Poing", "m"), "lisere_gant": ("Liserés du poing", "mp"),
    "poignet_gant": ("Bague de poignet", "f"),
    "jambe": ("Jambe", "f"), "bague_jambe": ("Bagues de jambe", "fp"),
    "lisere": ("Semelle noire", "f"), "semelle": ("Intercalaire", "m"), "bout": ("Bout", "m"),
    "tige": ("Empeigne", "f"), "flanc": ("Panneau arrière", "m"), "talon": ("Talon", "m"),
    "col": ("Collerette", "f"), "lacets": ("Bride", "f"), "pastille": ("Anneau lumineux", "m"),
    "vent_basket": ("Ouïes de basket", "fp"), "bord_bout": ("Trait du bout", "m"),
}
SIDE = {("droit", "m"): "droit", ("droit", "f"): "droite", ("droit", "mp"): "droits", ("droit", "fp"): "droites",
        ("gauche", "m"): "gauche", ("gauche", "f"): "gauche", ("gauche", "mp"): "gauches", ("gauche", "fp"): "gauches"}


def label(stem):
    """« bras_droit » → « Bras droit », « bras_gauche » → « Bras gauche », « epaule_droit » → « Épaule droite »."""
    kind, side = stem, None
    for sd in ("droit", "gauche"):
        if stem.endswith("_" + sd):
            kind, side = stem[: -len(sd) - 1], sd
    name, g = NAMES.get(kind, (kind.replace("_", " ").capitalize(), "m"))
    return name if side is None else "{} {}".format(name, SIDE[(side, g)])


def group_of(stem):
    for name, prefixes in GROUPS:
        if stem.startswith(prefixes):
            return name
    return DEFAULT_GROUP


def base_appearance(app):
    for lib in app.materialLibraries:
        try:
            aps = lib.appearances
        except Exception:
            continue
        for i in range(aps.count):
            if "Plastic" in aps.item(i).name or "Plastique" in aps.item(i).name:
                return aps.item(i)
    return None


def make_appearances(app, design, col):
    base = base_appearance(app)
    if base is None:
        return {}
    rgb = {k: tuple(srgb(c) for c in v[0]) for k, v in col.items()}
    rgb["glow"] = (255, 110, 20)       # joncs et liserés lumineux
    rgb["glass"] = (12, 12, 14)        # vitres des écrans
    made = {}
    for key, (r, g, b) in rgb.items():
        name = "MoodiMOS " + key
        ap = design.appearances.itemByName(name) or design.appearances.addByCopy(base, name)
        for prop in ap.appearanceProperties:
            if prop.objectType == adsk.core.ColorProperty.classType():
                try:
                    prop.value = adsk.core.Color.create(r, g, b, 255)
                except Exception:
                    pass
        made[key] = ap
    return made


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        if not os.path.isdir(STL_DIR):
            ui.messageBox("Dossier introuvable :\n{}\n\nGénérer d'abord les STL (robot_pieces.py, robot_membres.py).".format(STL_DIR))
            return
        files = sorted(f for f in os.listdir(STL_DIR) if f.lower().endswith(".stl"))
        if not files:
            ui.messageBox("Aucun STL dans {}.\nGénérer d'abord les pièces (voir LISEZMOI.md).".format(STL_DIR))
            return

        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        col, piece_col, glow = read_render_colors()
        appearances = make_appearances(app, design, col)

        identity = adsk.core.Matrix3D.create()
        groups = {}
        imported, errors = 0, []
        progress = ui.createProgressDialog()
        progress.show("MoodiMOS", "Import des pièces : %v / %m", 0, len(files), 0)
        for i, f in enumerate(files):
            stem = nfc(f[:-4])
            progress.progressValue = i
            if progress.wasCancelled:
                break
            try:
                gname = group_of(stem)
                if gname not in groups:
                    occ_g = root.occurrences.addNewComponent(identity)
                    occ_g.component.name = gname
                    groups[gname] = occ_g.component
                occ = groups[gname].occurrences.addNewComponent(identity)
                comp = occ.component
                comp.name = label(stem)
                base = comp.features.baseFeatures.add()
                base.startEdit()
                try:
                    bodies = comp.meshBodies.add(os.path.join(STL_DIR, f), adsk.fusion.MeshUnits.MillimeterMeshUnit, base)
                finally:
                    base.finishEdit()
                ap = appearances.get(color_key(stem, piece_col, glow))
                for k in range(bodies.count):
                    bodies.item(k).name = stem
                    if ap:
                        try:
                            bodies.item(k).appearance = ap
                        except Exception:
                            pass
                if ap:
                    try:
                        occ.appearance = ap
                    except Exception:
                        pass
                imported += 1
            except Exception as e:
                errors.append("{} : {}".format(f, e))
        progress.hide()
        app.activeViewport.fit()
        msg = "MoodiMOS : {} pièce(s) importée(s) depuis\n{}\n\nGroupes : {}.".format(
            imported, STL_DIR, ", ".join(sorted(groups)))
        if not appearances:
            msg += "\nAucun aspect « plastique » trouvé : couleurs non appliquées."
        if errors:
            msg += "\n\n{} erreur(s), par exemple :\n{}".format(len(errors), "\n".join(errors[:5]))
        ui.messageBox(msg)
    except Exception:
        if ui:
            ui.messageBox("Erreur :\n{}".format(traceback.format_exc()))
