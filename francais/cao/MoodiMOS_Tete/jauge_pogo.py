# MoodiMOS — jauge du connecteur pogo magnétique (5 broches, pas 2,5 mm, Sylen B0H22943X6)
#
# Sans pied à coulisse : même montage que la plaque de tête et le collier du torse, connecteur entré PAR L'INTÉRIEUR.
# Une peau de 2 mm (hauteur du nez côté aimants) percée de trois fentes oblongues pour le nez ; derrière chaque fente,
# une cavité à la longueur des oreilles, qui s'appuient sous la peau. Le nez doit passer sans forcer et sans jeu, sa face
# au ras de la peau.
#   fentes : longueur pogoBodyL (21,4), larges de 4,5 / 4,6 / 4,7 mm (1 à 3 points gravés côté cavités)
#   cavités : longueur pogoL (26,4), même largeur, 2 mm de haut
# Cotes relevées à la règle le 13/10/2026 : 25 de long avec les oreilles, 20 sans, 4 de large, 4 de haut (2 côté
# aimants, 1 d'oreilles, 1 côté soudures). Jauge du 13/10 (3,8 à 4,4 ; 20,6 / 25,6) : 4,4 presque bon mais un peu serré,
# nez et oreilles trop longs pour 20,6 et 25,6. (La toute première jauge, 22,4 de long, ne pouvait pas recevoir le connecteur.)
#
# Impression : peau sur le plateau (tel qu'exporté), cavités ouvertes vers le haut, aucun support. Essai : poser le
# connecteur dans une cavité, nez vers le bas dans la fente, puis retourner la jauge pour voir la face.
#
# Lancer : cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Tete/jauge_pogo.py
# Écrit cao/MoodiMOS_Organique/stl_impression/jauge_pogo.stl.

import os, sys
from manifold3d import CrossSection, Manifold

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tete_stl import OUT, P, write_stl

L, LB, T = P["pogoL"], P["pogoBodyL"], P["pogoFaceT"]    # oreilles, nez, peau : cotes de la tête
CAV = 2.0                                                 # hauteur des cavités (oreilles 1 + corps 1)
LARGEURS = (4.5, 4.6, 4.7)
PAS = 7.5                                                 # entraxe des fentes
BX, BZ = L + 8, T + CAV
BY = PAS * len(LARGEURS) + 2


def oblong(w, l):
    r = w / 2
    return CrossSection.circle(r, 48).translate((-(l / 2 - r), 0)) + CrossSection.circle(r, 48).translate((l / 2 - r, 0)) \
        + CrossSection.square((l - w, w), True)


def jauge():
    m = Manifold.cube((BX, BY, BZ), True).translate((0, 0, BZ / 2))
    for k, w in enumerate(LARGEURS):
        y = (k - (len(LARGEURS) - 1) / 2) * PAS
        m -= Manifold.extrude(oblong(w, LB), T + 0.02).translate((0, y, -0.01))          # fente du nez dans la peau
        m -= Manifold.extrude(oblong(w, L), CAV + 0.01).translate((0, y, T))             # cavité des oreilles
        for j in range(k + 1):                                  # repère : 1 à 3 points gravés sur le dessus
            m -= Manifold.cylinder(0.8, 0.5, 0.5, 24).translate((L / 2 + 2.0, y - 1.5 * k / 2 + 1.5 * j, BZ - 0.6))
    return m


if __name__ == "__main__":
    m = jauge()
    path = os.path.join(OUT, "jauge_pogo.stl")
    write_stl(path, m)
    b = m.bounding_box()
    print("{} : {:.1f} × {:.1f} × {:.1f} mm, {:.1f} cm³, {}".format(
        os.path.relpath(path), b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000, m.status().name))
