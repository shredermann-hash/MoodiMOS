# MoodiMOS — pièces organiques (bras, gants, jambes, baskets) en STL
#
# Formes décrites par des distances signées (SDF) fondues entre elles, puis maillées
# (marching cubes). Style jouet de la planche de référence, proportions harmonisées
# avec le torse 82 × 114 × 48 et la tête Ø 82.
#
# Utilisation (depuis la racine du projet) :
#   cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Organique/organique.py
# (environnement : python3 -m venv cao/MoodiMOS_Organique/.venv
#                  cao/MoodiMOS_Organique/.venv/bin/pip install -r cao/MoodiMOS_Organique/requirements.txt)
#
# Sortie : cao/MoodiMOS_Organique/stl/<pièce>_<côté>.stl, en mm, dans le repère du torse
# (x à droite, y = profondeur depuis la façade, z vers le haut, origine au centre de la façade) :
# l'assemblage Fusion les importe tels quels. Une pièce par couleur de filament, sans
# recouvrement : chaque pièce s'imprime seule puis se colle.
#   Bras   : épaule (gris, tenon Ø 9 aimanté dans le torse : bras orientable et amovible),
#            bras (ivoire, tenon de poignet),
#            bracelet (bleu, s'enfile sur le poignet), gant (ivoire).
#   Jambes : jambe (ivoire, tenon Ø 9 aimanté dans le torse, s'enfonce dans la basket),
#            liseré (bleu, sous la semelle), semelle, bout, col, lacets, pastille (blanc), tige (bleu).
#   Impression : stl_impression/gant_bracelet_*.stl (gant + bracelet d'un bloc, bracelet contre le
#   plateau) ; stl_impression/basket_*.stl réunit toute la basket en une pièce (changements de
#   filament à 2, 9 et 32,5 mm ; bout, lacets et pastille peints). stl/ garde les pièces par couleur
#   pour le rendu et pour une impression multicolore.

import json, os, struct
import numpy as np
from skimage.measure import marching_cubes

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")                   # pièces par couleur (rendu)
OUT_PRINT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl_impression")  # pièces fusionnées à imprimer
PLACEMENT = os.path.join(OUT_PRINT, "placement.json")   # où remettre chaque pièce imprimée sur le robot

# ── repères repris du torse (cao/MoodiMOS_Torse) ──
T = dict(W=82, H=120.5, D=48, shoulderDown=18, socketT=1.5, wallT=2.5,
         pegD=9.4, shHoleDepth=4.7, legHoleDepth=5.5, legX=23)
PEG = T["pegD"] - 0.4                   # tenons Ø 9 dans les trous Ø 9,4 (jeu 0,2 au rayon : pivot glissant)
MAG_R, MAG_T = 3.1, 3.2                 # logement d'aimant Ø 6 × 3 en bout de tenon (collé)
Z_SH = T["H"] / 2 - T["shoulderDown"]   # axe d'épaule
U_MID = T["D"] / 2
X_SOCKET = T["W"] / 2 - T["socketT"]    # fond de la cuvette d'épaule
Z_BOT = -T["H"] / 2                     # dessous du torse

LEG_VISIBLE = 36                        # jambe visible entre torse et col de la basket
SHOE_TOP = 40                           # hauteur de la basket, col compris
Z_GROUND = Z_BOT - LEG_VISIBLE - SHOE_TOP
SHOE_U = 14                             # origine basket : v = 0 ici, bout vers l'avant (u décroissant)
COLLAR_V = -10                          # axe de la jambe dans la basket (u = SHOE_U - COLLAR_V = U_MID)
DOWEL_R, DOWEL_DEPTH = 0.95, 4          # pions : filament 1,75 dans Ø 1,9
LISERE_H = 2.0                          # liseré bleu : 2 premiers mm de la semelle

# bras : fibre neutre (x, z, u) et rayons, côté droit ; hauteurs relatives à l'axe d'épaule
ARM_PTS = np.array([(46.5, Z_SH - 3, 24), (51, Z_SH - 9, 24), (55.5, Z_SH - 24, 24), (58, Z_SH - 39, 22.5),
                    (59, Z_SH - 53, 19), (59.5, Z_SH - 66, 15.5), (60, Z_SH - 75, 13.5)], float)   # mains écartées du tronc
ARM_R = np.array([7.0, 7.8, 8.2, 6.6, 7.2, 6.4, 6.2])   # épaule, biceps, coude, avant-bras, poignet
ELBOW_KNOB = (np.array([62.0, Z_SH - 40, 25.5]), 3.6)       # pointe du coude, en dehors et en arrière
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


