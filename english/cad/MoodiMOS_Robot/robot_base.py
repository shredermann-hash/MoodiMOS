# MoodiMOS Complete edition – round base, inspired by the bezel of the head
# 
# Full disk base: black band at bottom (guide groove), ivory body, step with top painted orange (bezel trim; guide groove on edge, before rounding), elevated ivory platform where the robot lands in its shoe impressions (2.5 mm depth, exact sole shape, angled 12° outward). Each foot has an embedded magnet (along the weight axis) holding the robot; a twin magnet fits into a hand-drilled hole in the sole (provided jig for drilling). The disk centers on both soles (tightest circle encompassing them). Below: four bearing mounts.
# 
# Run from project root after `robot_parts.py` and `robot_limbs.py` (STL files updated):
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_base.py
# ```
# Generate:
# - `print_stl/base.stl` (no support, below platform)
# - `print_stl/sneaker_magnet_jig_*.stl`
# - `base/*.stl` (rendered pieces: `ROBOT_SOCLE=1` Blender `-b` `-P cad/MoodiMOS_Robot/robot_render.py -- face 34`).

import json, math, os, struct
import numpy as np
from manifold3d import CrossSection, Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
STL, OUT_PRINT, OUT_RENDU = (os.path.join(HERE, d) for d in ("stl", "print_stl", "base"))

S = {
    "margin": 12.0,       # clearance between the sole and the edge of the disc
    "step": 6.0,          # orange staircase width (platform = disk – step)
    "H1": 15.0,           # height of the tiered seating
    "H": 20.0,            # height of the base plate (= robot floor)
    "rEdge": 2.0,         # curve of the step edge
    "rTop": 3.0,          # rounded edge of the base plate
    "printDepth": 2.5,    # footwear imprints
    "printFit": 0.5,      # clearance around the soles
    "magD": 10.2, "magT": 5.2,       # magnet Ø 10 × 5 (N52) flush with the bottom of the recess
    "bandZ": 5.0, "grooveW": 0.8, "grooveT": 0.6,   # groove for the black headband
    "stepGrooveW": 0.8, "stepGrooveT": 0.4,         # groove guide for the orange of the step (as with the bezel)
    "padD": 12.5, "padT": 1.0, "padInset": 18.0,    # adhesive pads Ø 12 under the base
    "jigT": 1.2, "jigRim": 1.6, "jigRimW": 1.2, "jigPilot": 3.0,   # drill templates
}
LEG = {"right": (34.0, 24.0), "left": (-34.0, 24.0)}   # leg axis (torso reference), magnet aligned vertically
SEG = 256


def load_manifold(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    tri = np.frombuffer(d[84:84 + 50 * n], dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])["v"].reshape(-1, 3)
    V, idx = np.unique(tri, axis=0, return_inverse=True)
    return Manifold(Mesh(vert_properties=V.astype(np.float32), tri_verts=idx.reshape(-1, 3).astype(np.uint32)))


def write_stl(path, m):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"], rec["v"] = nrm, tri
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS base".ljust(80, b" "))
        fh.write(struct.pack("<I", len(t)))
        fh.write(rec.tobytes())


def semelle(side, ground):
    'Draw the outline on the floor of the sneaker (torso reference point) based on the black edge of the sole.'
    return load_manifold(os.path.join(STL, "trim_%s.stl" % side)).slice(ground + 0.3)


def disque(ground):
    'Center (y) and radius of the disk: tightest circle enclosing both soles, plus margin.'
    pts = np.vstack([np.array(p) for side in LEG for poly in semelle(side, ground).to_polygons() for p in [poly]])
    best = min(((cy, np.max(np.hypot(pts[:, 0], pts[:, 1] - cy))) for cy in np.arange(-20, 40, 0.1)), key=lambda c: c[1])
    return best[0], math.ceil(best[1] + S["margin"])


def arc(cx, cz, r, a0, a1, n=12):
    return [(cx + r * math.cos(a), cz + r * math.sin(a)) for a in np.linspace(a0, a1, n)]


