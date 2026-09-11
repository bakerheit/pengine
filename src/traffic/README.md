# `src/traffic/` — the ambient population

Sim-side. Owns no GPU resource, reads no clock, holds no generator state.

This module answers one question that `src/road/` and `src/city/` deliberately
do not: **who exists, where are they at sim step `t`, and which of them is worth
stepping.** `road/` says where a lane is. `city/` says what a driver would
decide. Neither knows this module exists, and the dependency has to run that way
— a lane graph that knew about traffic could not be tested without it.

---

## The idea, and the specific failure it is aimed at

**Distant traffic is not simulated cheaply. It is not simulated at all — it is
*defined*.**

Every directed lane carries a fixed number of **phantom slots**. Slot `k` on
lane `L` has a nominal speed and a departure step derived from
`hash_coord3(map_seed, low32(L.key), high32(L.key), channel ^ slot)`, and its
position at step `t` is a closed form in `t`: one integer wrap and one multiply.
Nothing integrates. Nothing accumulates. Evaluating a phantom at step ten
million costs exactly what evaluating it at step ten costs, and neither answer
depends on the other having been asked for.

The failure this is aimed at is **promotion**, and it is worth stating precisely
because the other three traps in `docs/design/pinatty.md` §7.2 are easy and this
one is not:

> A cheaply-advanced car promoted to full simulation is not in the state it
> would have been in had it been simulated all along, and no amount of care
> makes an integrated approximation match an integrated truth.

There is nothing to promote *from* here. A phantom instantiates **at** its
closed form, exactly, at whatever step the player happens to arrive. Arriving
from the north and arriving from the south instantiate the identical car,
because the closed form does not know either of you was ever there. That is
asserted, on the real lane graph, in `tests/traffic_determinism_tests.cpp`.

Retirement is **permanent**. An agent that leaves the radius is never
re-instantiated, because a car you rammed cannot be described by a closed form
any more and pretending otherwise is the promotion bug wearing a hat.

**What is permanent is the DEPARTURE, not the slot**, and the difference is the
whole of `phantom_lap()`. A slot is a recurring departure: the closed form takes
the *remainder* of `(step - depart - slot*headway)` over the period, so slot `k`
leaves the lane again every `period_steps`. Identity is therefore
`(lane_key, slot, lap)` — `lap` being the *quotient* of that same division — and
retirement bans that triple forever. The next lap is a car nobody has simulated,
described by the closed form exactly, so instantiating it is not promotion.

Banning the `(lane_key, slot)` pair instead looks identical until you stand
still. It throws away every future departure along with the one car that was
simulated, and because a stationary player sees a fixed set of lanes, every slot
is eventually consumed: measured on the authored city, **104 cars fell to 26
over 270 seconds and were still falling**. That is the cost of conflating the two
identities, and it is pinned in `tests/traffic_runtime_tests.cpp`.

---

## Analytic and Integrating, and why the boundary is where it is

An active agent is in one of two modes:

| Mode | How its position is produced | Radius-invariant? |
|---|---|---|
| `Analytic` | **Reproduced** from the absolute step, by evaluating the closed form | Yes, bit-exactly |
| `Integrating` | **Advanced** by `speed * dt` from last step | No |

An analytic agent's state at step `t` cannot remember the step it was
instantiated at, because it is not derived from it. That is the whole claim, and
it is what makes an activation-radius change invisible.

**The transition is one-way and it happens for exactly two reasons:** the agent
is perturbed (a leader in the way, a red light), or its schedule wraps — an
active car may not teleport back to the start of the street it was leaving, even
though a phantom nobody is looking at may.

So the analytic share is bounded by *residency time over lane traversal time*,
not by anything about the mode. Measured numbers are in the module's bench;
`tests/traffic_bench.cpp --full` reprints them.

### What that means for the design doc's radius-invariance test