def glove_print_rotation(mirror=False):
    """Rotation (repère STL x, y = u, z) qui pointe le poignet vers le haut : bracelet sur le plateau, doigts en
    haut. Reprise par cao/verif_emboitements.py pour remettre le gant imprimable à sa place sur le robot."""
    wrist = ARM_PTS[-1] - ARM_PTS[-2]
    direction = np.array([wrist[0] * (-1 if mirror else 1), wrist[2], wrist[1]])
    direction /= np.linalg.norm(direction)
    up = np.array([0.0, 0.0, 1.0])
    cross = np.cross(direction, up)
    cosine = np.dot(direction, up)
    skew = np.array([[0.0, -cross[2], cross[1]],
                     [cross[2], 0.0, -cross[0]],
                     [-cross[1], cross[0], 0.0]])
    return np.eye(3) + skew + skew @ skew / (1.0 + cosine)


def note_placement(name, rotation, shift):
    """Range dans stl_impression/placement.json la matrice 4 × 4 (mm) qui remet la pièce imprimée à sa place sur
    le robot : robot = rotationᵀ · (imprimé + décalage). Lue par cao/MoodiMOS_Assemblage (« tel qu'imprimé »)."""
    m = np.eye(4)
    m[:3, :3] = rotation.T
    m[:3, 3] = rotation.T @ shift
    table = json.load(open(PLACEMENT)) if os.path.exists(PLACEMENT) else {}
    table[name] = [[round(float(x), 6) for x in row] for row in m]
    with open(PLACEMENT, "w") as fh:   # une rangée de matrice par ligne
        fh.write("{\n" + ",\n".join('"{}": [\n  {}]'.format(k, ",\n  ".join(json.dumps(r) for r in table[k]))
                                    for k in sorted(table)) + "\n}\n")