def corps(R):
    'Stepper disk, local reference (center at x = y = 0, bottom at z = 0): revolution profile (radius, height).'
    Rp, H1, H, re, rt = R - S["step"], S["H1"], S["H"], S["rEdge"], S["rTop"]
    prof = [(0, 0), (R, 0), (R, H1 - re)] + arc(R - re, H1 - re, re, 0, math.pi / 2) + \
           [(Rp, H1), (Rp, H - rt)] + arc(Rp - rt, H - rt, rt, 0, math.pi / 2) + [(0, H)]
    m = Manifold.revolve(CrossSection([prof]), SEG)
    g = S["grooveW"] / 2                                       # groove for the black headband
    ring = CrossSection.circle(R + 1, SEG) - CrossSection.circle(R - S["grooveT"], SEG)
    m -= Manifold.extrude(ring, 2 * g).translate((0, 0, S["bandZ"] - g))
    r1 = R - re                                                # elevation step (orange): from the wall of the platform to the groove.
    step = CrossSection.circle(r1, SEG) - CrossSection.circle(r1 - S["stepGrooveW"], SEG)   # just before the rounding
    return m - Manifold.extrude(step, S["stepGrooveT"] + 1).translate((0, 0, H1 - S["stepGrooveT"]))


def base(ground):
    'Complete base, local reference: disk center at (0, 0), bottom at z = 0, plate at z = H (robot floor).'
    cy, R = disque(ground)
    m = corps(R)
    for side, (lx, ly) in LEG.items():
        foot = semelle(side, ground).offset(S["printFit"]).translate((0, -cy))
        m -= Manifold.extrude(foot, S["printDepth"] + 1).translate((0, 0, S["H"] - S["printDepth"]))
        m -= Manifold.cylinder(S["magT"] + 0.01, S["magD"] / 2, S["magD"] / 2, 64).translate(
            (lx, ly - cy, S["H"] - S["printDepth"] - S["magT"]))
    for k in range(4):                                         # 45°, 135°, 225°, 315° angles
        a = math.radians(45 + 90 * k)
        rp = R - S["padInset"]
        m -= Manifold.cylinder(S["padT"] + 0.01, S["padD"] / 2, S["padD"] / 2, 64).translate(
            (rp * math.cos(a), rp * math.sin(a), -0.01))
    return m, cy, R


def gabarit(side, ground):
    'Drill template to print on the plate: reverse the sneaker placed upside down, its edge frames the sole, and its front hole marks the magnet’s center (drill Ø10 to 5 mm depth).'
    sole = semelle(side, ground).offset(0.4)
    rim = sole.offset(S["jigRimW"])
    plate = Manifold.extrude(rim, S["jigT"]) + Manifold.extrude(rim - sole, S["jigT"] + S["jigRim"])
    lx, ly = LEG[side]
    plate -= Manifold.cylinder(S["jigT"] + 1, S["jigPilot"] / 2, S["jigPilot"] / 2, 32).translate((lx, ly, -0.5))
    b = plate.bounding_box()
    return plate.translate((-(b[0] + b[3]) / 2, -(b[1] + b[4]) / 2, 0))


def main():
    ground = json.load(open(os.path.join(STL, "dimensions.json")))["ground_z"]
    os.makedirs(OUT_RENDU, exist_ok=True)
    m, cy, R = base(ground)
    write_stl(os.path.join(OUT_PRINT, "base.stl"), m)
    for side in LEG:
        write_stl(os.path.join(OUT_PRINT, "sneaker_magnet_jig_%s.stl" % side), gabarit(side, ground))
    # rendering:
    # base placed under the robot (platform on the robot’s floor), black band separate, stairway top painted orange.
    for f in os.listdir(OUT_RENDU):
        if f.endswith(".stl"):
            os.remove(os.path.join(OUT_RENDU, f))
    placed = m.translate((0, cy, ground - S["H"]))
    cut = Manifold.cube((400, 400, S["bandZ"]), True).translate((0, 0, ground - S["H"] + S["bandZ"] / 2))
    write_stl(os.path.join(OUT_RENDU, "base_corps.stl"), placed - cut)
    write_stl(os.path.join(OUT_RENDU, "base_bandeau.stl"), placed ^ cut)
    film = CrossSection.circle(R - S["rEdge"] - S["stepGrooveW"] - 0.05, SEG) - CrossSection.circle(R - S["step"] + 0.05, SEG)
    write_stl(os.path.join(OUT_RENDU, "base_filet.stl"),
              Manifold.extrude(film, 0.03).translate((0, cy, ground - S["H"] + S["H1"])))
    json.dump({"hauteur": S["H"], "rayon": R, "centre_y": cy}, open(os.path.join(OUT_RENDU, "base.json"), "w"))
    b = m.bounding_box()
    print('base: Ø {:.0f} × {:.1f} mm (center {:.1f} mm in front of the torso axis), {:.0f} cm³ full, {} body'.format(
        b[3] - b[0], b[5] - b[2], 24 - cy, m.volume() / 1000, len(m.decompose())))


if __name__ == "__main__":
    main()
