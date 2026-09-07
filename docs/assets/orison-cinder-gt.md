# 1994 Orison Cinder GT

An original fictional two-door sports coupe. Petrol teal paint, a low front-engine
hood with closed pop-up headlights, a compact fastback cabin, amber markers,
segmented rear lamps, twin exhausts and a low bridge wing establish the period.

The car is playable through **F1 > Vehicle > Choose Car > Orison > Cinder GT**,
or with `--player-car orison_cinder --start-driving`. It has model-specific
handling, collision, wheel fit, headlamps, brake lamps, engine pitch and save
support. Existing checkpoint model IDs remain unchanged.

The current model uses the standard E entry/exit path. Its closed pop-up lids
are static and its lower fixed lenses provide the runtime headlights. It does
not yet have an animated door/interior or a random traffic spawn entry.

## Files

- `build/orison-cinder/orison_cinder_gt_1994.blend`: editable scene with packed
  textures, studio lighting, a camera, and separate shared-wheel previews.
- `build/orison-cinder/orison-cinder-design-sheet.png`: four rendered views.
- `assets/models/vehicles/orison_cinder/body.emesh`: single static body export.
- `assets/textures/vehicles/orison_cinder/body.png`: one opaque 256x256 atlas.
- `build/orison-cinder-fit-report.json`: structural and wheel-clearance results.

Source and cooked private meshes stay in ignored output directories. The scene
retains named vertex groups for the body panels, cabin, fascia, wing and mirrors.
Select the BODY object to edit the car; the studio and preview wheels are separate.
The closed headlights are styled into the hood atlas; they are not animated.

## Shape contract

Coordinates in Blender are X across, +Y toward the nose, and +Z up. Units are metres.
The Cinder exporter maps `(x,y,z)` to `(-x,z,y)`, preserving handedness and
readable lettering with engine +Y up and +Z forward. Explicit outward normals
keep its single-layer glass visible with runtime back-face culling.

| Measure | Value |
|---|---:|
| Main shell length | 4.42 m |
| Main shell width | 1.90 m |
| Width including mirrors | 2.14 m |
| Roof height above ground | 1.245 m |
| Wheelbase | 2.54 m |
| Front / rear axle Y | +1.30 / -1.24 m |
| Wheel anchor X | +/-0.80 m |
| Wheel center Z / radius | 0.34 / 0.326 m |

The chin and exhaust tips extend beyond the main shell. Four preview wheels use
Apricot's actual shared wheel asset; no wheel geometry enters the body export.

## Rebuild and inspect

From the repository root, with Python, Pillow, NumPy and Blender installed:

```sh
python3 tools/make_orison_cinder_assets.py
python3 tools/validate_orison_cinder.py
build/bin/apricot_asset_lab --model models/vehicles/orison_cinder/body.emesh --texture textures/vehicles/orison_cinder/body.png --yaw 32 --frames 120 --screenshot build/orison-cinder/engine.png
```

The validator checks mesh indices, finite data, nonzero triangle areas, triangle
budget, symmetry, UV bounds, atlas format, four open arches, hood/deck coverage,
and sampled tire clearance across 36 wheel poses up to 0.82 radians front lock.
It is a geometric fit check, not a gameplay collision or handling test.

Gameplay regression coverage includes `ui_flow_tests`, `save_game_tests`,
`vehicle_model_tuning_tests`, the Cinder lamp checks in `emesh_reader_tests`, and
`audio_vehicle_runtime_tests --cinder` for rendered engine audio.