`docs/design/pinatty.md` §7.2 asks for: same tape, two activation radii,
bit-identical agent state at every step. **That test cannot pass as written, and
the reason is not a bug.** Measured on the district fixture at a doubled radius:

- analytic agents present in both runs: **all identical**
- integrating agents present in both runs: **most differ**

An integrating agent has history, and how much history it has is exactly what
the activation radius decides. You cannot have both an activation radius and a
radius-invariant ambient population. What you *can* have — and what this module
delivers and asserts — is that **instantiation is radius-, order- and
approach-independent**, and that the population is invariant for as long as it
has not been touched.

---

## The four rules the active set is held up by

Each is here because the obvious implementation is wrong.

1. **An agent's identity is `(lane key, slot)`, never an index.** Lane keys come
   off the authored spine, so an agent survives a rebuild and survives
   reordering the spine table. `LaneRef` is an index into one build; persisting
   one is how a save file starts pointing at a different street.

2. **The active set is kept sorted by that identity — for determinism, not for
   lookup.** `ped_separation()` sums a push vector over neighbours, float
   addition is not associative, and a neighbour list gathered in arrival order
   gives a different answer to the same list gathered in a different arrival
   order. Sorting makes iteration order a function of the *set* rather than of
   its history.

   This sort is load-bearing and does not look load-bearing. It is what makes
   `SubRatePolicy::ContainerIndex` safe, and the suite asserts that policy
   passes precisely so that removing the sort fails with a name attached.

3. **Every cross-agent read is of frozen data.** `rebuild_buckets()` snapshots
   the lane buckets and the pedestrian grid at the top of the step and nothing
   touches them again, so agent A reading agent B's gap gets the same answer
   whether A or B updated first. Without this, iteration order leaks into
   results even when the order itself is deterministic.

4. **A sub-rate phase is keyed, never counted.** See below.

---

## Sub-rate scheduling: the one that has a real answer

"Update agent `i` when `step % k == i % k`" is safe **if and only if `i` is
derived from the agent's identity.** Three variants are implemented, and the
suite runs all three:

| `SubRatePolicy` | Phase from | Result |
|---|---|---|
| `Keyed` | `hash(map_seed, lane key, slot) % k` | Holds, at k = 2, 4, 8 |
| `ContainerIndex` | position in the active vector | Holds — **only because rule 2 sorts that vector** |
| `SpawnOrdinal` | how many agents were made before this one | **Diverges at step 0** |

`SpawnOrdinal` is the negative control and it is shipped for that reason alone.
It is also the version everybody writes first: how many agents came before this
one is a fact about which way the player drove in, so the same car gets a
different phase in two runs of the same tape.

`ContainerIndex` is the interesting row. It passes, and it would be a mistake to
read that as "indices are fine".

---

## The step is four public phases, and that is not decoration

```cpp
if (step % refresh_every_steps == 0) crowd.refresh(step, player_xz);
crowd.rebuild_buckets();
crowd.step_vehicles(step, &player);
crowd.step_peds(step, &player);   // the player's car is a hazard on the pavement too
crowd.publish(scene);
```

Split for two reasons. The ordering constraint between them is real —
`rebuild_buckets()` must see the post-`refresh` population and must run before
either `step_*` — and writing it down beats implying it. And **the only legal
place to hold a clock is outside the sim**, so the phase boundaries are where
the bench's timers go. A profile taken at a seam that already exists is a
profile that costs nothing to keep.

`refresh()` runs on a **step count**, never on a frame. A 144 Hz machine would
otherwise change the population at different moments to a 60 Hz one, and the sim
would depend on the display.

---

## Known, measured, and not fixed here

