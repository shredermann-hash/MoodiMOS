# MoodiMOS — contrôle complet des STL d'impression et des jeux d'assemblage
#
# 1. Maillage de chaque STL d'impression (stl_impression/) : arêtes ouvertes (trous), arêtes non manifold,
#    orientation incohérente, triangles dégénérés ou en double, coques séparées, vides internes,
#    AUTO-INTERSECTIONS (faux raccords : la surface se traverse), parois plus minces que 0,8 mm.
# 2. Correspondance : chaque pièce d'impression a le volume de la pièce en place (stl/), à 1 % près.
# 3. Jeux : pour chaque couple de pièces qui se touchent une fois montées (stl/), interpénétration
#    (volume commun) et jeu minimal, comparés à l'attendu (contact, jeu de collage, jeu glissant).
#
# Lancer depuis la racine du projet, après les générateurs :
#   cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_controle_impression.py
# Écrit rendus/controle_impression.json (≈ 8 min : les auto-intersections des baskets sont longues).

import json, os, struct, sys, time, unicodedata
import numpy as np
from manifold3d import Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
PRINT_DIR, PLACED_DIR = os.path.join(HERE, "stl_impression"), os.path.join(HERE, "stl")
THIN = 0.8          # paroi minimale visée (2 lignes de buse 0,4)


# ── lecture et topologie ──

def load(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    tri = np.frombuffer(d[84:84 + 50 * n], dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])["v"]
    tri = tri.reshape(-1, 3, 3).astype(np.float64)
    V, idx = np.unique(tri.reshape(-1, 3), axis=0, return_inverse=True)     # soudure exacte des sommets
    return V, idx.reshape(-1, 3)


def find(name, folder):
    for f in os.listdir(folder):
        if unicodedata.normalize("NFC", f) == unicodedata.normalize("NFC", name):
            return os.path.join(folder, f)
    return None


def topology(V, F):
    m = len(F)
    area = 0.5 * np.linalg.norm(np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]]), axis=1)
    degenerate = int((area < 1e-10).sum()) + int((F[:, 0] == F[:, 1]).sum() + (F[:, 1] == F[:, 2]).sum() + (F[:, 0] == F[:, 2]).sum())
    duplicate = m - len(np.unique(np.sort(F, axis=1), axis=0))
    he = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])           # demi-arêtes orientées
    und = np.sort(he, axis=1)
    _, inv, cnt = np.unique(und, axis=0, return_inverse=True, return_counts=True)
    open_edges = int((cnt == 1).sum())
    nonmanifold = int((cnt > 2).sum())
    _, dcnt = np.unique(he, axis=0, return_counts=True)                       # même sens deux fois = orientation incohérente
    bad_orient = int((dcnt > 1).sum())
    # coques : composantes connexes des triangles par arêtes partagées
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    nv = len(V)
    g = coo_matrix((np.ones(len(he)), (he[:, 0], he[:, 1])), shape=(nv, nv))
    ncomp, lab = connected_components(g, directed=False)
    used = np.unique(F)
    shells = len(np.unique(lab[used]))
    # volume signé par coque (négatif = vide interne)
    tri = V[F]
    sv = np.einsum("ij,ij->i", tri[:, 0], np.cross(tri[:, 1], tri[:, 2])) / 6
    shell_vol = np.bincount(lab[F[:, 0]], weights=sv)
    shell_vol = shell_vol[np.unique(lab[F[:, 0]])]
    voids = int((shell_vol < 0).sum())
    return dict(triangles=m, open_edges=open_edges, nonmanifold_edges=nonmanifold, bad_orientation=bad_orient,
                degenerate=degenerate, duplicate=duplicate, shells=shells, voids=voids,
                volume_cm3=float(sv.sum() / 1000))


# ── auto-intersections et parois minces : segments contre triangles, par grille ──