def write_stl(path, verts, faces, mirror=False, print_name=None):
    """verts en (x, z, u) → STL en (x, y = u, z) ; pièces d'impression posées sur Z = 0."""
    v = np.column_stack([verts[:, 0], verts[:, 2], verts[:, 1]]).astype(np.float64)
    f = faces[:, ::-1]   # (x, z, u) → (x, u, z) échange deux axes : l'orientation s'inverse
    if mirror:
        v[:, 0] = -v[:, 0]
        f = f[:, ::-1]
    if print_name is not None:
        rotation = glove_print_rotation(mirror) if print_name == "gant_bracelet" else np.eye(3)
        v = v @ rotation.T
        # Les coordonnées d'assemblage ne doivent pas être importées telles quelles dans Cura.
        shift = np.append((v[:, :2].min(axis=0) + v[:, :2].max(axis=0)) / 2, v[:, 2].min())
        v -= shift
        note_placement(os.path.basename(path)[:-4], rotation, shift)
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
    X, Z, U, axes, st = grid((38, Z_SH - 87, 2), (70, Z_SH + 8, 34), 0.25)
    arm = tube(X, Z, U, pts, rads)
    arm = smin(arm, sphere(X, Z, U, *ELBOW_KNOB), 3.0)            # coude marqué
    cut = hand_top - 0.5 * t                                         # bout plat, juste sous la manchette du gant
    arm = np.maximum(arm, (X - cut[0]) * t[0] + (Z - cut[1]) * t[1] + (U - cut[2]) * t[2])
    arm = np.minimum(arm, capsule(X, Z, U, wrist, peg_end, 2.0))
    arm = np.maximum(arm, -(sphere(X, Z, U, BALL_C, BALL_R) - 0.15))   # coupelle : 0,15 de jeu pour la colle
    arm = np.maximum(arm, (T["W"] / 2 + 0.5) - X)                  # ne touche pas le flanc
    emit("bras", arm, axes, st)

    # bracelet : bague arrondie autour du poignet (s'enfile avant le gant)
    c = wrist - 2.5 * t
    # rayon réel du bras sous le bracelet (le fondu des tronçons gonfle un peu le tube)
    nrm = np.cross(t, [0, 0, 1.0])
    nrm /= np.linalg.norm(nrm)
    rs = np.linspace(4, 10, 1201)
    r_arm = 0.0
    for a_ in np.linspace(-6.25, 1.25, 16):
        P = (c + a_ * t)[None, :] + rs[:, None] * nrm[None, :]
        d_ = tube(P[:, 0], P[:, 1], P[:, 2], pts, rads)
        r_arm = max(r_arm, float(rs[np.argmax(d_ > 0)]))
    ri, ro, hh = r_arm + 0.3, r_arm + 3.8, 3.75

    def ring(X, Z, U, a0, a1, r0, r1, rr):
        """Anneau d'axe t autour du poignet, entre les abscisses a0 et a1 (depuis c)."""
        dx, dz, du = X - c[0], Z - c[1], U - c[2]
        a = dx * t[0] + dz * t[1] + du * t[2]
        rho = length(dx - a * t[0], dz - a * t[1], du - a * t[2])
        return rect2d(rho - (r0 + r1) / 2, a - (a0 + a1) / 2, ((r1 - r0) / 2, (a1 - a0) / 2), rr)

    def bracelet_f(X, Z, U):
        return ring(X, Z, U, -hh, hh, ri, ro, 1.6)

    # gant stylisé : paume épaisse, manchette, pouce et index séparés, trois doigts groupés
    # (plus solide en FDM et plus lisible à cette échelle)
    hx, hz, hu = hand_top[0] + 0.5, hand_top[1], hand_top[2]
    palm_c = (hx + 0.5, hz - 11, hu)

    def glove_f(X, Z, U):
        palm = ellipsoid(X, Z, U, palm_c, (7.0, 10.5, 9.5))
        cuff = cyl_axis(X, Z, U, (hx, hz, hu), (0, 1, 0), 7.4, -4.5, 0, 1.5)
        body = smin(palm, cuff, 2.0)

        def finger(du_, r, ext, curl):
            base = (palm_c[0], palm_c[1] - 7, hu + du_)
            mid = (palm_c[0] - 0.6, palm_c[1] - 14 - ext, hu + du_ * 1.03)
            tip = (palm_c[0] - curl, palm_c[1] - 18.5 - ext, hu + du_ * 1.05)
            return np.minimum(capsule(X, Z, U, base, mid, r, r - 0.1), capsule(X, Z, U, mid, tip, r - 0.1, r - 0.3))

        index = finger(-7.4, 2.8, 0.5, 2.0)                         # index plus droit, détaché (jour ≈ 1,1 mm)
        group = None
        for du_, r, ext in ((-0.3, 3.2, 1.5), (4.3, 3.1, 1.0), (8.3, 2.8, -0.5)):
            f = finger(du_, r, ext, 4.5)
            group = f if group is None else smin(group, f, 0.6)     # sillons arrondis entre les doigts
        fingers = smin(index, group, 0.6)
        thumb = capsule(X, Z, U, (palm_c[0] - 2, palm_c[1] + 2, hu - 8), (palm_c[0] - 4.5, palm_c[1] - 6, hu - 11.5), 3.4, 3.0)
        glove = smin(smin(body, fingers, 1.5), thumb, 1.5)
        socket = capsule(X, Z, U, wrist, peg_end + 0.5 * t, 2.2)   # logement du tenon de poignet
        return np.maximum(glove, -socket)

    X, Z, U, axes, st = grid(wrist - 12, wrist + 12, 0.15)
    emit("bracelet", bracelet_f(X, Z, U), axes, st)
    X, Z, U, axes, st = grid((46, Z_SH - 121, -4), (74, Z_SH - 69, 30), 0.2)
    emit("gant", glove_f(X, Z, U), axes, st)

    # impression : gant et bracelet d'un seul bloc, reliés par un anneau qui comble le jour de
    # 0,75 mm ; la face plate du bracelet se pose sur le plateau, le gant pousse au-dessus
    X, Z, U, axes, st = grid((40, Z_SH - 121, -6), (76, Z_SH - 60, 32), 0.25)
    # liaison dans l'axe du poignet : anneau (épaisseur du bracelet) autour du bout du bras,
    # puis disque plein sous le bout plat du bras (le logement du tenon y est recreusé)
    a_cut = 2.5 + 1.5                                                # bout du bras, compté depuis c
    collar = ring(X, Z, U, hh - 1.0, a_cut + 0.3, ri, ro, 0.8)
    plug = ring(X, Z, U, a_cut + 0.3, 2.5 + 6.5, 0.0, 7.6, 1.2)
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

