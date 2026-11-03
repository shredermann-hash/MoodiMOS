# MoodiMOS — socle rond, dans l'esprit de la lunette de la tête
#
# Socle plein en disque : bandeau noir en bas (rainure-guide), corps ivoire, gradin dont le dessus est peint en orange
# (le liseré de la lunette ; rainure-guide côté bord, avant l'arrondi), plateau ivoire surélevé où le robot se pose dans les empreintes de ses baskets (2,5 mm,
# forme exacte des semelles, tournées de 12° vers l'extérieur). Un aimant par pied, sous l'axe de la jambe (là où
# porte le poids), tient le robot ; l'aimant jumeau se loge sous la basket, dans un trou percé à la main : gabarits de
# perçage fournis. Le disque est centré sur les deux semelles (cercle le plus serré qui les contient). Dessous :
# quatre logements de patins.
#
# Lancer depuis la racine du projet, après robot_pieces.py et robot_membres.py (stl/ à jour) :
#   cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_socle.py
# Écrit stl_impression/socle.stl (dessous sur le plateau, aucun support), stl_impression/gabarit_aimant_basket_*.stl
# et socle/*.stl (pièces placées pour le rendu : ROBOT_SOCLE=1 blender -b -P cao/MoodiMOS_Robot/robot_rendu.py -- face 34).

import json, math, os, struct
import numpy as np
from manifold3d import CrossSection, Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
STL, OUT_PRINT, OUT_RENDU = (os.path.join(HERE, d) for d in ("stl", "stl_impression", "socle"))

S = {
    "margin": 12.0,       # marge entre les semelles et le bord du disque
    "step": 6.0,          # largeur du gradin orange (plateau = disque - step)
    "H1": 15.0,           # hauteur du gradin
    "H": 20.0,            # hauteur du plateau (= sol du robot)
    "rEdge": 2.0,         # arrondi de l'arête du gradin
    "rTop": 3.0,          # arrondi de l'arête du plateau
    "printDepth": 2.5,    # empreintes des baskets
    "printFit": 0.5,      # jeu autour des semelles
    "magD": 10.2, "magT": 5.2,       # aimant Ø 10 × 5 (N52) au ras du fond de l'empreinte
    "bandZ": 5.0, "grooveW": 0.8, "grooveT": 0.6,   # rainure-guide du bandeau noir
    "stepGrooveW": 0.8, "stepGrooveT": 0.4,         # rainure-guide de l'orange du gradin (comme celle de la lunette)
    "padD": 12.5, "padT": 1.0, "padInset": 18.0,    # patins adhésifs Ø 12 sous le socle
    "jigT": 1.2, "jigRim": 1.6, "jigRimW": 1.2, "jigPilot": 3.0,   # gabarits de perçage
}
LEG = {"droit": (34.0, 24.0), "gauche": (-34.0, 24.0)}   # axe des jambes (repère du torse), aimant à l'aplomb
SEG = 256


def load_manifold(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    tri = np.frombuffer(d[84:84 + 50 * n], dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])["v"].reshape(-1, 3)
    V, idx = np.unique(tri, axis=0, return_inverse=True)
    return Manifold(Mesh(vert_properties=V.astype(np.float32), tri_verts=idx.reshape(-1, 3).astype(np.uint32)))


def write_stl(path, m):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"], rec["v"] = nrm, tri
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS socle".ljust(80, b" "))
        fh.write(struct.pack("<I", len(t)))
        fh.write(rec.tobytes())


def semelle(side, ground):
    """Contour au sol de la basket (repère du torse), d'après le liseré noir de la semelle."""
    return load_manifold(os.path.join(STL, "lisere_%s.stl" % side)).slice(ground + 0.3)


def disque(ground):
    """Centre (y) et rayon du disque : cercle le plus serré qui contient les deux semelles, plus la marge."""
    pts = np.vstack([np.array(p) for side in LEG for poly in semelle(side, ground).to_polygons() for p in [poly]])
    best = min(((cy, np.max(np.hypot(pts[:, 0], pts[:, 1] - cy))) for cy in np.arange(-20, 40, 0.1)), key=lambda c: c[1])
    return best[0], math.ceil(best[1] + S["margin"])


def arc(cx, cz, r, a0, a1, n=12):
    return [(cx + r * math.cos(a), cz + r * math.sin(a)) for a in np.linspace(a0, a1, n)]


