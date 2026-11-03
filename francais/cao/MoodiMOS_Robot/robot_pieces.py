# MoodiMOS — pièces imprimables
#
# Même électronique et mêmes cotes de composants que la CAO principale : les cotes sont LUES dans
# ../MoodiMOS_Torse/MoodiMOS_Torse.py et ../MoodiMOS_Tete/MoodiMOS_Tete.py (rien n'est recopié).
# Seule l'allure change :
#   tête  : lunette NOIRE à face plate (liseré orange au bord ; index orange peints à 12, 3, 6, 9 h et en diagonale),
#           plaque à goutte orange avec face claire PEINTE dans la bélière, dos SPHÉRIQUE (dôme de rayon Rp centré sur l'arrière de la
#           plaque, tronqué à plat : disque arrière), casques latéraux d'une pièce avec le dôme (la tête s'ouvre),
#           grille de haut-parleur conique, jonc lumineux ; vis de la carte par 4 puits dans le dôme (M2×30) ;
#   torse : coque et dos de la CAO principale, plus un liseré lumineux vertical sur chaque flanc ;
#   membres : générateur ../MoodiMOS_Organique/organique.py, plus des bagues noires d'articulation
#           (rainures-guides dans les pièces ivoire, bagues séparées pour le rendu).
#
# Lancer depuis la racine du projet :
#   cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_pieces.py
# Sorties (mm, repère du torse : x à droite, y = profondeur depuis la façade, z en haut, origine au centre de la façade) :
#   stl/<pièce>.stl            : pièces en place, pour le rendu (robot_rendu.py) et l'assemblage
#   stl_impression/<pièce>.stl : pièces posées pour l'impression (plateau en z = 0)

import ast, math, os, struct, sys
import numpy as np
from manifold3d import CrossSection, JoinType, Manifold

HERE = os.path.dirname(os.path.abspath(__file__))
CAO = os.path.join(HERE, "..")
OUT = os.path.join(HERE, "stl")
OUT_PRINT = os.path.join(HERE, "stl_impression")
SEG = 128
from robot_style import TORSO_STYLE, LEG_VISIBLE, SHOE_TOP
TP_STYLE = TORSO_STYLE


