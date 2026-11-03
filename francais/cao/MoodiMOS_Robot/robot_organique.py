# MoodiMOS — membres organiques propres à cette édition.
# Générer avec robot_membres.py, qui ajoute les guides d'articulation et pose les pièces.
# Torse source conservé ; robot_style.py définit les chaussures et l'écartement.
# STL assemblés en mm, x à droite, y vers le dos, z en haut.
# Couleurs : bras/jambes/gants ivoire, bracelets/bouts/brides orange,
# arrière et base de chaussures noirs. Les traits noirs des gants sont peints.

import os, struct, ast
from robot_style import TORSO_STYLE, LEG_VISIBLE, SHOE_TOP, SHOE_X
import numpy as np
from skimage.measure import marching_cubes

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")                   # pièces par couleur (rendu)
OUT_PRINT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl_impression")  # pièces fusionnées à imprimer

# ── repères repris du torse (cao/MoodiMOS_Torse) ──
# Lire les interfaces dans le torse source, puis appliquer la silhouette.
_source = os.path.join(os.path.dirname(__file__), "..", "MoodiMOS_Torse", "MoodiMOS_Torse.py")
_tree = ast.parse(open(_source, encoding="utf-8").read())
_base = next(ast.literal_eval(n.value) for n in _tree.body if isinstance(n, ast.Assign)
             and getattr(n.targets[0], "id", "") == "PARAMS")
_base.update(TORSO_STYLE)
T = {k: _base[v] for k, v in dict(W="torsoW", H="torsoH", D="torsoD",
     shoulderDown="shoulderDown", socketT="socketT", wallT="wallT", pegD="pegD",
     shHoleDepth="shHoleDepth", legHoleDepth="legHoleDepth", legX="legX").items()}
PEG = T["pegD"] - 0.4                   # tenons Ø 9 dans les trous Ø 9,4 (jeu 0,2 au rayon : pivot glissant)
MAG_R, MAG_T = 3.1, 3.2                 # logement d'aimant Ø 6 × 3 en bout de tenon (collé)
Z_SH = T["H"] / 2 - T["shoulderDown"]   # axe d'épaule
U_MID = T["D"] / 2
X_SOCKET = T["W"] / 2 - T["socketT"]    # fond de la cuvette d'épaule
Z_BOT = -T["H"] / 2                     # dessous du torse

Z_GROUND = Z_BOT - LEG_VISIBLE - SHOE_TOP
SHOE_U = 6                             # origine basket : v = 0 ici, bout vers l'avant (u décroissant)
COLLAR_V = -18                          # axe de la jambe dans la basket (u = SHOE_U - COLLAR_V = U_MID)
DOWEL_R, DOWEL_DEPTH = 0.95, 4          # pions : filament 1,75 dans Ø 1,9
LISERE_H = 4.8                          # liseré noir sous la semelle

# bras : fibre neutre (x, z, u) et rayons, côté droit ; hauteurs relatives à l'axe d'épaule
ARM_PTS = np.array([(46.5, Z_SH - 3, 24), (51.5, Z_SH - 9, 24), (59, Z_SH - 27, 24), (63, Z_SH - 42, 22.5),
                    (67, Z_SH - 58, 19), (70, Z_SH - 74, 15.5), (71, Z_SH - 86, 13.5)], float)   # bras plus écartés
ARM_R = np.array([7.0, 8.6, 9.0, 7.4, 8.0, 7.2, 7.0])   # épaule, biceps, coude, avant-bras, poignet (plus épais)
ELBOW_KNOB = (np.array([65.0, Z_SH - 42, 25.5]), 3.8)       # pointe du coude, en dehors et en arrière
GLOVE_SCALE = 1.25                                          # gros poings (boule sculptée)
CUFF_C = 2.4                                                # centre du bracelet, en amont du poignet (il coiffe le haut du gant : imprimés d'un bloc)
CUFF_HH, CUFF_T = 5.0, 5.5                                  # bracelet haut (2 × 5) et épais (5,5 au-dessus du bras)
BALL_C, BALL_R = np.array([45.5, Z_SH, U_MID]), 8.5      # (x, z, u)


# ── primitives SDF (tableaux x, z, u) ──

def length(*c):
    return np.sqrt(sum(a * a for a in c))


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b + (a - b) * h - k * h * (1 - h)


def smax(a, b, k):
    """Intersection adoucie : arête arrondie (rayon ≈ k) au lieu d'une arête vive qui crénelle au maillage."""
    return -smin(-a, -b, k)