- **The schedule contains a `ceil()`, and a `ceil()` is a step function.**
  `run = ceil(length / (v * dt))` feeds the headway, which feeds the period,
  which feeds every phantom's phase. Rebuilding this district from a *reordered
  spine table* changes 2,160 of 6,240 lane centrelines and 972 arc lengths, by
  up to 0.244 mm — geometrically nothing, and enough to walk straight through
  the `ceil`. `ambient.cpp` floors the length to 1/16 m first, which takes the
  exposure from 34.6% of lanes to 3.37%. **That reduces it; it does not remove
  it.** The actual fix is for the lane graph to be bit-identical under a spine
  reorder, which is a road-module property. The defect is pinned as a number in
  `tests/traffic_determinism_tests.cpp` and that test is *expected to fail* the
  day it is fixed.

- **`retired_` holds one entry per `(lane, slot)`, never one per retirement.**
  Permanence is the rule and permanence has a cost, and lap-aware identity could
  easily have made it worse: a slot can be retired on lap after lap. It does not,
  because an entry keeps only the *newest* retired lap. The lap a slot is on
  rises monotonically with the step, so an entry for an older lap can never be
  asked about again — dropping it forgets nothing that could return, which is
  the distinction a naive purge gets wrong ("a forgotten agent is
  re-instantiable, which is demotion back to a phantom by another name"). The
  entry is now 24 bytes rather than 12; the *count* is bounded exactly as it was
  before laps existed, at one per distinct identity ever retired. **That bound is
  still per-`(lane, slot)`-ever-visited, so a very long drive still grows it** —
  this narrows the open item rather than closing it. Pinned by
  `retirement_memory_is_bounded_by_identities_not_retirements`.

- **A population cap that actually binds is scan-order dependent**, because
  which agents survive it depends on which were reached first.
  `CrowdTuning::max_vehicles` / `max_peds` are a safety valve against a
  pathological map, not a design knob, and `tests/traffic_bench.cpp` fails if
  one ever binds during a measurement.

- **There is no pedestrian lane network**, because `src/road/` deliberately does
  not emit one yet and says why. Peds here ride the vehicle lane's arc at a
  footway offset. That is a stand-in for the *geometry*. It is not a stand-in
  for the *cost*: the per-ped work — one `pose()`, one advance, one separation
  solve against a gathered neighbour list — is the work the real network will
  also demand.

- **PEDESTRIANS ARE NOT ANALYTIC, and this file used to say they were 96%.**
  `refresh()` sets `p.mode = AgentMode::Integrating` at spawn and nothing ever
  sets a pedestrian back, so `CrowdStats::peds_analytic` is identically zero and
  the "cars 51% / pedestrians 96%" line under **Measured** was describing a
  build that no longer exists. The number is not wrong by a little; the column
  it came from cannot be non-zero. The design reason for the change is sound —
  an active walker has to keep real path progress across a schedule wrap, and a
  closed form cannot — but the measurement did not follow it. Corrected below.

- **Junction negotiation is local, frozen, and deterministic.** The app uses
  the same signal phase for its visible bulbs and driver decisions. Cars make
  profile-specific yellow choices, perform a real stop-sign dwell, choose one
  right-of-way winner by commitment/priority/arrival/ETA/identity, and keep the
  junction until their rear has cleared. The runtime also wires the lifted
  player-hazard kernel and turn-speed caps. Player collisions split impulse
  between both cars; the traffic body carries a deterministic world-space
  shove/yaw, adopts longitudinal impacts as real route progress, then slows and
  steers a forward arc back into its lane. The same contacts store persistent
  six-region damage on both participants, so the host can dent the struck
  corner or side without changing the shared traffic mesh. AI cars collide through a
  deterministic spatial broadphase and planar oriented bodies. Junction claims
  begin at a stop edge derived from the widest carriageway plus the car body,
  and phantom promotion is excluded from that box, so a
  newly active traffic bubble cannot materialise cars inside an intersection.
  Drivers also reserve enough space on their chosen exit to clear the whole
  body before entering. A moving leader on the exact same approach/exit is
  projected conservatively to the follower's clear time, so one green can
  discharge a platoon without treating its own lead car as cross traffic. A
  stopped destination queue therefore stays behind the paint instead of
  gridlocking the signal box; after a profile-specific patience
  delay, the driver deterministically samples open alternate turns. Emergency
  yielding, civilian overtaking, lane changes and the jam-recovery ladder
  (nudge and evaporation; the physical reverse and three-point kernels in
  `city/traffic_ai.h` are still not wired) live in `emergency.cpp`, see the
  section below. A car
  that has already committed uses a separate traversal state until its rear
  clears the computed box: it ignores later junction signals, maintains a
  minimum clear speed, suppresses low-speed contact yaw, quickly re-aligns to
  its lane, and escalates to clearance throttle after one stalled second.
  Parallel arrivals which merge into one exit are serialized before entry.

---

## Civilian overtaking and lane changes

Until PENG-47 a civilian behind a stopped car waited forever: the lane graph
had `neighbour()` and `opposing()`, `city/traffic_ai.h` had the gap-acceptance
kernel, and nothing called either. The police bypass in `emergency.cpp` already
did everything a civilian pass needs — an out/past/back arc, a swept clearance
test over frozen bodies, a reservation set so two cars do not pick the same
patch — behind two booleans that said "police only". The civilian planner
(`Crowd::plan_civilian_maneuver`) runs in the same 10 Hz identity-phased slot,
through the same `submit`, and adds three things:

- **A separate wait clock.** `VehicleAgent::obstruction_wait_s` accrues only
  behind something well under the driver's own cruise on open road, never
  while `hold` (a red, a stop, a yield) or a blocked exit is what stopped the
  car, and never behind a QUEUE — an AI car stopped at or approaching a control
  within 60 m, or the player sitting at one. `delay_seconds` could not be
  reused for this: it runs at any standstill, and a red light is not something
  you overtake. `civilian_plan_candidate()` reads the clock, so free-flowing
  traffic never builds the frozen snapshot at all and the step cost is
  unchanged where nothing is stuck.
- **Room to pull out.** A car that closes to its ordinary 0.6 m behind a
  wreck cannot swing round it — no arc clears two metres of body in three
  metres of travel, and the sweep vetoed every offset. Behind a stationary
  obstruction that is not a queue the follow law now stops
  `CivilianManeuverTuning::standoff_m` (10 m) back instead, which is also what
  a driver who means to get past does.
- **Two moves, one gate.** On a road with a same-direction neighbour the car
  changes lane (`TrafficManeuverKind::LaneChange`, one S-curve, completion
  re-homes it and clears the old lane's junction memory); it tries outboard
  first, which is the keep-right bias with no roll. Safety and incentive are
  MOBIL's: `overtake_gap_acceptable` on the target lane (its rear
  time-to-collision term is the new-follower criterion) and a gain of at least
  2 m/s in sustainable speed, where "sustainable" is the leader's speed, not the
  follow law's answer for this second. On a single-lane two-way road it
  borrows the oncoming lane around something STATIONARY only
  (`CivilianOvertake`, the bypass geometry with `destination` unchanged), after
  the driver's own patience has run out (`traffic_profile_may_pass_jam`: never
  for Cautious) plus a keyed hesitation so a row of identical drivers does not
  pull out on one step, and only when the oncoming lane's nearest car is more
  than four seconds away at the combined closing speed.

