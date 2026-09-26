# PIZAZ Constant reference set

Generated design references for the fictional 1991 PIZAZ Constant. These images are modeling guides, not photographs of a measured real car. The original three-quarter image is the identity anchor; the flat views infer details that it cannot show. They may disagree in small places, so the shape contract in [`tools/pizaz_constant_threejs/README.md`](../../../tools/pizaz_constant_threejs/README.md) decides the build dimensions.

| View | Modeling use |
| --- | --- |
| [Front three-quarter](pizaz-constant-press-photo.png) | Overall identity, paint, stance, grille and wheel style |
| [Side](pizaz-constant-side.png) | Wheelbase, roofline, four door openings, overhangs and arch placement |
| [Front](pizaz-constant-front.png) | Lamp, grille, bumper and mirror spacing |
| [Rear](pizaz-constant-rear.png) | Inferred full-width lamp band, trunk, plate recess and bumper |
| [Top](pizaz-constant-top.png) | Hood, roof, windshield, rear glass and deck proportions |

The first procedural Three.js source is under [`tools/pizaz_constant_threejs/`](../../../tools/pizaz_constant_threejs/). It exports an editable GLB, then Blender imports that GLB and creates a `.blend` and `.fbx`. All 3D formats live under ignored `assets/models/vehicles/pizaz_constant/`; fixed review renders are under ignored `build/pizaz-constant-img2threejs/`. The current mesh is a shape pass, not a cooked Apricot vehicle.

The current refinement uses the side view to set the four-door silhouette, wheelbase and body crease; the front and rear views to place lamps, grilles, bumpers and plate recesses; and the top view to shape the hood, roof, glazing and deck widths. The matching Blender renders and comparison sheets are under `build/pizaz-constant-img2threejs/`.