def sphere(x, z, u, c, r):
    return length(x - c[0], z - c[1], u - c[2]) - r


def ellipsoid(x, z, u, c, rad):
    px, pz, pu = (x - c[0]) / rad[0], (z - c[1]) / rad[1], (u - c[2]) / rad[2]
    k0 = length(px, pz, pu)
    k1 = length(px / rad[0], pz / rad[1], pu / rad[2])
    return k0 * (k0 - 1) / np.maximum(k1, 1e-9)


def capsule(x, z, u, a, b, ra, rb=None):
    rb = ra if rb is None else rb
    ba = np.subtract(b, a)
    t = ((x - a[0]) * ba[0] + (z - a[1]) * ba[1] + (u - a[2]) * ba[2]) / max(np.dot(ba, ba), 1e-9)
    t = np.clip(t, 0, 1)
    return length(x - a[0] - t * ba[0], z - a[1] - t * ba[1], u - a[2] - t * ba[2]) - (ra + t * (rb - ra))


def catmull(pts, rads, n=10):
    """Courbe lisse passant par pts (Catmull-Rom), rayons interpolés."""
    P = np.vstack([2 * pts[0] - pts[1], pts, 2 * pts[-1] - pts[-2]])
    R = np.concatenate([[rads[0]], rads, [rads[-1]]])
    out_p, out_r = [], []
    for i in range(1, len(P) - 2):
        for s in np.linspace(0, 1, n, endpoint=False):
            s2, s3 = s * s, s * s * s
            out_p.append(0.5 * ((2 * P[i]) + (-P[i - 1] + P[i + 1]) * s
                                + (2 * P[i - 1] - 5 * P[i] + 4 * P[i + 1] - P[i + 2]) * s2
                                + (-P[i - 1] + 3 * P[i] - 3 * P[i + 1] + P[i + 2]) * s3))
            out_r.append(R[i] + (R[i + 1] - R[i]) * s)
    out_p.append(P[-2])
    out_r.append(R[-2])
    return np.array(out_p), np.array(out_r)


def tube(x, z, u, pts, rads, k=0.6):
    """Tube à rayon variable : tronçons fondus (smin) pour effacer les raccords."""
    d = None
    for i in range(len(pts) - 1):
        c = capsule(x, z, u, pts[i], pts[i + 1], rads[i], rads[i + 1])
        d = c if d is None else smin(d, c, k)
    return d


def box_round(px, py, pz, half, r):
    """Boîte arrondie centrée à l'origine (coordonnées déjà relatives)."""
    qx, qy, qz = np.abs(px) - half[0] + r, np.abs(py) - half[1] + r, np.abs(pz) - half[2] + r
    return length(np.maximum(qx, 0), np.maximum(qy, 0), np.maximum(qz, 0)) \
        + np.minimum(np.maximum(np.maximum(qx, qy), qz), 0) - r


