# MoodiMOS Complete edition — stencils for bezel paint (index and orange border)
# 
# The black bezel has 8 orange indices (bars of 7 mm to 12, 3, 6, 9 h, diagonal 2.5 mm marks at r 40.2) and an orange border strip (rim R0 → R) after a guide groove. Two parts to place on the bezel alone (before glass installation), then a single orange spray pass:
# 
# - `bezel_marks_stencil.stl`: Full 0.8 mm disk placed on the face. A lip centers in the glass pocket, masking the black face, groove, and interior. Opens 8 indices and the border band.
# - `bezel_side_mask.stl`: Ring that fits the rounded flank (front sphere on lunVis) and bottom flat. Orange does not drip on the black side. Slides forward and self-aligns on the flared flank. Its edge protrudes 0.5 mm in front of the face, covering the border clearance. Two notches on this edge (12 h and 6 h, mid-flat): align two large bars from the stencil.
# 
# Run command:
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_stencils.py
# ```
# Writes the two STL files to `print_stl/`, pre-oriented for printing (painted face up, wide-side cache down). No supports.

import os, sys
import numpy as np
from manifold3d import CrossSection, Manifold

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import robot_parts as robot                                   # Dimensions of-bezel (STYLE, h, R, flatY, sphf, write_stl)

T_PLATE = 0.8                            # thickness of the front stencil
LIP_H, LIP_W, LIP_FIT = 0.8, 1.0, 0.15   # centering lip in the glass pocket (depth: glassT)
FLANK_FIT, FLANK_W = 0.2, 1.2            # clearance and thickness of the side panel cover
RIM = 0.5                                # front bezel rim (in front of the bezel face)
NOTCH_W, NOTCH_D = 1.0, 1.5              # Alignment notches on the edge of the cover
SEG = 256


def stencil_index():
    'Bezel reference point: top face at z = T_PLATE (top of print), lip above, painted face at z = 0.'
    STYLE, h = robot.STYLE, robot.h
    r_pocket = h["glassD"] / 2 + h["glassFit"]                       # edge of the glass pocket
    r_out = STYLE["rimR0"] + STYLE["rimGrooveW"] / 2                          # mask up to the outer edge of the groove.
    m = Manifold.extrude(CrossSection.circle(r_out, SEG), T_PLATE)
    lip_out = r_pocket - LIP_FIT
    lip = Manifold.extrude(CrossSection.circle(lip_out, SEG) - CrossSection.circle(lip_out - LIP_W, SEG), LIP_H)
    m += lip.translate((0, 0, T_PLATE))
    for k in range(8):                                               # Same clips as in `robot_parts.paint_marks`.
        L = STYLE["idxBig"] if k % 2 == 0 else STYLE["idxSmall"]
        bar = Manifold.cube((L, STYLE["idxW"], T_PLATE + 2), True).translate((0, STYLE["idxR"], T_PLATE / 2))
        m -= bar.rotate((0, 0, 90 - 45 * k))
    return m


def solide_flanc(extra, d0, d1):
    'Revolved volume under the expanded front sphere (from extra) from depth d₀ to d₁ (z = depth).'
    prof = [(0, d0)] + [(robot.sphf(d) + extra, d) for d in np.linspace(d0, d1, 40)] + [(0, d1)]
    return Manifold.revolve(CrossSection([prof]), SEG)


def cote_haut(y):
    'Lower flat surface of the head ≥ y (y-plane offset).'
    return Manifold.cube((400, 400, 400), True).translate((0, 200 + y, 0))


def cache_flanc():
    'Bezel reference point (z = depth from the face), then reversed: wide side (rear) on the base plate.'
    lv = robot.STYLE["lunVis"]
    outer = solide_flanc(FLANK_FIT + FLANK_W, -RIM, lv) ^ cote_haut(robot.flatY - FLANK_FIT - FLANK_W)
    inner = solide_flanc(FLANK_FIT, -RIM - 1, lv + 1) ^ cote_haut(robot.flatY - FLANK_FIT)
    m = outer - inner
    for y in (1, -1):                                                # notches at 12 o’clock and 6 o’clock (mid-flat face)
        m -= Manifold.cube((NOTCH_W, 20, 2 * NOTCH_D), True).translate((0, y * (robot.R + 5), -RIM))
    return m.mirror((0, 0, 1)).translate((0, 0, lv))


def main():
    out = os.path.join(HERE, "print_stl")
    for name, m in (("bezel_marks_stencil", stencil_index()), ("bezel_side_mask", cache_flanc())):
        robot.write_stl(os.path.join(out, name + ".stl"), m)
        b = m.bounding_box()
        print("{} : {:.1f} × {:.1f} × {:.1f} mm, {:.1f} cm³, {} corps".format(
            name, b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000, len(m.decompose())))


if __name__ == "__main__":
    main()
