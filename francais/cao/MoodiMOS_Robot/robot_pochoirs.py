# MoodiMOS — pochoirs de peinture de la lunette (index et liseré orange)
#
# La lunette noire porte 8 index orange (barrettes de 7 mm à 12, 3, 6 et 9 h, marques de 2,5 mm en diagonale, à r 40,2)
# et un liseré orange sur la bande du bord (rimR0 → R), après une rainure-guide. Deux pièces à poser sur la lunette seule
# (avant la pose de la vitre), puis un seul passage de bombe orange :
#   pochoir_index_lunette.stl : disque plein de 0,8 posé sur la face. Une lèvre se centre dans la poche de vitre. Il masque
#       la face noire, la rainure et l'intérieur. Restent ouverts les 8 index et la bande du bord.
#   cache_flanc_lunette.stl   : bague qui épouse le flanc arrondi (sphère avant, sur lunVis) et le méplat du bas.
#       L'orange ne coule pas sur le côté noir. Elle s'enfile par l'avant et se cale d'elle-même sur le flanc qui s'évase.
#       Son rebord dépasse de 0,5 devant la face : il couvre le jeu au bord de la bande. Deux encoches sur ce rebord
#       (12 h et 6 h, au milieu du méplat) : y aligner deux grandes barrettes du pochoir.
#
# Lancer : cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_pochoirs.py
# Écrit les deux STL dans stl_impression/, déjà orientés pour l'impression, sans support : pochoir face peinte sur le
# plateau (lèvre en haut), cache côté large en bas.

import os, sys
import numpy as np
from manifold3d import CrossSection, Manifold

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import robot_pieces as robot                                   # cotes de la lunette (STYLE, h, R, flatY, sphf, write_stl)

T_PLATE = 0.8                            # épaisseur du pochoir de face
LIP_H, LIP_W, LIP_FIT = 0.8, 1.0, 0.15   # lèvre de centrage dans la poche de vitre (profonde de glassT)
FLANK_FIT, FLANK_W = 0.2, 1.2            # jeu et épaisseur du cache de flanc
RIM = 0.5                                # rebord du cache devant la face de la lunette
NOTCH_W, NOTCH_D = 1.0, 1.5              # encoches d'alignement sur le rebord du cache
SEG = 256


def pochoir_index():
    """Repère de la lunette : face posée à z = T_PLATE (dessus à l'impression), lèvre au-dessus, face peinte à z = 0."""
    STYLE, h = robot.STYLE, robot.h
    r_pocket = h["glassD"] / 2 + h["glassFit"]                       # bord de la poche de vitre
    r_out = STYLE["rimR0"] + STYLE["rimGrooveW"] / 2                          # masque jusqu'au bord extérieur de la rainure
    m = Manifold.extrude(CrossSection.circle(r_out, SEG), T_PLATE)
    lip_out = r_pocket - LIP_FIT
    lip = Manifold.extrude(CrossSection.circle(lip_out, SEG) - CrossSection.circle(lip_out - LIP_W, SEG), LIP_H)
    m += lip.translate((0, 0, T_PLATE))
    for k in range(8):                                               # mêmes barrettes que robot_pieces.peinture_index
        L = STYLE["idxBig"] if k % 2 == 0 else STYLE["idxSmall"]
        bar = Manifold.cube((L, STYLE["idxW"], T_PLATE + 2), True).translate((0, STYLE["idxR"], T_PLATE / 2))
        m -= bar.rotate((0, 0, 90 - 45 * k))
    return m


def solide_flanc(extra, d0, d1):
    """Volume de révolution sous la sphère avant élargie de extra, de la profondeur d0 à d1 (z = profondeur)."""
    prof = [(0, d0)] + [(robot.sphf(d) + extra, d) for d in np.linspace(d0, d1, 40)] + [(0, d1)]
    return Manifold.revolve(CrossSection([prof]), SEG)


def cote_haut(y):
    """Demi-espace y >= y (méplat du bas de la tête)."""
    return Manifold.cube((400, 400, 400), True).translate((0, 200 + y, 0))


def cache_flanc():
    """Repère de la lunette (z = profondeur depuis la face), puis retourné : côté large (arrière) sur le plateau."""
    lv = robot.STYLE["lunVis"]
    outer = solide_flanc(FLANK_FIT + FLANK_W, -RIM, lv) ^ cote_haut(robot.flatY - FLANK_FIT - FLANK_W)
    inner = solide_flanc(FLANK_FIT, -RIM - 1, lv + 1) ^ cote_haut(robot.flatY - FLANK_FIT)
    m = outer - inner
    for y in (1, -1):                                                # encoches à 12 h et 6 h (milieu du méplat)
        m -= Manifold.cube((NOTCH_W, 20, 2 * NOTCH_D), True).translate((0, y * (robot.R + 5), -RIM))
    return m.mirror((0, 0, 1)).translate((0, 0, lv))


def main():
    out = os.path.join(HERE, "stl_impression")
    for name, m in (("pochoir_index_lunette", pochoir_index()), ("cache_flanc_lunette", cache_flanc())):
        robot.write_stl(os.path.join(out, name + ".stl"), m)
        b = m.bounding_box()
        print("{} : {:.1f} × {:.1f} × {:.1f} mm, {:.1f} cm³, {} corps".format(
            name, b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000, len(m.decompose())))


if __name__ == "__main__":
    main()