def rect2d(px, py, half, r):
    qx, qy = np.abs(px) - half[0] + r, np.abs(py) - half[1] + r
    return length(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def extrude_round(d2, h, h0, h1, rr):
    """Profil plan d2 extrudé entre h0 et h1, arêtes arrondies rr."""
    qx = d2 + rr
    qy = np.abs(h - (h0 + h1) / 2) - (h1 - h0) / 2 + rr
    return np.minimum(np.maximum(qx, qy), 0) + length(np.maximum(qx, 0), np.maximum(qy, 0)) - rr


def cyl_axis(x, z, u, c, axis, r, a0, a1, rr=0.0):
    """Cylindre d'axe (unitaire) passant par c, entre les abscisses a0 et a1, arêtes arrondies rr."""
    dx, dz, du = x - c[0], z - c[1], u - c[2]
    a = dx * axis[0] + dz * axis[1] + du * axis[2]
    rho = length(dx - a * axis[0], dz - a * axis[1], du - a * axis[2])
    return extrude_round(rho - r, a, a0, a1, rr) if rr else np.maximum(rho - r, np.maximum(a0 - a, a - a1))


# ── maillage et STL ──

def grid(lo, hi, step):
    axes = [np.arange(lo[i] - 2 * step, hi[i] + 2 * step, step, dtype=np.float32) for i in range(3)]
    X, Zg, U = np.meshgrid(*axes, indexing="ij")
    return X, Zg, U, axes, step


def mesh(sdf, axes, step):
    vol = np.asarray(sdf, np.float32)
    if vol.min() >= 0:
        raise RuntimeError("pièce vide")
    verts, faces, _, _ = marching_cubes(vol, 0.0, spacing=(step, step, step))
    verts += np.array([axes[0][0], axes[1][0], axes[2][0]], np.float32)
    faces = drop_specks(faces, len(verts))
    verts = taubin(verts, faces)
    tri = verts[faces]
    vol_signed = np.einsum("ij,ij->i", tri[:, 0], np.cross(tri[:, 1], tri[:, 2])).sum() / 6
    if vol_signed < 0:
        faces = faces[:, ::-1]
    return verts, faces


def drop_specks(faces, nverts, min_faces=40):
    """Retire les îlots de quelques triangles laissés par le maillage (pas les vraies pièces multiples)."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    e = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]]])
    g = coo_matrix((np.ones(len(e)), (e[:, 0], e[:, 1])), shape=(nverts, nverts))
    _, lab = connected_components(g, directed=False)
    flab = lab[faces[:, 0]]
    counts = np.bincount(flab)
    return faces[counts[flab] >= min_faces]


def taubin(verts, faces, iterations=12, lam=0.5, mu=-0.53):
    """Lissage de Taubin : efface les marches du maillage (marching cubes) sans faire rétrécir la pièce."""
    from scipy.sparse import coo_matrix, diags
    n = len(verts)
    e = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]])
    e = np.unique(np.sort(e, axis=1), axis=0)
    a = coo_matrix((np.ones(2 * len(e)), (np.r_[e[:, 0], e[:, 1]], np.r_[e[:, 1], e[:, 0]])), shape=(n, n)).tocsr()
    deg = np.asarray(a.sum(axis=1)).ravel()
    avg = diags(1.0 / np.maximum(deg, 1)) @ a
    v = verts.astype(np.float64)
    for _ in range(iterations):
        v = v + lam * (avg @ v - v)
        v = v + mu * (avg @ v - v)
    return v.astype(np.float32)


def write_stl(path, verts, faces, mirror=False, print_name=None):
    """verts en (x, z, u) → STL en (x, y = u, z) ; pièces d'impression posées sur Z = 0."""
    v = np.column_stack([verts[:, 0], verts[:, 2], verts[:, 1]]).astype(np.float64)
    f = faces[:, ::-1]   # (x, z, u) → (x, u, z) échange deux axes : l'orientation s'inverse
    if mirror:
        v[:, 0] = -v[:, 0]
        f = f[:, ::-1]
    if print_name == "gant_bracelet":
        # Le poignet doit pointer vers le haut : bracelet sur le plateau, doigts en haut.
        wrist = ARM_PTS[-1] - ARM_PTS[-2]
        direction = np.array([wrist[0] * (-1 if mirror else 1), wrist[2], wrist[1]])
        direction /= np.linalg.norm(direction)
        up = np.array([0.0, 0.0, 1.0])
        cross = np.cross(direction, up)
        cosine = np.dot(direction, up)
        skew = np.array([[0.0, -cross[2], cross[1]],
                         [cross[2], 0.0, -cross[0]],
                         [-cross[1], cross[0], 0.0]])
        rotation = np.eye(3) + skew + skew @ skew / (1.0 + cosine)
        v = v @ rotation.T
    if print_name is not None:
        # Les coordonnées d'assemblage ne doivent pas être importées telles quelles dans Cura.
        v[:, :2] -= (v[:, :2].min(axis=0) + v[:, :2].max(axis=0)) / 2
        v[:, 2] -= v[:, 2].min()
    v = v.astype(np.float32)
    tri = v[f]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS".ljust(80, b" "))
        fh.write(struct.pack("<I", len(f)))
        data = np.zeros(len(f), dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])
        data["n"] = n
        data["v"] = tri.reshape(-1, 9)
        fh.write(data.tobytes())