def seg_tri_hits(P0, P1, A, B, C, eps=1e-9):
    """Segments [P0, P1] contre triangles (A, B, C), deux à deux : intersection stricte (Möller–Trumbore)."""
    d = P1 - P0
    e1, e2 = B - A, C - A
    p = np.cross(d, e2)
    det = np.einsum("ij,ij->i", e1, p)
    ok = np.abs(det) > 1e-14
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    s = P0 - A
    u = np.einsum("ij,ij->i", s, p) * inv
    q = np.cross(s, e1)
    v = np.einsum("ij,ij->i", d, q) * inv
    t = np.einsum("ij,ij->i", e2, q) * inv
    return ok & (u > eps) & (v > eps) & (u + v < 1 - eps) & (t > eps) & (t < 1 - eps)


def cell_pairs(lo_a, hi_a, lo_b, hi_b, cell, origin):
    """Couples (a, b) dont les boîtes partagent une cellule de grille (boîtes petites devant la cellule)."""
    def keys(lo, hi):
        i0 = np.floor((lo - origin) / cell).astype(np.int64)
        i1 = np.floor((hi - origin) / cell).astype(np.int64)
        span = (i1 - i0).max(axis=0)
        out_k, out_i = [], []
        for dx in range(span[0] + 1):
            for dy in range(span[1] + 1):
                for dz in range(span[2] + 1):
                    c = i0 + np.array([dx, dy, dz])
                    sel = np.all(c <= i1, axis=1)
                    k = (c[sel, 0] * 73856093) ^ (c[sel, 1] * 19349663) ^ (c[sel, 2] * 83492791)
                    out_k.append(k)
                    out_i.append(np.nonzero(sel)[0])
        return np.concatenate(out_k), np.concatenate(out_i)
    ka, ia = keys(lo_a, hi_a)
    kb, ib = keys(lo_b, hi_b)
    ob = np.argsort(kb, kind="stable")
    kb, ib = kb[ob], ib[ob]
    left = np.searchsorted(kb, ka, "left")
    right = np.searchsorted(kb, ka, "right")
    n = right - left
    rep_a = np.repeat(ia, n)
    starts = np.repeat(left, n) + (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n))
    return rep_a, ib[starts]


def segments_vs_mesh(P0, P1, V, F, skip_vertex=None, skip_tri=None, chunk=2_000_000):
    """Pour chaque segment : True s'il traverse un triangle du maillage (hors triangles exclus)."""
    A, B, C = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    tlo, thi = np.minimum(np.minimum(A, B), C), np.maximum(np.maximum(A, B), C)
    slo, shi = np.minimum(P0, P1), np.maximum(P0, P1)
    cell = max(float(np.median(np.linalg.norm(B - A, axis=1))) * 4, float(np.median(np.linalg.norm(P1 - P0, axis=1))) * 1.5, 0.3)
    origin = np.minimum(tlo.min(0), slo.min(0)) - 1
    si, ti = cell_pairs(slo, shi, tlo, thi, cell, origin)
    pair = np.unique(si * len(F) + ti)
    si, ti = pair // len(F), pair % len(F)
    hit = np.zeros(len(P0), bool)
    for k in range(0, len(si), chunk):
        s, t = si[k:k + chunk], ti[k:k + chunk]
        keep = np.ones(len(s), bool)
        if skip_vertex is not None:                       # arête : ignorer les triangles qui partagent un sommet
            ev = skip_vertex[s]
            keep &= ~((F[t] == ev[:, :1]).any(1) | (F[t] == ev[:, 1:2]).any(1))
        if skip_tri is not None:                          # rayon : ignorer le triangle d'origine
            keep &= t != skip_tri[s]
        s, t = s[keep], t[keep]
        h = seg_tri_hits(P0[s], P1[s], A[t], B[t], C[t])
        hit[s[h]] = True
    return hit


def self_intersections(V, F):
    E = np.unique(np.sort(np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]]), axis=1), axis=0)
    hit = segments_vs_mesh(V[E[:, 0]], V[E[:, 1]], V, F, skip_vertex=E)
    return int(hit.sum()), (V[E[hit, 0]][:5].round(2).tolist() if hit.any() else [])