def shoe_shape(v, w, h):
    """Volume de la tige (repère basket : v vers le bout, w latéral, h hauteur sans cambrure)."""
    heel = box_round(v + 18, w, h - 18, (17, 17.5, 10), 8)
    toe = ellipsoid(v, w, h, (14, 0, 9), (19, 18, 11.5))
    collar = cyl_axis(v, w, h, (COLLAR_V, 0, 0), (0, 0, 1), 13, 9, 34, 3)
    vamp = capsule(v, w * 0.72, h, (14, 0, 8), (-6, 0, 20), 12)     # cou-de-pied : pente du bout vers le col
    return smin(smin(smin(heel, toe, 8), collar, 6), vamp, 5)


def shoe_lift(v):
    """Relevé du bout (toe spring) : 0 jusqu'à v = 18, 2,5 mm au bout."""
    return 2.5 * np.clip((v - 18) / 14, 0, 1) ** 2


def shoe_fields(X, Z, U):
    """Champs de la basket droite dans le repère monde ; renvoie un dict de SDF."""
    v, w = SHOE_U - U, X - T["legX"]
    h = Z - Z_GROUND - shoe_lift(v)                                 # toute la basket suit le relevé du bout
    # semelle à deux niveaux : base pleine, étage supérieur en retrait de 1,2
    sole = smin(extrude_round(rect2d(v + 4, w, (36, 19.5), 15), h, 0, 5.5, 2.0),
                extrude_round(rect2d(v + 4, w, (34.8, 18.3), 13.8), h, 4.5, 9, 1.8), 0.8)
    blob = shoe_shape(v, w, h)

    def top_h(vv, ww):                                              # dessus de la tige en (v, w)
        hs = np.linspace(45, 5, 801)
        return hs[np.argmax(shoe_shape(np.full_like(hs, vv), np.full_like(hs, ww), hs) < 0)]

    def side_w(vv, hh):                                             # flanc extérieur de la tige
        ws = np.linspace(30, 0, 601)
        return ws[np.argmax(shoe_shape(np.full_like(ws, vv), ws, np.full_like(ws, hh)) < 0)]

    # languette : dépasse au-dessus des lacets, devant le col
    tv = 4.0
    tongue = box_round(v - tv, w, h - (top_h(tv, 0) + 1.5), (1.5, 6.0, 3.5), 1.4)
    # panneaux latéraux en léger relief (bande inclinée vers le talon)
    band = np.abs(h - (11 + 0.12 * (v + 20))) - 2.0
    panel = smax(smax(smax(smax(blob - 0.8, band, 0.8), 9 - np.abs(w), 2.0), v - 12, 2.0), -34 - v, 2.0)
    panel = smax(panel, 11 - h, 1.0)   # s'arrête au-dessus de la semelle (pas de bourrelet au talon)
    # contrefort de talon relevé
    counter = smax(smax(smax(blob - 0.9, -v - 30, 1.5), h - 24, 1.5), 8 - h, 1.5)   # descend dans la semelle : pas de bourrelet au pied
    upper = smin(smin(smin(blob, tongue, 0.8), panel, 0.4), counter, 0.4)
    body = smin(sole, smax(upper, 8 - h, 1.0), 2.5)   # raccord large : pas de fente semelle / talon

    hole = cyl_axis(v, w, h, (COLLAR_V, 0, 0), (0, 0, 1), 7.25, 16, 60)
    cuffc = (COLLAR_V, 0, SHOE_TOP - 3.8)
    cuff = length(length(v - cuffc[0], w) - 10, h - cuffc[2]) - 3.8

    laces = None
    for lv in (7.0, 11.0, 15.0):                                    # barrettes droites le long du cou-de-pied
        hc = top_h(lv, 0)
        l = capsule(v, w, h, (lv, -7.0, hc - 0.7), (lv, 7.0, hc - 0.7), 1.9)
        laces = l if laces is None else np.minimum(laces, l)

    bv, bh = -16.0, 19.0                                            # pastille latérale (côté extérieur)
    wsurf = side_w(bv, bh)
    rho = length(v - bv, h - bh)
    # dessus qui épouse le flanc réel (panneaux en relief compris) : 0,8 de relief partout, sinon la tige
    # bombée côté bout recouvre l'anneau et l'entaille
    badge = smax(smax(np.maximum(rho - 5.0, 2.4 - rho), body - 0.8, 0.3), (wsurf - 2.5) - w, 0.3)

    toe_region = np.maximum((16 + 0.3 * (h - 9)) - v, 9 - h)       # < 0 : bout blanc (tiers avant)
    dowel = None
    for dv in (-18.0, -30.0, 24.0):
        dd = cyl_axis(v, w, h, (dv, 0, 0), (0, 0, 1), DOWEL_R, 9 - DOWEL_DEPTH, 9 + DOWEL_DEPTH)
        dowel = dd if dowel is None else np.minimum(dowel, dd)
    return dict(body=body, hole=hole, cuff=cuff, laces=laces, badge=badge, toe=toe_region, h=h, dowel=dowel)


