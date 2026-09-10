# `src/city/` — Pinatty, Florangia, and the city decision layers

A sim-side module (`apricot_sim`) holding two things that arrived from different
directions.

**THE STATES, MAP, AND ROADS** — `states.h`, `map.h`, `districts.h`, `landmarks.h`,
`terrain_ops.h`, `roads.h`, `spines.{h,cpp}` — are authored `constexpr` tables
and the argument for compiling them rather than loading them is in `map.h`.

**THE DECISION LAYERS** — traffic, police, pedestrians, weather, objectives —
landed by **PENG-29** as a lift out of
[`probablecause`](../../../probablecause). Roughly 5,000 lines of source and
4,300 lines of tests, all of it free functions over plain-data view structs.

**The MAP, ROADS, and first traffic layer are wired into the app.**
`App::init()` calls `city::map_spines()` and the road network bakes, uploads and
draws on every launch. `traffic::Crowd` now promotes nearby lane phantoms into
moving cars and applies follow gaps, profile-aware signals and stops,
deterministic junction right of way, turn-speed planning, and player-hazard
braking. Intersection entry is claimed at a geometry-derived stop edge that
keeps the whole car outside the widest crossing road, and nearby AI traffic
uses deterministic body collision rather than ghosting. Destination-lane
storage and deterministic alternate-turn planning keep queues out of the
intersection box. Committed cars ignore later signals until their rear clears
that computed box and use fast contact recovery, merge reservations, and an
intersection-clearance watchdog rather than returning to ordinary queue
behavior halfway through a turn. The overtake/emergency maneuver stack, the
police, and the pedestrians are all connected too: cruisers yield to sirens,
pull aside, pass stopped traffic, turn around mid-block and — once dispatched —
run reds, stop signs, yields and right of way at a capped crossing speed, and
leave the lane centre to make contact with the player. **An engaged unit is
exempt from the RULE, never from the car**: it ignores the bulb and the
priority, and still stops for a vehicle already committed to the junction box.

**A pursuit is an outcome, not a rule, and that is why it was broken for so
long.** Every police suite here passed while a chase read as nobody chasing
you, because each one asked whether a rule fired — does a patrol witness, does
a cruiser convert, does an officer dismount — and none measured the gap over a
whole drive. `tests/police_chase_tests.cpp` now does: it flees down the real
authored streets and asserts on the distance. What it found was four separate
defects, none visible from any single rule. Pursuers were retired as ordinary
traffic at ~320 m, so no cruiser ever stayed with you — the car behind you was
never the same car twice. A pursuer whose route degraded picked its turns from
`choose_next()`, the weighted **random** draw ambient traffic uses. Right of way
could hold a stopped pursuer indefinitely, because a stopped car has a long ETA,
a long ETA loses the compare, and losing keeps it stopped. And the overtake that
exists to get a cruiser *out* of traffic ran at 5 m/s, so a suspect at 40 mph
gained 120 m inside the one manoeuvre meant to save the chase. Measured on the
authored city, fixing those took the mean gap from 82 m to 52 m, the worst gap
from beyond 240 m to under 105 m, and mid-chase pursuer deletions from routine
to zero. **Do not tune police driving without re-running that suite** — every
one of those defects looked correct in isolation.

**A fifth defect only showed up once the suite measured more than one street:
a cruiser could not get past a queue.** `PoliceBypass` passes ONE stopped car
mid-block — it needs 28 m of run and must finish before the junction clearance —
and a car queued at a red has neither. Worse, `prepare_emergency_maneuvers`
refused to plan anything at all inside the junction approach, which is the only
place a queue ever forms, so the cruiser could not pass and the car in front of
it could not yield. Units averaged 8.7 m/s against a suspect doing 18 and sat at
a standstill for 29% of the chase. The queue jump runs the outside of the queue
instead and deliberately finishes at the junction mouth at ZERO offset — ending
beside the queue parks a car until it can merge, and it cannot merge inside the
zone. That took time-at-a-standstill from 36% to 23% and the close-contact share
from 34% to 43%.

**What no amount of police work fixes, and the reason the suite runs three
different starts:** in the densest authored districts ambient traffic itself
averages about 4.5 m/s, and a pursuit cannot outrun the medium it is driving in.
One of the four scenarios sits in such a district and the gap there stays above
150 m however well the cruisers drive — verified by running the same district
with the chase switched off and getting the same traffic speeds. That is a
traffic-density and junction-throughput question, not a police one. The suite
asserts on **time at a standstill** as well as the gap for exactly that reason:
it is the half of "they just sit in traffic" that police code can actually move.