def corps(R):
    """Disque à gradin, repère local (centre en x = y = 0, dessous à z = 0) : profil de révolution (rayon, hauteur)."""
    Rp, H1, H, re, rt = R - S["step"], S["H1"], S["H"], S["rEdge"], S["rTop"]
    prof = [(0, 0), (R, 0), (R, H1 - re)] + arc(R - re, H1 - re, re, 0, math.pi / 2) + \
           [(Rp, H1), (Rp, H - rt)] + arc(Rp - rt, H - rt, rt, 0, math.pi / 2) + [(0, H)]
    m = Manifold.revolve(CrossSection([prof]), SEG)
    g = S["grooveW"] / 2                                       # rainure-guide du bandeau noir
    ring = CrossSection.circle(R + 1, SEG) - CrossSection.circle(R - S["grooveT"], SEG)
    m -= Manifold.extrude(ring, 2 * g).translate((0, 0, S["bandZ"] - g))
    r1 = R - re                                                # orange du gradin : de la paroi du plateau à la rainure,
    step = CrossSection.circle(r1, SEG) - CrossSection.circle(r1 - S["stepGrooveW"], SEG)   # juste avant l'arrondi
    return m - Manifold.extrude(step, S["stepGrooveT"] + 1).translate((0, 0, H1 - S["stepGrooveT"]))


def socle(ground):
    """Socle complet, repère local : centre du disque en (0, 0), dessous à z = 0, plateau à z = H (sol du robot)."""
    cy, R = disque(ground)
    m = corps(R)
    for side, (lx, ly) in LEG.items():
        foot = semelle(side, ground).offset(S["printFit"]).translate((0, -cy))
        m -= Manifold.extrude(foot, S["printDepth"] + 1).translate((0, 0, S["H"] - S["printDepth"]))
        m -= Manifold.cylinder(S["magT"] + 0.01, S["magD"] / 2, S["magD"] / 2, 64).translate(
            (lx, ly - cy, S["H"] - S["printDepth"] - S["magT"]))
    for k in range(4):                                         # patins à 45°, 135°, 225°, 315°
        a = math.radians(45 + 90 * k)
        rp = R - S["padInset"]
        m -= Manifold.cylinder(S["padT"] + 0.01, S["padD"] / 2, S["padD"] / 2, 64).translate(
            (rp * math.cos(a), rp * math.sin(a), -0.01))
    return m, cy, R


def gabarit(side, ground):
    """Gabarit de perçage, à imprimer plaque sur le plateau : retourné sur la basket posée à l'envers, son rebord
    entoure la semelle et son avant-trou marque le centre de l'aimant (forer ensuite Ø 10 sur 5 mm)."""
    sole = semelle(side, ground).offset(0.4)
    rim = sole.offset(S["jigRimW"])
    plate = Manifold.extrude(rim, S["jigT"]) + Manifold.extrude(rim - sole, S["jigT"] + S["jigRim"])
    lx, ly = LEG[side]
    plate -= Manifold.cylinder(S["jigT"] + 1, S["jigPilot"] / 2, S["jigPilot"] / 2, 32).translate((lx, ly, -0.5))
    b = plate.bounding_box()
    return plate.translate((-(b[0] + b[3]) / 2, -(b[1] + b[4]) / 2, 0))


def main():
    ground = json.load(open(os.path.join(STL, "cotes.json")))["ground_z"]
    os.makedirs(OUT_RENDU, exist_ok=True)
    m, cy, R = socle(ground)
    write_stl(os.path.join(OUT_PRINT, "socle.stl"), m)
    for side in LEG:
        write_stl(os.path.join(OUT_PRINT, "gabarit_aimant_basket_%s.stl" % side), gabarit(side, ground))
    # rendu : socle placé sous le robot (plateau au sol du robot), bandeau noir à part, dessus du gradin peint en orange
    for f in os.listdir(OUT_RENDU):
        if f.endswith(".stl"):
            os.remove(os.path.join(OUT_RENDU, f))
    placed = m.translate((0, cy, ground - S["H"]))
    cut = Manifold.cube((400, 400, S["bandZ"]), True).translate((0, 0, ground - S["H"] + S["bandZ"] / 2))
    write_stl(os.path.join(OUT_RENDU, "socle_corps.stl"), placed - cut)
    write_stl(os.path.join(OUT_RENDU, "socle_bandeau.stl"), placed ^ cut)
    film = CrossSection.circle(R - S["rEdge"] - S["stepGrooveW"] - 0.05, SEG) - CrossSection.circle(R - S["step"] + 0.05, SEG)
    write_stl(os.path.join(OUT_RENDU, "socle_filet.stl"),
              Manifold.extrude(film, 0.03).translate((0, cy, ground - S["H"] + S["H1"])))
    json.dump({"hauteur": S["H"], "rayon": R, "centre_y": cy}, open(os.path.join(OUT_RENDU, "socle.json"), "w"))
    b = m.bounding_box()
    print("socle : Ø {:.0f} × {:.1f} mm (centre {:.1f} mm devant l'axe du torse), {:.0f} cm³ plein, {} corps".format(
        b[3] - b[0], b[5] - b[2], 24 - cy, m.volume() / 1000, len(m.decompose())))


if __name__ == "__main__":
    main()
