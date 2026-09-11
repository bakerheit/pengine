# Car 8 ambulance paint

Created 2026-09-11 with the Codex built-in `image_gen` tool, driven headless as
`codex exec --enable image_generation` (the feature is off by default in
`codex exec`; it is on in the interactive app). Original generated raster
material. No reference photographs, real ambulance services, agency liveries,
stock assets or external logos were supplied. The existing Car 8 atlas
established every panel line, window, lamp and reflector; the generation pass
only laid paint on top of it.

## Assets

| File under assets/textures/vehicles/car8/ | Size | Role |
| --- | --- | --- |
| ambulance.png | 128 x 128, RGB opaque | the runtime paint |
| ambulance-flank-reference.png | 960 x 408, RGB opaque | generated flank panel, kept for re-cooks |
| ambulance-rear-reference.png | 500 x 1020, RGB opaque | generated rear-door panel, kept for re-cooks |

Cook with `python3 tools/make_car8_ambulance_texture.py` from the repository
root. The two references are stored downscaled from what `image_gen` returned
(1923 x 817 and 878 x 1791); the cook only ever samples them at 120 x 51 and
25 x 51, so the stored size is already far above what it needs.

## Method, and why it is not a straight paste

Both panels were generated as **edits of crops of the stock atlas**, not as new
artwork, so they came back with the van's geometry where it started. The cook
folds each one in as a ratio against the crop it was generated from:

    out = base * (generated / source)

Where the edit changed nothing the ratio is 1 and the stock pixel survives
untouched. Only where paint was laid down does the ratio carry it. This matters
because the panels arrive at 16x and 20x the atlas resolution: a plain
downsample-and-paste turns every one-pixel panel line into grey mush, and at
128 x 128 those lines are most of what the model has. Over glass and tyre-black
a ratio says nothing useful, so the cook switches to an additive term below a
luminance of 40.

The flank and rear panels leave the roof, nose, hood and cab front untouched.
Those are lifted to the same white by a tone curve whose control points were
measured off the unpainted areas of the generated flank panel -- stock grey 172
lands on 232 -- rather than picked by eye, so the shell does not read two-tone.
The tone curve is deliberately flat below about 60 so glass and rubber stay put,
and it is applied only to pixels `body.emesh` actually samples, which is how the
shared wheel's tyre and hub in the atlas's top-left corner stay out of it.

## Alignment check

The generated flank came back at 1923 x 817 against a 960 x 408 input: aspect
2.3537 against 2.3529. The black wheel-arch cut-out's centroid sits at
(0.8460, 0.9107) of the frame in the generated panel against (0.8458, 0.9122) in
the source -- under a quarter of one atlas pixel of drift. The rear came back at
878 x 1791 against 500 x 1020, aspect 0.49023 against 0.49020.

## Inspection

- Both generated panels were opened and viewed before compositing.
- The cooked atlas was rendered on the real Car 8 body by
  `build/bin/apricot_asset_lab` at yaw 200, 320 and 20 -- 30 frames, 0 GL
  errors each -- and each render was viewed. The van reads as an ambulance from
  the front three-quarter, the rear three-quarter and the side.
- Flank confirmed intact after the composite: body ribs, cab door and handle,
  door window, windscreen quarter glass, wheel arch, roof drip rail, both amber
  marker lamps and the red reflector. Rear confirmed intact: window, centre door
  seam, frame lines, both round tail lamps, the lower-left dark rectangle.
- "AMBULANCE" is spelled correctly and is legible at 120 px of atlas width.

**Known, and inherent to the imported atlas:** Car 8's flank is one UV island
shared mirrored by both sides, so AMBULANCE reads correctly on one flank and
backwards on the other. The rear island mirrors too, which is why its single
generated Star of Life lands on both doors -- there it happens to read as
intentional. `mail.png`'s envelope mirrors the same way. Removing the lettering
and leaning on the Star of Life alone is the only fix that does not mean
re-UV-ing an imported body.

## How it is wired

`PlayerCarId::LegacyCar8Ambulance` is a catalog row, so the paint is selectable
from the dev menu: F1 -> VEHICLE -> CHOOSE BRAND -> LEGACY -> CAR 8 AMBULANCE.
The id is **appended** to the enum, after `LegacyCar5Next`, so every existing
checkpoint's model id still means what it meant.

The row points at Car 8's own mesh and carries Car 8's fit numbers, because it
is a repaint and not a second vehicle: the mesh path is what selects a model
folder for lamps, snow and the engine note, so sharing it is the point. Keep the
two rows' fit numbers equal if either ever changes.

