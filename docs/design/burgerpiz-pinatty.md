# BurgerPiz — northeast Pinatty

BurgerPiz occupies the block between Wren Street and Cinder Street, south of
Tenth and north of Ninth. Its six-metre driveway joins Ninth Street (road 39).
The restaurant appears on both the atlas and radar with a food marker.

- Site centre: world X/Z **419.399, -283.813**, ground **12 m**.
- Pinatty grid centre: east **322**, south **-279**.
- Parcel: **60 × 38 m**, aligned with the Pinatty grid.
- Main entrance: approximately world **416.42, -283.22**.

## Supplied model

The source is `BurgerPiz.rar`, supplied locally by the user. The cooker uses
its GLB and keeps 312 restaurant objects at their original metre scale:
35,288 triangles in 59 material groups. It retains the shell, roof, original
BurgerPiz lettering, furnished dining room, service counter, kitchen,
restrooms, menu boards, windows, local paving and landscaping.

The original scene also contains a large demo city. The cooker selects only
whole objects inside source X [-10, 20], Z [-11, 31]; it records both inclusion
and exclusion lists in the private asset manifest. No source geometry is
decimated. The two main door leaves retain their meshes and are rotated open
90 degrees about their outer hinges before both rendering and collision cook.

The scene is turned to face Ninth, using:

```
site-local = (10 - source.z, source.y + 0.24, source.x - 8)
```

Original base-colour textures and UVs are retained. Glass uses the engine's
transparent material. The supplied ceiling emission texture supplies visible
lamp glow; 37 matching ceiling positions drive real local lights.

The archive extraction, source models, cooked meshes, textures and manifests
remain under ignored `assets/models/buildings/burgerpiz/`. Reproduce from the
repository root, after extracting the archive into that folder's `source/`:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 4 \
  --python tools/cook_burgerpiz.py -- \
  assets/models/buildings/burgerpiz/source/BurgerPiz/Models/BurgerPiz.glb
```

## Physical integration

The new asphalt parcel uses the shared frontage and curb-cut bake. Terrain
grading and scatter exclusion use the same developed parcel. The supplied
parking stripes and landscaped islands remain visible in front of the entry.

The cooker generates 816 compound box colliders from architectural wall
faces and furniture bounds. Joined shells use wall-sized pieces to preserve
the original doorway and room openings. Flat floor and sidewalk patches have
matching heights registered as support rectangles. These are box/rectangle
approximations of the detailed mesh, not triangle collision. The original
floor sits 14 cm above the new asphalt. Its interior streaming volume keeps
the surrounding neighborhood resident during entry and exit.

## Validation

Passed on the main development checkout:

- `burgerpiz_tests --require-assets`: cooked mesh bounds, textures, glazing,
  ground support, public-road clearance, connection to Ninth, actual character
  traversal through the doors, dining aisle, counter approach and return.
  Negative controls confirm the original counter and a sealed doorway block
  movement. This flag requires the private assets; ordinary clean-checkout
  runs explicitly skip only the private geometry checks when assets are absent.
- `authored_city_layout_tests`: 79 active lots, 3,081 pairs, no overlaps.
- `building_access_tests`: all 52 entrances connected.
- `loom_cultural_tests`: museum and gazebo routes remain clear.
- Rebuilt `apricot` and `apricot_map_lab`. Bounded 300-frame daytime and
  nighttime game runs and the map render completed with clean GL queues.

Evidence in the ignored build directory:

- `burgerpiz-corner.png`: exterior, original sign and frontage.
- `burgerpiz-front2.png`: entrance, parking and transparent windows.
- `burgerpiz-interior-final.png`: furnished dining room and ceiling lights at night.
- `burgerpiz-map.png`: northeast location and named marker.
- `burgerpiz-tests.log`, `burgerpiz-regressions.log`: focused checks.

To visit the restaurant with an isolated save:

```sh
./build/bin/apricot --start-at 390 -257 --start-player-at 400 -260 \
  --start-heading -45 --road-start --daylight --clear \
  --save-file /tmp/apricot-burgerpiz-visit.json
```

Parking-lamp update (2026-09-09): both heads now glow and cast real night light,
using the same dusk fade, range and distance cutoff as street lamps. See
[asset and validation notes](../assets/burgerpiz-parking-lamps.md).

A second imported shell now shares the Wren/Cinder column: the
[Church of Waffles](church-of-waffles-pinatty.md) on Eighth Street, cooked from
a different supplied GLB by its own cooker. It writes the same cooked file set,
so the loader, the collision path and the parking-lamp sync stay shared.