**When the pass is refused for good (PENG-50)** — the oncoming lane never
clears, the road has no neighbour lane — the car is deep-blocked: stopped at
its stand-off behind something stationary that is not a queue, on
`VehicleAgent::jam_steps`. After its patience the lifted ladder runs
(`recovery_plan`): a **Nudge** edges round the obstruction on our own half of
the road (`nudge_pick_target` chooses the side, the same arc builder and
sweep as the pass execute it); Reverse and the three-point turn are
structurally ineligible this tranche because a lane agent has no drivable
reverse (fill `rear_gap` and `road_bidirectional` later to enable them); and
with the ladder dry, far from the player (60 m, or no player in the step) and
past the 30 s ceiling, the car is **retired** — marked in the loop, swept after
it with the same sequence `take_vehicle()` uses, permanently. A queue at a
control never reaches any of this: it never accrues the clock. **Drivers also
honk at the player (PENG-51)**: the player-hazard kernel's `honk` is latched
into a debounced one-shot on the car (`honk_player_fire`, sim-step clock, 4 s
interval) that `audio/traffic_horn_audio.h` plays through a second predicate,
outranking frustration and indifferent to signal phase.

Not done here, on purpose: the opposing-lane view reads one lane's bucket and
does not look one hop past its start the way `rebuild_buckets` does for heads
(the run is at most 44 m and the per-step sweep still stops the car);
keep-right returns to the outer lane when it is free are not planned (that
would fire for every inner-lane car and cost the early-out); tapers are never
changed on or into (the auxiliary-merge code owns those). `tests/
civilian_maneuver_tests.cpp` pins the Street pass, the oncoming refusal and
its release, the signal queue, the Arterial lane change with its re-home, the
reversed-order identity, and that a leader at cruise plans nothing.