`vehicle_model_tuning.h` gives it its own performance row rather than Car 8's.
Same shell and running gear, but loaded with a stretcher, cabinets and crew:
1900 kg against Car 8's 1480, more torque to move it, less of everything mass
takes away, stiffer springs, and a centre of mass 20 mm higher because the load
sits high. That row is a first pass from the numbers, **not** yet driven.

**One papercut this introduced.** `--player-car` in `src/main.cpp` matches on a
mesh-path folder and returns the first hit, and these are the first two catalog
rows to share a mesh folder. So `--player-car car8` still gives plain Car 8 and
the ambulance has no `--player-car` spelling at all. The dev menu reaches it;
the QA flag does not. Fixing that means changing how that flag resolves a car,
which is main.cpp's call to make.

It is still **not** in `kCar8Paints` in `src/app/traffic_visual.cpp`. That list
is the ambient box-truck palette, and a vehicle drawn from it spawns as ordinary
traffic with no lamps, no siren and no emergency behaviour.

## Exact final prompts

These are the prompts `image_gen` received, as recorded in the Codex session
rollouts.

### Flank

EDIT the provided image. Produce exactly ONE image. Use case:
precise-object-edit. This is an existing flat UV atlas panel, 960 x 408
(~2.35:1), of the side of a boxy 1990s step van. Repaint the existing pixels as
a city ambulance livery. Preserve exact framing and aspect ratio. CRITICAL
geometry and UV invariants: keep EVERY existing feature in EXACTLY its existing
position, silhouette and shape: all horizontal body ribs, cab door outline and
handle, black door window, black windscreen quarter glass, black wheel arch
cut-out at bottom right, roof drip rail across top, small amber marker lamps at
top left and top centre, amber lower right lamp, red reflector at lower left,
all panel edges and seams. Do not redraw or move geometry. Retain the original
low-resolution pixelated forms. Change only paint and painted graphics. Bright
clean white body instead of grey. One bold saturated orange-red horizontal
safety stripe across full panel length at about 60 percent of image height,
thickness about one eighth of image height, with thin navy blue pinstripe on its
upper edge; stripe crosses cargo box and continues across cab door while handle
and underlying rib relief stay in place. Above stripe on left cargo-box area,
large blue Star of Life, six-armed asterisk containing white staff-and-serpent
at centre, intended size about two thirds panel height, fitted to available
paintable area above stripe. To its right, above stripe and entirely before cab
door, render ONLY the word 'AMBULANCE' in bold navy blue block capitals, filling
remaining cargo-box space. Ensure complete correctly spelled word. Gritty
stylized late-1990s console vehicle texture, solid flat high contrast colors,
mild paint wear and subtle grain, readable from distance. Flat orthographic
albedo/diffuse texture only, evenly lit. NO perspective, no rotation, no crop,
no background, no added vehicle parts, no drop shadows, no gloss/specular
highlights, no vignette. No other text, numbers, city/agency names, logos,
watermark or fine print. Output only the edited UV panel.

### Rear doors

EDIT the supplied image. Produce exactly ONE image. This is the edit target, a
500x1020 flat UV texture panel for the REAR FACE of a boxy 1990s step-van. Keep
its exact aspect ratio, framing, outline, and intentionally horizontally
squashed UV geometry. Repaint only the body and add ambulance livery. Preserve
EVERY existing feature at EXACT same position, size, shape: dark left window,
vertical center door seam, all horizontal frame lines, two small round lamps at
lower right, dark rectangle at lower left, amber marker lamps along the very
top. Do not move, resize, erase or redesign any hardware. Body paint clean
bright white instead of grey, retaining subtle low fidelity panel detailing. Add
one bold horizontal saturated orange-red #E04A18 safety stripe across full width
at about 60 percent of image height, about 1/12 panel height thick. Thin navy
#1E3C78 pinstripe along its upper edge. Above stripe add navy-blue Star of Life
emblem, six-barred asterisk with white staff and serpent in center,
approximately half panel height. Center on the door area as far as possible
while absolutely avoiding the window; use the open white door area to the right
of window and preserve window fully. Emblem may share intentional horizontal
squash of UV layout. Existing seams remain legible. Gritty stylized late-1990s
console vehicle texture, flat solid high contrast colors, mild paint wear and
subtle surface grain, matching coarse pixelated source. Flat orthographic
albedo/diffuse only, evenly lit. No perspective, rotation, crop, background,
drop shadow, gloss, specular highlights or vignette. No text, numbers,
real-world agency names, extra logos or watermark.

## Source output locations

- Flank: `~/.codex/generated_images/01a0906d-d0b8-7063-9ab5-b60bcaa5668d/exec-1134c206-8c70-4332-8f2d-a8ab76dc4183.png`
- Rear: `~/.codex/generated_images/01a09070-2fee-7060-baa3-a5d52a6b1973/exec-96561832-d6f7-4344-af81-db0eb4af7a25.png`
