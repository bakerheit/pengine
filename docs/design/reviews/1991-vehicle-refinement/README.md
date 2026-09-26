# 1991 vehicle refinement review

This pass moves the three playable models toward the selected reference photos: tighter stamped panels and trim, clearer shut lines, realistic cabin proportions, less padded upholstery, dished wheels, ribbed lamp lenses and fitted mechanical parts. The Meridian has a lower roof and a longer hood. The Hookline has a higher cab beltline, service-panel seams and a supported recovery boom with winch and sheave hardware.

These studio images are renders of the saved Blender models. The game captures show the cooked meshes, transparent glass, driver and atlas in Apricot's renderer. They are separate from the original [24 concept and directional reference photos](../../references/1991-selected-vehicles.md).

## GLM Meridian

### Roof and windows matched together

The latest cabin revision replaces the rejected roof-only rollover. It uses a narrower and shorter roof, inclined side windows, larger front-window corner curves, sloping rear-quarter glazing and gently bowed end screens. The roof, pillars and window surrounds share their boundaries.

[Compare the original references directly against the revised model](../meridian-cabin-reference-match/README.md).

![Refined GLM Meridian](glm_meridian-front.png)

![Rear cabin](glm_meridian-rear.png)

[Front flat view](glm_meridian-front-flat.png) · [Rear flat view](glm_meridian-rear-flat.png) · [Side flat view](glm_meridian-side-flat.png) · [Door partly open](glm_meridian-door-partial.png) · [Door open](glm_meridian-door-open.png) · [Door interior](glm_meridian-door-inside.png) · [Game capture](glm_meridian-game.png)

[All eight Meridian references](../../references/glm_meridian/README.md#reference-photo-gallery).

## Rodeo Switchback

![Refined Rodeo Switchback](rodeo_switchback-front.png)

[Rear](rodeo_switchback-rear.png) · [Side](rodeo_switchback-side.png) · [Door partly open](rodeo_switchback-door-partial.png) · [Door open](rodeo_switchback-door-open.png) · [Door interior](rodeo_switchback-door-inside.png) · [Game capture](rodeo_switchback-game.png)

## Harrow Hookline

![Refined Harrow Hookline](harrow_hookline-front.png)

[Rear](harrow_hookline-rear.png) · [Side](harrow_hookline-side.png) · [Door partly open](harrow_hookline-door-partial.png) · [Door open](harrow_hookline-door-open.png) · [Door interior](harrow_hookline-door-inside.png) · [Game capture](harrow_hookline-game.png)

## Verification and limits

The Meridian cabin revision remains below the existing 60,000 body-triangle ceiling; the exact reviewed count and asset hashes are in `manifest.json`. Its asset checks include wheel radii; the rebuilt game passed the 650-frame door/driver transition check and a fresh 180-frame daylight capture with a clean graphics error queue.

The checks below cover the relevant geometry and entry/exit behavior. The bounded run is not a frame-rate benchmark.

- Asset validation checks finite geometry, unit normals, nondegenerate triangles, mapped UVs, wheel openings, dimensions, atlas format and required parts.
- `new_vehicle_models_tests` passed again for this cabin revision. The earlier fleet refinement also passed `vehicle_driver_pose_tests` and `license_plate_tests`.
- All three passed the actual game's driver-transition check: door sweep, entry, exit, blocked paths, cancellation, re-entry and exit-camera clearance.
- All three completed the final 180-frame daylight game captures with a clean graphics error queue.
- The existing `vehicle_snow_mesh_tests` failure remains on the unrelated Halcyon Sovereign windshield. The full suite is not claimed green.
- Tow equipment is modeled visually; towing gameplay and factory-paint respray profiles remain outside this refinement.

Rebuild with `python3 tools/make_1991_candidates_assets.py`. Render a saved model with `/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python tools/render_1991_candidates.py -- glm_meridian` (or the other model key). Editable sources are generated in the private `assets/models/vehicles/<model>/source.blend` folders. `manifest.json` records the reviewed asset hashes.
