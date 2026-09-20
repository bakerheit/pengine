# Kyjhi payphones

Source: user-supplied `kyjhi.psx.payphones.zip`, a four-model PSX-style
payphone pack (`payhpone.fbx`, `phone.fbx`, `phonebooth.fbx`, `telephone.fbx`
plus atlas textures). Three are cooked and used:

- `phonebooth.fbx` — the enclosed booth. Scattered one per district.
- `payhpone.fbx` — an open pedestal payphone, no enclosure. One, standing on
  Halloway Gas's west wall sidewalk.
- `phone.fbx` — a small wall-mount handset, pre-elevated in the source FBX
  (its own Z already spans 1.26–2.13 m, a real mounting height). One, flush
  on Halloway Gas's west wall, the other of the two west-wall spots.

`telephone.fbx` (a second enclosed box, red-phone-box style) is not cooked —
one enclosed-booth model is enough.

Halloway Gas's west wall was asked for twice: first as two more enclosed
booths, then corrected to use the two uncovered models instead — the
pedestal and the wall handset REPLACE what was briefly a second pair of
booths there, they don't sit alongside them. The district set
(`kKyjhiPhoneboothSites`) stays booth-only, ten sites, no Halloway Gas entry.

Cook with Blender:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b \
  --python tools/cook_kyjhi_payphones.py -- \
  /path/to/kyjhi.psx.payphones
```

The source pack and cooked output remain local. Runtime output is under three
ignored directories, `assets/models/props/kyjhi_{phonebooth,payphone,wall_phone}/`,
each with one `.emesh`, one texture, a one-line `materials.txt`, a one-line
`collision.txt`, and `manifest.json` recording the FBX SHA-256. Retain those
directories when moving this build; the cooker and integration source alone
do not contain the supplied artwork.

The cook script does NOT recentre the vertical axis — the source FBX's own Z
already carries each model's real mounting height (a floor-standing model
sits at Z=0; the wall handset arrives pre-elevated), so the collision box
centre is computed from the model's real bounding-box centre, not assumed to
rest on the ground. Only X and the horizontal footprint are recentred so
placement math can drop each model straight onto its target point.

The booth and the pedestal — the two models the pack and our own site names
call "payphone" — are cooked at 75% scale (`cook(..., scale=0.75)`). The
scale is applied about local `(0, 0, 0)`, the ground point under the model,
not the model's own centre, so a floor-standing model's Z=0 base stays
exactly at Z=0 after shrinking — only the footprint and the height above the
ground shrink. The wall handset is left at full scale: it mounts above the
ground, not on it, so the same ground-anchored scale would slide it down the
wall instead of shrinking it in place.

Sites, all in `src/city/kyjhi_phonebooth_asset.h`:

- `kKyjhiPhoneboothSites` (10 `StartSite` entries): one booth per district.
  Each `(x, z)` and `ground_m` was measured against `kMapSeed` with a
  standalone probe built directly against `TerrainGround`/`district_at()`
  (`src/road/road_graph.h`, `src/city/map.h`), not guessed or hand-typed from
  a comment. Five sites sit near an already-authored building
  (`kNessBillboardSite`, `kEastArmPlazaSite`, `kMarlinDockSite`,
  `kNorthPinattyGasStationSite`, `kAirportSite`); the other five (Saltmarsh,
  Ferrone Hill, Nickel Heights, The Strand, Marrow) had no existing authored
  site in that district, so their coordinates were chosen near a
  District/Corner-tier landmark (`src/city/landmarks.h`) and the ground
  height sampled directly. Ferrone Hill in particular required probing a
  small grid to find a moderate-grade shelf on the massif rather than the
  steep slope right at the mast's base.
- `kKyjhiPayphoneSites` (1 entry): the open pedestal, on Halloway Gas's west
  wall sidewalk (`kGasStoreWalls` "store west wall", local x=-5.0,
  z -18.7..-7.7 — blank, no door or window to clip), at the wall's south
  spot, local (-5.9, -10.5), 0.9 m clear of the wall face — the same spot and
  clearance a booth briefly used.
- `kKyjhiWallPhoneSites` (1 entry): the wall handset, flush-mounted at the
  wall's north spot, local (-5.35, -16.0), only 0.2 m clear since its own
  footprint is far shallower than the pedestal's.

Both Halloway Gas sites are transformed through `kGasStationSite`'s own
origin and grid yaw (`kGridCos`/`kGridSin`) so they sit flush with the
building rather than world-axis aligned; their `ground_m` matches the site's
default, `kStartAreaGroundM`.

All three site tables share one loader (`load_kyjhi_prop_asset`) and one
placement pair (`kyjhi_prop_world`, `add_kyjhi_prop_collision`), since the
cooked layout is identical across all three models — only the asset root
differs (`kKyjhiPhoneboothAssetRoot`, `kKyjhiPayphoneAssetRoot`,
`kKyjhiWallPhoneAssetRoot`).

Integration: `src/app/world.cpp` has one `place_kyjhi_prop` lambda called
once per asset kind. Each call loads its asset once and instances it at every
site in that kind's table, uploading one mesh/material pair and one oriented
collision box per site, alongside the existing `apricot_sim` player-blocking
colliders. Meshes are torn down and reloaded with the rest of the starting
area.

Every site also gets a map/minimap POI: entries in `kMapSiteMarkers`
(`src/app/game_ui.cpp`), reusing the existing letter-badge pattern (no new
icon art) — a blue disc with a "T", the same mechanism used for the pawn shop
("P"), gun store ("G") and auto repair ("R"). No extra `world.cpp` wiring is
needed for the markers themselves; they resolve position straight off the
same `StartSite` the props use.

Verified in this checkout on 2026-09-13:

- `cmake --build build --target apricot`: builds clean, no new warnings.
- `--start-at 40 -85 --frames 120 --screenshot`: Pinatty Row booth renders
  correctly seated on the sidewalk, textured, "Phone" sign legible, GL error
  queue clean.
- `--start-at 2040 270 --frames 120 --screenshot`: The Strand booth likewise
  seated correctly (this site sits in parkland near, not directly on, the
  pier itself).
- `--start-at 13.5 -18` and `--start-at 13.2 -13`: after the correction, both
  Halloway Gas west-wall spots show the wall handset and the pedestal
  respectively — no enclosed booth remains there.
- The remaining district sites load without error (confirmed in the run log,
  "kyjhi phonebooth: 10 sites, 1 material meshes each") but were not each
  individually screenshotted.
- `--start-at 40 -140 --frames 120 --screenshot`: confirmed the Pinatty Row
  "T" marker renders on the minimap, distinct from the player and from
  neighboring ATM/bank icons.
- After the 75% rescale: `--start-at 40 -85` and `--start-at 13.2 -13`
  reshot — both the district booth and the Halloway Gas pedestal read
  visibly smaller (closer to the character's own height) with the base still
  flush on the ground, no floating or sinking.
- `tools/ci.sh` (guards, full build under `-Werror`, ctest): 201/202 passing,
  repeated across five separate runs through this feature's development. The
  one failure, `police_chase_tests`, was confirmed pre-existing and
  unrelated — it fails intermittently on a clean stash of the code before any
  of this change, and passed on a subsequent rerun of that same pre-change
  code.