def build_leg_and_shoe():
    # jambe : du dessous du torse au fond du logement de la basket, tenon en haut
    zb = Z_GROUND + 16.5
    X, Z, U, axes, st = grid((T["legX"] - 10, zb - 1, U_MID - 12), (T["legX"] + 10, Z_BOT + 7, U_MID + 10), 0.25)
    # partie visible galbée (cuisse, genou, mollet, cheville) ; partie cachée dans la basket : Ø 14 droit
    lx = T["legX"]
    leg_pts = np.array([(lx, Z_BOT + 3, U_MID), (lx, Z_BOT - 8, U_MID - 0.4), (lx, Z_BOT - 16, U_MID - 0.6),
                        (lx, Z_BOT - 24, U_MID + 0.4), (lx, Z_BOT - 32, U_MID + 0.2), (lx, Z_BOT - 38, U_MID)], float)
    leg_r = np.array([7.2, 7.4, 6.9, 7.3, 6.5, 6.3])
    pts, rads = catmull(leg_pts, leg_r, n=12)
    leg = tube(X, Z, U, pts, rads)
    leg = smin(leg, sphere(X, Z, U, (lx, Z_BOT - 15, U_MID - 6.0), 2.6), 2.5)   # rotule discrète
    leg = np.maximum(leg, Z - Z_BOT)                                            # dessus plat contre le torse
    hidden_top = Z_BOT - LEG_VISIBLE - 1.0                                      # sous le col de la basket
    leg = np.minimum(leg, cyl_axis(X, Z, U, (lx, 0, U_MID), (0, 1, 0), 7.0, zb, hidden_top, 0.8))
    top = Z_BOT + T["legHoleDepth"] - 0.2
    ten = cyl_axis(X, Z, U, (T["legX"], 0, U_MID), (0, 1, 0), PEG / 2, Z_BOT - 0.5, top, 0.4)
    pocket = cyl_axis(X, Z, U, (T["legX"], 0, U_MID), (0, 1, 0), MAG_R, top - MAG_T, top + 1)
    emit("jambe", np.maximum(np.minimum(leg, ten), -pocket), axes, st)

    lo = (T["legX"] - 23, Z_GROUND - 1, SHOE_U - 36)
    hi = (T["legX"] + 23, Z_GROUND + SHOE_TOP + 1, SHOE_U + 42)
    X, Z, U, axes, st = grid(lo, hi, 0.25)
    f = shoe_fields(X, Z, U)
    body = np.maximum(f["body"], -f["hole"])
    cuff = np.maximum(f["cuff"], -f["hole"])
    h = f["h"]
    lisere = np.maximum(body, h - LISERE_H)                        # liseré bleu sous la semelle
    sole = np.maximum(np.maximum(np.maximum(body, h - 9), LISERE_H - h), -f["dowel"])
    toe = np.maximum(np.maximum(np.maximum(body, f["toe"]), -f["laces"]), -f["dowel"])
    upper = body
    for cut in (9 - h, -f["toe"], -f["cuff"], -f["laces"], -f["badge"], -f["dowel"]):
        upper = np.maximum(upper, cut)
    emit("lisere", lisere, axes, st)
    emit("semelle", sole, axes, st)
    emit("bout", toe, axes, st)
    emit("tige", upper, axes, st)
    emit("col", cuff, axes, st)
    emit("lacets", f["laces"], axes, st)
    emit("pastille", f["badge"], axes, st)

    # basket d'un seul bloc pour l'impression : pas de pions ; couleurs par changement de filament
    # (liseré bleu 0-2, semelle blanche 2-9, tige bleue 9-32,5, col blanc au-dessus), bout, lacets
    # et pastille peints en blanc (rainures comme guides)
    whole = smin(smin(smin(f["body"], f["cuff"], 2.0), f["laces"], 0.5), f["badge"], 0.5)   # col raccordé : pas de fente dessous
    emit("basket", np.maximum(whole, -f["hole"]), axes, st, OUT_PRINT)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_PRINT, exist_ok=True)
    print("Bras :")
    build_arm()
    print("Jambe et basket :")
    build_leg_and_shoe()
    print("STL écrits dans", OUT)
