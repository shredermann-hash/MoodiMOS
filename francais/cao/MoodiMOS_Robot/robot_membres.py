# MoodiMOS — bras, gants, jambes et baskets
#
# Générateur robot_organique.py (copie de ../MoodiMOS_Organique/organique.py : gros poings, bracelets épais,
# baskets massives ; mêmes tenons aimantés et mêmes emboîtements), plus le style de la fiche :
#   bandes d'articulation noires (coude, poignet, hanche, cheville) : bande creusée de 0,3 mm dans la pièce ivoire
#   (guide de peinture) et bague noire séparée qui la remplit, pour le rendu ou une impression multicolore.
# Les couleurs (bracelet et bout de basket orange, liseré noir…) sont posées au rendu (robot_rendu.py).
#
# Lancer depuis la racine du projet (≈ 1 min) :
#   cao/MoodiMOS_Organique/.venv/bin/python cao/MoodiMOS_Robot/robot_membres.py
# Sorties : stl/ (pièces en place, repère du torse) et stl_impression/ (gants + bracelets, baskets,
# bras, épaules et jambes posés pour l'impression).

import os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.join(HERE, "..", "MoodiMOS_Organique"))
import robot_organique as o          # noqa: E402
import orienter_impression as oi  # noqa: E402

o.OUT = os.path.join(HERE, "stl")
o.OUT_PRINT = os.path.join(HERE, "stl_impression")
oi.SRC, oi.OUT = o.OUT, o.OUT_PRINT

BAND_DEPTH, BAND_RAMP = 0.3, 0.3        # creux de la bande (guide de peinture) et rampe de ses bords


def bands_for(name):
    """Bandes noires de la pièce : [(centre (x, z, u), direction, demi-largeur)]."""
    if name == "bras":
        P = o.ARM_PTS
        return [(P[3], P[4] - P[2], 3.0),
                (P[-1] - 11 * (P[-1] - P[-2]) / np.linalg.norm(P[-1] - P[-2]), P[-1] - P[-2], 3.2)]                                 # coude
    if name == "jambe":
        lx, zb, um = o.T["legX"], o.Z_BOT, o.U_MID
        return [((lx, zb - 3.0, um), (0, 1, 0), 2.2),                     # hanche, sous le torse
                ((o.SHOE_X, zb - 34, um), (0, 1, 0), 2.0)]                      # cheville
    return []


_emit = o.emit


def emit_robot(name, sdf, axes, step, folder=None):
    folder = o.OUT if folder is None else folder
    bands = bands_for(name) if folder == o.OUT else []
    if not bands:
        return _emit(name, sdf, axes, step, folder)
    X, Z, U = np.meshgrid(*axes, indexing="ij")
    sdf = np.asarray(sdf, np.float32)
    recess = np.zeros_like(sdf)
    in_band = None
    for c, t, half in bands:
        t = np.asarray(t, float) / np.linalg.norm(t)
        a = np.abs((X - c[0]) * t[0] + (Z - c[1]) * t[1] + (U - c[2]) * t[2])
        recess = np.maximum(recess, BAND_DEPTH * np.clip((half - a) / BAND_RAMP, 0, 1))
        slab = a - half
        in_band = slab if in_band is None else np.minimum(in_band, slab)
    _emit(name, sdf + recess, axes, step, folder)                        # pièce ivoire, bandes creusées
    ring = np.maximum(np.maximum(sdf - 0.05, -(sdf + BAND_DEPTH + 0.6)), in_band)   # coquille qui comble le creux
    _emit("bague_" + name, ring, axes, step, folder)


o.emit = emit_robot

if __name__ == "__main__":
    os.makedirs(o.OUT, exist_ok=True)
    os.makedirs(o.OUT_PRINT, exist_ok=True)
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--partie", choices=("tout", "bras", "chaussures"), default="tout")
    args = parser.parse_args()
    if args.partie in ("tout", "bras"):
        print("Bras (édition complète) :")
        o.build_arm()
    if args.partie in ("tout", "chaussures"):
        print("Jambe et basket (édition complète) :")
        o.build_leg_and_shoe()
    oi.main()
