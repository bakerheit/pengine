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
crowd.step_vehicles(step);
crowd.step_peds(step);
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

- **`retired_` only grows.** Permanence is the rule and permanence has a cost:
  every retirement is a permanent 12-byte entry. At the retire rates the bench
  measures this is small for a session and unbounded for a long one, and it
  needs a bounded policy before anything ships. Forgetting an entry is *not* the
  fix on its own — a forgotten agent is re-instantiable, which is demotion back
  to a phantom by another name.

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

- **Junction negotiation is a signal phase and a stop-line hold, and nothing
  more.** `city/traffic_ai.h` has the permissive-left yield kernel, the overtake
  gap test, the recovery ladder and the emergency-yield classifier, and none of
  them are wired here. This module exists to answer a scale question; wiring the
  full decision stack is a different ticket and will move the per-car cost.

---

## Measured

One machine (Apple silicon, `RelWithDebInfo`, single-threaded), on a 40 x 40
street grid at 92 m pitch — 3,120 edges, 6,240 directed lanes, a 3.6 km square.
Reproduce with `./build/bin/traffic_bench --full`; `ctest` runs the short ladder.

**The 120 Hz step budget is 8.333 ms for the whole game.**

| Config | cars | peds | ms/step | % budget |
|---|---|---|---|---|
| 110 m / 55 m | 42 | 47 | 0.012 | 0.1% |
| **220 m / 110 m** (the doc's first pass) | **169** | **355** | **0.042** | **0.5%** |
| 450 m / 200 m | 531 | 1,105 | 0.130 | 1.6% |
| 900 m / 340 m | 1,668 | 2,661 | 0.376 | 4.5% |
| whole district | 8,551 | 68,306 | 7.04 | 84% |
| whole district, 2x density | 21,361 | 140,875 | 17.03 | 204% |

Marginal cost, decision phases only: **13.5 ns per car per step, 63.9 ns per
pedestrian per step.** A pedestrian is five times a car because its work is a
nine-bucket neighbour gather over cold memory and a car's is a modulo and a
`pose()`.

Two things that number hides, and both matter more than it does:

- **The mean fitting is not the same as fitting.** At the largest run that fits,
  `refresh()` alone costs 4.4 ms on the step it lands on and the worst whole step
  is 11.1 ms — over budget — while the mean is 7.0. Membership refresh has to be
  spread across steps before any of this ships.
- **Sub-rate scheduling buys far less than k.** At k = 8 the per-step agent count
  drops 8x and the frame only drops **2.0x**, because `refresh`, `rebuild_buckets`,
  `publish` and `Scene::update` are all proportional to the POPULATION and not to
  how many of it you chose to think about. Sub-rate attacks the decision cost;
  the decision cost was 65% of the frame.

The population that is merely *defined* costs **1.3 ns per phantom**: evaluating
all 12,480 vehicle phantoms in the district, every step, would be 0.017 ms — 0.2%
of the budget, and nothing ever asks for all of them.

Render side, separately: **74,367 agent nodes cull in 0.163 ms** (2.2 ns/node,
1% of a 60 Hz frame). Culling agents is not a problem at any scale reached here.

**Analytic share**, at 450 m / 200 m over 7.5 s of sim: cars **51%**, pedestrians
**96%**. Turning `per_slot_speed` on moves that by less than a point (51.2% /
95.0%), which refutes the thing it was added to test: **the dominant reason a car
leaves its closed form is reaching the end of its lane, not catching another
car.** Pedestrians stay analytic because separation is lateral and does not touch
their along-lane schedule.