def thin_walls(V, F, length=THIN, samples=40000, seed=1):
    """Part de la surface où la matière fait moins de `length` (rayon vers l'intérieur depuis le centre des faces)."""
    rng = np.random.default_rng(seed)
    pick = rng.choice(len(F), min(samples, len(F)), replace=False)
    A, B, C = V[F[pick, 0]], V[F[pick, 1]], V[F[pick, 2]]
    n = np.cross(B - A, C - A)
    area = np.linalg.norm(n, axis=1)
    ok = area > 1e-12
    n = n[ok] / area[ok, None]
    c = (A[ok] + B[ok] + C[ok]) / 3
    w = area[ok]
    hit = segments_vs_mesh(c - n * 1e-3, c - n * length, V, F, skip_tri=pick[ok])
    frac = float((w * hit).sum() / w.sum())
    pts = c[hit][:3].round(1).tolist()
    return frac, pts


# ── manifold3d (volumes communs et jeux) ──

def to_manifold(V, F):
    m = Manifold(Mesh(vert_properties=V.astype(np.float32), tri_verts=F.astype(np.uint32)))
    return m if m.status().name == "NoError" else None


_cache = {}


def placed(name):
    if name not in _cache:
        p = find(name + ".stl", PLACED_DIR)
        if p is None:
            raise FileNotFoundError(name)
        V, F = load(p)
        m = to_manifold(V, F)
        if m is None:                                      # petits trous de marching cubes : bouchés comme à l'impression
            sys.path.insert(0, os.path.join(HERE, "..", "MoodiMOS_Organique"))
            import orienter_impression as oi
            tri, _ = oi.fill_small_holes(V[F])
            V, F = load_tri(tri)
            m = to_manifold(V, F)
        _cache[name] = m
    return _cache[name]


def load_tri(tri):
    V, idx = np.unique(tri.reshape(-1, 3).round(6), axis=0, return_inverse=True)
    return V, idx.reshape(-1, 3)


def union(names):
    m = None
    for n in names:
        p = placed(n)
        m = p if m is None else m + p
    return m


# couples à contrôler : (pièce A, pièce B, attendu, jeu mini, jeu maxi)
#   contact : faces en appui (jeu 0 à 0,05, pas d'interpénétration) ;
#   collage / glissant : jeu voulu entre les bornes ; libre : ne doivent pas se toucher (jeu ≥ mini).
SHOE = ["lisere", "semelle", "bout", "tige", "flanc", "talon", "col", "lacets", "pastille", "vent_basket", "bord_bout"]
PAIRS = []
PAIRS += [("tete_cerclage", "tete_plaque", "contact", 0, 0.05),
          ("tete_plaque", "tete_dos", "contact", 0, 0.05),
          ("tete_dos", "tete_cerclage", "libre (casques compris)", 0.3, 99),
          ("vitre_tete", "tete_cerclage", "poche de vitre", 0, 0.4),
          ("tete_plaque", "torse_coque", "méplat sur le collier", 0, 0.05),
          ("tete_dos", "torse_coque", "appui sur le collier", 0, 0.05),
          ("torse_coque", "torse_dos", "lèvre de centrage", 0, 0.25),
          ("torse_cadre", "torse_facade", "contact (collé)", 0, 0.1),
          ("torse_facade", "torse_coque", "joint vissé", 0, 0.05),
          ("torse_boite_hp", "torse_dos", "bague de centrage", 0, 0.25),
          ("vitre_torse", "torse_facade", "poche de vitre", 0, 0.25),
          ("torse_jonc_droit", "torse_coque", "rainure (collé)", 0, 0.15),
          ("compo_bh1750", "torse_dos", "trou du capteur", 0, 0.3)]