def check(name, faces, nverts):
    """Contrôle d'un maillage : nombre de pièces, arêtes ouvertes ou partagées par plus de 2 faces."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    e = np.sort(np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]]), axis=1)
    _, cnt = np.unique(e, axis=0, return_counts=True)
    g = coo_matrix((np.ones(len(e)), (e[:, 0], e[:, 1])), shape=(nverts, nverts))
    parts = connected_components(g, directed=False)[0] - (nverts - len(np.unique(faces)))
    return parts, int((cnt == 1).sum()), int((cnt > 2).sum())


def emit(name, sdf, axes, step, folder=OUT):
    verts, faces = mesh(sdf, axes, step)
    if folder == OUT_PRINT:
        parts, open_e, bad_e = check(name, faces, len(verts))
        status = "OK" if parts == 1 and open_e == 0 and bad_e == 0 else "À REVOIR"
        print("  contrôle {:<14} {} pièce(s), {} arête(s) ouverte(s), {} non manifold : {}".format(
            name, parts, open_e, bad_e, status))
    for side, mirror in (("droit", False), ("gauche", True)):
        write_stl(os.path.join(folder, "{}_{}.stl".format(name, side)), verts, faces, mirror,
                  print_name=name if folder == OUT_PRINT else None)
    print("  {:<12} {:>7} triangles".format(name, len(faces)))


# ── bras ──

def build_arm():
    pts, rads = catmull(ARM_PTS, ARM_R, n=12)
    wrist, before = ARM_PTS[-1], ARM_PTS[-2]
    t = (wrist - before) / np.linalg.norm(wrist - before)          # direction du poignet (x, z, u)
    peg_end = wrist + 7 * t
    hand_top = wrist + 2.0 * t                                      # haut du gant, sous le bracelet

    # épaule : boule aplatie au fond de la cuvette + tenon dans le torse
    X, Z, U, axes, st = grid((X_SOCKET - 6, Z_SH - 9, U_MID - 9), (BALL_C[0] + 9, Z_SH + 9, U_MID + 9), 0.2)
    ball = np.maximum(sphere(X, Z, U, BALL_C, BALL_R), X_SOCKET - X)
    ten_len = T["shHoleDepth"] - 0.2
    tenon = cyl_axis(X, Z, U, (X_SOCKET, Z_SH, U_MID), (1, 0, 0), PEG / 2, -ten_len, 0.2)
    pocket = cyl_axis(X, Z, U, (X_SOCKET - ten_len, Z_SH, U_MID), (1, 0, 0), MAG_R, -1, MAG_T)
    emit("epaule", np.maximum(np.minimum(ball, tenon), -pocket), axes, st)

    # bras : tube courbe, emboîté sur la boule, tenon de poignet
    X, Z, U, axes, st = grid((38, Z_SH - 100, 0), (84, Z_SH + 8, 36), 0.25)
    arm = tube(X, Z, U, pts, rads)
    arm = smin(arm, sphere(X, Z, U, *ELBOW_KNOB), 3.0)            # coude marqué
    cut = hand_top - 0.5 * t                                         # bout plat, juste sous la manchette du gant
    arm = np.maximum(arm, (X - cut[0]) * t[0] + (Z - cut[1]) * t[1] + (U - cut[2]) * t[2])
    arm = np.minimum(arm, capsule(X, Z, U, wrist, peg_end, 2.0))
    arm = np.maximum(arm, -(sphere(X, Z, U, BALL_C, BALL_R) - 0.15))   # coupelle : 0,15 de jeu pour la colle
    arm = smax(arm, (T["W"] / 2 + 0.5) - X, 0.6)                   # ne touche pas le flanc ; raccord arrondi (pas de pli au maillage)
    emit("bras", arm, axes, st)

    # bracelet : bague arrondie autour du poignet (s'enfile avant le gant)
    c = wrist - CUFF_C * t
    # rayon réel du bras sous le bracelet (le fondu des tronçons gonfle un peu le tube)
    nrm = np.cross(t, [0, 0, 1.0])
    nrm /= np.linalg.norm(nrm)
    rs = np.linspace(4, 10, 1201)
    r_arm = 0.0
    for a_ in np.linspace(-CUFF_HH - 0.5, CUFF_HH + 0.5, 16):
        P = (c + a_ * t)[None, :] + rs[:, None] * nrm[None, :]
        d_ = tube(P[:, 0], P[:, 1], P[:, 2], pts, rads)
        r_arm = max(r_arm, float(rs[np.argmax(d_ > 0)]))
    ri, ro, hh = r_arm + 0.3, r_arm + CUFF_T, CUFF_HH

    def ring(X, Z, U, a0, a1, r0, r1, rr):
        """Anneau d'axe t autour du poignet, entre les abscisses a0 et a1 (depuis c)."""
        dx, dz, du = X - c[0], Z - c[1], U - c[2]
        a = dx * t[0] + dz * t[1] + du * t[2]
        rho = length(dx - a * t[0], dz - a * t[1], du - a * t[2])
        return rect2d(rho - (r0 + r1) / 2, a - (a0 + a1) / 2, ((r1 - r0) / 2, (a1 - a0) / 2), rr)

    def bracelet_f(X, Z, U):
        return ring(X, Z, U, -hh, hh, ri, ro, 2.2)

    # gant stylisé : paume épaisse, manchette, pouce et index séparés, trois doigts groupés
    # (plus solide en FDM et plus lisible à cette échelle)
    hx, hz, hu = hand_top[0] + 0.5, hand_top[1], hand_top[2]
    palm_c = (hx + 0.5, hz - 11, hu)

    def glove_parts(X, Z, U):
        """Poing SCULPTÉ dans une boule (fiche) : un seul volume ovale fondu dans la manchette, un lobe de pouce et
        quatre bombés de doigts repliés FONDUS dans la masse ; puis des sillons creusés (peints en noir) : 3 entre
        les doigts, la pliure des doigts côté intérieur, la jonction du pouce. Renvoie (volume, creusement)."""
        px, pz = hx + 0.5, hz - 10.0
        blob = ellipsoid(X, Z, U, (px, pz - 1.0, hu), (8.5, 10.0, 9.5))
        cuff = cyl_axis(X, Z, U, (hx, hz, hu), (0, 1, 0), 7.4, -4.5, 0, 1.5)
        thumb = ellipsoid(X, Z, U, (px - 4.5, pz + 0.5, hu - 8.0), (4.0, 6.5, 3.6))
        body = smin(smin(blob, cuff, 2.5), thumb, 1.8)
        for du_ in (-6.9, -2.3, 2.3, 6.9):                                     # bombés des doigts repliés, fondus
            body = smin(body, ellipsoid(X, Z, U, (px - 4.0, pz - 7.0, hu + du_), (4.2, 3.4, 2.6)), 2.0)
        win = lambda d, half, ramp: np.clip((half - np.abs(d)) / ramp, 0, 1)    # profil d'un sillon
        lower = np.clip((pz - 1.0 - Z) / 2.5, 0, 1)                             # moitié basse : doigts repliés
        carve = np.zeros_like(body)
        for du_ in (-4.6, 0.0, 4.6):                                           # entre les doigts
            carve = np.maximum(carve, 1.1 * win(U - (hu + du_), 0.45, 0.3) * lower)
        inner = np.clip(((px - 3.0) - X) / 2.0, 0, 1) * np.clip((U - (hu - 6.5)) / 1.5, 0, 1)
        carve = np.maximum(carve, 0.9 * win(Z - (pz - 3.5), 0.5, 0.3) * inner)    # pliure des doigts
        # jonction du pouce : seulement là où pouce et boule se touchent
        carve = np.maximum(carve, 0.8 * win(thumb, 0.5, 0.45) * win(blob, 1.4, 0.8) * np.clip((pz + 6.0 - Z) / 2.0, 0, 1))
        return body, carve

    def glove_shape(X, Z, U):
        body, carve = glove_parts(X, Z, U)
        return body + carve

    def glove_lines(X, Z, U):
        """Liserés noirs du poing (rendu ; peinture dans les sillons) : volume exact du cœur des sillons."""
        s_ = GLOVE_SCALE
        B, C = glove_parts(hx + (X - hx) / s_, hz + (Z - hz) / s_, hu + (U - hu) / s_)
        B, C = B * s_, C * s_
        return np.maximum(np.maximum(B, -(B + C)), 0.6 * s_ - C)

    def glove_f(X, Z, U):
        s_ = GLOVE_SCALE                                             # gros poing, haut du gant fixe
        g = glove_shape(hx + (X - hx) / s_, hz + (Z - hz) / s_, hu + (U - hu) / s_) * s_
        socket = capsule(X, Z, U, wrist, peg_end + 0.5 * t, 2.2)   # logement du tenon de poignet (cote d'origine)
        return np.maximum(g, -socket)

    WRIST_BAND = 3.0                                                 # bague noire du poignet (haut de la manchette)

    X, Z, U, axes, st = grid(wrist - 16, wrist + 16, 0.15)
    emit("bracelet", bracelet_f(X, Z, U), axes, st)
    X, Z, U, axes, st = grid((45, Z_SH - 130, -6), (92, Z_SH - 78, 34), 0.2)
    g = glove_f(X, Z, U)
    emit("gant", np.maximum(g, Z - (hz - WRIST_BAND)), axes, st)        # main ivoire
    emit("poignet_gant", np.maximum(g, (hz - WRIST_BAND) - Z), axes, st)   # bague noire du poignet (peinte)
    emit("lisere_gant", glove_lines(X, Z, U), axes, st)             # liserés noirs peints dans les sillons

    # impression : gant et bracelet d'un seul bloc, reliés par un anneau qui comble le jour de
    # 0,75 mm ; la face plate du bracelet se pose sur le plateau, le gant pousse au-dessus
    X, Z, U, axes, st = grid((42, Z_SH - 130, -6), (94, Z_SH - 68, 38), 0.25)
    # liaison dans l'axe du poignet : anneau (épaisseur du bracelet) autour du bout du bras,
    # puis disque plein sous le bout plat du bras (le logement du tenon y est recreusé)
    a_cut = CUFF_C + 1.5                                             # bout du bras, compté depuis c
    collar = ring(X, Z, U, hh - 1.0, a_cut + 0.3, ri, ro, 0.8)
    plug = ring(X, Z, U, a_cut + 0.3, CUFF_C + 6.5, 0.0, 7.6 * GLOVE_SCALE, 1.2)
    joined = smin(smin(bracelet_f(X, Z, U), collar, 0.8), plug, 0.8)
    glove = glove_f(X, Z, U)
    socket = capsule(X, Z, U, wrist, peg_end + 0.5 * t, 2.2)
    # empreinte du bout du bras (+ 0,2 de jeu) : la pièce s'enfile sans forcer
    near = [i for i in range(len(pts)) if np.linalg.norm(pts[i] - wrist) < 25]
    lo_i, hi_i = max(min(near) - 1, 0), min(max(near) + 2, len(pts))
    arm_end = tube(X, Z, U, pts[lo_i:hi_i], rads[lo_i:hi_i])
    arm_end = np.maximum(arm_end, (X - cut[0]) * t[0] + (Z - cut[1]) * t[1] + (U - cut[2]) * t[2])
    # raccord large : le bracelet suit l'axe du bras (≈ 13°), le haut du gant est horizontal ; sans
    # lui, un coin très fermé reste sous le bracelet (fente à l'impression)
    piece = np.maximum(np.maximum(smin(joined, glove, 3.0), -socket), -(arm_end - 0.2))
    emit("gant_bracelet", piece, axes, st, OUT_PRINT)