---

## What a pedestrian does besides walk

`PedAgent::activity` is a `PedActivity` — a POD enum with six values and no
presentation concept anywhere near it:

| State | What it means |
|---|---|
| `Walking` | making progress along the footway |
| `Idling` | stopped and loitering, of its own accord |
| `Waiting` | held at a kerb because crossing now would be stupid |
| `Alarmed` | has noticed a car bearing down and has not yet run |
| `Fleeing` | panic run, pulled away from the kerb |
| `Downed` | knocked over and not getting up this second |
| `Rising` | on the floor but getting up: stationary, not panicking |
| `Dead` | out of health, and terminal: this one is not getting up |

**`src/app/` reads `activity` and chooses a clip. The sim never names one.**
That is the boundary, and it is the same shape `core/input_frame.h` uses: plain
data across, no clip name, no blend weight, no animation timer on the agent. A
clip name in `PedAgent` makes the sim depend on which model is loaded, and the
first thing that breaks is a headless test.

**Nothing here is a new concept.** `Alarmed` and `Fleeing` are the two halves of
`city/traffic_ai.h`'s `PanicPhase`, driven by the same `panic_should_trigger()`
and `panic_tick()` the cars use — including that kernel's "the timer always
decays" guarantee, which is what makes it impossible to get stuck panicking. The
hidden nerve is `city/pedestrian_reactions.h`'s `Disposition`, the same one the
punch reaction uses, and it is rolled **in the spawner** off `kChannelPedNerve`
keyed to `(map_seed, lane key, slot)` exactly as that header demands. The
disposition scales the kernel's closing-speed gate and nothing else, so there is
still one definition of "reckless" and the nerve only says who agrees with it.

**Every timed transition counts STEPS and every roll is keyed.** `ped_roll()`
folds the slot *and the person's own decision ordinal* into the channel. That
ordinal is a count of **this person's** decisions, never of the population —
which is the whole difference between a keyed draw and a sequential one, and it
is why the activity machine survives `tests/ped_life_tests.cpp`'s reversed-scan
comparison with `population_hash()` equal on every step. The digest folds
`activity`, `disposition`, `activity_steps`, `activity_decisions`,
`panic_seconds`, `health` and `wounded_steps` in, because a state the digest
cannot see is a state that comparison is not proving anything about.

**Damage is `city/body_damage.h`, and it is one scale for the whole city.** A
`PedAgent` carries the same hundred points the player and the officers do.
Three pistol rounds spend them, or four punches, or one car at thirty miles an
hour — the run-over curve is squared across the band from
`knockdown_speed_mps` to 13.4 m/s, so a kerb-speed nudge floors somebody
unhurt and thirty is a death. A round that does not kill holds the panic
kernel's **trigger** down for `wounded_panic_seconds` rather than writing its
own timer; that distinction is load-bearing and the comment on that field says
why. `Dead` has no timer and no transition out of it: the body settles where
it fell under the same ragdoll integrator a `Downed` one uses, and leaves the
city the way everything else does, by being retired on distance.

