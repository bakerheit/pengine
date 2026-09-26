# Selected 1991 vehicle reference photos

[World vehicle directory](../../world/vehicles/README.md) — manufacturer and model pages.

All source concepts and directional reference photos are stored in the docs tree.
The images guide the models; they are generated design references, not measured engineering drawings.

[Original five concepts](../concepts/1991-car-candidates/README.md)

## Photo galleries

Open a model for all eight reference photos, displayed inline.

### GLM Meridian — family minivan

![GLM Meridian design reference](glm_meridian/front-three-quarter.png)

[View every Meridian reference photo](glm_meridian/README.md) · [Current model renders](../reviews/1991-vehicle-refinement/README.md#glm-meridian)

### Rodeo Switchback — two-door SUV

![Rodeo Switchback design reference](rodeo_switchback/front-three-quarter.png)

[View every Switchback reference photo](rodeo_switchback/README.md) · [Current model renders](../reviews/1991-vehicle-refinement/README.md#rodeo-switchback)

### Harrow Hookline — recovery truck

![Harrow Hookline design reference](harrow_hookline/front-three-quarter.png)

[View every Hookline reference photo](harrow_hookline/README.md) · [Current model renders](../reviews/1991-vehicle-refinement/README.md#harrow-hookline)

## View index


| View | GLM Meridian | Rodeo Switchback | Harrow Hookline |
|---|---|---|---|
| Selected concept | [GLM Meridian](glm_meridian/concept-selected.png) | [Rodeo Switchback](rodeo_switchback/concept-selected.png) | [Harrow Hookline](harrow_hookline/concept-selected.png) |
| Front | [GLM Meridian](glm_meridian/front.png) | [Rodeo Switchback](rodeo_switchback/front.png) | [Harrow Hookline](harrow_hookline/front.png) |
| Rear | [GLM Meridian](glm_meridian/rear.png) | [Rodeo Switchback](rodeo_switchback/rear.png) | [Harrow Hookline](harrow_hookline/rear.png) |
| Left | [GLM Meridian](glm_meridian/left.png) | [Rodeo Switchback](rodeo_switchback/left.png) | [Harrow Hookline](harrow_hookline/left.png) |
| Right | [GLM Meridian](glm_meridian/right.png) | [Rodeo Switchback](rodeo_switchback/right.png) | [Harrow Hookline](harrow_hookline/right.png) |
| Top | [GLM Meridian](glm_meridian/top.png) | [Rodeo Switchback](rodeo_switchback/top.png) | [Harrow Hookline](harrow_hookline/top.png) |
| Front three-quarter | [GLM Meridian](glm_meridian/front-three-quarter.png) | [Rodeo Switchback](rodeo_switchback/front-three-quarter.png) | [Harrow Hookline](harrow_hookline/front-three-quarter.png) |
| Rear three-quarter | [GLM Meridian](glm_meridian/rear-three-quarter.png) | [Rodeo Switchback](rodeo_switchback/rear-three-quarter.png) | [Harrow Hookline](harrow_hookline/rear-three-quarter.png) |

The model notes in each folder record which views control proportions and resolve conflicting details.

## Refinement contract

The refinement replaces slab sides and box roofs with sampled pressed panels, crowned hoods and roofs, restrained window corner radii, fitted seals, dished wheels and tapered upholstery. Tight metal creases, thin panel gaps, reflector-patterned lenses and restrained material gloss replace the rejected broad, padded bevels. The Switchback retains its cream removable hardtop; the Meridian retains its sliding-door tracks and three seat rows; the Hookline retains its service lockers, recovery boom and winch.

Wheel centers, wheelbase and the existing 256×256 semantic atlases remain the runtime contract. The Meridian roof is lowered to 1.84 m and its cowl moves rearward to give the hood a real slope. The Hookline beltline is raised to 1.36 m. Both front-door hinges move with the revised door openings; driver seating and steering positions remain aligned to the runtime pose. The body safety ceiling is 60,000 triangles per car for this closer-view refinement; actual counts are written to the generated fit reports. Glass remains separate, and exposed inner surfaces stay mapped.

Rebuild all three with `python3 tools/make_1991_candidates_assets.py`. Saved editable sources are under the private `assets/models/vehicles/<slug>/source.blend` folders.

[Refined model review: studio angles, door interiors and game captures](../reviews/1991-vehicle-refinement/README.md).
