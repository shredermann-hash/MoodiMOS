# Design verification — 4 October 2026

This is the historical design review for the revision. The source reference images and earlier
renders are kept in the author's project and are not part of this public build package. Their
printed dimension labels were appearance references, not manufacturing drawings.

## Component envelope

The proposed shorter body was cancelled. The torso stayed **82 × 120.5 × 48 mm**. Camera, cradle
and radar frame returned to their original layout; the radar was not moved behind the screen.
Electronic component, glass, standoff and interface dimensions were read from the source CAD files.

The torso corner radius changed from 6 to **8 mm**. Leg mounting centres moved from ±23 to
**±27.5 mm** for the wider stance; peg diameters, socket depths and electronic interfaces stayed
the same. The source parts of the other edition were not changed.

## Visual corrections

| Observation in the earlier render | correction |
|---|---|
| Grey bezel trim instead of orange | Filename accents normalised for macOS; intended luminous material applied |
| Dull orange and visible horizon | Revised palette and lighting, with a continuous cyclorama background |
| Mitten-like hands | Solid oval palm, short fingers sculpted into the lower quarter and a short attached thumb |
| Black joint band halfway down the leg | Band moved to the ankle; black hip band retained |
| Feet too close together and low shoes | Legs spread towards the ankles; 73.9 mm shoe profile, 12° outward turn, thicker black sole and rounded orange toe |
| Small laces and thick roundel | Orange grooved strap, thinner side ring and two black vents |
| Face and torso smiley too small | Graphics enlarged within the real active display areas |
| Plain ivory front | Thin black frame around the torso glass, cut for the camera |

## Checks recorded at the time

- All **22** then-current print STLs were closed, positive-volume, single-part meshes, with their
  lowest point within **0.01 mm** of the bed.
- Print orientation repaired four small missing triangles on each arm; the repaired meshes passed.
- Torso component parameters matched their source values except for the two explicit design changes.
- Scripts passed syntax checks, and the new front, three-quarter, hand and shoe views were inspected.

The sole measured **73.93 mm along its own axis**, against the 73.9 mm appearance target.
The side roundel had a flat face and complete orange ring; the older raised panels were removed.

## Limits and later build context

The overall height became **315.8 mm**, compared with 309.8 mm before this revision, because the
shoes added 6 mm to the nominal setting. The torso was not shortened. This was not an exact copy
of the source illustration: real camera placement and screen margins constrained the geometry.

Mesh closure did not demonstrate actual board fit or measured joint clearance. The changed leg
mounts, strap and fingers still needed prototype trials, and cable paths and FDM supports still
needed checking. Orange side strips in renderings did not define a working lighting installation.

Later changes, including the two-piece torso, are described in the current build guide and CAD
README. The current English package contains **23** print STLs, including optional parts and aids;
the historical count above is deliberately not presented as the current inventory. Run
`robot_verify.py` for the current mesh report in `renders/mesh_verification.json`.