def read_assign(path, name):
    tree = ast.parse(open(path, encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == name:
            return ast.literal_eval(node.value)
    raise RuntimeError("{} introuvable dans {}".format(name, path))


TP = read_assign(os.path.join(CAO, "MoodiMOS_Torse", "MoodiMOS_Torse.py"), "PARAMS")
TP = dict(TP, **TP_STYLE)
HP = {n: v for n, v, _, _ in read_assign(os.path.join(CAO, "MoodiMOS_Tete", "MoodiMOS_Tete.py"), "PARAMS")}

# ── style (n'agit pas sur les composants) ──
STYLE = {
    "plateExtra": 3,    # débord de la plaque sur le cerclage Ø 82 : sphère de la tête Ø 92, plus grosse que le torse (fiche)
    "domeT": 32,        # dôme arrière de la tête : profondeur derrière la plaque (disque arrière Ø ≈ 57)
    "headScrewWell": 11, "headScrewWellD": 5,   # puits de vis dans le dôme ; tête de vis sur la colonne (M2×30)
    "earD": 28, "earOut": 0.9, "earRound": 1.5, "earTilt": 22, "earRoot": 30,   # casques : disque plat, d'une pièce avec le dôme
    # (saillie earOut au-delà de la sphère, dans l'axe ; ≈ 6 au bord), congé, inclinaison
    "earRingW": 1.0, "earRingT": 0.4, "earRingInset": 0.5,   # jonc orange PEINT au bord de la face : légère rainure-guide
    "earHoleR": 0.4, "earHoleDepth": 0.6, "earHoleTaper": 15, "earHolePitch": 1.6,   # grille conique
    # lunette (cerclage noir) : face plate, liseré orange au bord ; index orange PEINTS (rendu seulement, pas de géométrie)
    "idxR": 40.2, "idxW": 1.4, "idxBig": 7.0, "idxSmall": 2.5,   # rayon, largeur radiale, longueurs (barrettes / marques)
    # liseré orange PEINT sur la bande extérieure de la face de la lunette (rimR0 → rimR1 = bord) ; une légère rainure
    # (rimGrooveW × rimGrooveT, centrée sur rimR0) sépare la face noire de la bande orange et guide le pinceau
    # lunette : seuls les lunVis premiers mm sont noirs et visibles ; derrière, la lunette passe en retrait de lunStep
    # et la plaque orange l'enveloppe d'une collerette (jeu lunFit) : la profondeur de 12 mm de l'empilement écran reste
    "lunVis": 6.0, "lunStep": 2.0, "lunFit": 0.1,
    "rimR0": 41.4, "rimR1": 43.0, "rimGrooveW": 0.8, "rimGrooveT": 0.4,
    # logement de la LiPo 403040 (V2), moulé dans le dôme : parois sur toute la hauteur de la batterie (+ gonflement),
    # deux ergots de retenue sur les grands côtés, au bord de l'ouverture : la batterie se clipse, sans colle, démontable
    "lipoWallH": 5.4, "lipoLipIn": 1.2, "lipoLipL": 16.0, "lipoLipT": 1.0,   # paroi 5,4 = LiPo 4 + gonflement 0,4 + ergot 1
    "tabThick": 6.0,    # épaisseur de la bélière (partie hors de la sphère), face avant alignée sur la plaque (lunVis)
    "tabInset": 0.6, "tabGrooveW": 0.8, "tabGrooveT": 0.4,   # face claire PEINTE de la bélière : bord orange, rainure-guide
    # médaillon gris (disque arrière de la fiche) : fait partie du dôme, peint ; une légère rainure en marque le bord
    "medD": 54, "medGrooveW": 0.8, "medGrooveT": 0.4,
    "ledW": 3, "ledZ0": -22, "ledZ1": 28, "ledU": 15, "ledT": 1.0,   # liseré lumineux des flancs du torse
}


def seg(r):
    return max(32, int(SEG * min(1.0, r / 20.0 + 0.25)))


# ── primitives dans le repère du torse (x, y = profondeur, z) ──

def box(x0, x1, y0, y1, z0, z1):
    if not (x1 > x0 and y1 > y0 and z1 > z0):
        raise ValueError("Boîte de dimensions nulles ou inversées : " + str((x0, x1, y0, y1, z0, z1)))
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl_y(r, x, z, y0, y1, r1=None):
    """Cylindre (ou cône si r1) d'axe y, de y0 (rayon r) à y1 (rayon r1)."""
    c = Manifold.cylinder(y1 - y0, r, r if r1 is None else r1, seg(max(r, r1 or 0)))   # axe z, de 0 à h
    return c.rotate((-90, 0, 0)).translate((x, y0, z))                                 # z → +y


def cyl_x(r, y, z, x0, x1):
    return Manifold.cylinder(x1 - x0, r, r, seg(r)).rotate((0, 90, 0)).translate((x0, y, z))


def cyl_z(r, x, y, z0, z1):
    return Manifold.cylinder(z1 - z0, r, r, seg(r)).translate((x, y, z0))


def section_goutte(r, sens):
    """Section en goutte (cercle de rayon r au centre, pointe à r·√2 sur l'axe b, du côté sens = ±1) : les cylindres
    couchés à l'impression (coque façade en bas, verticale d'impression = profondeur y) s'impriment sans support,
    pointe vers la façade pour un bossage. Les trous des tenons restent ronds : le tenon y pivote, tiré par son aimant,
    et c'est son frottement dans un trou rond qui tient le membre dans la pose choisie."""
    n = seg(r)
    a0 = math.atan2(sens, 0.0)                                   # direction de la pointe dans le plan (a, b)
    pts = [(r * math.cos(a0 + t), r * math.sin(a0 + t)) for t in np.linspace(math.pi / 4, 7 * math.pi / 4, n)]
    return CrossSection([[(0.0, sens * r * math.sqrt(2))] + pts])


def goutte_x(r, y, z, x0, x1, sens):
    """Cylindre d'axe x en goutte : section (y, z), pointe selon y."""
    m = Manifold.extrude(section_goutte(r, sens), x1 - x0)                # repère local (a, b, h), pointe selon b
    M = np.array([[0, 0, 1, x0], [0, 1, 0, y], [-1, 0, 0, z]], dtype=float)  # rotation : x = h, y = b, z = -a
    return m.transform(M)


def goutte_z(r, x, y, z0, z1, sens):
    """Cylindre d'axe z en goutte : section (x, y), pointe selon y."""
    return Manifold.extrude(section_goutte(r, sens), z1 - z0).translate((x, y, z0))


def rrect(w, h, r, cx=0.0, cz=0.0):
    """Rectangle arrondi dans le plan (x, z)."""
    r = max(min(r, w / 2 - 0.01, h / 2 - 0.01), 0.05)
    pts = []
    for (sx, sz, a0) in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        ccx, ccz = cx + sx * (w / 2 - r), cz + sz * (h / 2 - r)
        for a in np.linspace(math.radians(a0), math.radians(a0 + 90), 12):
            pts.append((ccx + r * math.cos(a), ccz + r * math.sin(a)))
    return CrossSection([pts])


def slab(cs, y0, y1):
    """Section (x, z) extrudée en profondeur entre y0 et y1."""
    return Manifold.extrude(cs, y1 - y0).rotate((90, 0, 0)).mirror((0, 1, 0)).translate((0, y0, 0))


def rounded_block(w, h, r, y0, y1, f_front=0.0, f_back=0.0, n=12):
    """Bloc à section rectangulaire arrondie, congés f_front / f_back sur le pourtour avant / arrière (enveloppe convexe)."""
    parts = []
    for f, ys in ((f_front, y0), (f_back, y1)):
        if f <= 0:
            parts.append(slab(rrect(w, h, r), y0 if ys == y0 else y1 - 0.01, y0 + 0.01 if ys == y0 else y1))
            continue
        for t in np.linspace(0, math.pi / 2, n):
            ins = f * (1 - math.cos(t))
            dy = f - f * math.sin(t)
            yy = ys + dy if ys == y0 else ys - dy
            cs = rrect(w - 2 * ins, h - 2 * ins, r - ins)
            parts.append(slab(cs, yy, yy + 0.01) if ys == y0 else slab(cs, yy - 0.01, yy))
    return Manifold.batch_hull(parts)


# ════════════════════════════════════════════════════════════════════════
# TORSE (cotes de la CAO principale ; ajout : liserés lumineux des flancs)
# ════════════════════════════════════════════════════════════════════════

p = TP
W, H, D = p["torsoW"], p["torsoH"], p["torsoD"]
wt = p["wallT"]
shellD = D - p["backT"]
zG = -H / 2 + p["glassBot"] + p["glassH"] / 2
zBand0, zCeil = zG + p["glassH"] / 2 + p["glassFit"], H / 2 - wt
zX = (zBand0 + zCeil) / 2
zCam = zX + p["camOffZ"]
uMid = D / 2
zSh = H / 2 - p["shoulderDown"]
HOLES = [(sx * p["holeX"], zG + sz * p["holeZ"]) for sx in (1, -1) for sz in (1, -1)]


def support_collier():
    """Impression seulement : support détachable sous le collier du cou. Le corps du torse s'imprime avant en bas (joint
    avec la façade sur le plateau) : le collier (Ø neckD, 6 de saillie) est couché et sa face avant porte à faux. Trois
    ailettes de 0,8 sur une semelle de 1, à 0,2 sous le cylindre, là où sa pente dépasse 60° (|x| ≤ 9) ; à 0,4 du dessus
    du torse."""
    r, g, xs = p["neckD"] / 2, 0.2, 9.0
    xs_ = np.linspace(-xs, xs, 37)
    prof = [(-xs, wt), (xs, wt)] + [(x, p["neckU"] - math.sqrt(r * r - x * x) - g) for x in xs_[::-1]]
    cs = CrossSection([prof])                                        # plan (x, y) : y = profondeur = verticale d'impression
    z0, z1 = H / 2 + 0.4, H / 2 + p["neckH"]
    fins = Manifold()
    for zf in np.linspace(z0, z1 - 0.8, 3):
        fins += Manifold.extrude(cs, 0.8).translate((0, 0, zf))
    return fins + box(-xs, xs, wt, wt + 1.0, z0, z1)                # semelle sur le plateau (plan du joint)


SUPPORTS_IMPRESSION = {"torse_coque": (support_collier,)}     # corps détachables, STL d'impression seuls


def supports_impression(name):
    return [f() for f in SUPPORTS_IMPRESSION.get(name, ())]


def vis_facade():
    """Positions des 4 vis M2 de la façade (anciennes vis décoratives) : à mi-chemin entre la poche de vitre et l'arrondi."""
    fit = p["glassFit"]
    dx = (p["glassW"] / 2 + fit + W / 2 - p["frontFillet"]) / 2
    zTop = (zG + p["glassH"] / 2 + fit + H / 2 - p["frontFillet"]) / 2
    zBot = zG - p["glassH"] / 2 + p["glassR"]
    return [(sx * dx, zTop) for sx in (1, -1)] + [(sx * dx, zBot) for sx in (1, -1)]


def torse_complet():
    outer = rounded_block(W, H, p["cornerR"], 0, shellD, f_front=p["frontFillet"])
    inner = slab(rrect(W - 2 * wt, H - 2 * wt, p["cornerR"] - wt), wt, shellD + 1)
    shell = outer - inner
    fit = p["glassFit"]
    cut = slab(rrect(p["glassW"] + 2 * fit, p["glassH"] + 2 * fit, p["glassR"] + fit, 0, zG), -1, p["glassT"])
    cut += slab(rrect(p["winW"], p["winH"], 2, 0, zG), -1, wt + 0.1)
    cut += cyl_y(p["camD"] / 2, 0, zCam, -1, wt + 0.1)
    # vis M2 de la façade : chambrage de la tête et passage dans la façade
    for x, z in vis_facade():
        cut += cyl_y(p["screwDecoD"] / 2, x, z, -1, p["screwDecoT"])
        cut += cyl_y(p["faceScrewD"] / 2, x, z, -1, wt + 0.01)
    # ouïes des flancs (BME280 à gauche)
    n = int(p["ventN"])
    for side in (1, -1):
        for k in range(n):
            z = p["ventZ"] + (k - (n - 1) / 2) * p["ventPitch"]
            x0, x1 = (W / 2 - wt - 1, W / 2 + 1) if side > 0 else (-W / 2 - 1, -W / 2 + wt + 1)
            cut += box(x0, x1, uMid - p["ventL"] / 2, uMid + p["ventL"] / 2, z - p["ventW"] / 2, z + p["ventW"] / 2)
    # liseré lumineux vertical sur chaque flanc (rainure ; jonc orange à part)
    for side in (1, -1):
        cut += liseré(side, grow=0.0)
    # USB-C (bas) et PWR (flanc droit)
    cut += box(-p["usbW"] / 2, p["usbW"] / 2, p["usbU"] - p["usbH"] / 2, p["usbU"] + p["usbH"] / 2, -H / 2 - 1, -H / 2 + wt + 1)
    cut += cyl_x(p["pwrD"] / 2, p["pwrU"], zG - p["glassH"] / 2 + p["pwrZ"], W / 2 - wt - 1, W / 2 + 1)
    shell = shell - cut

    # collier du cou : connecteur pogo magnétique monté par l'intérieur du torse : nez dans la fente de la peau du
    # collier (épaisseur = nez, face au ras), oreilles en appui dessous ; cavité oblongue ouverte vers l'intérieur
    zTop = H / 2 + p["neckH"]
    shell += cyl_z(p["neckD"] / 2, 0, p["neckU"], H / 2 - 1, zTop)
    zS = zTop - p["pogoFaceT"]
    for Lo, z0, z1 in ((p["pogoBodyL"], zS - 0.01, zTop + 1), (p["pogoL"], H / 2 - wt - 1, zS)):
        Lr = Lo - p["pogoW"]
        shell -= box(-Lr / 2, Lr / 2, p["neckU"] - p["pogoW"] / 2, p["neckU"] + p["pogoW"] / 2, z0, z1)
        for side in (1, -1):
            shell -= cyl_z(p["pogoW"] / 2, side * Lr / 2, p["neckU"], z0, z1)

    # épaules : cuvette, bossage intérieur, trou de tenon, logement d'aimant
    xFloor = W / 2 - p["socketT"]
    xMag = xFloor - p["shHoleDepth"] - p["skinT"] - p["limbMagT"]
    for side in (1, -1):
        sg = lambda a, b: (min(side * a, side * b), max(side * a, side * b))
        shell -= cyl_x(p["socketD"] / 2, uMid, zSh, *sg(xFloor, W / 2 + 1))
        shell += goutte_x(p["shoulderBossD"] / 2, uMid, zSh, *sg(xMag, W / 2 - wt + 0.5), -1)   # pointe vers la façade
        shell -= cyl_x(p["pegD"] / 2, uMid, zSh, *sg(xFloor - p["shHoleDepth"], xFloor + 0.1))   # rond : le tenon pivote
        shell -= cyl_x(p["limbMagD"] / 2, uMid, zSh, *sg(xMag - 0.1, xMag + p["limbMagT"]))

    # jambes : bossages, trous de tenon, aimants
    legStack = p["legHoleDepth"] + p["skinT"] + p["limbMagT"]
    for side in (1, -1):
        x = side * p["legX"]
        shell += goutte_z(p["legBossD"] / 2, x, uMid, -H / 2 + wt - 0.5, -H / 2 + legStack, -1)   # pointe vers la façade
        shell -= cyl_z(p["pegD"] / 2, x, uMid, -H / 2 - 1, -H / 2 + p["legHoleDepth"])          # rond : le tenon pivote
        shell -= cyl_z(p["limbMagD"] / 2, x, uMid, -H / 2 + legStack - p["limbMagT"], -H / 2 + legStack + 0.1)

    # intérieur : berceau caméra, cadre radar, compartiment BME280
    xw = p["camModW"] / 2 + p["camFit"]
    uC1 = wt + p["camModT"] + p["camFit"]
    for side in (1, -1):
        x0, x1 = sorted((side * xw, side * (xw + p["cradleT"])))
        shell += box(x0, x1, wt - 0.5, uC1, zBand0 + 0.2, zCeil + 0.5)
    xR0 = p["radX0"]
    xR1 = xR0 + p["radW"]
    zR0 = zX - p["radH"] / 2
    uR = wt + 2.0
    shell += box(xR0 - p["radFit"] - p["cradleT"], xR0 - p["radFit"], wt - 0.5, wt + 2.0, zBand0 + 0.2, zCeil + 0.5)
    shell += box(xR1 + p["radFit"], xR1 + p["radFit"] + p["railT"], wt - 0.5, uR, zBand0 + 0.2, zCeil + 0.5)
    shell += box(xR0 - p["radFit"] - p["cradleT"], xR1 + p["radFit"] + p["railT"], wt - 0.5, uR, zBand0 + 0.2, zR0 - p["radFit"])
    zv = (n - 1) / 2 * p["ventPitch"] + p["ventW"] / 2 + 0.5
    xi, bw = -(W / 2 - wt), p["bmeWallT"]
    uB0, uB1 = uMid - p["ventL"] / 2 - 1, uMid + p["ventL"] / 2 + 1
    shell += box(xi - 0.5, xi + p["bmeBoxX"] + bw, uB0 - bw, uB1, p["ventZ"] - zv - bw, p["ventZ"] + zv + bw)
    shell -= box(xi, xi + p["bmeBoxX"], uB0, uB1 + 1, p["ventZ"] - zv, p["ventZ"] + zv)
    # bossages des vis de façade, côté corps, reliés au flanc par une nervure ; avant-trou M2
    rb, lb = p["faceBossD"] / 2, p["faceBossL"]
    for x, z in vis_facade():
        sx = 1 if x > 0 else -1
        x0, x1 = sorted((x, sx * (W / 2 - wt + 0.5)))
        shell += cyl_y(rb, x, z, wt, wt + lb) + box(x0, x1, wt, wt + lb, z - 1.5, z + 1.5)
        shell -= cyl_y(p["facePilotD"] / 2, x, z, wt - 0.01, wt + lb - 1.0)
    return shell


_COMPLET = []


def _complet():
    if not _COMPLET:
        _COMPLET.append(torse_complet())
    return _COMPLET[0]


def torse_facade():
    """Façade du torse : paroi avant (0 à wt), avec la poche de vitre, l'épaulement, la fenêtre de l'écran, le trou de
    la caméra et les chambrages des 4 vis. S'imprime dos sur le plateau : l'épaulement se construit sans support."""
    return _complet() ^ box(-BIG, BIG, -BIG, wt, -BIG, BIG)


def torse_coque():
    """Corps du torse : tout ce qui est derrière la façade (joint plan à wt, sur l'arrondi du bord, même contour). S'imprime
    avant en bas, sans support Cura (bossages des membres en goutte, ailettes sous le collier)."""
    return _complet() ^ box(-BIG, BIG, wt, BIG, -BIG, BIG)


def torse_cadre():
    """Cadre noir mince collé sur la façade, autour de la vitre et de la caméra.
    Même ouverture que la vitre : aucune extension fictive de la zone d'écran."""
    frame = slab(rrect(p["glassW"] + 5, H - 8, 7), -0.85, 0.0)        # posé sur la façade (y = 0), sans s'y enfoncer
    frame -= slab(rrect(p["glassW"] + 0.4, p["glassH"] + 0.4, p["glassR"] + 0.2, 0, zG), -1, 1)
    frame -= cyl_y(p["camD"] / 2, 0, zCam, -1, 1)
    return frame


def peinture_vis():
    """Rendu seulement (jamais imprimé) : têtes noires des 4 vis M2 de la façade, au fond de leur chambrage."""
    m = None
    for x, z in vis_facade():
        c = cyl_y(p["screwDecoD"] / 2 - 0.05, x, z, p["screwDecoT"] - 0.1, p["screwDecoT"] - 0.02)
        m = c if m is None else m + c
    return m


def liseré(side, grow=0.0):
    """Rainure verticale du flanc (grow = 0) ; le jonc orange qui la remplit est la même forme."""
    x0, x1 = (W / 2 - STYLE["ledT"], W / 2 + 1) if side > 0 else (-W / 2 - 1, -W / 2 + STYLE["ledT"])
    w = STYLE["ledW"] / 2 + grow
    return box(x0, x1, STYLE["ledU"] - w, STYLE["ledU"] + w, STYLE["ledZ0"], STYLE["ledZ1"])


def torse_jonc(side):
    """Jonc lumineux du flanc (pièce orange translucide, collée dans la rainure) : affleurant."""
    x0, x1 = (W / 2 - STYLE["ledT"], W / 2) if side > 0 else (-W / 2, -W / 2 + STYLE["ledT"])
    w = STYLE["ledW"] / 2 - 0.1
    return box(x0, x1, STYLE["ledU"] - w, STYLE["ledU"] + w, STYLE["ledZ0"] + 0.1, STYLE["ledZ1"] - 0.1)


def torse_dos():
    back = rounded_block(W, H, p["cornerR"], shellD, D, f_back=p["backFillet"])
    lf, lw = p["lipFit"], p["lipW"]
    ri = p["cornerR"] - wt - lf
    lip = slab(rrect(W - 2 * (wt + lf), H - 2 * (wt + lf), ri), shellD - p["lipH"], shellD + 0.01) \
        - slab(rrect(W - 2 * (wt + lf + lw), H - 2 * (wt + lf + lw), max(ri - lw, 0.5)), shellD - p["lipH"] - 1, shellD + 1)
    back += lip
    for x, z in HOLES:
        back += cyl_y(p["bossD"] / 2, x, z, p["standoffU"], shellD + 0.01)
    for x, z in HOLES:
        back -= cyl_y(p["screwD"] / 2, x, z, p["standoffU"] - 1, D + 1)
        back -= cyl_y(p["screwHeadD"] / 2, x, z, D - p["screwHeadT"], D + 1)
    # BH1750
    rb = (p["domeBaseD"] + 0.4) / 2
    back += cyl_y(rb + 1.2, 0, p["domeZ"], shellD - p["domeRingH"], shellD + 0.01) - cyl_y(rb, 0, p["domeZ"], shellD - p["domeRingH"] - 1, shellD + 1)
    back -= cyl_y((p["domeD"] + 0.4) / 2, 0, p["domeZ"], shellD - 1, D + 1)
    # bague de la boîte HP
    spkIn = p["spkD"] + 0.4
    boxOD = spkIn + 2 * p["boxT"]
    r0 = (boxOD + 0.4) / 2
    back += cyl_y(r0 + 1.2, p["spkX"], p["spkZ"], shellD - 2, shellD + 0.01) - cyl_y(r0, p["spkX"], p["spkZ"], shellD - 3, shellD + 1)
    # grilles HP
    n = int(p["grilleN"])
    for k in range(n):
        z = p["spkZ"] + (k - (n - 1) / 2) * p["grillePitch"]
        back -= box(p["spkX"] - p["grilleW"] / 2, p["spkX"] + p["grilleW"] / 2, shellD - 1, D + 1, z - p["grilleH"] / 2, z + p["grilleH"] / 2)
    return back


def torse_boite_hp():
    spkIn = p["spkD"] + 0.4
    boxOD = spkIn + 2 * p["boxT"]
    depth = p["spkT"] + p["spkVol"]
    b = cyl_y(boxOD / 2, p["spkX"], p["spkZ"], shellD - (depth + p["boxT"]), shellD)
    b -= cyl_y(spkIn / 2, p["spkX"], p["spkZ"], shellD - depth, shellD + 1)
    b -= cyl_x(1.5, shellD - depth + 3, p["spkZ"], p["spkX"] + boxOD / 2 - p["boxT"] - 1, p["spkX"] + boxOD / 2 + 1)
    return b


# ════════════════════════════════════════════════════════════════════════
# TÊTE (repère de la tête : X à droite, Y en haut, Z = profondeur depuis la vitre)
# ════════════════════════════════════════════════════════════════════════

h = dict(HP, bezelD=86, plateExtra=STYLE["plateExtra"])     # cotes de la tête, débord de plaque propre à l'édition complète
R = h["bezelD"] / 2
Rp = R + h["plateExtra"]
frontT, plateT = h["frontT"], h["plateT"]
zP, zB = frontT, frontT + plateT
zR = zB + STYLE["domeT"]
flatY = -Rp + h["flatH"]
zMid = (zP + zB) / 2
BIG = 300


cF = math.sqrt(Rp * Rp - R * R)        # centre de la sphère AVANT (rayon Rp) : elle passe par l'arête avant de la lunette (R, 0)


def sphf(d):
    """Rayon de la sphère avant à la profondeur d (lunette puis plaque, tangente au cylindre de l'équateur en cF)."""
    return math.sqrt(max(Rp * Rp - (d - cF) ** 2, 0.0))


def sphf_arc(d0, d1, n=48):
    return [(sphf(d), d) for d in np.linspace(d0, d1, n)]


def sph(d, rad=Rp):
    """Rayon du dôme arrière à la profondeur d (sphère de rayon rad centrée sur l'arrière de la plaque)."""
    return math.sqrt(max(rad * rad - (d - zB) ** 2, 0.0))


def sph_arc(d0, d1, rad=Rp, n=64):
    return [(sph(d, rad), d) for d in np.linspace(d0, d1, n)]


def revolve(profile, n=SEG * 2):
    return Manifold.revolve(CrossSection([profile]), n)


def hcircle(r, cx=0.0, cy=0.0):
    return CrossSection.circle(r, seg(r)).translate((cx, cy))


def hprism(cs, z0, z1):
    return Manifold.extrude(cs, z1 - z0).translate((0, 0, z0))


def hbox(x0, x1, y0, y1, z0, z1):
    if not (x1 > x0 and y1 > y0 and z1 > z0):
        raise ValueError("Boîte de dimensions nulles ou inversées : " + str((x0, x1, y0, y1, z0, z1)))
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def rod_y(r, x, z, y0, y1):
    return Manifold.cylinder(y1 - y0, r, r, seg(r)).rotate((-90, 0, 0)).translate((x, y0, z))


def screw_positions():
    rot = math.radians(h["boardRot"])
    out = []
    for sx, hh in ((h["sUpX"], 20.36), (h["sLoX"], 61.06)):
        for side in (1, -1):
            x, y = side * sx, h["pcbTop"] - hh
            out.append((x * math.cos(rot) - y * math.sin(rot), x * math.sin(rot) + y * math.cos(rot)))
    return out


SCREWS = screw_positions()


def meplat():
    return hbox(-BIG, BIG, -BIG, flatY, -BIG, BIG)


def tete_cerclage():
    """Lunette noire (édition complète) : face plate, poche de vitre et épaulement d'origine, rainure du liseré orange du bord.
    Les index (barrettes à 12, 3, 6, 9 h, petites marques en diagonale) sont peints : aucune géométrie."""
    rg = h["glassD"] / 2 + h["glassFit"]
    rl = h["ledgeD"] / 2
    rs = R - STYLE["lunStep"]                                            # arrière en retrait, caché par la collerette de la plaque
    rb = h["boreD"] / 2                                              # lamage arrière : USB-C, lecteur microSD, nappe
    prof = [(rb, frontT), (rb, h["boreD0"]), (rl, h["boreD0"]), (rl, h["glassT"]), (rg, h["glassT"]), (rg, 0)] + \
           sphf_arc(0, STYLE["lunVis"]) + \
           [(rs, STYLE["lunVis"]), (rs, frontT)]                        # flanc sur la sphère avant : continuité avec la plaque
    gw = STYLE["rimGrooveW"] / 2
    groove = hprism(hcircle(STYLE["rimR0"] + gw) - hcircle(STYLE["rimR0"] - gw), -1, STYLE["rimGrooveT"])   # rainure-guide du liseré peint
    return revolve(prof) - groove - meplat()


def peinture_liseré_lunette():
    """Rendu seulement (jamais imprimé) : le liseré orange peint sur la bande extérieure de la face de la lunette."""
    return hprism(hcircle(STYLE["rimR1"] - 0.02) - hcircle(STYLE["rimR0"] + STYLE["rimGrooveW"] / 2), -0.03, 0.0) - meplat()


def peinture_index():
    """Rendu seulement (jamais imprimé) : les index peints de la lunette, film de 0,05 sur la face avant."""
    m = None
    for k in range(8):
        L = STYLE["idxBig"] if k % 2 == 0 else STYLE["idxSmall"]
        bar = hbox(-L / 2, L / 2, STYLE["idxR"] - STYLE["idxW"] / 2, STYLE["idxR"] + STYLE["idxW"] / 2, -0.05, 0.0).rotate((0, 0, 90 - 45 * k))
        m = bar if m is None else m + bar
    return m


def goutte():
    d, rt = h["tabD"], h["tabR"]
    a = math.asin((Rp - rt) / d)
    pts = [(Rp * math.cos(t), Rp * math.sin(t)) for t in np.linspace(math.pi - a, 2 * math.pi + a, 200)]
    pts += [(rt * math.cos(t), d + rt * math.sin(t)) for t in np.linspace(a, math.pi - a, 80)]
    return CrossSection([pts])


def front_envelope():
    """Volume avant de la tête : sphère avant (de lunVis à cF), cylindre de l'équateur jusqu'à l'arrière de la plaque."""
    return revolve([(0, STYLE["lunVis"] - 1)] + sphf_arc(STYLE["lunVis"] - 1, cF) + [(Rp, zB + 1), (0, zB + 1)])


def tab_front_zone():
    """La bélière et sa base : goutte (élargie de 1, hors des flancs) au-dessus des points de tangence, hors de la sphère avant."""
    d, rt = h["tabD"], h["tabR"]
    yt = Rp * (Rp - rt) / d                                            # hauteur des points de tangence
    above = CrossSection.square((400, 400)).translate((-200, yt))
    return (goutte().offset(1.0, JoinType.Round, 2.0, 64) - hcircle(sphf(STYLE["lunVis"]) - 0.3)) ^ above


def tab_zone():
    """Zone de découpe de la bélière : la goutte hors de la sphère (au-delà de Rp), élargie de 1 mm vers l'extérieur pour
    ne pas coïncider avec les flancs de la plaque (faces confondues → copeaux d'épaisseur nulle)."""
    return goutte().offset(1.0, JoinType.Round, 2.0, 64) - hcircle(Rp + 0.05)


def belier_zone():
    """Bélière hors du bord orange : la goutte en retrait de tabInset, hors du disque de la tête et autour du trou."""
    cs = goutte().offset(-STYLE["tabInset"], JoinType.Round, 2.0, 64)
    return cs - hcircle(Rp + STYLE["tabInset"]) - hcircle(h["holeD"] / 2 + STYLE["tabInset"], 0, h["tabD"])


def belier_peint():
    """Face claire peinte de la bélière : la zone moins la largeur de sa rainure-guide."""
    return belier_zone().offset(-STYLE["tabGrooveW"], JoinType.Round, 2.0, 64)


def tete_plaque():
    cs = goutte() - hcircle(h["holeD"] / 2, 0, h["tabD"]) - hcircle(h["plateOpenD"] / 2)
    for x, y in SCREWS:
        cs = cs - hcircle(h["screwD"] / 2, x, y)
    plate = hprism(cs, zP, zB)
    # la plaque entière (goutte et bélière) avance jusqu'à lunVis derrière la façade : elle enveloppe l'arrière en retrait de
    # la lunette ; la lunette noire sort directement de la face orange (façade noire visible = lunVis, sans bande orange)
    front = goutte() - hcircle(h["holeD"] / 2, 0, h["tabD"]) - hcircle(R - STYLE["lunStep"] + STYLE["lunFit"])
    plate += hprism(front, STYLE["lunVis"], zP + 0.01)
    # avant de la plaque sur la sphère avant (continuité lunette → plaque → dôme) ; la bélière garde sa face plate
    plate ^= front_envelope() + hprism(tab_front_zone(), STYLE["lunVis"] - 1, zB + 1)
    plate -= meplat()
    plate -= hprism(tab_zone(), STYLE["lunVis"] + STYLE["tabThick"], zB + 1)        # bélière amincie (derrière : rien)
    plate -= hprism(belier_zone() - belier_peint(), STYLE["lunVis"] - 1, STYLE["lunVis"] + STYLE["tabGrooveT"])   # rainure-guide
    # connecteur pogo monté par l'intérieur : nez dans la fente de la peau du méplat (épaisseur = nez, face au ras),
    # oreilles en appui derrière la peau ; canal oblong jusqu'à l'ouverture centrale pour l'y glisser, fils soudés
    L, Wd, t = h["pogoL"], h["pogoW"], h["pogoFaceT"]
    yC = -math.sqrt((h["plateOpenD"] / 2) ** 2 - (L / 2) ** 2) + 1.0       # haut du canal, dans l'ouverture centrale
    for Lo, y0, y1 in ((h["pogoBodyL"], flatY - 1, flatY + t + 0.01), (L, flatY + t, yC)):
        plate -= hbox(-(Lo - Wd) / 2, (Lo - Wd) / 2, y0, y1, zMid - Wd / 2, zMid + Wd / 2)
        for side in (1, -1):
            plate -= rod_y(Wd / 2, side * (Lo - Wd) / 2, zMid, y0, y1)
    return plate


def peinture_belier():
    """Rendu seulement (jamais imprimé) : la face claire peinte de la bélière, dans sa rainure-guide."""
    return hprism(belier_peint(), STYLE["lunVis"] - 0.03, STYLE["lunVis"])


def tete_dos():
    """Dôme : sphère Rp centrée sur l'arrière de la plaque, tronquée à zR ; coque de wallT ; colonnes de vis pleines
    jusqu'au fond des puits (la tête de vis M2×35 s'y pose), puis tubes jusqu'au fond (accrochées à la paroi et au
    fond) ; berceau de LiPo sur le fond plat."""
    w = h["wallT"]
    outer = [(0, zB)] + sph_arc(zB, zR) + [(0, zR)]
    inner = [(0, zB - 1)] + sph_arc(zB - 1, zR - w, Rp - w) + [(0, zR - w)]
    inner_full = [(0, zB - 1)] + sph_arc(zB - 1, zR - w + 0.5, Rp - w + 0.5) + [(0, zR - w + 0.5)]   # colonnes : dans la coque
    back = revolve(outer) - revolve(inner)
    zTopCol = zR - STYLE["headScrewWell"]
    for x, y in SCREWS:
        back += hprism(hcircle(h["bossD"] / 2, x, y), zB, zR - w + 0.01) ^ revolve(inner_full)
    for x, y in SCREWS:
        back -= hprism(hcircle(h["screwD"] / 2, x, y), zB - 1, zR + 1)
        back -= hprism(hcircle(STYLE["headScrewWellD"] / 2, x, y), zTopCol, zR + 1)
    rm, gw = STYLE["medD"] / 2, STYLE["medGrooveW"] / 2                      # rainure-guide du médaillon peint (bord Ø medD)
    back -= hprism(hcircle(rm + gw) - hcircle(rm - gw), zR - STYLE["medGrooveT"], zR + 1)
    wi, li = h["lipoW"] / 2 + h["lipoFit"], h["lipoL"] / 2 + h["lipoFit"]
    wo, lo = wi + h["ribW"], li + h["ribW"]
    z0 = zR - w - STYLE["lipoWallH"]                                       # ouverture du logement (côté carte)
    frame = hprism(CrossSection.square((2 * wo, 2 * lo), True) - CrossSection.square((2 * wi, 2 * li), True), z0, zR - w + 0.01)
    for sx in (1, -1):                                                 # ergots de retenue, au bord de l'ouverture
        xa, xb = sorted((sx * (wi - STYLE["lipoLipIn"]), sx * (wi + 0.5)))
        frame += hbox(xa, xb, -STYLE["lipoLipL"] / 2, STYLE["lipoLipL"] / 2, z0, z0 + STYLE["lipoLipT"])
    g = h["lipoWireGap"] / 2
    frame -= hbox(-g, g, -lo - 0.5, -li + 0.5, z0 - 1, zR)
    return (back + frame + casques_dome()) - meplat()


def peinture_medaillon():
    """Rendu seulement (jamais imprimé) : le médaillon gris peint sur le disque arrière du dôme, dans sa rainure."""
    return hprism(hcircle(STYLE["medD"] / 2 - STYLE["medGrooveW"] / 2), zR, zR + 0.03)


def casque_local():
    """Casque d'axe +X depuis l'origine (centre du dôme), avant inclinaison : coque, rainure du jonc, grille conique.
    Renvoie (coque, jonc)."""
    a0, ao, d, rf = STYLE["earRoot"], Rp + STYLE["earOut"], STYLE["earD"], STYLE["earRound"]
    rad = d / 2
    prof = [(0, 0), (rad, 0)] + [(rad - rf + rf * math.cos(t), (ao - a0) - rf + rf * math.sin(t))
                                  for t in np.linspace(0, math.pi / 2, 16)] + [(0, ao - a0)]
    pod = revolve(prof).rotate((0, 90, 0)).translate((a0, 0, 0))
    ro = rad - rf - STYLE["earRingInset"]
    ri = ro - STYLE["earRingW"]
    ring = Manifold.cylinder(STYLE["earRingT"] + 1, ro, ro, seg(ro)) - Manifold.cylinder(STYLE["earRingT"] + 1, ri, ri, seg(ri))
    cut = ring.translate((0, 0, -STYLE["earRingT"]))
    k = math.tan(math.radians(STYLE["earHoleTaper"]))
    pitch, rmax = STYLE["earHolePitch"], ri - 0.8
    pts, r = [(0.0, 0.0)], pitch
    while r <= rmax + 0.01:
        n = max(6, int(round(2 * math.pi * r / pitch)))
        pts += [(r * math.cos(2 * math.pi * j / n), r * math.sin(2 * math.pi * j / n)) for j in range(n)]
        r += pitch
    hr, hd = STYLE["earHoleR"], STYLE["earHoleDepth"]
    for u, v in pts:
        cut += Manifold.cylinder(hd + 0.4, max(hr - hd * k, 0.02), hr + 0.4 * k, 12).translate((u, v, -hd))
    to_face = lambda m: m.rotate((0, 90, 0)).translate((ao, 0, 0))     # repère local (z vers l'extérieur) → face du casque
    pod = pod - to_face(cut)
    # jonc peint (rendu seulement) : film orange au fond de la rainure, qui fait partie du casque
    jonc = to_face((Manifold.cylinder(0.03, ro - 0.02, ro - 0.02, seg(ro))
                    - Manifold.cylinder(0.03, ri + 0.02, ri + 0.02, seg(ri))).translate((0, 0, -STYLE["earRingT"])))
    return pod, jonc


def casque_tilt(m):
    return m.rotate((0, -STYLE["earTilt"], 0)).translate((0, 0, zB))   # +X → vers l'arrière, centre du dôme


def casques_dome():
    """Casques d'une pièce avec le dôme : la partie qui sort de la sphère et son ancrage dans la paroi, sans rien dans la
    cavité ni devant le dôme (côté plaque)."""
    pod, _ = casque_local()
    w = h["wallT"]
    cav = revolve([(0, zB - 1)] + sph_arc(zB - 1, zR - w, Rp - w) + [(0, zR - w)])
    front = hprism(CrossSection.square((4 * Rp, 4 * Rp), True), zB - 20, zB)
    m = casque_tilt(pod) - cav - front
    return m + m.mirror((1, 0, 0))


def casques_rendu():
    """Casques peints en noir au rendu : centre du dôme, axe et rayon d'emprise de chaque casque, dans le repère du torse."""
    def to_torso(p):
        b = head_to_torso(Manifold.cube((0.02, 0.02, 0.02), True).translate(p)).bounding_box()
        return [round((b[i] + b[i + 3]) / 2, 4) for i in range(3)]
    out = []
    for side in (1, -1):
        c = to_torso((0, 0, zB))
        a = math.radians(STYLE["earTilt"])
        f = to_torso((side * Rp * math.cos(a), 0, zB + Rp * math.sin(a)))
        n = math.dist(c, f)
        out.append({"centre": c, "axe": [(f[i] - c[i]) / n for i in range(3)], "rayon": STYLE["earD"] / 2 + 0.3, "rmin": Rp + 0.02})
    return out


def joncs_casques():
    """Joncs peints des casques (rendu seulement) : [(nom, manifold)] dans le repère de la tête."""
    _, jonc = casque_local()
    joncR = casque_tilt(jonc)
    return [("peinture_jonc_casque_droit", joncR), ("peinture_jonc_casque_gauche", joncR.mirror((1, 0, 0)))]


# repère de la tête → repère du torse : méplat sur le collier, pogo au-dessus du pogo
HEAD_DZ = TP["torsoH"] / 2 + TP["neckH"] - flatY
HEAD_DY = TP["neckU"] - zMid


def head_to_torso(m):
    return m.rotate((90, 0, 0)).mirror((0, 1, 0)).translate((0, HEAD_DY, HEAD_DZ))   # (X, Y, Z) → (X, Z, Y) + décalage


# ════════════════════════════════════════════════════════════════════════
# Rendu seulement : vitres (non imprimées)
# ════════════════════════════════════════════════════════════════════════

def compo_bh1750():
    """Boule du BH1750 (composant) : dôme Ø domeD qui sort du dos du torse."""
    r = p["domeD"] / 2
    return cyl_y(r, 0, p["domeZ"], shellD, D) + Manifold.sphere(r, seg(r) * 2).translate((0, D, p["domeZ"])) \
        - box(-BIG, BIG, -BIG, shellD, -BIG, BIG)


def vitre_tete():
    return revolve([(0, 0), (h["glassD"] / 2 - 0.05, 0), (h["glassD"] / 2 - 0.05, h["glassT"] - 0.05), (0, h["glassT"] - 0.05)])


def vitre_torse():
    return slab(rrect(p["glassW"] - 0.1, p["glassH"] - 0.1, p["glassR"], 0, zG), 0.05, p["glassT"])


# ════════════════════════════════════════════════════════════════════════
# écriture
# ════════════════════════════════════════════════════════════════════════

def write_stl(path, m):
    m = m.simplify(0.001)                       # supprime les triangles d'aire nulle laissés par les booléens (1 µm)
    if m.status().name != "NoError" or m.is_empty():
        raise ValueError("Export refusé : pièce vide ou invalide pour " + path)
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"], rec["v"] = nrm, tri
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS".ljust(80, b" "))
        fh.write(struct.pack("<I", len(t)))
        fh.write(rec.tobytes())


def on_plate(m):
    b = m.bounding_box()
    return m.translate((-(b[0] + b[3]) / 2, -(b[1] + b[4]) / 2, -b[2]))


PIECES_ATTENDUES = {"peinture_index": 8, "peinture_vis": 4}   # rendu seulement : les 8 index peints de la lunette


def report(name, m, extra=0):
    b = m.bounding_box()
    n = PIECES_ATTENDUES.get(name, 1) + extra
    ok = m.status().name == "NoError" and len(m.decompose()) == n
    print("  {:<22} {:6.1f} × {:6.1f} × {:6.1f} mm {:7.1f} cm³  {}".format(
        name, b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000,
        "étanche, {} pièce{}".format(n, "s" if n > 1 else "") if ok else "À REVOIR"))


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_PRINT, exist_ok=True)
    placed = {  # repère du torse
        "torse_cadre": torse_cadre(), "torse_coque": torse_coque(), "torse_facade": torse_facade(), "torse_dos": torse_dos(),
        "torse_boite_hp": torse_boite_hp(),
        "torse_jonc_droit": torse_jonc(1), "torse_jonc_gauche": torse_jonc(-1), "peinture_vis": peinture_vis(),
        "tete_cerclage": head_to_torso(tete_cerclage()), "tete_plaque": head_to_torso(tete_plaque()),
        "tete_dos": head_to_torso(tete_dos()),
        "peinture_medaillon": head_to_torso(peinture_medaillon()), "peinture_belier": head_to_torso(peinture_belier()),
        "peinture_liseré_lunette": head_to_torso(peinture_liseré_lunette()), "peinture_index": head_to_torso(peinture_index()),
        "vitre_tete": head_to_torso(vitre_tete()), "vitre_torse": vitre_torse(), "compo_bh1750": compo_bh1750(),
    }
    for name, m in joncs_casques():
        placed[name] = head_to_torso(m)
    org = os.path.join(CAO, "MoodiMOS_Organique", "organique.py")
    ground = -H / 2 - LEG_VISIBLE - SHOE_TOP
    top = HEAD_DZ + h["tabD"] + h["tabR"]
    cotes = {"head_glass_y": HEAD_DY, "head_center_z": HEAD_DZ, "torso_glass_z": zG, "ground_z": ground,
             "top_z": top, "mid_z": (ground + top) / 2,
             "torso_w": W, "torso_h": H, "torso_d": D,
             "head_bezel_d": h["bezelD"], "head_glass_d": h["glassD"],
             "torso_glass_w": p["glassW"], "torso_glass_h": p["glassH"],
             "torso_glass_r": p["glassR"], "casques": casques_rendu()}
    import json
    json.dump(cotes, open(os.path.join(OUT, "cotes.json"), "w"), indent=1)
    # référence pour le script Fusion (MoodiMOS_Robot) : cotes, profils déjà calculés, volumes attendus
    polys = lambda cs: [[list(map(float, pt)) for pt in poly] for poly in cs.to_polygons()]
    ref = {"tp": TP, "h": h, "STYLE": STYLE,
           "tete": {"R": R, "Rp": Rp, "zP": zP, "zB": zB, "zR": zR, "flatY": flatY, "zMid": zMid,
                    "head_dy": HEAD_DY, "head_dz": HEAD_DZ, "screws": SCREWS},
           "belier_zone": polys(belier_zone()), "tab_zone": polys(tab_zone()),
           "tab_front_zone": polys(tab_front_zone()), "cF": cF,
           "belier_peint": polys(belier_peint()),
           "volumes_mm3": {k: float(m.volume()) for k, m in placed.items()}}
    json.dump(ref, open(os.path.join(HERE, "fusion_reference.json"), "w"), indent=1, ensure_ascii=False)
    print("Hauteur totale : {:.1f} mm (sol {:.1f}, haut de la bélière {:.1f})".format(top - ground, ground, top))
    print("Pièces en place ({}) :".format(os.path.relpath(OUT)))
    for name, m in placed.items():
        write_stl(os.path.join(OUT, name + ".stl"), m)
        report(name, m)

    # impression : coque façade en bas, dos face extérieure en bas, boîte HP fond en bas,
    # cerclage face arrière en bas, plaque à plat, dôme (avec ses casques) face arrière en bas
    up_y = lambda m: m.rotate((90, 0, 0))       # (x, y, z) → (x, -z, y) : y = 0 en bas
    down_y = lambda m: m.rotate((-90, 0, 0))    # (x, y, z) → (x, z, -y) : y max en bas
    printed = {
        "torse_cadre": on_plate(up_y(placed["torse_cadre"])),
        "torse_coque": on_plate(up_y(sum(supports_impression("torse_coque"), placed["torse_coque"]))),   # corps, joint en bas
        "torse_facade": on_plate(down_y(placed["torse_facade"])),     # dos de la façade sur le plateau
        "torse_dos": on_plate(down_y(placed["torse_dos"])),
        "torse_boite_hp": on_plate(up_y(placed["torse_boite_hp"])),
        "tete_cerclage": on_plate(tete_cerclage().rotate((0, 180, 0))),
        "tete_plaque": on_plate(tete_plaque().rotate((0, 180, 0))),   # face arrière sur le plateau, collerette en haut
        "tete_dos": on_plate(tete_dos().rotate((0, 180, 0))),          # médaillon sur le plateau : 3 250 mm² d'appui (650 ouverture
                                                                       # en bas) et aucun support sous le fond plat, dans le dôme
    }
    supports = {k: {"volume_cm3": sum(m.volume() for m in supports_impression(k)) / 1000,
                    "corps": sum(len(m.decompose()) for m in supports_impression(k))} for k in SUPPORTS_IMPRESSION}
    json.dump(supports, open(os.path.join(OUT, "supports_impression.json"), "w"), indent=1)   # lu par les contrôles
    print("Pièces d'impression ({}) :".format(os.path.relpath(OUT_PRINT)))
    for name, m in printed.items():
        write_stl(os.path.join(OUT_PRINT, name + ".stl"), m)
        report(name, m, supports.get(name, {}).get("corps", 0))


if __name__ == "__main__":
    main()
