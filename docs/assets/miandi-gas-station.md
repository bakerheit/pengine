# Miandi 6twelve gas station

Source: user-supplied `/Users/andrewbaker/Downloads/Gas_station.rar`.
The archive's `Models/Gas_station.fbx` contains the complete furnished scene;
`Gas_station_Props.fbx` is a separate prop library and is not instanced again.
No instructions or executable code from the Unity package are used.

Cook with Blender:

```sh
bsdtar -xf /Users/andrewbaker/Downloads/Gas_station.rar -C /tmp/apricot-gas-station-source
/Applications/Blender.app/Contents/MacOS/Blender -b \
  --python tools/cook_miandi_gas_station.py -- \
  /tmp/apricot-gas-station-source/Gas_station
```

The source and cooked models/textures remain local. Runtime output is under
ignored `assets/models/buildings/miandi_gas_station/`. Retain that directory
when moving this build; the cooker and integration source alone do not contain
the supplied artwork. `manifest.json` records the FBX SHA-256, selected/excluded
object names, and transform. There are 941 retained objects, 56 material meshes,
42,127 triangles, 413 collision boxes, and 26 lights.

The importer preserves the source textures, store stock, pumps, canopy, sign,
restroom block, dumpster, office, and fixtures. It excludes the demonstration
road, background cards, surrounding foliage, and underground stray props.
It opens the customer/store and restroom entry leaves before baking both
render geometry and collision. Large architectural meshes use per-face wall
collision so their bounds cannot fill the store or the canopy's drive lanes.
Flat source paving supplies the walk support; small stock remains visual.

Site: `(7600, 7960)`, north side of Gateway Drive in Miandi. Ground is 8 m;
the 40 by 56 m parcel sits between avenue junctions, with a 10 m curb cut onto
road 230. The forecourt faces south, toward Gateway; the stocked shop sits
behind the pumps. No existing road IDs or occupied blocks are replaced.
The existing Miandi plate and district scatter exclusion already cover it.

The full map and minimap share store, canopy, restroom and forecourt footprints,
plus a gas-station icon. Fixture positions supply real local lights, and the
store roof and canopy supply rain cover. This addition places the supplied
station; it does not add fuel purchasing or a new shop economy.

Validation: build `apricot miandi_gas_station_tests miandi_integration_tests
building_access_tests`, then run `build/bin/miandi_gas_station_tests --require-assets`.
The station suite checks Gateway connection, terrain, parcel bounds, private
mesh loading, character travel from the road through the store and back,
pump-lane clearance, and blocked-door/counter negative controls.

Verified in this checkout on 2026-09-08:

- `miandi_gas_station_tests --require-assets`: pass, including the character route and negative controls.
- `miandi_integration_tests`: pass.
- `building_access_tests`: pass, 52/52 entrances connected.
- Daytime forecourt and midnight interior: inspected actual game captures under `build/qa-miandi-gas/`. The 300-frame runs finished with clean GL queues. Startup terrain streaming spikes were reported; this is not a general performance pass.
- The movement route is a simulation test. Manual driving/walking was not exercised through live controls.
