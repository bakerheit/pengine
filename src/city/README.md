# `src/city/` — Pinatty's map, its roads, and its decision layers

A sim-side module (`apricot_sim`) holding two things that arrived from different
directions.

**The MAP AND THE ROADS** — `map.h`, `districts.h`, `landmarks.h`,
`terrain_ops.h`, `roads.h`, `spines.{h,cpp}` — are authored `constexpr` tables
and the argument for compiling them rather than loading them is in `map.h`.

**THE DECISION LAYERS** — traffic, police, pedestrians, weather, objectives —
landed by **PENG-29** as a lift out of
[`probablecause`](../../../probablecause). Roughly 5,000 lines of source and
4,300 lines of tests, all of it free functions over plain-data view structs.

**The MAP and the ROADS are wired into the app; the DECISION LAYERS are not.**
`App::init()` calls `city::map_spines()` and the road network bakes, uploads and
draws on every launch. The traffic, police and pedestrian code below is what
PENG-29 landed and stopped at: every file compiles, every test runs headless,
and no `src/app/` code calls any of it yet. Do not read the lower half of this
README as a description of a running city.

| File | What it decides |
|---|---|
| `traffic_ai.{h,cpp}` | Follow gaps, yellow lights, jam passing, the recovery ladder, permissive-left yield, overtake gap acceptance, player hazards, panic, emergency yield, go-around kinematics |
| `police_ai.{h,cpp}` | Witness and contact gates, terminal pursuit, wanted heat decay, graceful stand-down, response delay, ram attribution, roadblock composition |
| `pedestrian_separation.h` | One pedestrian's per-frame sidestep |
| `pedestrian_reactions.h` | How a punched pedestrian reacts, and the fighter tunables |
| `character_punch.h` / `character_getup.h` | The melee and get-up phase clocks |
| `weather.{h,cpp}` | The weather state machine: kinds, scheduler, drift, Rain→Storm progression |
| `lightning.{h,cpp}` | Strike scheduling, flash envelope, distance-delayed thunder |
| `objective_runtime.{h,cpp}` | Sphere triggers and the tracked-objective state machine |
| `mission_def.h` | The authored-mission data contract |
| `road_author.{h,cpp}` + `road_types.h` | The authoring node/edge road graph and the road-type registry |
| `roads.h` | **Pinatty's road network.** 92 authored spines, and the table every `Grade` terrain operator is derived from |
| `spines.{h,cpp}` | `map_spines()` — the one file here that includes from `src/road/`, and the static_asserts that keep the two modules' width, sidewalk, class and structure tables from drifting |
| `city_rng.h` | How this module draws randomness, and the channel list |

---

## The roads and the ground under them are ONE table

`roads.h` is the road network. It is also where every `Grade` terrain operator
comes from: `terrain_ops.h` derives one corridor per road that sets
`shapes_ground`, and there is **no hand-written Grade in the operator table any
more**. There used to be five, and a spine list was going to arrive later beside
them — two descriptions of one road, which is the oldest failure in this repo
wearing a new hat.

### Why a road needs a corridor at all

A road ribbon is baked onto the **level 0** drawn surface. Draw the terrain
under it at level 3 and the two are no longer the same surface, so the road
floats or sinks. Measured over Marrow's quarry before any of this existed:
0.324 m at level 1, 0.677 m at level 2, **1.020 m at level 3**, which is why
road draw distance was pinned at 640 m.

A `Grade` corridor fixes it *exactly* rather than approximately. Inside the
corridor at full weight the height IS the corridor's own profile — linear along
the path, constant across it, a **plane**. Every LOD level samples the same
global lattice and interpolates linearly between its samples, and linear
interpolation of a plane is that plane.

That is why `kLodCorridorMarginM` is 12 and not a round number somebody liked.
Level 3 samples every 8 m, and the mesher reconstructs a point from the four
lattice corners around it — up to `8 * sqrt(2) = 11.32 m` away. A point at the
very edge of the ribbon needs **full** corridor weight out to 11.32 m beyond it,
or one of the corners it is interpolated from sits in the feathered margin and
drags the surface off the plane. Anything less is a road that is exact in the
middle and floats at the kerb.

After the corridors, over all 65,514 draped vertices of the real bake:

