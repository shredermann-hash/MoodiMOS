# MoodiMOS — printing settings

These are starting settings recorded on a Creality Ender 3 S1 Pro with a 0.4 mm nozzle, PEI bed
and UltiMaker Cura. Adjust temperatures to the filament manufacturer's range and check the first
layer. The robot's printable files are in `cad/MoodiMOS_Robot/print_stl/` at the package root.
The detailed part list and orientations follow the profiles below.

## PETG reference profile

The reference profile, “MoodiMOS PETG 0.16”, starts from Cura Dynamic Quality 0.16 mm.
Recorded right-sneaker trial, 30/09/2026: **8 h 03, 43 g**.

| Cura section | Setting | Value |
|---|---|---|
| Quality | Layer height | 0.16 mm |
| Walls | Wall thickness | 1.6 mm, 4 lines |
| Walls | Horizontal expansion | 0.0 mm |
| Walls | Z seam alignment | Sharpest corner |
| Top / Bottom | Top/bottom thickness | 1.0 mm, rounded to 7 layers at 0.16 mm |
| Infill | Density / pattern | 40%, gyroid; see individual part settings |
| Material | Printing temperature | 235 °C, PETG |
| Material | Bed temperature | 75 °C |
| Speed | Print speed | 50 mm/s |
| Speed | Outer wall speed | 25 mm/s |
| Speed | Initial layer speed | 20 mm/s |
| Travel | Retraction | 0.8 mm at 40 mm/s |
| Travel | Combing mode | Not in outer surface |
| Travel | Z hop when retracted | Enabled |
| Cooling | Fan speed | 40% |
| Cooling | Initial fan speed | 0% |
| Support | Structure | Tree, where required by the part |
| Support | Placement | Touching buildplate |
| Support | Overhang angle | 60° |
| Support | Z distance | 0.32 mm, two layers; PETG can bond to supports |
| Build plate adhesion | Type / width | Brim, 4 mm unless overridden below |

Use a light glue-stick film as a release layer for PETG on PEI. The recorded drying setting for
PETG exposed to room air was **4 h at 65 °C**. Check the first layer before leaving the printer.

## PLA+ alternative

Recorded trial, 13/10/2026: head bezel and plate, then the dome, which took **5 h 55**,
**12.1 m**, approximately **36 g**. Duplicate the PETG profile and change these values:

| Cura section | Setting | PLA+ value |
|---|---|---|
| Material | Printing temperature | 215 °C; adjust within the spool's stated range |
| Material | Bed temperature | 60 °C; 65 °C for the initial layer |
| Cooling | Fan speed | 100% from layer 3 |
| Cooling | Initial fan speed | 0% |
| Speed | Print speed | 60 mm/s |
| Speed | Outer wall speed | 30 mm/s |
| Speed | Initial layer speed | 15 mm/s |
| Speed | Initial layer travel speed | 50 mm/s; 100 mm/s caught on the test towers |
| Quality | Initial layer line width | 120% |
| Travel | Retraction | 0.8 mm at 40 mm/s, direct-drive extruder |
| Travel | Z hop when retracted | Disabled |
| Support | Z distance | 0.16 mm, one layer |

Use clean PEI, degreased with isopropyl alcohol, **without glue** for this PLA+ profile. The recorded
drying setting was **4–6 h at 50 °C**. A temperature tower helps choose the spool's setting, usually
around 210–220 °C in these trials. PLA softens around 55–60 °C: keep the robot out of a sun-heated
window or car.

PLA+ is stiffer than PETG, so magnet pockets and peg sockets can be tight. Print the head bezel
first and test the glass, board and fasteners. If holes are tight, test **+0.1 mm hole horizontal
expansion** before applying it to all parts. The unused battery-pocket clips in the CAD can be
made less aggressive by reducing `lipoLipIn` from 1.2 to 0.8 mm; this USB build uses no battery.

Wash a greasy bed with dishwashing liquid and warm water, then avoid touching its centre.
Remove a plastic blob from the hot nozzle after a failed print so it does not knock over the next
small features. On a Sonic Pad, the recorded workaround after copying a file was to wait 2–3 minutes
before starting; launching while its object-cancellation rewrite was still running caused
“Unable to parse move”.

## Fits and assembly clearances

- Sneaker leg socket: Ø 14.5 mm for a Ø 14 mm leg.
- Limb magnet pockets: Ø 6.2 mm; pegs Ø 9 mm in Ø 9.4 mm sockets.
- Arm-to-shoulder cup: Ø 17 mm ball, 0.15 mm glue clearance.
- Head glass pocket: Ø 75.6 mm for Ø 75 mm glass, 0.3 mm radial clearance, 1.0 mm depth.
- Head ledge: Ø 72.4 mm for the Ø 71.8 mm LCD; rear relief Ø 78 mm for the USB sockets,
  microSD reader and folded ribbon. Test the whole board without forcing it.
- M2 screw passages: Ø 2.3 mm, head recesses Ø 4.2 mm.
- Magnetic neck connector: 21.4 × 4.6 mm nose slot in a 2 mm skin, with a channel for its ears.
  Confirm its width with `pogo_gauge.stl`; the face should be flush without forcing the connector.

The dome lies on its exterior back/medallion face. Use a **skirt**, not a brim, to avoid a lip at
its back edge. Tree supports touch the bed only under the side headphones; add a support blocker
inside each of the four screw wells. If a plug was printed there, push it out with a 2 mm rod
through the screw passage.