**Then the real one: THEY STAY ON THE ROAD.** Everything above makes a cruiser
a better piece of *traffic* — it runs the lights, it jumps the queue, it corners
harder — and none of it can make a lane-following agent leave the road, because
a lane-follower is defined by the lane. In this genre a police car stops using
the road network in the last thirty metres and drives at you across whatever is
in between. `police_terminal_pursuit_cmd` in `police_ai.{h,cpp}` is the command
kernel for exactly that: it came across with the rest of the police lift and
**was never called by anything**, so only the lane-following half of the port
was ever wired up, and a pursuit here was a commute with a siren on.

A dispatched cruiser inside `free_chase_range` now leaves the lane graph and is
stepped by `step_vehicle` — the same physics, collider and fixed `dt` as the
player's own car — with that kernel steering it. Its lane fields go stale for
the duration and are re-anchored on release. Four guards keep it honest, and
each one is load-bearing:

- **line of sight.** Aiming is the whole controller, so a building in between
  does not become a detour, it becomes the thing it drives into. No sight, no
  hand-off; the lane path is what gets you round a corner.
- **a stall timer.** Throttle open and not moving means the aim has walked the
  car into something. Give the road back rather than sit there revving, and
  stay on it for five seconds so it does not repeat the mistake immediately.
- **never at a suspect on foot.** That case belongs to the officer — the
  cruiser brakes, the door opens, somebody walks after you. Driving at a
  pedestrian runs the dismount phase machine off stale lane fields and simply
  drives over the arrest.
- **wide hysteresis** (engage 34 m, release 90 m) so a cruiser does not flicker
  between the two modes at the boundary.

**The hand-off distance is `route_handoff_range` for a reason.** Set wider —
34 m was tried — a cruiser spends the extra distance driving AT a moving target,
overshoots it, and then has to turn around in front of the player. That
turn-around is where "the police just reverse into things instead of trying to
turn around" comes from, and the reverse itself was wrong twice over: the port
reversed unconditionally whenever the target was behind, and reversed with the
wheels pointed AT the target. The front wheels still steer in reverse, so the
nose goes OPPOSITE them — steering at the target while backing up swings the
car further away, it never comes round, and it goes on reversing into whatever
is behind it. A car with room now turns around forwards at full lock, and only
backs up when that has visibly failed, with the steering inverted so the reverse
leg actually rotates the nose toward the target.

**Two bugs hid inside that hand-off, and the screenshot found both.**

The first: the free-drive seed built its orientation with `glm::quatLookAt(-forward, up)`. `quatLookAt` maps local **-Z** onto the direction it is given, and `vehicle_forward()` is `orientation * (0,0,-1)` — so it takes `forward` directly, and negating it seeds the car facing **exactly backwards**. That is a clean 180 degrees in a single step (21,600 deg/s at 120 Hz) in front of the player, on every single engagement, and it poisons everything downstream: a car seeded backwards reads "the target is behind me" from the first tick and spends the whole engagement turning around from a spin it never performed. It also flattered every measurement, because the resulting reverse ran the cruiser *toward* the player and closed the gap. `police_chase_tests` asserts free-driving cruisers stay under 400 deg/s; healthy is about 70.

The second: the hand-off was distance-only, so a suspect who left the road entirely — a verge, a forecourt, a car park — was chased by cruisers that had nothing to route to and parked on the nearest tarmac. `police_target_offroad_m_` (set in `set_police_context` from the suspect's distance to the nearest lane centreline) widens the hand-off to the release radius once he is off the network, because that is precisely when the lane path cannot reach him. Measured on a player who pulls onto the verge and stops: closest approach 73 m → 1.2 m.

**Known and NOT fixed here:** ordinary lane-following traffic has its own 180-degree pose snaps at junctions — the same 21,600 deg/s — and they persist with police free-drive switched off entirely. They predate all of this and live in the junction turn commit, not the police path. The suite prints both figures side by side and asserts only on the free-driving one, so nobody pins someone else's bug by accident.

**Do not tune that pair on distance alone.** Reversing toward the player shrinks
the gap, so at a wide hand-off the BROKEN controller scores a better mean gap
than the fixed one while looking obviously wrong — which is exactly how it
survived. `police_chase_tests` prints time-spent-reversing and
time-spent-pointed-away for that reason. At the tuned 22 m neither of those
separates the two (the gap does, 69 m against 93 m); they are there to stay
honest if someone widens the hand-off again.

In the jammed district this is the whole difference: mean gap 163 m → 62 m, and
a cruiser within 60 m for **71%** of the chase instead of 12%. Across the four
scenarios, 91 m → 69 m and 43% → 51% within 60 m. The suite asserts that
pursuers spend real time OFF the lane graph, because no gap or speed figure
catches "they never left the road" — and it fails with that hand-off disabled.

The suite's world is filled with coarse block proxies (every patch of ground
more than 13 m from a lane) for one reason: measuring free driving over bare
terrain lets a cruiser cut across city blocks that hold buildings in the real
game, which would flatter the feature into meaninglessness.