| level | terrain spacing | mean | worst |
|---|---|---|---|
| 1 | 2 m | 0.0001 m | 0.110 m |
| 2 | 4 m | 0.0003 m | 0.146 m |
| 3 | 8 m | 0.0011 m | 0.311 m |

### Three ways to get this wrong, all of them found by measuring

**A corridor that doubles back inside its own width steps vertically.**
`op_weight` takes the profile from the *nearest* point on the polyline, so at a
hairpin two arms are equidistant with different heights and the operator has to
pick one. The first draft of the Shoulder had a **seven metre cliff** through the
middle of its second hairpin, 26 m from the apex, directly under the kerb line.
The fix is in the data: level 70 m either side of every apex, which is what a
real switchback does because you cannot climb and turn hard at the same time.

**A corridor caps its end height for half a width past its last point.** So
where a short spur meets a road that is climbing, whichever composes LAST decides
the ground — and a spur composing second levels off the through road's final
twenty metres, which reads on a hillside as a shelf and then a lump. The district
blocks are therefore ordered **stub-first**: the road whose gradient is the
gameplay composes last and wins its own approach. Composition order is the map,
one level down from where `terrain_ops.h` says it.

**A road is exactly one corridor, never two.** Splitting a long road into two
corridors sharing an endpoint makes each one cap the other's approach, for the
same reason. `kMaxCorridorPoints == kMaxRoadPoints` is what prevents it.

### `shapes_ground` is authored but not trusted

A road on a district plate that is already flat at full strength — Vellum Row's
12.0 m, Saltmarsh's 5.5 m — is already exact at every level, because a constant
is as planar as a plane gets, and an operator there would be pure cost. A decked
road must **never** grade, or it fills in the channel it was built to cross;
that one is a compile-time error, not a convention.

Everything else is measured. `tests/city_roads_tests.cpp` fails any road that
declines a corridor and turns out to need one, which is how it found Berth 1 and
Berth 2 driving through a five metre trough that the Kessel Channel's feather
cuts across the container apron.

---

## The suite drives

`tests/city_roads_tests.cpp` runs the real chain — `map_spines()` into a real
`RoadGraph` into a real `bake_ribbons()` on the real terrain — and then plans a
route with the real `LaneGraph` and **drives a real `VehicleState` down it**
through `step_vehicle()` against a real `TerrainCollider` at 120 Hz. Five
journeys out of Vellum Row: to Camber Point over the causeway, up Ferrone Hill's
switchbacks, to Kepler Flats over the Kessel Bridge, out to the Strand, and out
to Marrow's dirt. All five arrive and the car sinks 0.00 m below the drawn
ground on every one.

That is a different claim from "the graph is connected", and it is the one worth
making. A graph can be beautifully connected across a forty per cent side slope.

---

## The three things that changed on the way in

### 1. Nothing draws off a stream it should not

probablecause seeded its city from sequential generators: `std::mt19937` for
driver profiles and lane picks, a hand-rolled `xorshift32` for weather and
lightning. apricot forbids the first outright and has no room for the second.

**`random_driver_profile(std::mt19937&)` is gone.** In its place:

```cpp
DriverProfile driver_profile_from_roll(float roll);
DriverProfile driver_profile_for(uint64_t seed, int32_t cell_x, int32_t cell_z,
                                 uint32_t slot);
```

The mix is unchanged — 18% Cautious, 60% Normal, 16% Impatient, 6%
AggressiveLite. The *entropy* is not. A stream answers differently depending on
how many cars were spawned before this one, and in a streamed city that count
is a function of which way the player drove in: two players reaching the same
junction from opposite directions would meet different drivers, and a replay
would meet a third set. Keying the roll to the car's spawn identity removes the
question.

**Weather and lightning keep a stream, deliberately, but not their own
generator.** `core/rng.h` permits `Rng` for genuinely order-local work, and a
single scheduler advanced once per sim step is exactly that — there is no
approach order to lose. What they may not do is carry a second algorithm, so
both now take an `apricot::Rng` seeded through `hash_coord()` on separate
channels. A side effect worth having: their old `seed == 0 ? 1 : seed` hack is
gone, because `hash_coord`'s gamma offset already keeps zero off splitmix64's
fixed point.

`city_rng.h` holds the channel list. **Add a channel rather than reusing one** —
two decisions that share a channel share their entropy, and correlated "random"
choices read in game as "why are all the impatient drivers on this street".

