# PIZAZ Constant procedural source

Procedural reconstruction of the generated 1991 PIZAZ Constant [four-view reference set](../../docs/design/references/pizaz-constant.md). The model is authored as an editable Three.js hierarchy and exported as GLB, then opened in Blender for FBX and review renders. The rear design follows its generated rear view; the underside and far-side detail remain inferred. The runtime derivative is cooked separately for Apricot.

## Shape contract

All dimensions are metres. The source uses +X for driver-left, +Y up and +Z forward, with the ground at Y=0.

| Measurement | Target |
| --- | ---: |
| Length | 4.62 |
| Body width | 1.79 |
| Roof height | 1.32 |
| Wheelbase | 2.76 |
| Wheel track | 1.56 |
| Tire radius | 0.32 |
| Front axle Z | 1.38 |
| Rear axle Z | -1.38 |
| Wheel arch radius | 0.365 |
| Hood rear / front edge | 0.87 / 0.65 |
| Deck front / rear edge | 0.89 / 0.75 |
| Beltline | 0.87 |

Visual contract: long wedge hood, compact four-door cabin, short deck, slim rectangular front lamps and narrow grille, five-spoke wheels, a straight shoulder crease and burgundy metallic paint. Body, glass, doors and wheels remain separately named. The four generated flat views guide the side silhouette, front spacing, rear lamps/plate, and top-surface widths. The original three-quarter image remains the design identity anchor.

Run `npm install`, then `npm run check` and `npm run export`. The export writes `assets/models/vehicles/pizaz_constant/pizaz_constant.glb` from the repository root. That directory is ignored because cooked/editable model files stay private; this source is reproducible. Run `/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python tools/pizaz_constant_threejs/render_blender.py` from the repository root to import the GLB, save `.blend` and `.fbx` handoffs, and write front, rear, side, top, three-quarter, and open-door review renders under `build/pizaz-constant-img2threejs/`.

## Rebuild after visual rejection

The rejected pass had oversized rear doors, floating window pieces, flat skins, an overly tall body and slab fascia. It is preserved under `build/pizaz-constant-img2threejs/rejected-modular-pass/` for comparison.

`surface.ts` now owns the shared side contours. Door skins and fixed quarters sample the same curved surface, using a regular grid clipped to their outlines. Door shells are 47 mm deep, window frames have 25 mm returns, and the rear quarter glass stays on the fixed cabin. The rear door slopes forward toward its lower corner and ends ahead of the rear wheel opening. The roof, beltline and seams were remeasured from the side reference. The bumper module uses wraps around the corners.

The source is split into `bodyModule.ts` (hood, deck, sides, fenders, wheel openings and rockers), `cabinModule.ts` (roof, glass, interior, mirrors and four hinged doors), and `fasciaWheelModule.ts` (front/rear fascia and four wheels). `createPizazConstant.ts` joins them and defines materials. The GLB keeps named parts and an `open_four_doors` demonstration animation.

Export checks reject non-finite vertex positions, missing doors/wheels, a rear door extending into the fixed quarter, or loss of the specified door depth. Blender renders true orthographic front/rear/side views, top, front/rear three-quarter and open-door views. The Blender import report records the current mesh/triangle counts; these checks do not establish visual acceptance.

The older sculpt-spec and review reports in the build directory describe earlier passes. They are not acceptance evidence for this rebuild. This remains an editable modeling asset with simple materials and an inferred interior. The runtime derivative preserves this source and is generated with the command below.

## Apricot runtime derivative

From the repository root, run `python3 tools/make_pizaz_constant_assets.py` after installing this folder's npm dependencies. `--skip-source` reuses the existing GLB. The cooker imports the approved geometry, removes redundant tessellation, creates a 256×256 semantic atlas, and writes runtime `.emesh` parts plus `runtime.blend` beside the editable source. It does not overwrite the approved FBX or Blender handoff.

The body is about 37,400 triangles. Wheels, front doors and six glass groups remain separate; rear doors stay closed in the current game rig. Both sides of each custom wheel have rim faces because the runtime shares axle meshes without mirroring them. Paint occupies the upper atlas half, away from glass, trim and lamps.

`python3 tools/validate_pizaz_constant_assets.py` checks format, finite geometry, UVs, bounds, wheel centering, outer wheel-opening samples and the 40,000-triangle body ceiling. Reports are written to `build/pizaz-constant-cook.json` and `build/pizaz-constant-fit-report.json`.

The car is registered as **PIZAZ → CONSTANT**. The game CLI key is `pizaz_constant`. Runtime integration uses the native four-wheel rig and animated front doors; the four-door demonstration remains available in the editable GLB.
