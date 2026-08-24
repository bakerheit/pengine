# `src/city/` — Pinatty's decision layers

A sim-side module (`apricot_sim`), landed by **PENG-29** as a lift out of
[`probablecause`](../../../probablecause). Roughly 5,000 lines of source and
4,300 lines of tests, all of it free functions over plain-data view structs.

**Nothing here is wired into the app.** PENG-29 landed the library and stopped.
Every file compiles, every test runs headless, and no `src/app/` code calls any
of it yet. Do not read this README as a description of a running city.

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
| `city_rng.h` | How this module draws randomness, and the channel list |

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
  vehicle substep closed-loop against the maneuver kernels on flat ground.
  apricot's `step_vehicle()` does not steer or accelerate yet — throttle, brake
  and handbrake are read and do nothing. The steering sign convention is
  therefore documented in `traffic_ai.h` as a **contract the vehicle must
  satisfy**, explicitly unverified here. The closed loop goes back in the moment
  there is a car that drives.
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
