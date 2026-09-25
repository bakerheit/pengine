# Ferrone Mast

WFRN 97.3's transmitter tower on the summit of Ferrone Hill — the island-tier
landmark `docs/design/pinatty.md` has always asked for, and until now a row in
`kLandmarks` with nothing standing on it.

Source: **procedural, ours.** `tools/ferrone_mast_blender.py` places every
member in Blender and cooks the result; no supplied pack is involved. The
signs are tracked PNGs from `tools/make_ferrone_mast_textures.py` (repo fonts,
fictional station and registration number). The chain-link is Halberd's
tracked `assets/textures/world/halberd/chain-link.png`, at the same 2 m tile.

## Build it

```sh
python3 tools/make_ferrone_mast_textures.py   # only if the signs change
/Applications/Blender.app/Contents/MacOS/Blender -b --python tools/ferrone_mast_blender.py
python3 tools/validate_ferrone_mast.py
```

Output lands in the ignored `assets/models/props/ferrone_mast/` like every
other cooked mesh, together with `ferrone_mast.blend` for anyone who wants to
open it. **A checkout that has not run the script has no tower**: World logs a
warning and the summit shows the bare pad. It does not refuse to start.

Previews for review, without starting the game:
`-- --preview out.png` (plus `--night`, `--far`, `--close`, `--top`, or
`--cam x,y,z --look x,y,z --lens mm`).

## What is in it

About 35k triangles in 43 parts.

- **Lattice**: 52 m, four legs, 7.2 m base tapering to 1.8 m at 40 m and
  straight above. Tubular legs with splice flanges, angle-iron X bracing with
  gusset plates, extra redundant members in the tall lower panels, and plan
  bracing. Seven aviation orange/white bands over the full 60 m.
- **Foundations**: concrete piers, grout pads, base plates, anchor bolts and
  nuts, and copper ground straps.
- **Access**: a climbing ladder with a fall-arrest cable and a padlocked
  anti-climb guard, a rest platform at 30 m, and a railed work platform
  cantilevered at 52 m.
- **RF**: four side-mounted FM bays one wavelength (3.08 m) apart, fed by
  3‑1/8" rigid line. Three cellular sectors, each with three panel antennas,
  their radio units and jumpers. Three microwave dishes with radomes, aimed at
  Trinity Tower, the Kepler flare and the Nickel water tower. A cable ladder
  carries the coax bundle.
- **Lights**: an L‑864 red beacon on a 6.8 m top pole with a lightning rod to
  60.0 m, and L‑810 steady red side lights on all four legs at 29 m.
- **Compound**: a precast shelter with its door, rain hood, wall-pack lamp, twin
  wall-mount HVAC units, cable entry port and station sign. A diesel generator
  on a belly tank, a meter/disconnect rack, the ice bridge from shelter to
  tower, and a chain-link fence with barbed wire and a chained double gate.

## How it is placed

`src/city/ferrone_mast.h` is pure: the site, the pad bake, the lot test, and
the lighting schedule. `src/city/ferrone_mast_asset.h` is the loader.

- **Position** is `kLandmarks[0]` (`599, -1798`), the middle of the summit
  plateau. The row used to say `600, -1780`, which is the lip of an 8 m
  terrace step. The site reads the landmark rather than repeating it.
- **The pad** is a retaining block, gravel and a curb, baked in C++ from the
  real `TerrainGround`. Its top clears the highest ground sample by 0.3 m and
  its footing goes 0.6 m below the lowest. On kMapSeed that means a pad top
  at 123.75 m, walls up to 11 m on the low sides, and a tip at 183.75 m ASL.
  The plateau is smaller than the compound, so walls that tall cannot be
  avoided anywhere on the summit (probed).
- **Scatter** is zeroed over the lot plus 6 m (`city/map.cpp`).
- **Collision** is 121 boxes from the cooker, made from the same member list
  that draws: a box per leg per panel, a core box per panel, platforms, pole,
  shelter, HVAC, generator, rack, bridge posts and fence runs.

## Distance and night

`World::sync_ferrone_mast()` runs every frame and only changes how things are
displayed.

- The steel lattice draws out to 560 m. From 520 m out, four alpha-cut
  trapezoids take over. Their texture is rasterised from the same member list,
  so the bands and panel pattern survive at a range where the real members
  are sub-pixel.
- The beacon flashes 30 times a minute (on 0.8 s out of every 2 s) on the
  sim clock. The side lights are steady. All of them are lit at night only,
  like a real painted tower.
- There is no bloom in the renderer, so each red light has a glow sphere
  scaled with distance to stay about 3 px across at 1080p.
- The shelter's wall-pack lamp is a real tiled spot light within 70 m.

## Not done

- **No road reaches the summit.** It is an isolated peak: the Shoulder tops out
  at 116 m about 350 m away, across a valley. You can see the mast, fly to it,
  and walk the compound, but you cannot drive up. An access track would be a
  new spine in `roads.h`, and that is a map change with a cost of its own.
- The gate is closed and has no door system, and the shelter has no interior.
