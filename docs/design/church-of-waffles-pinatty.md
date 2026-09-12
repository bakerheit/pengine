# Church of Waffles — Eighth Street

The Church of Waffles is the fourth imported restaurant in Pinatty Row and the
first built from a **second** supplied shell. BurgerPiz, Freaky Franks and
TacoMaco are three brands on one model; this is a different building — a long,
shallow diner with a wraparound glazed dining room, a service counter with
stools, a working kitchen line and staff rooms behind it.

It takes the last free block on the Wren/Cinder column, between Eighth Street
and Ninth Street, so the column now reads TacoMaco (Fourth), Freaky Franks
(Sixth), **Church of Waffles (Eighth)**, BurgerPiz (Ninth).

- Site centre: world X/Z **412.919, -222.153**, ground **12 m**.
- Pinatty grid centre: east **322**, row **-217** (mid-block).
- Parcel: **60 × 38 m**, aligned with the Pinatty grid, front facing local +Z.
- Main entrance: approximately world **412.29, -219.55**.
- Its six-metre driveway joins **Eighth Street (road 38)** at local X -24.
- The restaurant appears on both the atlas and radar with a purple food marker.

## Supplied model

The source is `Quequis_House.rar`, supplied locally by the user. The cooker uses
its GLB and keeps 520 objects at their original metre scale: 78,664 triangles in
86 material groups. It retains the shell, the roof, the wraparound storefront
glazing, the furnished dining room, booths, the counter and its stools, the
kitchen with grills, ovens, the waffle irons and the extraction hood, the
restrooms, the stock room, the ceiling lighting and the two lamp posts that
stand in the forecourt.

The original scene also contains a large demo city — a gas station, a car wash,
a bridge and two hundred houses. The cooker selects only whole objects inside
source X [-20, 23], Y [-15, 13.5]; it records both inclusion and exclusion lists
in the private asset manifest. No source geometry is decimated.

**Twelve foliage objects are dropped on purpose,** and listed separately in the
manifest as `foliage_cards`. The supplied landscaping is alpha-cutout cards, and
the imported-restaurant render path has no cutout material: each card drew as an
opaque black rectangle standing in the forecourt. Widening the cooked material
format would mean re-cooking the three BurgerPiz-shell brands that read the same
files, which is not worth a few shrubs.

The scene is turned to face Eighth, using:

```
site-local = (-(source.x - 2.11), source.z + 0.25, source.y + 3.33)
```

Both sign swaps keep the basis right-handed, so no winding is reversed. Original
base-colour textures and UVs are retained. Glass uses the engine's transparent
material. The supplied ceiling emission texture supplies visible lamp glow; 23
matching ceiling positions drive real local lights.

The archive extraction, source models, cooked meshes, textures and manifests
remain under ignored `assets/models/buildings/churchofwaffles/`. Reproduce from
the repository root, after extracting the archive into that folder's `source/`:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 4 \
  --python tools/cook_church_of_waffles.py -- \
  assets/models/buildings/churchofwaffles/source/Quequis_House/Models/Quequis_House.glb