**Two things are deliberately not here.** A knockdown changes the *person* and
applies no impulse to the car — a pedestrian that stops a vehicle is a worse bug
than one that ignores it. And the kerb gate reads the signal and the frozen lane
buckets, never a live agent, so it does not become a second way for iteration
order to reach a result.

**The kerb gate is what releases `Waiting`, not the hesitation timer**, and that
is not a style choice: clearing the state when the timer expires puts a person
back to `Walking` at a kerb, where the gate immediately holds them again and
rolls a *fresh* hesitation, every step, forever.

## Kerbside parking

`city/districts.h` has authored `.pop = {.traffic, .ped, .parked}` per district
since the table was written. `.traffic` and `.ped` were carried onto every lane;
**`.parked` was read by nothing at all**, so Nickel Heights — which authors 1.2
parked against 0.8 traffic — had exactly as many cars at its kerb as Marrow's
quarry, which authors 0.05. It now runs
`districts.h` → `spines.cpp` → `RoadSpine` → `RoadEdge` → `Lane::parked_density`.

**A parked car is not an agent**, and `ambient.h` says so at length. It has no
speed, no mode and no retirement because it is a pure function of
`(map_seed, lane key, slot)`: there is nothing to promote and nothing to lose,
so `Crowd::refresh()` rebuilds the resident list wholesale. The day one becomes
something a player can shunt or steal it stops being describable by that
function and has to become a real `VehicleAgent` with permanent retirement —
the same argument this file makes about promotion, which is why the type already
carries the identity a real agent would.

`ParkedLaneBay` splits **has this road got room** (`lateral_m`, from the class
table and the lane geometry) from **how many the district puts there**
(`slots`). Collapsing the two makes every measurement of authored density
secretly a measurement of road class: the first draft of
`tests/parked_density_tests.cpp` came out non-monotonic, and the cause was that
Arterials leave only 0.55 m between their outer lane centre and a kerbside body
against the authored 1.00 m clearance, so they get no bay whatever the district
says.

Measured on the real island: **438 bays, 2,512 parked cars**, worst lane
clearance 1.30 m, worst junction setback 11.61 m, tightest neighbouring pair
5.39 m. Nickel Heights comes out at **89.2 cars per km of usable kerb against
Marrow's 3.2**.

**Drawn, not solid.** `Crowd::ambient_parked()` hands out identity, world pose
and `TrafficVehicleKind`, and since PENG-48 `src/app/traffic_visual.*` draws
each resident one through the same rig recipe as a moving car (a stationary
shell agent with the parked kind; no lights, no brake lamps), reconciled on
`(lane_key, slot)` exactly like the moving rigs. Since PENG-49 they are also
obstacles to AI drivers: `gather_parked_near()` finds the parked bodies on a
car's own lane, its same-direction neighbours and its opposing lane, and they
enter the follow law (a body reaching into the driving corridor is followed
like a leader — and, being stationary and not a queue, overtaken by the
civilian planner) and the maneuver clearance sweep with their real footprints.
A kerb-clear bay is passed at cruise: the corridor test uses the car's own
half-width plus the parked half-width plus a 5 cm margin. Phantoms are never
materialised inside an intruding bay slot.

**A 14 m Street is about 10 cm too narrow for a box truck to pass a parked box
truck, and that is left true.** The bay stands a body 1.10 m from the lane
centre; a box truck driving that centre wants 1.20 m. On the island that is
643 of 2,500 parked cars — every parked box truck — against the 4/15 of drivers
that are box trucks. Those drivers go round, which is what a driver does, and
`tests/parked_corridor_tests.cpp` is the proof they can.

