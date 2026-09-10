# TacoTaco — Sixth Street

Historical record: this location is now [Freaky Franks](freaky-franks-pinatty.md).
The previous assets and kitchen-performance evidence below are retained for reference.

TacoTaco is a second, independently cooked BurgerPiz variation on Sixth Street,
between Wren and Cinder. Its centre is world **399.957, -98.832**, ground **12 m**
(Pinatty grid east 322, south -93). It sits three blocks south of BurgerPiz.
The original BurgerPiz remains on Ninth Street with its original cooked assets.

The variation has a teal roof and trim, cream masonry, coral accents, tinted
booth upholstery, and two extruded **TacoTaco** signs in Arial Rounded Bold
with warm cream illumination for night readability.
Both original BurgerPiz letter meshes are replaced. Taco menu photography
replaces the burger menu texture while retaining the existing board geometry
and UV layout. The new atlas and its prompt are documented in
`docs/assets/tacotaco-menu.md`.

Both locations use the same full-size furnished shell, open entrance leaves,
transparent windows, 816 collision boxes, floor/paving supports, and 37 real
ceiling lights. The shared loader now accepts a site and asset root, so each
restaurant loads its own materials and geometry. TacoTaco has 51,600 triangles
in 58 material groups, including the new text meshes.

The 60 × 38 m lot connects to Sixth Street (road 36) with a six-metre driveway.
It has a terrain flatten/scatter exclusion patch, interior streaming volume,
atlas footprint, and teal food marker. The supplied local parking stripes and
landscaped islands remain part of the variant.

## Cook and visit

The generated menu must be present at
`assets/models/buildings/tacotaco/menu_atlas.png` before cooking. From the repo:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 4 \
  --python tools/cook_burgerpiz.py -- \
  assets/models/buildings/burgerpiz/source/BurgerPiz/Models/BurgerPiz.glb --tacotaco

./build/bin/apricot --start-at 370 -72 --start-player-at 363 -85 \
  --start-heading -66 --road-start --daylight --clear \
  --save-file /tmp/apricot-tacotaco-visit.json
```

The cooker writes only the chosen variant's private asset folder. Source
geometry and both cooked model sets stay inside ignored `assets/models/`.

## Validation

- `burgerpiz_tests --require-assets` passes for **both** sites: assigned street
  access, ground/road bounds, textured mesh loading, entry, dining aisle,
  counter approach, return to street, and blocked-route controls. It also
  verifies that both variants retain identical interior collision and lights,
  and that TacoTaco has its own lettering and menu atlas.
- `authored_city_layout_tests`: 80 active lots, 3,160 pairs, no overlaps.
- `building_access_tests`: 53 of 53 entrances connected.
- Rebuilt `apricot` and `apricot_map_lab`; 300-frame day and night runs and the
  atlas render complete with clean GL queues.
- Screenshots: `build/tacotaco-corner.png`, `build/tacotaco-counter.png`,
  `build/tacotaco-map.png`. Logs use the same `build/tacotaco-` prefix.

## Kitchen rendering cost (2026-09-08)

Two sources of wasted shading were fixed without changing the cooked assets:

- Wide ceiling spotlights were binned using only an enclosing cone pyramid.
  The shader uses radial range, so a seven-metre light with outer cosine 0.35
  was assigned an 18.7-metre base radius. The grid now also clips against the
  radial spherical sector's bounds. There is still no light-count cap.
- Both restaurants opt their opaque materials into an early opaque pass.
  Their walls now write depth before the outdoor city is shaded. Each surface
  still draws once, with the same material batches. Glass and surface overlays
  retain their existing passes.

Same counter camera, 2560 × 1440, clear weather, frozen midnight simulation;
the existing interactive game was suspended during each benchmark and resumed
afterward. `--lighting-benchmark` uses 300 warmup frames and alternating light
phases, with GPU queries covering world and character rendering:

| Measurement | Before | Final (two runs) |
| --- | ---: | ---: |
| GPU world + people, lights on | 4.924 ms | 3.867 / 3.989 ms |
| GPU world + people, lights off | 3.316 ms | 3.407 / 3.551 ms |
| CPU light grid + upload, median | 0.374 ms | 0.276 / 0.269 ms |
| Light references | 205,149 | 127,347 |
| Maximum lights per cell | 37 | 23 |

This is a 19–21% reduction in the measured lights-on GPU pass, not a claim
of equivalent whole-game FPS improvement. Whole-frame averages remained
8.5–8.9 ms and included streaming spikes. The reported hail scene and its
higher display resolution were not reproduced in these controlled timings.

```sh
./build/bin/apricot --start-at 370 -72 --start-player-at 399.8 -106.89 \
  --start-heading -96 --road-start --frames 900 --lighting-benchmark --clear \
  --save-file /tmp/taco-perf-qa.json --screenshot build/taco-perf-qa.bmp
```

The benchmark enforces 1,200 total frames. Evidence logs are
`build/taco-perf-isolated-before.log`, `build/taco-perf-after.log` (range fix
alone), `build/taco-perf-order.log`, and `build/taco-perf-final.log`.
The light-grid and emergency-lighting tests pass. The new grid regression
sweeps broad and narrow cones, rotated cameras, near-plane crossings, odd
viewport sizes and radial boundaries. Both final benchmark runs report clean
GL queues. The counter screenshots before/after the draw-order change are
pixel-identical. A separate 300-frame daylight view toward the dining room
and windows also passed on the non-instanced path, with clean GL output
(`build/taco-perf-outward.log`, `build/taco-perf-outward.png`).