sys.path.insert(0, HERE)
import robot_pieces as _qp                                    # cotes du torse (trous de tenons)
_XS = _qp.W / 2 - _qp.p["socketT"]                        # fond de la cuvette d'épaule
_ZB = -_qp.H / 2                                          # dessous du torse
from robot_style import TORSO_STYLE as _TS
_LX = _TS.get("legX", _qp.p["legX"])
for s, sg in (("droit", 1), ("gauche", -1)):
    x0, x1 = sorted((sg * (_XS - 8.0), sg * (_XS - 0.3)))
    PAIRS += [(("epaule_" + s, (x0, x1, -99, 99, -99, 199), "tenon"), "torse_coque", "tenon dans son trou", 0.1, 0.35),
              (("jambe_" + s, (sg * _LX - 9, sg * _LX + 9, -99, 99, _ZB + 0.3, _ZB + 9), "tenon"), "torse_coque",
               "tenon dans son trou", 0.1, 0.35)]
for s in ("droit", "gauche"):
    PAIRS += [("epaule_" + s, "torse_coque", "appui dans la cuvette", 0, 0.05),
              ("bras_" + s, "epaule_" + s, "coupelle (collée)", 0, 0.25),
              ("bras_" + s, "torse_coque", "libre", 0.3, 99),
              ("bracelet_" + s, "bras_" + s, "enfilé", 0, 0.5),
              ("gant_" + s, "bras_" + s, "tenon de poignet", 0, 0.35),
              ("gant_" + s, "torse_coque", "libre", 0.5, 99),
              ("jambe_" + s, "torse_coque", "appui sous le torse", 0, 0.05),
              ("jambe_" + s, ["%s_%s" % (p, s) for p in SHOE], "logement de la jambe", 0.1, 0.4)]
PAIRS += [(["%s_droit" % p for p in SHOE], ["%s_gauche" % p for p in SHOE], "libre", 0.5, 99),
          ("gant_droit", "jambe_droit", "libre", 0.5, 99), ("gant_gauche", "jambe_gauche", "libre", 0.5, 99)]


def pair_name(x):
    if isinstance(x, tuple):
        return x[0] + " (" + x[2] + ")"
    if isinstance(x, list):
        side = x[0].rsplit("_", 1)[1]
        return "basket_" + side
    return x


def check_pair(a, b, kind, gmin, gmax):
    def get(x):
        if isinstance(x, tuple):                       # (pièce, boîte de découpe (x0, x1, y0, y1, z0, z1), libellé)
            lo, hi = x[1][0::2], x[1][1::2]
            return placed(x[0]) ^ Manifold.cube(tuple(h - l for l, h in zip(lo, hi))).translate(lo)
        return union(x) if isinstance(x, list) else placed(x)
    ma, mb = get(a), get(b)
    inter = (ma ^ mb).volume()
    gap = ma.min_gap(mb, 3.0)                 # 3 mm de recherche : au-delà, renvoie 3
    ok = inter < 0.5 and gmin - 1e-3 <= gap <= gmax + 1e-3
    if kind.startswith("libre"):
        ok = inter < 0.01 and gap >= gmin
    return dict(a=pair_name(a), b=pair_name(b), attendu=kind, interpenetration_mm3=round(inter, 3),
                jeu_min_mm=round(gap, 3), borne=[gmin, gmax], ok=bool(ok))


# ── correspondance impression / pièce en place ──

MERGED = {"basket": SHOE, "gant_bracelet": ["gant", "bracelet"]}
ACCESSOIRES = ("socle", "gabarit_", "pochoir_", "cache_")


def supports_impression():
    """Corps détachables ajoutés aux seuls STL d'impression (robot_pieces.SUPPORTS_IMPRESSION) : volume et nombre de corps."""
    path = os.path.join(PLACED_DIR, "supports_impression.json")
    return json.load(open(path)) if os.path.exists(path) else {}


