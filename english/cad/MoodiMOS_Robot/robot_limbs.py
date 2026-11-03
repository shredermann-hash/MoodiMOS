# MoodiMOS Complete edition — arms, gloves, legs, and sneakers
# 
# `robot_organic.py` (copy from `../MoodiMOS_Organic/organic.py`: thick fists, bulky wristbands, heavy sneakers; same magnetic tenons and embedding)
# 
# Includes-style sheet notes:
# - Black articulation bands (elbow, wrist, hip, ankle): a 0.3 mm-deep groove in the ivory piece (painting guide) + a separate black ring to fill it, for aesthetic effect or multicolor printing.
# - Colors (wristband/orange, sneaker toe, black trim) applied via `robot_render.py`.
# 
# Run from project root (~1 min):
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_limbs.py
# ```
# Outputs:
# - `stl/` (assembled pieces, torso reference)
# - `print_stl/` (gloves + wristbands, sneakers, arms, shoulders, legs for printing).

import os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.join(HERE, "..", "MoodiMOS_Organic"))
import robot_organic as o          # # Noqa: E402
import orient_for_print as oi  # # Noqa: E402

o.OUT = os.path.join(HERE, "stl")
o.OUT_PRINT = os.path.join(HERE, "print_stl")
oi.SRC, oi.OUT = o.OUT, o.OUT_PRINT

BAND_DEPTH, BAND_RAMP = 0.3, 0.3        # groove of the band (paint guide) and ramp of its edges


def bands_for(name):
    'Black bands on the part: [(center (x, z, u), direction, half-width)].'
    if name == "arm":
        P = o.ARM_PTS
        return [(P[3], P[4] - P[2], 3.0),
                (P[-1] - 11 * (P[-1] - P[-2]) / np.linalg.norm(P[-1] - P[-2]), P[-1] - P[-2], 3.2)]                                 # elbow
    if name == "leg":
        lx, zb, um = o.T["legX"], o.Z_BOT, o.U_MID
        return [((lx, zb - 3.0, um), (0, 1, 0), 2.2),                     # hip, beneath the torso
                ((o.SHOE_X, zb - 34, um), (0, 1, 0), 2.0)]                      # peg
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
    _emit(name, sdf + recess, axes, step, folder)                        # ivory piece, grooved strips
    ring = np.maximum(np.maximum(sdf - 0.05, -(sdf + BAND_DEPTH + 0.6)), in_band)   # shell filling the hollow cavity
    _emit("ring_" + name, ring, axes, step, folder)


o.emit = emit_robot

if __name__ == "__main__":
    os.makedirs(o.OUT, exist_ok=True)
    os.makedirs(o.OUT_PRINT, exist_ok=True)
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--partie", choices=("tout", "arm", "chaussures"), default="tout")
    args = parser.parse_args()
    if args.partie in ("tout", "arm"):
        print('Arm (complete edition)')
        o.build_arm()
    if args.partie in ("tout", "chaussures"):
        print('Leg and foot (complete edition)')
        o.build_leg_and_shoe()
    oi.main()
