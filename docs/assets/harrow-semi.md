# Harrow Hauler and Freight Trailer

Original PSX-style conventional day-cab tractor and detachable ribbed box trailer.
The tractor uses oxblood paint, an ivory belt stripe, an upright split windshield,
a tall grille, broad open front fenders, silver fuel tanks, twin exhaust stacks,
and an exposed slotted fifth wheel. The trailer has matching freight paint,
tandem rear axles, paired cargo doors and an underride guard.

## Reproduce

From the repository root, with Python 3, NumPy, Pillow and Blender installed:

```sh
python3 tools/make_harrow_hauler_assets.py --asset-lab
python3 tools/make_harrow_freight_trailer_assets.py --asset-lab
python3 tools/preview_harrow_semi.py
```

Blender is invoked at `/Applications/Blender.app/Contents/MacOS/Blender`.
`--asset-lab` uses the existing `build/bin/apricot_asset_lab`; it does not build.
`--blockout` produces plain-material silhouettes and shared-wheel contact sheets.
`--texture-only` regenerates the atlas and validates/previews the current mesh.
The two small per-model entry points share `harrow_semi_blender.py` and
`make_harrow_semi_assets.py`; their specs keep the stable dimensions separate.

Each model generates an editable `source.blend` and cooked `body.emesh` under
`assets/models/vehicles/<slug>/`. These private model directories remain ignored.
Each public `assets/textures/vehicles/<slug>/body.png` is one opaque 256x256 RGBA
atlas. The static export has one joined body and no tire or wheel geometry.
Four Blender wheel-anchor empties and one coupling empty record attachment data.
Runtime loads the body plus the existing common wheel asset separately.

## Runtime contract

All dimensions and anchors are **native world meters**, X lateral, Y upward,
+Z forward. Preserve identity scale. The standard car wheelbase/track fitting
must not shrink these assets. Convert +Z source-forward to chassis -Z-forward
with the same 180-degree Y rotation as the existing car visuals.

| Item | Hauler tractor | Freight trailer |
|---|---:|---:|
| Slug | `harrow_hauler` | `harrow_freight_trailer` |
| Triangles | 936 | 608 |
| Measured X bounds | -1.25 to +1.25 | -1.255 to +1.255 |
| Measured Y bounds | .38 to 3.25 | .28 to 3.805 |
| Measured Z bounds | -3.31 to +3.30 | -5.03 to +5.003 |
| Wheel X anchors | +/-1.10 | +/-1.10 |
| Wheel Y anchors | .50 | .50 |
| Front/rear wheel Z | +2.05 / -2.05 | -3.00 / -4.20 |
| Tire radius | .50 | .50 |
| Coupling X, Y, Z | 0, 1.28, -1.90 | 0, 1.28, +4.00 |

The tractor has a single driven rear axle and front steering axle. The trailer's
four wheel nodes represent two fixed tandem rear axles. Its front axle field
has negative Z intentionally; do not infer steering from the field name.
At zero articulation the trailer origin is 5.90m behind the tractor origin.
Trailer front then reaches tractor source Z -.897. The cab rear begins at
source Z +.05, leaving approximately .95m of straight clearance.

Landing gear is omitted from the body so gameplay can retract it. Mounts sit at
X +/- .85, Z +2.40, Y 1.28..1.42; draw moving legs/feet separately below them.
The kingpin is below the front box floor, and the fifth-wheel slot faces toward
the tractor rear for backing under the trailer.

## Validation evidence

The generator validates finite vertices, indices, triangle areas, declared
triangle budgets, symmetry, measured bounds, UV ranges and atlas format. It
checks broad hood/box-floor coverage and samples body surfaces against the
actual shared-wheel tire cylinder at 3.5cm maximum spacing. Tractor front tires
are checked through +/- .82 radians of steering (68 total wheel poses); trailer
wheels are checked in four fixed poses. Both pass with a .50m tire radius and
measured .14917m tire half-width.

`build/<slug>-fit-report.json` contains exact measurements, hashes and named
checks. `build/<slug>-uv-guide.png` overlays cooked UV triangles on the atlas.
`build/<slug>-preview.png` shows six shared-wheel views, including full lock.
`build/harrow-semi-combined-preview.png` shows the measured joint straight and
at 35 degrees of articulation. These previews were opened and inspected.

Both models also passed three 60-frame Asset Lab runs: native body, shared-wheel
fixture at full steering lock, and rear view. All six runs reported zero GL
errors; the resulting `build/<slug>-engine-*.png` screenshots were inspected.
The combined fixture and 512x256 QA atlas live only under `build/`; production
bodies remain wheel-less with 256x256 textures. Gameplay coupling, collision,
parking location and interaction checks are owned by the runtime integration.