The torso front panel lies back-down so the glass pocket opens upwards. The body lies on its
front-panel joint, with supports disabled: detachable fins are already in its STL below the neck
collar. Remove them after printing. Teardrop-shaped internal bosses reduce support needs while
leaving the peg sockets round. If a socket ceiling sags, the recorded correction is to turn a
9.5 mm drill by hand. Check all bridging and supports in the slicer's layer preview.

## Parts and orientation


The robot has **18 printed parts**: 10 for the limbs (two shoulders, two arms, two glove-and-bracelets,
two legs, two sneakers), 3 for the head and 5 for the torso (body, front panel, back, speaker box, screen
frame). Each glove-and-bracelet is printed in one piece. Five other files are optional: the base, its two
drilling jigs, then the stencil and the mask that help paint the bezel.

All these files are in `cad/MoodiMOS_Robot/print_stl/`. Keep the supplied orientation, except for the
arms: the parts were laid out to limit supports and to put their marks on the least visible faces.

- **Head bezel** × 1. File(s): `head_bezel.stl`. Planned supports: None. Infill: 30 %.
- **Head teardrop plate** × 1. File(s): `head_plate.stl`. Planned supports: None. Infill: 30 %.
- **Head dome, with its headphones** × 1. File(s): `head_back.stl`. Planned supports: Tree, from the bed, under the headphones only; a support blocker in each of the 4 screw wells. Infill: 30 %.
- **Torso body** × 1. File(s): `torso_body.stl`. Planned supports: None: supports disabled in the slicer, fins built in under the collar. Infill: 25 %.
- **Torso front panel** × 1. File(s): `torso_front.stl`. Planned supports: None, back on the bed. Infill: 30 %.
- **Torso back** × 1. File(s): `torso_back.stl`. Planned supports: None. Infill: 20 to 30 %.
- **Speaker box** × 1. File(s): `torso_speaker_box.stl`. Planned supports: None. Infill: 20 to 30 %.
- **Screen frame** × 1. File(s): `torso_frame.stl`. Planned supports: None. Infill: 100 %.
- **Shoulders** × 2. File(s): `shoulder_right.stl`, `shoulder_left.stl`. Planned supports: None. Infill: 100 %.
- **Arms** × 2. File(s): `arm_right.stl`, `arm_left.stl`. Planned supports: Tree, from the bed; lay them down again, see below. Infill: 40 %.
- **Gloves with bracelets** × 2. File(s): `glove_cuff_right.stl`, `glove_cuff_left.stl`. Planned supports: Tree, from the bed. Infill: 40 %.
- **Legs** × 2. File(s): `leg_right.stl`, `leg_left.stl`. Planned supports: None. Infill: 40 %.
- **Sneakers** × 2. File(s): `sneaker_right.stl`, `sneaker_left.stl`. Planned supports: Under the raised toe. Infill: 40 %.
- **Base (optional)** × 1. File(s): `base.stl`. Planned supports: None, underside on the bed. Infill: 10 to 15 %.
- **Drilling jigs (optional)** × 2. File(s): `sneaker_magnet_jig_right.stl`, `sneaker_magnet_jig_left.stl`. Planned supports: None. Infill: 20 %.
- **Index stencil (optional)** × 1. File(s): `bezel_marks_stencil.stl`. Planned supports: None. Infill: 100 %.
- **Bezel side mask (optional)** × 1. File(s): `bezel_side_mask.stl`. Planned supports: None. Infill: 100 %.

**Arms.** The file lays the arm on its inner face, which leaves this round arm only 5 to 7 mm² of contact
with the bed. In the slicer, lay it down in the plane of its elbow: contact rises to about 80 mm². My
settings: tree supports from the bed, 8 mm brim, 4 walls, 40 % infill.

**Front panel.** Print it before the body and try the torso screen module in it, with the ribbon folded.
On my robot it fits through its 56 × 82 mm window. If yours jams, enlarge `winW` and `winH` (step 2) and
regenerate the parts, and check what this changes around the glass shoulder. I did not need this correction.

**Screen frame.** It is less than one millimetre thick. If it looks too thin to print or glue, paint it
directly on the front panel.

The pogo connector gauge, `cad/MoodiMOS_Organic/print_stl/pogo_gauge.stl`, is only used to try the
connector's pocket: it does not count among the robot's parts.

I printed the head in **PLA+**, with a **0.16 mm** layer height, four **0.4 mm** walls, the nozzle at
**215 °C** and the bed at **60 °C**, on clean PEI with no glue. **PETG** also works: nozzle at **235 °C**,
bed at **75 °C**, a film of glue. These are starting values; adapt them to what your spool recommends. The
tree supports touch the bed only, with a Z distance of **0.16 mm** in PLA+ and **0.32 mm** in PETG.

Plan a **4 mm** brim, raised to **6 to 10 mm** for parts with little bed contact: arms, gloves, shoulders
and legs. For the dome, prefer a **skirt**: a brim would leave a lip on its rear edge. Both complete
profiles are in `cad/MoodiMOS_Organic/print_stl/PRINT_SETTINGS.md` (in English). The dome took
**5 h 55 min and about 36 g of PLA+**; that is not a valid estimate for every part.

**Print the head bezel first.** Try the round board in it without forcing, then check the magnet pockets
and pegs on a first part. If the holes come out too tight, try a hole horizontal expansion of **+0.1 mm**
in Cura and check the result on that part before applying the correction to the rest.

Before each print, go through the preview layer by layer. Look especially at bed adhesion and the supports
under the arms, the fingers and the dome's headphones. The body's STL already contains fins under the
collar: remove them with cutters after printing and clean up the area. They are printing aids, not parts
to keep in the robot.

