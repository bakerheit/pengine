# Cruiser 91-C: Legacy Car 5 style pass

The 91-C is the one 1991 police study fitted against **LEGACY > CAR 5**, the
semi-realistic PSX sedan already in the tree. Car 5's local cooked mesh and
atlas are the reference; none of its geometry or texture is copied.

The car keeps its navy/cream livery, four sealed beams, push bar, lightbar,
A-pillar spotlight, whip antenna, shared wheels, driver pose and articulated
door and glass.

## What the measured gap was, and what closed it

Both cars were placed in the same world space using their real runtime fits and
measured off the cooked meshes. Five things separated them; all five moved.

| | before | after | Car 5 |
|---|---|---|---|
| side profile length lost from widest station to 15 mm off the body floor | 1.9% | **52%** | 56% |
| fender overhang past the tyre's outer sidewall | −0.001 m | **+0.099 m** | +0.169 m |
| tyre outer face as a fraction of body half-width | 100.1% | **90.6%** | 83.8% |
| arch mouth clear of the tread, fore and aft | 0.217 m | **0.060 m** | ~0.05 m |
| arch mouth clear of the tread, above | 0.157 m | **0.105 m** | 0.055 m |
| tyre past the flank at the 0.64 rad lock cap | +0.202 m | **+0.092 m** | +0.003 m |
| front-end depth across the lamp band | 0.014 m | **0.125 m** | ~0.12 m |
| UV: triangles with collapsed (degenerate) UVs | 76 | **0** | 4 |
| UV: area-weighted stretch | 6.82 | **1.34** | 1.33 |
| UV: triangles stretched past 4:1 | 39.5% | **0.2%** | 5.9% |
| atlas colours | 51 | **82** | 2503 |
| adjacent bodyside texels that are byte-identical | 93.2% | **44%** | 75% |

Overall stance was never the problem and did not move: roof height over length
is 0.277 against Car 5's 0.279, and over wheelbase 0.476 against 0.504.

### 1. The body is a section again, not an extrusion

`lower_ring` held width within 0.07 m and the top surface within 0.10 m for the
whole 5.42 m car, so the flank was an extruded slab with a roof on it.
`body_ring` replaces it: fourteen sides, and `floor`, `rocker`, `shoulder`,
`top_width` and `top` all vary per station. The floor sweeps up 0.285 m at the
nose and 0.275 m at the tail, the rocker tucks 0.09 m inside the beltline, and
the shoulder is two facets instead of one 78-degree crease.

### 2. The wheels sit under fenders

Half-track went 0.94 → 0.845 and the visible wheel radius 0.373 → 0.355.
What actually forced 0.94 was the `CentralChassis` filler box at x = ±0.58: the
front tyre's inboard swing at full lock reaches 0.548. That box is gone — the
lofted shell is a closed solid with its own swept floor and does the job — and
with it the constraint. `src/app/player_car_catalog.h` carries the matching
`physical_half_track` and `physical_wheel_radius`.

Every clearance is now derived in `municipal_cruiser_91c_spec.py`, not typed:

- `WELL_ALONG` = max(tyre radius, swept-at-lock) + a stated 0.06 m margin.
  Turning the wheel *shrinks* its longitudinal extent (0.348 < 0.355), so the
  binding case is straight ahead and the old 0.59 cut was pure daylight.
- `WELL_UP` = tyre radius + `BUMP_ALLOWANCE`. That allowance is the travel the
  wheel has **left** from its static pose, not the whole strut: at 1885 kg on
  this profile's spring rate the static sag is 0.056 m, so 0.105 m remains
  before the bumpstop. Clearing the full 0.16 m opened a hole in the flank.

### 3. Both ends have depth

Everything used to sit between z = 2.667 and 2.681 — 14 mm for grille, lamp
pocket, four sealed beams and bumper chrome. Now a boolean aperture cuts a bay
through each end cap, `bay_walls` builds its brow, floor and side returns, the
fascia sits 75 mm back with quad beams outboard and the grille filling the
centre, and a tapered bumper loft with a 175 mm chrome face stands out in
front. The push bar is two posts and two rails, deliberately clear of both
lamp-region centres.

`assets/shaders/vehicle_headlight_profiles.inc` and
`vehicle_brakelight_profiles.inc` moved with the lamps. Those tables compile
into both the C++ header and the shader, and `vehicle_headlight_origin` fails
traffic loading outright if they disagree with the mesh, so they are part of
the same change, not a follow-up.

### 4. The UVs are honest

`assign_shell_materials` classified by threshold (`abs(normal.x) > .30`), which
sent the shoulder facets to the side's Y/Z projection and smeared them. It now
picks the **dominant axis**, which bounds projection stretch at 1.73 for every
face by construction. `assign_uvs` additionally checks each face against its
receiver's plane and, when it is edge-on, projects it on its own best axes —
into a neutral `CLADDING` swatch for livery receivers, so a stray face shows
painted cladding rather than a slice of the word POLICE. Per-face fallbacks fit
the cell at one pixels-per-metre instead of stretching to fill it.

The pale hairline arc that used to cross the front fender was the arch lip's
collapsed UV band sampling the livery highlight line. It is gone.

### 5. The paint carries a value field

The old cook was three compounding crushes: a NEAREST 4× reduction that
discarded fifteen of every sixteen source pixels, a 48-colour no-dither
quantize, and a six-step navy ramp at a fixed 0–80 exposure. All shading came
from flat-shaded normals, so a 5.4 m flank rendered as one value.