# ── jambe et basket ──

# Basket, d'après les vues 3/4, profil et dos de la fiche, en mm réels (aucun étirement) :
# semelle noire épaisse, intercalaire ivoire, bout orange bombé cerné de noir, empeigne ivoire à deux ouïes noires,
# panneau arrière noir avec anneau orange incrusté à fleur, talon arrière orange, bride orange sur le dessus,
# collerette ivoire rebondie autour de la jambe. Repère basket : v vers le bout, w vers l'extérieur, h depuis le sol.
SHOE_SH = SHOE_TOP / 40.0          # (hauteur du fond du logement de la jambe : zb = Z_GROUND + 16,5 × SHOE_SH)
SHOE_L, SHOE_W = 73.9, 42.0        # longueur et largeur hors tout
SHOE_VC = COLLAR_V + 12.0          # centre de la semelle : la jambe tombe aux 2/5 arrière
OUTSOLE_H, MIDSOLE_H = 5.0, 11.5   # semelle noire, puis intercalaire ivoire
TOE_V0 = SHOE_VC + 5.0             # bout orange en avant de cette abscisse (à mi-hauteur de l'empeigne)
REAR_V = COLLAR_V - 1.0            # panneau noir en arrière de cette abscisse…
HEEL_V = SHOE_VC - SHOE_L / 2 + 8.5   # … et talon orange en arrière de celle-ci
CUFF_R, CUFF_TUBE = 12.5, 5.5      # collerette rebondie (tore), haut à SHOE_TOP
BADGE_C, BADGE_R, BADGE_W = (REAR_V - 10.0, 23.0), 7.0, 1.1   # anneau orange du flanc : centre (v, h), rayon, demi-largeur


