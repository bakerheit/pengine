# Spagatti Shū

The red exotic GT follows the supplied grand-tourer reference: connected rounded fenders, an arched coupe roof, a tall narrow horseshoe grille, four recessed round headlights and small rounded mirrors. Four round rear lamps, rear-quarter intakes and paired central exhausts complete the body. This finish keeps its original dimensions, handling and shared animated wheels.

| Contract | Value |
| --- | --- |
| Body bounds | 2.150 × 1.065 × 4.822 m (width, height, length) |
| Body triangles | 1,599; limit 1,600 |
| Glass | Six separate panes, 136 triangles total |
| Atlas | One 256 × 256 RGBA body atlas; 27 colors |
| Wheel anchors | X ±0.94, Y 0.43, front Z 1.45, rear Z −1.35 |
| Source axes | Blender X / forward Y / up Z; exported X / up Y / forward Z |

`tools/spagatti_shu_spec.py` owns the shape, UV receivers, wheel anchors and plate mounts. `tools/spagatti_shu_blender.py` constructs the shell, checks its manifold edges, welds the roof/window surrounds before adding inward thickness, and exports the opaque body separately from the glass. The native scene includes the actual shared wheels as display-only objects and named wheel/plate anchors.

The four headlamp buckets are cut into the measured fender surface. Their cutters use planar caps outside the full receiver to keep curved bodywork from covering the lenses. The front and rear glazing use matched vertical strips so the thin inner and outer surfaces cannot triangulate across one another.

The restrained atlas is authored against the final physical UV projections. The older imagegen source remains as reference and is not used by this cook. Paint, warm seats, dark trim and metal have their own atlas cells; glass uses the game's transparent material. The shared wheel texture and gameplay rig are unchanged. The doors remain static, as in the previous Shū.

The nose wraps back into both front corners, with a shallow hood crown and a continuous roll into the fascia. The grille and lamp buckets follow the same deformation as the body. Each curved lens uses one continuous UV projection across its triangles. The offset front plate sits on a rigid landing cut into the finished fascia; the rear plate sits between the tail lamps. Both backings and raised surrounds are blank. The game generates registration text. Source coordinates are declared in `PLATE_MOUNTS` and matched in `vehicle_plate_mesh.h`.

## Rebuild and check

```sh
python3 tools/make_spagatti_shu_assets.py
python3 tools/make_spagatti_shu_assets.py --validate-only
python3 tools/preview_spagatti_shu.py
```

Native source: `assets/models/vehicles/spagatti_shu/source.blend`. Cooked meshes stay in the ignored model directory; scripts and the body atlas are tracked. `--blockout` writes an unadorned shape cook; rerun the normal command to restore the full model.

The cooker probes 1,032 positions from the actual shared wheel mesh at five front steering angles. The validator checks the budget, bounds, real wheel openings, retained center hood/deck, six separate panes, clear window apertures, atlas format/palette, 18 plate-backing clearance samples, and 20 front-facing rays that must reach the actual lens material on all four headlights. Reports and matched preview sheets are under `build/`; the initial rework is retained under `build/spagatti-rework/`; the supplied reference, prior model, and current captures are under `build/spagatti-reference-pass/`.

For the exact player loader and glass material:

```sh
build/bin/apricot_mistral_driver_lab --car spagatti_shu --player-car --view front --frames 60 --screenshot build/spagatti-reference-pass/engine-front.png
```

The new round headlight regions share one table between the CPU and shader in `assets/shaders/vehicle_headlight_profiles.inc`.
The Shū-specific neutral-color check in `lit.frag` keeps adjacent red paint out
of the headlight glow. Glazing remains attached to the static body; no new driver or door animation is introduced by this asset pass.

## Verification before the reference refinement

- Cooked asset checks passed, including 1,032 wheel samples and 18 plate samples.
- Six focused vehicle, plate and snow suites passed after the final lamp change.
- Asset Lab plus the production player loader rendered the body, windshield and rear with zero GL errors.
- Daylight and night gameplay captures use an isolated save, a staged parking space and 300 frames each.
- Full local CI: 202/203 suites passed. `police_chase_tests` failed at line 654 on the lane-following heading-snap case. No police or traffic simulation code was changed by this model pass.

## Reference refinement verification

- Production cook: 1,577 body triangles, six panes / 136 glass triangles, 27-color atlas. All geometry, lamp, glass, wheel and plate checks pass.
- Fresh builds: `apricot`, `apricot_asset_lab`, `apricot_mistral_driver_lab` and the six focused test targets.
- Focused suites: `vehicle_interaction_tests`, `vehicle_snow_shader_tests`, `vehicle_snow_mesh_tests`, `new_vehicle_models_tests`, `vehicle_model_tuning_tests`, `license_plate_tests` — 6/6 passed.
- Production player-loader captures: front, rear and windshield, 60 frames each, zero GL errors. The windshield is transparent, the four lamp buckets are visible, and the roof frame has no gaps in these views.
- Daylight and night gameplay: 300 frames each with isolated saves, both report a clean GL queue. Night inspection confirms that the four lens faces illuminate without lighting adjacent red paint.
- Startup streaming spikes were logged (16 day frames / 13 night frames above 4 ms); these captures are visual and stability checks, not a performance pass.
- The earlier full CI result above remains the broader-suite evidence; full CI was not repeated for this refinement.

Captures and logs: `build/spagatti-reference-pass/`.

## Rounded front pass

The front-corner sweep is now 0.310 m across the measured fascia samples (previously 0.158 m); the center hood sample is 0.918 m high (previously 0.817 m). These are cooked-surface measurements, checked by the asset validator. The front plate mount moved to `(0.52, 0.33, 2.324)` to stay flush with the reshaped bumper. The final shell receives a second manifold and steering-clearance check after the front is shaped. Hidden seat/tunnel undersides were removed to keep the body within 1,600 triangles.

The current shape, lens UV, lamp visibility, wheel and plate checks pass. The prior flat nose is rejected by the new shape check. `license_plate_tests`, `vehicle_snow_mesh_tests` and `vehicle_model_tuning_tests` pass. Matched views and runtime logs are under `build/spagatti-rounded-front/`.

The rounded-front production-loader capture and Asset Lab each complete 60 frames without GL errors. The final day and night runs each complete 300 frames with clean GL queues; inspection confirms the rounder fascia and illuminated lenses without red-paint bleed. Full CI was not repeated.
