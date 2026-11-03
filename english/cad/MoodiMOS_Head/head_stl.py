# MoodiMOS — STL for head printing (without Fusion)
# 
# Reproduces the construction of `MoodiMOS_Head.py` (same PARAMS, read from this script) via exact CSG (manifold3d) and outputs one piece per file in `cad/MoodiMOS_Organic/print_stl/` (with baskets and gloves), already oriented for printing (Z = 0 on the bed):
# 
# - `head_bezel.stl`: Front ring, rear face on the bed (steps and glass pocket at the top)
# - `head_plate.stl`: Drip plate, flat (pogo-fence cut into the flat surface; channel = horizontal holes)
# - `head_back.stl`: Curved back, opening on the bed (columns on the bed; supports "touching the bed" under the LiPo cradle and base)
# 
# Run command:
# ```bash
# cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Head/head_stl.py
# ```
# 
# Construction reference:
# - X = x (front-facing right view),
# - Y = head height (+ toward the drip),
# - Z = depth (0 = glass face, backward direction), same as Y in the Fusion script.

import ast, math, os, struct
import numpy as np
from manifold3d import CrossSection, Manifold

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "MoodiMOS_Organic", "print_stl")   # with the shoes and gloves
SEG = 256                                   # segments per full circle


def read_params():
    src = open(os.path.join(HERE, "MoodiMOS_Head.py"), encoding="utf-8").read()
    tree = ast.parse(src)
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "PARAMS":
            return {n: v for n, v, _, _ in ast.literal_eval(node.value)}
    raise RuntimeError('PARAMS not found in MoodiMOS_Head.py')


P = read_params()


def seg(r):
    return max(48, int(SEG * min(1.0, r / 20.0 + 0.25)))


def circle(r, cx=0.0, cy=0.0):
    return CrossSection.circle(r, seg(r)).translate((cx, cy))


def rect(x0, x1, y0, y1):
    return CrossSection([[(x0, y0), (x1, y0), (x1, y1), (x0, y1)]])


def prism(cs, z0, z1):
    'Extrusion of a section (X, Y) between depths z₀ and z₁.'
    return Manifold.extrude(cs, z1 - z0).translate((0, 0, z0))


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def rod_y(r, x, z, y0, y1):
    'Cylinder with Y-axis (horizontal holes beneath the head).'
    c = Manifold.cylinder(y1 - y0, r, r, seg(r))               # Z-axis
    return c.rotate((-90, 0, 0)).translate((x, y0, z))           # Z-axis → Y-axis


def revolve(profile):
    'Profile (radius, depth) → revolutionary solid with Z-axis.'
    return Manifold.revolve(CrossSection([profile]), SEG)


def arc(cx, cy, r, a0, a1, n=24):
    return [(cx + r * math.cos(a), cy + r * math.sin(a)) for a in np.linspace(a0, a1, n)]


R = P["bezelD"] / 2
Rp = R + P["plateExtra"]
frontT, plateT, backT = P["frontT"], P["plateT"], P["backT"]
zP, zB, zR = frontT, frontT + plateT, frontT + plateT + backT     # back plate, rear side front face
flatY = -Rp + P["flatH"]                                          # top of the flat surface (neck rest)
zMid = (zP + zB) / 2
BIG = 200


def screw_positions():
    rot = math.radians(P["boardRot"])
    out = []
    for sx, h in ((P["sUpX"], 20.36), (P["sLoX"], 61.06)):
        for side in (1, -1):
            x, y = side * sx, P["pcbTop"] - h
            out.append((x * math.cos(rot) - y * math.sin(rot), x * math.sin(rot) + y * math.cos(rot)))
    return out


SCREWS = screw_positions()


def meplat():
    return box(-BIG, BIG, -BIG, flatY, -BIG, BIG)


def bezel():
    rg = P["glassD"] / 2 + P["glassFit"]
    r1, r2, rl, rb = P["step1D"] / 2, P["step2D"] / 2, P["ledgeD"] / 2, P["boreD"] / 2
    # rear lamage (thinned-back wall behind the shoulder) for USB-C, microSD reader, and cable tray.
    prof = [(rb, frontT), (rb, P["boreD0"]), (rl, P["boreD0"]), (rl, P["glassT"]), (rg, P["glassT"]), (rg, 0),
            (r1, 0), (r1, P["step1T"]), (r2, P["step1T"]), (r2, P["step2T"]), (R, P["step2T"]), (R, frontT)]
    return revolve(prof) - meplat()


