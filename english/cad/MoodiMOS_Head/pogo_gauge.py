# MoodiMOS — magnetic pogo-pin connector gauge (5-pin, pitch 2.5 mm, Sylen B0H22943X6)
# 
# No tailstock: same assembly as the head plate and torso ring, connector inserted from the inside.
# A 2 mm-thick skin (neck height near magnets) with three elongated slots for the nose; behind each slot,
# a cavity matching the ear length, resting under the skin. The nose must fit snugly without clearance, flush with the skin.
# 
# Slots:
# - Length: `pogoBodyL` (21.4 mm)
# - Width: 4.5 / 4.6 / 4.7 mm (1st–3rd engraved points on cavity sides)
# 
# Cavities:
# - Length: `pogoL` (26.4 mm), same width, 2 mm tall
# 
# Dimensions checked 13/10/2026:
# - 25 mm long (with ears), 20 mm without, 4 mm wide, 4 mm tall (2 sides: magnets; 1: ears; 1: soldering).
# 
# Gauge check 13/10 (3.8–4.4 mm; 20.6/25.6 mm):
# 4.4 mm nearly ideal but slightly tight; nose/ears too long for 20.6/25.6. (First gauge: 22.4 mm too short for connector.)
# 
# Printing:
# - Skin on the bed (as exported), cavities open upward, no supports.
# Test: Place connector in a cavity, nose down in slot, then flip to check fit.
# 
# Run:
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Head/pogo_gauge.py
# Output: cad/MoodiMOS_Organic/print_stl/pogo_gauge.stl

import os, sys
from manifold3d import CrossSection, Manifold

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from head_stl import OUT, P, write_stl

L, LB, T = P["pogoL"], P["pogoBodyL"], P["pogoFaceT"]    # ears, nose, skin: head components (head)
CAV = 2.0                                                 # height of cavities (ears 1 + body 1)
LARGEURS = (4.5, 4.6, 4.7)
PAS = 7.5                                                 # pitch of slots
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
        m -= Manifold.extrude(oblong(w, LB), T + 0.02).translate((0, y, -0.01))          # Nostril slit in the skin.
        m -= Manifold.extrude(oblong(w, L), CAV + 0.01).translate((0, y, T))             # ear cavity
        for j in range(k + 1):                                  # reference: 1–3 engraved points on top
            m -= Manifold.cylinder(0.8, 0.5, 0.5, 24).translate((L / 2 + 2.0, y - 1.5 * k / 2 + 1.5 * j, BZ - 0.6))
    return m


if __name__ == "__main__":
    m = jauge()
    path = os.path.join(OUT, "pogo_gauge.stl")
    write_stl(path, m)
    b = m.bounding_box()
    print("{} : {:.1f} × {:.1f} × {:.1f} mm, {:.1f} cm³, {}".format(
        os.path.relpath(path), b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000, m.status().name))
