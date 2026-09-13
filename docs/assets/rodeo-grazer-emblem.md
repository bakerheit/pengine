# Rodeo Grazer emblem

The Grazer uses the selected round-01 Rodeo R on its grille and tailgate:
a black bowl and upright, an orange diagonal leg, and a thin metal edge.
The two rectangular RODEO wordmark cards have been replaced with shallow
cast geometry. The counter and space between the legs are real openings.

The shape is authored in `rodeo_emblem()` in
`tools/rodeo_grazer_blender.py`. Mounts, dimensions and enamel colors live in
`tools/rodeo_grazer_spec.py`. The rear instance reverses its local X axis so
the mark reads correctly when viewed from behind the truck.

| Mount | Width | Height | Center, runtime X/Y/Z |
| --- | --- | --- | --- |
| Grille | 195 mm | 165 mm | 0 / 0.872 / 2.381 m |
| Tailgate | 218 mm | 185 mm | 0 / 0.824 / -2.531 m |

Both have a 6 mm extrusion and 1.4 mm edge bevel. The original 64 x 32 badge
cell is divided into solid black, orange and metal swatches within the same
256 x 256 RGBA atlas. All other atlas pixels, body geometry, normals and UVs
were compared with the pre-emblem assets and are unchanged. The two emblems
use 296 triangles total, replacing four card triangles; the closed body is
12,772 triangles, within its existing 15,000-triangle limit.

Regenerate from the repository root:

```sh
python3 tools/make_rodeo_grazer_assets.py
```

The cook runs `tools/validate_rodeo_grazer_assets.py`. It checks both the
closed body and articulated body for emblem colors, placement, front/rear
orientation, and the two open spaces in the R. The existing 68 swept-wheel
poses, cab seams, door openings, glass, and load-bed checks also remain active.

Render matching front/rear studio close-ups:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 \
  --python tools/preview_rodeo_grazer_emblems.py
```

This writes `build/grazer-emblems/front.png` and `rear.png`. These are studio
renders of the cooked Blender source. Separate real-renderer captures are
`build/grazer-emblem-front-engine.png`, `grazer-emblem-rear-engine.png`, and
`grazer-emblem-asset-lab.png`. The bounded world capture is
`build/grazer-emblem-game.bmp`.

The generators and public atlas are source assets. Cooked `.emesh` files and
`articulated.blend` remain private under ignored `assets/models/`.

Validation on 2026-09-12:

- `python3 tools/validate_rodeo_grazer_assets.py`: all structural and emblem checks passed.
- `ctest --test-dir build --output-on-failure -R '^rodeo_grazer_tests$'`: passed.
- `tools/ci.sh`: both guards, full build, and all 200 tests passed (359.32 seconds for tests).
- Asset Lab: 60 frames, 0 GL errors. Front/rear production-loader lab: 60 frames each, 0 GL errors.
- Isolated game run: 300 frames with `--player-car rodeo_grazer --start-driving --daylight --clear`; clean GL queue.
- Inspected matched before/after studio views, both production-loader views, and the world screenshot. The tailgate mark is visible from the chase camera.

The game run reported 17 streaming spikes above 4 ms during the 300 frames;
this is a startup/rendering smoke pass, not a performance benchmark.