def shoe_shape(v, w, h):
    """Tige (au-dessus de l'intercalaire) : bout bombé, empeigne, talon et embase de la collerette fondus."""
    toe = ellipsoid(v, w, h, (SHOE_VC + 15.0, 0, 12.0), (21.0, 19.5, 17.0))
    vamp = capsule(v, w * 0.92, h, (SHOE_VC + 8.0, 0, 17.0), (COLLAR_V - 4.0, 0, 22.0), 15.5)
    heel = ellipsoid(v, w, h, (SHOE_VC - 22.0, 0, 18.0), (14.5, 19.0, 15.0))
    collar = cyl_axis(v, w, h, (COLLAR_V, 0, 0), (0, 0, 1), 13.0, 18.0, SHOE_TOP - 2 * CUFF_TUBE + 1.0, 3.0)
    blob = smin(smin(smin(toe, vamp, 6.0), heel, 6.0), collar, 5.0)
    blob = smax(blob, np.abs(w) - (SHOE_W / 2 - 1.2), 2.0)                  # ne déborde pas de la semelle
    return smax(blob, MIDSOLE_H - 1.0 - h, 1.0)


def shoe_fields(X, Z, U):
    """Champs de la basket droite dans le repère monde ; renvoie un dict de SDF."""
    angle = np.deg2rad(12)                                           # pieds tournés de 12° vers l'extérieur
    dv, dx = U_MID - U, X - SHOE_X
    v = COLLAR_V + dv * np.cos(angle) + dx * np.sin(angle)
    w = dx * np.cos(angle) - dv * np.sin(angle)
    h = Z - Z_GROUND
    plan = rect2d(v - SHOE_VC, w, (SHOE_L / 2, SHOE_W / 2), 17.0)
    outsole = extrude_round(plan, h, 0, OUTSOLE_H + 0.5, 1.6)
    midsole = extrude_round(rect2d(v - SHOE_VC, w, (SHOE_L / 2 - 0.8, SHOE_W / 2 - 0.8), 16.2), h, OUTSOLE_H, MIDSOLE_H, 2.2)
    sole = smin(outsole, midsole, 0.6)
    upper = shoe_shape(v, w, h)
    body = smin(sole, upper, 1.5)

    hole = cyl_axis(v, w, h, (COLLAR_V, 0, 0), (0, 0, 1), 7.25, 16.5 * SHOE_SH - 1.0, 80)
    cuff = length(length(v - COLLAR_V, w) - CUFF_R, h - (SHOE_TOP - CUFF_TUBE)) - CUFF_TUBE

    def top_h(vv):                                                   # dessus de la tige sur l'axe (w = 0)
        hs = np.linspace(SHOE_TOP + 5, 5, 1001)
        return hs[np.argmax(shoe_shape(np.full_like(hs, vv), np.zeros_like(hs), hs) < 0)]

    # bride orange : plaquette inclinée posée sur le cou-de-pied, devant la collerette, deux rainures
    lv = COLLAR_V + 18.0                                             # en avant de la collerette (visible, fiche)
    hc = top_h(lv)
    tilt = 0.55
    dv_, dh_ = v - lv, h - (hc + 0.8)
    nn = np.sqrt(1 + tilt * tilt)
    along, normal = (dv_ - tilt * dh_) / nn, (tilt * dv_ + dh_) / nn
    flap = box_round(along, w, normal, (6.5, 8.5, 2.0), 1.3)
    for off in (-2.2, 2.2):
        flap = np.maximum(flap, -box_round(along - off, w, normal - 2.0, (0.5, 6.6, 0.65), 0.35))

    shell = lambda d: np.maximum(body - 0.05, -(body + d))          # peau de la tige, épaisseur d (incrustations)
    rho = length(v - BADGE_C[0], h - BADGE_C[1])
    badge = np.maximum(np.maximum(shell(0.8), np.abs(rho - BADGE_R) - BADGE_W), 2.0 - w)    # anneau à fleur, côté extérieur
    vents = None
    for vv in (COLLAR_V + 7.0, COLLAR_V + 10.0):                     # deux ouïes noires sur l'empeigne, côté extérieur
        slot = np.maximum(np.maximum(shell(0.9), box_round(v - vv, w, h - 21.0, (0.75, 30, 2.6), 0.6)), 2.0 - w)
        vents = slot if vents is None else np.minimum(vents, slot)
    toe_cut = (TOE_V0 + 0.3 * (h - 20.0)) - v                        # < 0 : bout orange
    border = np.maximum(np.maximum(shell(0.5), np.abs(toe_cut) - 0.45), MIDSOLE_H - h)   # trait noir autour du bout
    return dict(body=body, hole=hole, cuff=cuff, laces=flap, badge=badge, vents=vents, border=border,
                toe=np.maximum(toe_cut, MIDSOLE_H - h), h=h, v=v)