def goutte():
    d, rt = P["tabD"], P["tabR"]
    sn = (Rp - rt) / d
    a = math.asin(sn)
    pts = arc(0, 0, Rp, math.pi - a, 2 * math.pi + a, 200)      # Left-to-right, bottom-to-top.
    pts += arc(0, d, rt, a, math.pi - a, 80)                    # upper arc, right to left
    return CrossSection([pts])


def plate():
    cs = goutte() - circle(P["holeD"] / 2, 0, P["tabD"]) - circle(P["plateOpenD"] / 2)
    for x, y in SCREWS:
        cs = cs - circle(P["screwD"] / 2, x, y)
    plate = prism(cs, zP, zB) - meplat()
    # internal-pogo-connector assembly:
    # nose inserted into the flat-side groove (thickness = nose, facing flush),
    # ears pressed against the skin behind it;
    # oblong channel to the central opening for insertion, wired soldered.
    L, W, t = P["pogoL"], P["pogoW"], P["pogoFaceT"]
    yC = -math.sqrt((P["plateOpenD"] / 2) ** 2 - (L / 2) ** 2) + 1.0       # top of the channel, in the central opening
    for Lo, y0, y1 in ((P["pogoBodyL"], flatY - 1, flatY + t + 0.01), (L, flatY + t, yC)):
        plate = plate - box(-(Lo - W) / 2, (Lo - W) / 2, y0, y1, zMid - W / 2, zMid + W / 2)
        for side in (1, -1):
            plate = plate - rod_y(W / 2, side * (Lo - W) / 2, zMid, y0, y1)
    return plate


def back():
    Rb, f, w = P["backD"] / 2, P["backFillet"], P["wallT"]
    outer = [(0, zB), (Rb, zB)] + arc(Rb - f, zR - f, f, 0, math.pi / 2, 40) + [(0, zR)]
    fi = f - w
    inner = [(0, zB - 1), (Rb - w, zB - 1)] + arc(Rb - f, zR - f, fi, 0, math.pi / 2, 40) + [(0, zR - w)]
    back = revolve(outer) - revolve(inner)
    for x, y in SCREWS:                                         # columns, after the shell
        back = back + prism(circle(P["bossD"] / 2, x, y), zB, zR - w + 0.01)
    for x, y in SCREWS:
        back = back - prism(circle(P["screwD"] / 2, x, y), zB - 1, zR + 1)
        back = back - prism(circle(P["screwHeadD"] / 2, x, y), zR - P["screwHeadT"], zR + 1)
    gr = P["grooveD"] / 2
    back = back - prism(circle(gr) - circle(gr - P["grooveW"]), zR - P["grooveT"], zR + 1)
    # cradle of LiPo (V2): frame of ribs on the back, slot for wires at the bottom
    wi, li = P["lipoW"] / 2 + P["lipoFit"], P["lipoL"] / 2 + P["lipoFit"]
    wo, lo = wi + P["ribW"], li + P["ribW"]
    z0 = zR - w - P["ribH"]
    frame = prism(rect(-wo, wo, -lo, lo) - rect(-wi, wi, -li, li), z0, zR - w + 0.01)
    g = P["lipoWireGap"] / 2
    frame = frame - box(-g, g, -lo - 0.5, -li + 0.5, z0 - 1, zR)
    return back + frame


def write_stl(path, m):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    with open(path, "wb") as fh:
        fh.write(b"MoodiMOS head".ljust(80, b" "))
        fh.write(struct.pack("<I", len(t)))
        rec = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
        rec["n"], rec["v"] = n, tri
        fh.write(rec.tobytes())


def on_plate(m, flip=False):
    'Print orientation: depth → height; flip: reverse the part so the back face is at the bottom.'
    if flip:
        m = m.mirror((0, 0, 1))
    b = m.bounding_box()
    return m.translate((0, 0, -b[2]))


def main():
    os.makedirs(OUT, exist_ok=True)
    parts = {
        "head_bezel": on_plate(bezel(), flip=True),
        "head_plate": on_plate(plate()),
        "head_back": on_plate(back()),
    }
    print('Head (STL print, {}) :'.format(os.path.relpath(OUT)))
    for name, m in parts.items():
        b = m.bounding_box()
        ok = m.status().name == "NoError" and m.genus() >= 0
        write_stl(os.path.join(OUT, name + ".stl"), m)
        print("  {:<14} {:6.1f} × {:6.1f} × {:5.1f} mm  {:7.1f} cm³  {} triangles  {}".format(
            name, b[3] - b[0], b[4] - b[1], b[5] - b[2], m.volume() / 1000, m.num_tri(),
            'Sealed' if ok else 'ERROR {}'.format(m.status())))


if __name__ == "__main__":
    main()