def main():
    t0 = time.time()
    supports = supports_impression()
    report = dict(pieces={}, paires=[], echecs=[])
    sys.stdout.reconfigure(line_buffering=True)            # avancement visible dans le journal, fichier par fichier
    # accessoires sans pièce montée (socle, gabarits) : maillages denses, vérifiés par robot_verifier.py ; les auto-intersections
    # et parois minces sur ces fichiers saturaient la mémoire (8 Go) et faisaient durer le contrôle des heures
    files = sorted(f for f in os.listdir(PRINT_DIR) if f.lower().endswith(".stl") and not f.startswith(ACCESSOIRES))
    print("1. Maillage des {} STL d'impression".format(len(files)))
    for f in files:
        name = unicodedata.normalize("NFC", f[:-4])
        V, F = load(os.path.join(PRINT_DIR, f))
        info = topology(V, F)
        info["auto_intersections"], info["exemples_intersection"] = self_intersections(V, F)
        info["paroi_mince_pct"], info["exemples_mince"] = thin_walls(V, F)
        info["paroi_mince_pct"] = round(100 * info["paroi_mince_pct"], 2)
        # correspondance avec la pièce en place
        kind, _, side = name.rpartition("_")
        try:
            if kind in MERGED:
                ref = None   # pièce fusionnée pour l'impression : comparée aux pièces en place, avec tolérance large
                parts = ["%s_%s" % (p, side) for p in MERGED[kind]]
                vol_ref = sum(placed(p).volume() for p in parts) / 1000
                info["volume_en_place_cm3"] = round(vol_ref, 2)
                info["correspond"] = abs(info["volume_cm3"] - vol_ref) / vol_ref < 0.15
            else:
                vol_ref = placed(name).volume() / 1000
                info["volume_en_place_cm3"] = round(vol_ref, 3)
                vol_print = info["volume_cm3"] - supports.get(name, {}).get("volume_cm3", 0.0)   # sans les supports intégrés
                info["correspond"] = abs(vol_print - vol_ref) / max(vol_ref, 1e-6) < 0.01
        except FileNotFoundError:
            info["correspond"] = None
        sound = (info["open_edges"] == 0 and info["nonmanifold_edges"] == 0 and info["bad_orientation"] == 0
                 and info["degenerate"] == 0 and info["duplicate"] == 0 and info["voids"] == 0
                 and info["shells"] == 1 + supports.get(name, {}).get("corps", 0)
                 and info["auto_intersections"] == 0 and info["correspond"] is not False)
        info["ok"] = bool(sound)
        report["pieces"][name] = info
        if not sound:
            report["echecs"].append(name)
        print("  {:<24} {:>7} tri  trous {:>3}  non-manif {:>3}  orient {:>3}  dégén {:>3}  coques {}  vides {}  "
              "auto-inter {:>4}  <{} mm {:>5.1f} %  vol {:7.2f} cm³ ({})  {}".format(
                  name, info["triangles"], info["open_edges"], info["nonmanifold_edges"], info["bad_orientation"],
                  info["degenerate"], info["shells"], info["voids"], info["auto_intersections"], THIN,
                  info["paroi_mince_pct"], info["volume_cm3"],
                  "=" if info["correspond"] else ("≠" if info["correspond"] is False else "?"),
                  "OK" if sound else "À REVOIR"))
    print("2. Jeux entre pièces montées")
    for a, b, kind, gmin, gmax in PAIRS:
        try:
            r = check_pair(a, b, kind, gmin, gmax)
        except FileNotFoundError as e:
            r = dict(a=pair_name(a), b=pair_name(b), attendu=kind, erreur="introuvable : %s" % e, ok=False)
        report["paires"].append(r)
        if not r["ok"]:
            report["echecs"].append("{} / {}".format(r["a"], r["b"]))
        print("  {:<22} / {:<22} {:<22} commun {:>8} mm³  jeu {:>6} mm  (attendu {}–{})  {}".format(
            r["a"], r["b"], kind, r.get("interpenetration_mm3", "—"), r.get("jeu_min_mm", "—"), gmin,
            gmax if gmax < 99 else "∞", "OK" if r["ok"] else "À REVOIR"))
    report["duree_s"] = round(time.time() - t0, 1)
    os.makedirs(os.path.join(HERE, "rendus"), exist_ok=True)
    json.dump(report, open(os.path.join(HERE, "rendus", "controle_impression.json"), "w"), indent=1, ensure_ascii=False)
    print("Échecs :", report["echecs"] or "aucun", "— {} s".format(report["duree_s"]))


if __name__ == "__main__":
    main()