**That gate used to be measured against the wrong body (PENG-51).**
`parked_lane_bay()` reasoned about `AmbientTuning::parked_half_width_m`, a
nominal 0.95 m, while the runtime hazard test used the real footprint. The two
disagreed by 0.20 m, so "kerb-clear" was a claim about a car that was not
there. `widest_parked_half_width_m()` is now what the gate reads, and
`parked_half_width_m` is documented as what it always was: the slot a body is
PLACED in, not a body. Placement is deliberately one station for every kind —
sizing each slot to its own body makes a wide one reach FURTHER into the lane,
not less.

**The stuck traffic was never really the geometry, it was the escape.** Three
faults stacked: the gate above, then `emergency_path_clear` padding the body by
0.28 m — which against a legally parked car vetoes the lane the car is already
sitting in, and so vetoes every arc out of it — and then arcs that merged back
onto the next parked bumper or were declined for want of a textbook run. A car
on a maneuver has left the follow law, the recovery ladder and the intersection
escape behind, so a permanently vetoed arc is a permanent wedge with no clock
still running: one measured at 18.8 s mid-junction with a queue behind it. The
sweep now excuses SCENERY — and only scenery, never a car that merely has zero
speed this step — when the conflict exists at the lane baseline too and the
body only clips the corridor by less than the pad; `lateral_arc_run()` fits the
arc's end to the kerb; and an arc that cannot advance for a second is
abandoned (`VehicleAgent::maneuver_stall_steps`).

**The game no longer opts out.** `--police-pursuit-check` was the reason it
did: obstacles on, before the fix, the cruiser was still 66 m out at 36 s and
never arrived. It now PASSES at 26.0 s, against 15.2 s with the obstacles off —
and those 11 s are what parked cars cost, not a regression. A Street with a
full kerb has nowhere for a civilian to pull over, so a siren waits behind it.

They still have no collision response of their own — a player who hits one
drives through it — and `parked_lane_clearance_m` stays at 1.00 m: lowering it
to put bays on Arterials is a separate decision with its own bench numbers,
because every outer-lane car would then meet a hazard at its first bay and
change inboard.

**`set_parked_vehicle_poses()` is a different thing** and always was: that is
the *app's* list of cars the **player** abandoned, which AI drivers treat as
hazards. The ambient population never enters it.

## Where the crowd clumps

`ped_hotspot_gain()` multiplies a lane's authored `ped_density` by a factor
derived from the **arity of the junctions at its two ends**. Corners are where
the shopfronts are, and how many roads meet at a corner is authored — it is the
map — so a lane between two crossroads carries more people than one that
dead-ends.