def build_leg_and_shoe():
    # jambe : du dessous du torse au fond du logement de la basket, tenon en haut
    zb = Z_GROUND + 16.5 * SHOE_SH
    X, Z, U, axes, st = grid((T["legX"] - 11, zb - 1, U_MID - 12), (SHOE_X + 11, Z_BOT + 7, U_MID + 10), 0.25)
    # partie visible galbée (cuisse, genou, mollet, cheville) ; partie cachée dans la basket : Ø 14 droit
    lx = T["legX"]
    leg_pts = np.array([(lx, Z_BOT + 3, U_MID), (lx + 1.4, Z_BOT - 8, U_MID - 0.4), (lx + 3, Z_BOT - 16, U_MID - 0.6),
                        (lx + 4.5, Z_BOT - 24, U_MID + 0.4), (SHOE_X, Z_BOT - 32, U_MID + 0.2), (SHOE_X, Z_BOT - 38, U_MID)], float)
    leg_r = np.array([7.2, 7.4, 6.9, 7.3, 6.5, 6.3]) * 1.12            # jambes plus épaisses
    pts, rads = catmull(leg_pts, leg_r, n=12)
    leg = tube(X, Z, U, pts, rads)
    leg = smin(leg, sphere(X, Z, U, (lx, Z_BOT - 15, U_MID - 6.0), 2.6), 2.5)   # rotule discrète
    leg = np.maximum(leg, Z - Z_BOT)                                            # dessus plat contre le torse
    hidden_top = Z_BOT - LEG_VISIBLE - 1.0                                      # sous le col de la basket
    leg = np.minimum(leg, cyl_axis(X, Z, U, (SHOE_X, 0, U_MID), (0, 1, 0), 7.0, zb, hidden_top, 0.8))
    top = Z_BOT + T["legHoleDepth"] - 0.2
    ten = cyl_axis(X, Z, U, (T["legX"], 0, U_MID), (0, 1, 0), PEG / 2, Z_BOT - 0.5, top, 0.4)
    pocket = cyl_axis(X, Z, U, (T["legX"], 0, U_MID), (0, 1, 0), MAG_R, top - MAG_T, top + 1)
    emit("jambe", np.maximum(np.minimum(leg, ten), -pocket), axes, st)

    lo = (SHOE_X - 40, Z_GROUND - 1, U_MID - 50)
    hi = (SHOE_X + 40, Z_GROUND + SHOE_TOP + 2, U_MID + 50)
    X, Z, U, axes, st = grid(lo, hi, 0.25)
    f = shoe_fields(X, Z, U)
    body = np.maximum(f["body"], -f["hole"])
    cuff = np.maximum(f["cuff"], -f["hole"])
    h, v = f["h"], f["v"]
    inlays = np.minimum(np.minimum(f["badge"], f["vents"]), f["border"])
    lisere = np.maximum(body, h - OUTSOLE_H)                                   # semelle noire
    sole = np.maximum(np.maximum(body, h - MIDSOLE_H), OUTSOLE_H - h)         # intercalaire ivoire
    upper = np.maximum(np.maximum(np.maximum(body, MIDSOLE_H - h), -f["cuff"]), -f["laces"])
    upper = np.maximum(upper, -inlays)
    emit("lisere", lisere, axes, st)
    emit("semelle", sole, axes, st)
    emit("bout", np.maximum(upper, f["toe"]), axes, st)                       # bout orange
    rest = np.maximum(upper, -f["toe"])
    emit("tige", np.maximum(rest, REAR_V - v), axes, st)                      # empeigne ivoire
    emit("flanc", np.maximum(np.maximum(rest, v - REAR_V), HEEL_V - v), axes, st)   # panneau noir
    emit("talon", np.maximum(rest, v - HEEL_V), axes, st)                     # talon orange
    emit("col", cuff, axes, st)
    emit("lacets", f["laces"], axes, st)                                      # bride orange
    emit("pastille", f["badge"], axes, st)                                    # anneau orange (incrusté)
    emit("vent_basket", f["vents"], axes, st)                                 # ouïes noires (incrustées)
    emit("bord_bout", f["border"], axes, st)                                  # trait noir du bout

    # basket d'un seul bloc pour l'impression (couleurs : changements de filament et peinture ; l'anneau, les ouïes
    # et le trait du bout restent creusés de 0,5 à 0,9 mm comme guides de peinture)
    whole = smin(smin(f["body"], f["cuff"], 2.0), f["laces"], 0.6)
    emit("basket", np.maximum(np.maximum(whole, -f["hole"]), -inlays), axes, st, OUT_PRINT)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_PRINT, exist_ok=True)
    print("Bras :")
    build_arm()
    print("Jambe et basket :")
    build_leg_and_shoe()
    print("STL écrits dans", OUT)