Now the reduction is BOX, the quantize is gone, the ramps are longer (14 navy,
11 cream) with a 4×4 ordered dither, and the value **structure** is baked in
model space by `baked_value`: ground bounce low down, a dark reflection horizon
where the flank turns, a bright shoulder, a crowned top. The imagegen source is
kept for what the model cannot supply — local grain — so the art survives any
re-generation and cannot drift out of register with the geometry.

## Validators

The old "hood and deck stay broad **and nearly level** at z = ±2.48" check is
the one that made the car an extrusion: it forbade a hood slope or a deck drop.
It is replaced, not loosened. New checks, each verified to reject the
predecessor asset:

| check | rejects the predecessor with |
|---|---|
| `shape_ladder` | only 1.9% of the side profile has swept away (needs 30%) |
| `wheel_fit` | flank 1.049 vs tyre face 1.051 (needs 0.06 m of overhang) |
| `fascia_depth` | 41 mm of depth across the lamp band (needs 75 mm) |
| `uv_quality` | 76 triangles have collapsed UVs |
| `paint_structure` | 93.2% identical adjacent texels (cap 86%) |

Both sides of each tradeoff are pinned: `shape_ladder` also requires a rocker
between the axles, `wheel_fit` also caps the arch mouth and runs an exact
swept-tyre test over the steering range (the front axle only — the rear does
not steer), and the hood/deck presence check survives at ±2.05.

`tests/municipal_cruiser_91_tests.cpp` gained a 91-C-only `cruiser_91c_fender_fit`
that derives the tyre width from the shared wheel mesh rather than retyping it.
`tests/police_character_pose_tests.cpp` now reads the cruiser's anchors from
`player_car_definition` instead of the hard-coded `.94f` it had drifted from.

The triangle budget moved 1500 → 1560 because the body genuinely gained
surface. The closed body is **1,484** triangles and the atlas is 256×256 RGBA
with 82 colours. All nine cooked components keep their existing paths.

## Rebuild and review

```sh
python3 tools/make_municipal_cruiser_91c_assets.py --asset-lab
```

Editable source: `assets/models/vehicles/municipal_cruiser_91c/source.blend`.
Public atlas: `assets/textures/vehicles/municipal_cruiser_91c/body.png`.
Private cooked models remain ignored by Git.

## Verification actually run

- `python3 tools/make_municipal_cruiser_91c_assets.py` — Blender cook,
  validator and previews, exit 0. A baseline re-cook before any edit was
  byte-identical to the committed assets, so the pipeline is deterministic.
- `cmake --build build -j8` — clean under `-Werror`.
- `apricot_asset_lab` at yaw 40 / 90 / 220 on the rebuilt asset — 0 GL errors.
- `apricot --player-car municipal_cruiser_91c --start-driving --frames 420` —
  ran to the frame limit, 12 police cars in traffic on the same asset, GL error
  queue clean for the whole session.
- `apricot --player-car municipal_cruiser_91c --driver-transition-check
  --frames 1200` — "transition regression PASSED: door sweep clearance, staged
  entry, repeated input, blocked exit, blocked entry/exit cancellation, safe
  exit, re-entry, parked ignition once per entry", and the exit camera passed
  at 3.988 m minimum distance.
- `apricot --player-car municipal_cruiser_91c --police-check --frames 900` —
  captured red, blue and off lightbar phases, GL queue clean.
- Focused suites green: `municipal_cruiser_91_tests`, `emesh_reader_tests`,
  `vehicle_driver_pose_tests`, `police_character_pose_tests`,
  `vehicle_model_tuning_tests`, `emergency_lighting_tests`,
  `vehicle_snow_mesh_tests`, `new_vehicle_models_tests`.

### `tools/ci.sh`: 172/180, eight pre-existing failures

Same eight suites, same assertion messages, as the previous pass recorded
before this change. None touch the 91-C, and the working tree shows the
relevant sources dirty from other agents' concurrent work:

| suite | assertion | belongs to |
|---|---|---|
| `ostend_road_tests` | access path x ≈ −1979 | `src/city/roads.h` (dirty) |
| `road_name_tests` | `name != nullptr` | `src/city/roads.h` (dirty) |
| `ui_flow_tests` | `menu.item_count() == 2` | `src/app/game_ui.cpp` (dirty) |
| `terrain_determinism_tests` | scatter count 73 | `src/terrain/heightmap.*` (dirty) |
| `traffic_junction_tests` | longest junction wait | traffic-AI work in flight |
| `harrow_cityliner_tests` | body damage gain 1.18 vs 1 | another vehicle's profile |
| `pawn_shop_tests` | neighbour yaw sign | unrelated |
| `tractor_trailer_tests` | hauler brand index 3 | brand list is unchanged from HEAD; expects an ordering from before FANG was added |

Full log: `/tmp/apricot-overnight-20260909/ci.log`.

## Known, deliberate, and not yet done

- **Handling changed.** Track 1.88 → 1.69 m and wheel radius 0.373 → 0.355 m
  alter how the 91-C drives; roll stiffness scales with track². No golden test
  pins these — the cruiser suite asserts relationships, not absolutes — but it
  is a real feel change and has not had a feel-check by a human.
- At the 0.64 rad lock cap the front tyre still reaches 0.092 m past the flank.
  Car 5 reaches 0.003 m, but only because its player fit squeezes its track to
  74.6% of its half-width. Closing this fully would mean a 1.51 m track on a
  2.10 m body, which is not a full-size sedan.
- The arch still clears the tread by 0.105 m against Car 5's 0.055 m. That is
  suspension travel, not styling; closing it means accepting tread clip on
  ordinary bumps.
