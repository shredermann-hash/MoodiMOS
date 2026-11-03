# MoodiMOS — arms, shoulders, and legs oriented for printing (Z = 0 on platform)
# 
# Reuses `stl/arm_*.stl`, `stl/épaule_*.stl`, and `stl/leg_*.stl` (torso reference, written by `organic.py`) without altering their shape, orients them, and writes them to `print_stl/`:
#    `arm_*.stl`: laid on its inner flat side (the flat that follows the torso’s flank); the outer, visible side remains up, unsupported; supports touching the platform under the forearm, inner side (hidden against the body).
#    `épaule_*.stl`: tenon downward (magnetic housing on the platform), ball upward; no support (1.5 mm border between tenon and ball, hidden in the cup).
#    `leg_*.stl`: standing as in the robot, with the hidden tip in the sneaker (Ø 14 disk), magnetic tenon at the top; no overhang beyond 60°: no support.
# 
# Run after `organic.py`: `cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Organic/orient_for_print.py`

import os, struct
import numpy as np
from manifold3d import Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
SRC, OUT = os.path.join(HERE, "stl"), os.path.join(HERE, "print_stl")


def load(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    tri = np.frombuffer(d[84:84 + 50 * n], dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])["v"]
    return tri.reshape(-1, 3, 3).astype(np.float64)


def save(path, tri):
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(tri), dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])
    rec["n"], rec["v"] = n, tri.reshape(-1, 9)
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS impression".ljust(80, b" "))
        fh.write(struct.pack("<I", len(tri)))
        fh.write(rec.tobytes())


def fill_small_holes(tri, max_edges=12):
    'Seal small mesh holes (marching cubes defects) with fan-shaped triangles oriented like adjacent faces; returns (triangles, number of added triangles)'
    pts, idx = np.unique(tri.reshape(-1, 3).round(5), axis=0, return_inverse=True)
    f = idx.reshape(-1, 3)
    half = {}
    for a_, b_ in np.concatenate([f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]]]):
        half[(int(a_), int(b_))] = half.get((int(a_), int(b_)), 0) + 1
    border = {a_: b_ for (a_, b_) in half if (b_, a_) not in half}   # half edges without mates
    new, seen = [], set()
    for start in list(border):
        if start in seen:
            continue
        loop, v = [], start
        while v not in seen and v in border and len(loop) <= max_edges:
            seen.add(v)
            loop.append(v)
            v = border[v]
        if v != start or len(loop) > max_edges:
            continue
        loop = loop[::-1]                          # opposite direction to the edge: same orientation as neighbors
        for i in range(1, len(loop) - 1):
            new.append((loop[0], loop[i], loop[i + 1]))
    if not new:
        return tri, 0
    f = np.vstack([f, np.array(new)])
    return pts[f], len(new)


def check(tri):
    'Closed mesh without defects (fusion of identical vertices, followed by manifold3D check).'
    pts, idx = np.unique(tri.reshape(-1, 3).round(5), axis=0, return_inverse=True)
    m = Manifold(Mesh(vert_properties=pts.astype(np.float32), tri_verts=idx.reshape(-1, 3).astype(np.uint32)))
    return m.status().name == "NoError" and not m.is_empty(), m.volume()


def place(tri):
    'Position on the bed (z min = 0), centered in x and y.'
    p = tri.reshape(-1, 3)
    lo, hi = p.min(0), p.max(0)
    return tri - np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]])


def rot(tri, R):
    return tri @ np.asarray(R, dtype=np.float64).T


# Side of the torso (−x to the right, +x to the left) → toward the bed (new z = old x to the right):
# shoulder tenon downward, inner side of the arm on the bed
INNER_DOWN = {"right": [[0, 0, -1], [0, 1, 0], [1, 0, 0]], "left": [[0, 0, 1], [0, 1, 0], [-1, 0, 0]]}
AS_IS = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]                  # leg: already standing, tenon at the top


def flip_if_needed(tri, R):
    'A proper rotation (det = +1) preserves face orientation; otherwise, faces are reversed.'
    return tri if np.linalg.det(np.asarray(R, float)) > 0 else tri[:, ::-1]


def main():
    os.makedirs(OUT, exist_ok=True)
    print('Arm, shoulders, and legs (STL print files, {}):'.format(os.path.relpath(OUT)))
    for side in ("right", "left"):
        for name, R in (("arm", INNER_DOWN[side]), ("shoulder", INNER_DOWN[side]), ("leg", AS_IS)):
            tri = place(flip_if_needed(rot(load(os.path.join(SRC, "{}_{}.stl".format(name, side))), R), R))
            tri, filled = fill_small_holes(tri)
            ok, vol = check(tri)
            save(os.path.join(OUT, "{}_{}.stl".format(name, side)), tri)
            p = tri.reshape(-1, 3)
            size = p.max(0) - p.min(0)
            print("  {:<14} {:5.1f} × {:5.1f} × {:5.1f} mm  {:5.1f} cm³  {}{}".format(
                "{}_{}".format(name, side), *size, vol / 1000, 'Sealed' if ok else 'REVIEW',
                '({} triangle(s) added to fill mesh defects)'.format(filled) if filled else ""))


if __name__ == "__main__":
    main()