One measurement trap worth keeping: the suite pins the app's own ambient density
(`48 m` spacing, 16 slots — see `src/app/world.cpp`) rather than the
`AmbientTuning` defaults (`34 m`, 32 slots). The defaults are close to double,
gridlock the authored grid on their own, and made every pursuit look hopeless
whether the police code was good or not.

| File | What it decides |
|---|---|
| `traffic_ai.{h,cpp}` | Follow gaps, yellow lights, jam passing, the recovery ladder, permissive-left yield, overtake gap acceptance, player hazards, panic, emergency yield, go-around kinematics |
| `police_ai.{h,cpp}` | Witness and contact gates, the free-drive pursuit command (`police_terminal_pursuit_cmd`) and its hand-off range, the engaged-unit control override and ram gate, wanted heat decay, graceful stand-down, response delay, ram attribution, roadblock composition |
| `pedestrian_separation.h` | One pedestrian's per-frame sidestep |
| `police_officer.h` | The officer occupancy phase machine, and his health: three pistol rounds put an on-foot officer down, which freezes his phase, disarms him and holds his cruiser for the window |
| `pedestrian_reactions.h` | How a punched pedestrian reacts, and the fighter tunables |
| `character_punch.h` / `character_getup.h` | The melee and get-up phase clocks |
| `weather.{h,cpp}` | The weather state machine: kinds, scheduler, drift, Rain→Storm progression |
| `lightning.{h,cpp}` | Strike scheduling, flash envelope, distance-delayed thunder |
| `objective_runtime.{h,cpp}` | Sphere triggers and the tracked-objective state machine |
| `mission_def.h` | The authored-mission data contract |
| `road_author.{h,cpp}` + `road_types.h` | The authoring node/edge road graph and the road-type registry |
| `building_creator.{h,cpp}` | Renderer-free Sims-style walls, openings, roofs, fixtures and stairs, baked into render/collision pieces |
| `start_area.h` | Complete creator documents for Halloway Gas, Causeway Court Motel, Halloway Flats, Cloggers, and the enterable Pinatty Savings bank |
| `tacomaco.h` | Second site for the copied Cloggers fast-food shell, with independent parcel and brand identity |
| `bank_vault_layout.h` | Hinged bank-vault geometry and the shared world/local transform for interactions and moving collision |
| `roads.h` | **Pinatty's road network.** 99 authored spines, and the table every `Grade` terrain operator is derived from |
| `spines.{h,cpp}` | `map_spines()` — the one file here that includes from `src/road/`, and the static_asserts that keep the two modules' width, sidewalk, class and structure tables from drifting |
| `city_rng.h` | How this module draws randomness, and the channel list |

---

## Bank vault

On foot, use **E / controller A** beside the manager's desk to read the note,
then use the same button at the vault keypad. Enter four digits and press
**Enter** (or click the keypad / use D-pad and A); **Esc / B** cancels.
The right code opens the vault and keeps it unlocked for the current session.
Use E / A at either side's control to open or close it again. The hinged leaf
and its collision update together on fixed steps; movement pauses while the
player or player car occupies the swing area. A fresh launch starts locked.

Bank body collisions retain each piece's yaw, including the moving vault
leaf. Character overlap, support probes and camera rays use those exact
rotated footprints rather than the enclosing world-axis boxes. This keeps
the teller and vault aisles clear without disabling the visible furniture.

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

A road on a district plate that is already flat at full strength — Pinatty Row's
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
journeys out of Pinatty Row: to Camber Point over the causeway, up Ferrone Hill's
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
