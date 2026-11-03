# MoodiMOS — bras, épaules et jambes orientés pour l'impression (plateau en z = 0)
#
# Reprend stl/bras_*.stl, stl/epaule_*.stl et stl/jambe_*.stl (repère du torse, écrits par organique.py) sans
# changer leur forme, les oriente et les écrit dans stl_impression/ :
#   bras_*.stl   : couché sur sa face intérieure (le méplat qui longe le flanc du torse) ; la face
#                  extérieure, la plus visible, reste en haut, sans support ; supports « touchant le
#                  plateau » sous l'avant-bras, côté intérieur (caché contre le corps).
#   epaule_*.stl : tenon vers le bas (logement d'aimant sur le plateau), boule au-dessus ; pas de
#                  support (rebord de 1,5 mm entre tenon et boule, caché dans la cuvette).
#   jambe_*.stl  : debout comme dans le robot, sur le bout caché dans la basket (disque Ø 14),
#                  tenon aimanté en haut ; aucun surplomb au-delà de 60° : pas de support.
#
# Lancer après organique.py : cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Organique/orienter_impression.py

import os, struct
import numpy as np
from manifold3d import Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
SRC, OUT = os.path.join(HERE, "stl"), os.path.join(HERE, "stl_impression")


def load(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    tri = np.frombuffer(d[84:84 + 50 * n], dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])["v"]
    return tri.reshape(-1, 3, 3).astype(np.float64)


def save(path, tri):
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(tri), dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])
    rec["n"], rec["v"] = n, tri.reshape(-1, 9)
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS impression".ljust(80, b" "))
        fh.write(struct.pack("<I", len(tri)))
        fh.write(rec.tobytes())


def fill_small_holes(tri, max_edges=12):
    """Bouche les petits trous du maillage (défauts du marching cubes) par des éventails de triangles
    orientés comme les faces voisines ; renvoie (triangles, nombre de triangles ajoutés)."""
    pts, idx = np.unique(tri.reshape(-1, 3).round(5), axis=0, return_inverse=True)
    f = idx.reshape(-1, 3)
    half = {}
    for a_, b_ in np.concatenate([f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]]]):
        half[(int(a_), int(b_))] = half.get((int(a_), int(b_)), 0) + 1
    border = {a_: b_ for (a_, b_) in half if (b_, a_) not in half}   # demi-arêtes sans jumelle
    new, seen = [], set()
    for start in list(border):
        if start in seen:
            continue
        loop, v = [], start
        while v not in seen and v in border and len(loop) <= max_edges:
            seen.add(v)
            loop.append(v)
            v = border[v]
        if v != start or len(loop) > max_edges:
            continue
        loop = loop[::-1]                          # sens opposé au bord : même orientation que les voisines
        for i in range(1, len(loop) - 1):
            new.append((loop[0], loop[i], loop[i + 1]))
    if not new:
        return tri, 0
    f = np.vstack([f, np.array(new)])
    return pts[f], len(new)


def check(tri):
    """Maillage fermé et sans défaut (fusion des sommets identiques, puis contrôle manifold3d)."""
    pts, idx = np.unique(tri.reshape(-1, 3).round(5), axis=0, return_inverse=True)
    m = Manifold(Mesh(vert_properties=pts.astype(np.float32), tri_verts=idx.reshape(-1, 3).astype(np.uint32)))
    return m.status().name == "NoError" and not m.is_empty(), m.volume()


def place(tri):
    """Pose sur le plateau (z min = 0), centrée en x et y."""
    p = tri.reshape(-1, 3)
    lo, hi = p.min(0), p.max(0)
    return tri - np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]])


def rot(tri, R):
    return tri @ np.asarray(R, dtype=np.float64).T


# côté du torse (−x à droite, +x à gauche) → vers le plateau (nouveau z = ancien x à droite) :
# tenon de l'épaule vers le bas, face intérieure du bras sur le plateau
INNER_DOWN = {"droit": [[0, 0, -1], [0, 1, 0], [1, 0, 0]], "gauche": [[0, 0, 1], [0, 1, 0], [-1, 0, 0]]}
AS_IS = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]                  # jambe : déjà debout, tenon en haut


def flip_if_needed(tri, R):
    """Une rotation propre (det = +1) garde l'orientation des faces ; sinon on les retourne."""
    return tri if np.linalg.det(np.asarray(R, float)) > 0 else tri[:, ::-1]


def main():
    os.makedirs(OUT, exist_ok=True)
    print("Bras, épaules et jambes (STL d'impression, {}) :".format(os.path.relpath(OUT)))
    for side in ("droit", "gauche"):
        for name, R in (("bras", INNER_DOWN[side]), ("epaule", INNER_DOWN[side]), ("jambe", AS_IS)):
            tri = place(flip_if_needed(rot(load(os.path.join(SRC, "{}_{}.stl".format(name, side))), R), R))
            tri, filled = fill_small_holes(tri)
            ok, vol = check(tri)
            save(os.path.join(OUT, "{}_{}.stl".format(name, side)), tri)
            p = tri.reshape(-1, 3)
            size = p.max(0) - p.min(0)
            print("  {:<14} {:5.1f} × {:5.1f} × {:5.1f} mm  {:5.1f} cm³  {}{}".format(
                "{}_{}".format(name, side), *size, vol / 1000, "étanche" if ok else "À REVOIR",
                " ({} triangle(s) ajouté(s) pour boucher un défaut de maillage)".format(filled) if filled else ""))


if __name__ == "__main__":
    main()