```

## The purple rebrand

The supplied building is a yellow-and-cream diner. The cooker repaints it:

| Source material | Becomes |
| --- | --- |
| `Y` (the shell's accent band) | vivid violet `.42 .12 .72`, the sign ground |
| `RooftilesMetal` (the awning) | flat violet `.56 .27 .88` |
| `Plaster`, `Plaster_01`, `Tile` | lilac and amethyst tints over the supplied images |
| `Roof_tiles` | plum `.50 .31 .72` |
| `Armchair`, `Chair` | flat purple booths and stool pads |
| `Floor`, `Wall_W`, `Office_Ceiling` | pale lilac tints |

Textured materials keep their base-colour image and take a multiplied tint.
**Four do not, and the reason matters:** the awning fascia and the three
upholstery materials are painted a strong red in their own images, so a purple
tint through them lands on maroon rather than violet. Those are painted flat
instead — which is what the BurgerPiz-shell brands already do for the same two
surfaces.

Both original letter meshes are replaced with extruded **Church of Waffles**
type in a pale-violet emissive material, on the street-facing band and on the
east return. The face is **Righteous** — `assets/fonts/Righteous-Regular.ttf`,
the Google display face the game already ships and sets its own Probable Cause
logo in. Using a tracked repo font rather than a macOS system font also means
this cooker reproduces off a clean checkout with no host font installed.

Both signs are fitted uniformly to the original band and the **height** is what
binds: 7.70 × 0.75 m of type in a 0.751 m band. The width figures in the cooker
are the budget a longer brand name would run into, not what governs today.

Seventeen characters twice over is enough geometry to matter: at the BurgerPiz
cooker's curve resolution the lettering alone came to 31,648 triangles, more
than the rest of the restaurant put together, so this cooker halves the curve
resolution and the pair costs 10,672.

## Physical integration

The parcel uses the shared frontage and curb-cut bake. Terrain grading and
scatter exclusion use the same developed parcel
(`kChurchOfWafflesTerrainOp`). The lot is its own plan rather than the shared
BurgerPiz one, because this shell is 31.7 × 12.1 m rather than a box: the
support plan is the 60 × 38 m asphalt parcel, an interior floor slab matching
the imported `Floor` mesh's own footprint, and eight nose-in bays painted four
either side of the door approach.

The cooker generates 441 compound box colliders from architectural wall faces
and furniture bounds. The joined shell, the storefront glazing and its mullions
use wall-sized pieces to preserve the doorway; everything else uses furniture
bounds. Flat floor patches have matching heights registered as 10 support
rectangles. These are box/rectangle approximations of the detailed mesh, not
triangle collision. The original floor sits 14 cm above the new asphalt.
Its interior streaming volume keeps the surrounding neighborhood resident
during entry and exit.

**The single glazed entrance leaf is propped open at 150 degrees,** folded back
against the storefront rather than square to it. The opening is 1.06 m wide and
the character is 0.64 m across: at ninety degrees the leaf ate a quarter of that
and left 12 cm of play either side. Collision is cooked after the same pose, so
what blocks is what draws.

## Validation

Passed on the main development checkout:

- `church_of_waffles_tests --require-assets`: cooked mesh bounds, textures,
  glazing, the purple rebrand, the lit sign, ground support, public-road
  clearance, connection to Eighth, and actual character traversal from the
  forecourt through the propped door, down the dining aisle past the counter to
  the east booths and back out to the street. Negative controls confirm the
  service counter and a sealed doorway both stop the same character. This flag
  requires the private assets; ordinary clean-checkout runs skip only the
  geometry checks when assets are absent.
- `authored_city_layout_tests`: 84 active lots, 3,486 pairs, no overlaps.
- `building_access_tests`: all 54 access lots connected.
- `burgerpiz_tests`, `tacomaco_tests`: the three older brands are untouched.
- Rebuilt `apricot` and `apricot_map_lab`. Bounded 260/300-frame daytime and
  nighttime runs and the map render completed with clean GL queues.

Evidence in the ignored build directory:

- `waffles-corner.png`: both signs, the awning and the frontage.
- `waffles-front.png`: the entrance and parking from Eighth Street.
- `waffles-counter.png`: counter, stools and the kitchen line.
- `waffles-interior2.png`: booths and the dining room looking out.
- `waffles-night.png`: lit sign, interior lights and the parking lamps.
- `waffles-map.png`: the location and named marker between BurgerPiz and
  Freaky Franks.

To visit the restaurant with an isolated save:

```sh
./build/bin/apricot --start-at 409.78 -192.31 --start-player-at 417.21 -205.61 \
  --start-heading 20 --road-start --daylight --clear \
  --save-file /tmp/apricot-waffles-visit.json
```

That drops the player on the forecourt facing the door. To start at the counter
instead, `--start-player-at 405.12 -224.48 --start-player-height 12.24
--start-heading -6`. Do not aim `--start-player-at` at the doorway itself: the
opening is 1.06 m and the spawn is refused as blocked by world geometry.

## Known limits

The supplied shell is glazed only across its east third. The remaining twenty
metres of street frontage is a blank plastered wall with no openings, which is
what the source model is; nothing has been added to break it up.

The night parking-lamp pool blows out the wall it falls on. That is the existing
imported-restaurant lighting behaviour, not something this parcel introduced —
BurgerPiz does the same thing from the same camera distance.