### 2. Comments do not name the host layer

`tools/guard_sim_purity.sh` is a plain text search over `glad|SDL|<GL/|
miniaudio|imgui`, and it trips on comments too. Every cross-boundary reference
in this module says "the host layer" or "the platform layer" instead.

### 3. Provenance is kept, and labelled as provenance

The `PCG-nnn` markers throughout are **probablecause ticket numbers, not this
board's**. They are kept on purpose: the comments they head explain why a
constant is the value it is, and the ticket is the only remaining trace of the
session that chose it. Every lifted file says so in its banner.

---

## What was left behind, and why

Each of these is also marked at the point in the source where a reader will
look for it.

| Left behind | Why |
|---|---|
| `roadblock_select_site()` | Walks a `LaneGraph` — loaded lanes, junction arity, arc-length projection along a directed lane. That class is not in this tree. `RoadblockSite` stays as the plain-data handoff, so everything downstream of "a site was chosen" works. |
| `apply_weather(SkyEnv&, const WeatherState&)` | `gfx/sky_env.h` already owns lighting modulation, with the same exact-no-op-at-zero guarantee, already pinned. Two functions modulating one struct is how a look starts depending on which one ran. |
| `apply_lightning_flash(SkyEnv&, float)` | Same boundary. `flash()` hands out the scalar; the host layer decides what a flash looks like. |
| `road_author`'s `load()` / `save()` / `migrate_roads()` and its `std::filesystem` path | probablecause's CI carries an `EXCLUDE` list whose entries are excluded **because they write into live world data**. `tools/ci.sh` has no such list and `docs/architecture.md` says it must never grow one. That makes "the editor writes to a temp directory by construction, not by convention" a design requirement of whatever file layer replaces these. `serialize()`/`deserialize()` survive intact and are everything such a layer needs. |
| `nearest_road_type()` | Its only caller was `migrate_roads()`. Four lines to write back. |
| `mission_def`'s `.mis` text serialiser | It came with a file grammar, a loader and disk paths. A data model that outlives its file format is worth more than one that drags it along. |
| `road_grid.h` | Pure, and 66 lines, but nothing lifted here consumes it. Constants with no consumer are scaffolding. |

`GridDir` is the one thing that came the other way. `traffic_ai.h` needed
exactly four enumerators out of probablecause's road-graph header, so the enum
is declared here beside its users. **Its values are load-bearing** —
`turn_kind()` classifies a turn by the difference of two of them modulo 4, so
`East=0, North=1, West=2, South=3` is a contract, not an ordering preference.

---

## Tests, and where they are weaker than the originals

Ten suites, all registered in `tests/CMakeLists.txt`, all linking only
`apricot_sim`.

Two places are **weaker than what they were lifted from**, and both say so in
the source rather than quietly passing:

- **`maneuver_controller_tests` did not come across at all.** It drove a real
  vehicle substep closed-loop against the maneuver kernels on flat ground. This
  entry used to say `step_vehicle()` "does not steer or accelerate yet"; that
  stopped being true and the note was stale. It steers, it has a gearbox and it
  makes torque, and `tests/city_roads_tests.cpp` now drives a real
  `VehicleState` five journeys across the island on the authored roads. What is
  still missing is the closed loop against the MANEUVER KERNELS specifically, so
  the steering sign convention in `traffic_ai.h` remains a **contract the
  vehicle must satisfy**, still unverified against those kernels.
- **`nudge_pick_target`'s geometry test computes `mid_offset` instead of reading
  it.** probablecause read it off a lane built by the real producer. There is no
  lane builder here, so the arithmetic is pinned and the producer is not. When
  the lane graph lands, that test should go back to asking it.

`road_author_tests` also gained a replacement for the lane-graph tests it lost:
one that pins the contract a lane producer will consume from `to_polylines()`
— exact endpoints, no duplicate samples, per-edge width, type and sidewalk flag
carried through. Two things about it are worth knowing before you tighten it:

- **A straight edge tessellates to exactly two points at any step.** That is
  correct — a segment is fully described by its ends — and anything assuming
  uniform sampling breaks the first time it meets a long straight road.
- **On a curve the step is a target, not a bound.** The segment count comes off
  an *estimated* arc length that under-reads through a tight bend. Measured on
  the test curve at step 4: gaps run 3.23 m to 5.07 m.