`Lane::block_quality` was the obvious candidate and it is the **wrong** one:
`city/roads.h` authors it as *roadblock staging quality* ("0 means never stage
here; 255 means this is what this road is for"), which describes a long open
road with clear sightlines. Using it would have clumped the crowd onto exactly
the roads it should have thinned.

**The two ends are centred on the real island, not chosen to look tidy.** The
first pass used 0.55 / 1.85, which reads as symmetric about 1.0 and is not: most
of Pinatty's junctions are three- or four-way, so the length-weighted mean gain
came out at **1.38** and the island's pedestrian total moved by 28% — a density
change wearing a clumping name, which would have quietly invalidated every
measured cost beside it. At 0.40 / 1.35 the island measures **gain 0.56–1.35,
length-weighted mean 1.01, 390 lanes quieter and 798 busier, total ×1.10**.
`tests/ped_life_tests.cpp` fails if that mean drifts again.

## Measured

One machine (Apple silicon, `RelWithDebInfo`, single-threaded), on a 40 x 40
street grid at 92 m pitch — 3,120 edges, 6,240 directed lanes, a 3.6 km square.
Reproduce with `./build/bin/traffic_bench --full`; `ctest` runs the short ladder.

**The 120 Hz step budget is 8.333 ms for the whole game.**

| Config | cars | peds | ms/step | % budget |
|---|---|---|---|---|
| 110 m / 55 m | 42 | 47 | 0.012 | 0.1% |
| **220 m / 110 m** (the doc's first pass) | **169** | **355** | **0.041** | **0.5%** |
| 450 m / 200 m | 531 | 1,105 | 0.123 | 1.5% |
| 900 m / 340 m | 1,668 | 2,661 | 0.364 | 4.4% |
| whole district | 8,551 | 68,306 | 6.79 | 82% |
| whole district, 2x density | 21,361 | 140,875 | 15.16 | 182% |

Run-to-run spread on this machine is about 5%; every figure below comes from one
`--full` run of the committed tree rather than from a best-of.

Marginal cost, decision phases only: **13.9 ns per car per step, 63.3 ns per
pedestrian per step.** A pedestrian is five times a car because its work is a
nine-bucket neighbour gather over cold memory and a car's is a modulo and a
`pose()`.

**A stepped pedestrian finds 0.06 neighbours on average**, so that 63.3 ns is
almost entirely *finding out there is nobody there*. The nine bucket-start
probes are nine random accesses into a table sized to the population; the
candidates they turn up are nearly free because there are nearly none.

That was worth knowing precisely, because the obvious fix is the wrong one:
packing the cell key and the position into the bucket entries so a bucket scan
is contiguous **made no measurable difference** (63.9 ns vs 62.5 ns across two
runs, inside the 5% spread), and the change was reverted. There is nothing to
scan. The fix that would work is a
coarse occupancy pre-check over a much larger cell, so the 94% of pedestrians
with no neighbour at all pay one probe instead of nine — **not done here**, and
deliberately: it moves the wall and does not move the recommendation.

Two things that number hides, and both matter more than it does:

- **The mean fitting is not the same as fitting.** At the largest run that fits,
  `refresh()` alone costs 4.16 ms on the step it lands on and the worst whole
  step is 10.7 ms — over budget — while the mean is 6.79. Membership refresh has
  to be spread across steps before any of this ships.
- **Sub-rate scheduling buys far less than k.** At k = 8 the per-step agent count
  drops 8x and the frame only drops **2.02x** (6.88 ms to 3.40 ms), because
  `refresh`, `rebuild_buckets`, `publish` and `Scene::update` are all
  proportional to the POPULATION and not to
  how many of it you chose to think about. Sub-rate attacks the decision cost;
  the decision cost was 65% of the frame.

The population that is merely *defined* costs **2.0 ns per phantom**: evaluating
all 12,480 vehicle phantoms in the district, every step, would be 0.024 ms — 0.3%
of the budget, and nothing ever asks for all of them.

Render side, separately: **74,367 agent nodes cull in 0.158 ms** (2.1 ns/node,
0.9% of a 60 Hz frame). Culling agents is not a problem at any scale reached here.

**Analytic share**, at 450 m / 200 m over 7.5 s of sim: cars **51%**. Turning
`per_slot_speed` on moves that by less than a point (51.2%), which refutes the
thing it was added to test: **the dominant reason a car leaves its closed form is
reaching the end of its lane, not catching another car.**

**Pedestrians are 0%, and this paragraph used to claim 96%.** It is not a
measurement that drifted — `refresh()` sets `p.mode = AgentMode::Integrating` at
spawn and nothing sets it back, so the column cannot be non-zero. The old figure
described a build in which a walker was reproduced from its schedule; that stopped
being true when active walkers started keeping real path progress across a
schedule wrap, which a closed form cannot express. Nothing about the ped cost
below changes: the per-step work was always measured on stepped agents.

**And the ped radius moved.** `CrowdTuning::ped_activate_m` is now **165 m**
(retire 230 m), up from 110/160, because 110 m is close enough that a person can
appear inside the draw distance. The ladder below carries a rung at the shipping
`220 m / 165 m` for exactly this reason — so the configuration that ships has a
cost printed beside it on every bench run instead of one interpolated between two
rungs that move the vehicle radius as well. On this machine: 220/110 is 0.153 ms
per step (1.8% of budget), 220/165 is 0.262 ms (3.1%).
