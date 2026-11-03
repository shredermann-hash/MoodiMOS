# MoodiMOS — CAD and Fusion

This folder contains the robot's geometry, generators and Fusion scripts. The complete design
walkthrough is in **Step 2** of the build guide on Instructables. Component dimensions come
from `../MoodiMOS_Head/MoodiMOS_Head.py` and `../MoodiMOS_Torso/MoodiMOS_Torso.py`; the Scripts apply
the silhouette changes without scaling the electronics.

## Design and dimensions

The torso remains **82 × 120.5 × 48 mm**. The style has a visible **Ø 86 mm** black head bezel,
a **Ø 75 mm** glass and an approximately **Ø 92 mm** orange head envelope, plus the side headphones.
`robot_style.py` sets the shared proportions: 36 mm visible leg height, 46 mm nominal sneaker height,
shoe axes at ±34 mm and leg sockets at ±27.5 mm. Overall nominal height is **315.8 mm**, without
the optional base. Sneakers are about **73.9 mm** long and turned **12° outwards**.

The supplied torso separates its front panel from the body. Four M2 × 8 button-head screws attach
it; the radar sits next to the camera cradle to clear the upper-right front-panel boss. This newer
body has not been assembled with the board; the photographed robot uses the supplied front panel
on an earlier one-piece shell. Check the screen fit before printing the complete body.

Hands and sneakers are sculpted shapes. The arm glues to the shoulder ball, and the leg glues to
the sneaker. Magnetic pegs at the torso make the limbs removable. The head stays on the neck in
the USB version; no battery is fitted. Small grooves guide the painted joint bands and trim.

## Generate and check

From the package root, prepare the Python environment as described in Step 2, then run:

```sh
cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_parts.py
cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_limbs.py
cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_verify.py
```

`stl/` contains parts in assembly coordinates. `print_stl/` contains parts placed for printing.
The latter is the folder to use in your slicer; check the arm orientation as described in Step 3.
`fusion_reference.json` contains dimensions, profiles and expected volumes used by the full Fusion
script. Wait until the generators finish before importing or rendering.

| Script | Role |
|---|---|
| `robot_style.py` | Shared design dimensions |
| `robot_parts.py` | Head, torso and screen frame; assembly coordinates and Fusion reference |
| `robot_organic.py` | Sculpted limb geometry and measured torso interfaces |
| `robot_limbs.py` | Joint bands and print preparation for limbs |
| `robot_base.py` | Round base and sneaker magnet drilling jigs |
| `robot_stencils.py` | Bezel painting stencil and side mask |
| `robot_screens.py` | Presentation screen textures |
| `robot_render.py` | Blender scene, materials and cameras |
| `robot_finish.py` | Glow and presentation image finishing |
| `robot_verify.py` | Mesh closure, volume, component count and placement on the bed |
| `robot_print_check.py` | Additional mesh, thickness and fitting analysis |

Render scripts are optional presentation tools; printing the supplied STLs does not require Blender
or Fusion. Wait for geometry generation before rendering: the generators rewrite the mesh files.

## The two Fusion workflows

Open **Utilities > Add-Ins > Scripts and Add-Ins**, choose the **Scripts** tab, add the required
folder with **+**, then select the script and **Run**. Keep the directory tree intact.

**`MoodiMOS_Robot/`** is recommended for understanding the construction. It rebuilds the head
and torso as Fusion solids with feature history, places electronic component envelopes and imports
sculpted limbs as meshes. It reads `fusion_reference.json` and `stl/`, so run both generators first.
Components are grouped as Head, Torso and Limbs. The final report compares applicable solid
volumes with the reference, allowing **1.5%** difference. Save the generated document in Fusion.
Not every dimension is exposed as a user parameter; make reproducible changes in the source files
and regenerate. A change made only in Fusion does not update Python-generated STLs automatically.

**`MoodiMOS_Assembly/`** imports the generated mesh parts without rebuilding solids. It also
separates rendering-only objects, such as glass and painted areas. It is useful for inspection
and exploded views. Imported meshes do not acquire a sketch-and-feature history.

Use the Animation workspace to separate components in a storyboard, then create a drawing from
that animation. This illustrates assembly order; it is not a simulation of motorised joints.
The API uses centimetres internally while the generators use millimetres; the Fusion script makes
the conversion. For background and menu links, see Step 2 of the guide.

## Optional base and painting aids

The base is **Ø 150 × 20 mm**, with a **Ø 138 mm** upper platform and **2.5 mm** sneaker recesses.
A **Ø 10 × 5 mm** magnet below each foot attracts a matching magnet fitted into the sneaker using
its drilling jig. Four **Ø 12 mm** feet fit underneath. The decorative finish described in the guide
is ivory, with a black lower band and orange step top.

Run `robot_base.py` for the base and two drilling jigs. Run `robot_stencils.py` for the bezel index stencil
and side mask. Paint the bezel separately before fitting the glass. Align the stencil's long bars
with the mask notches at 12 and 6 o'clock; push the stencil out from behind the bezel to remove it.

## What the checks mean

Closed meshes do not establish actual board fit, cable clearance, print support quality or painted
joint tolerances. Measure provisional camera and speaker dimensions on your components and test
critical fits before large prints. The sub-millimetre black screen frame can be painted directly
if printing or gluing it is impractical. Glowing side strips in design renderings are presentation
features, not a lighting installation supplied by this USB build.
