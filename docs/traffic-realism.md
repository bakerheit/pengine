# Traffic controls and driver patience

Implemented 2026-09-07 in the main checkout, starting from `0b3c6fc`.

## Road behavior

| Junction | Driver rule | Visible fixture |
| --- | --- | --- |
| Busy arterial crossing | Existing shared signal phases | Traffic lights |
| Local street four-way | Arrive, stop, dwell, take turns | STOP with ALL WAY plaque and stop bar |
| Local T or freeway merge | Minor approach checks for a gap; through road has priority | YIELD and give-way teeth |
| Alley or dirt access onto a better road | Side road stops; through road continues | STOP; paint on paved approaches |
| Dirt-road crossing | All-way stop | STOP with ALL WAY plaque |

Road continuity is based on authored spine identity before angle. Marlow Loop
keeps priority through its bend at `(820, 460)`; the straighter dead-end
Aldermans End yields. Busy arterial controls are retained.

The current map has 223 signal heads and 382 stop/yield signs: 332 all-way
stop approaches, 19 side-road stops, and 31 yield approaches. One sign serves
each incoming road edge, including multilane approaches. Sign orientation,
one-way carriageway width, tapered road width, and stop/yield paint derive from
the same lane data the drivers use. Posts are checked against connected
carriageways and shifted along the verge/upstream when necessary. Lettering,
octagons, inverted triangles, and plaques are shared procedural meshes.

## Driver behavior

- Priority junctions scan approaching traffic up to twelve seconds ahead,
  rather than relying on the old 18-meter gate scan.
- A minor-road driver can use a gap only when there is time to enter, clear
  the entire junction, and leave a comfort margin. Predictions allow priority
  traffic to accelerate. Committed cars and downstream storage still win.
- A clear yield approach rolls through; STOP still requires a full dwell.
- Delay accumulates while a healthy driver is stuck and fades once moving.
  Frustration reduces following headway by at most 15%, increases launch
  acceleration by at most 15%, and reduces the profile's gap margin by at most
  35%, with a 0.9-second minimum beyond estimated physical clearance. It does
  not raise cruise speed, reduce minimum bumper clearance, weaken braking,
  bypass a stop, enter on red, or enter a blocked exit.
- Stop gates allow for crosswalk depth. Unsignalized skewed approaches reserve
  their angled lane corridors so a waiting car cannot sit in passing traffic.

All cross-agent decisions use frozen snapshots. Delay state is included in
the deterministic population hash; no wall clock or shared random stream is
used for driver behavior.

## Validation

`traffic_behavior_tests` exercises open rolling yields, mandatory side-road
stops, near/far priority traffic, patience limits, sign/control agreement,
post clearance, mesh winding, authored road continuity, and reverse activation.

Production map, terrain, lane graph and Crowd; seed `0xDEADBEEF`; 120 Hz;
one seeded lead per incoming lane with cautious/impatient profiles:

| Authored junction | Approaches cleared | Time until all clear | Longest stop | Contact resolutions |
| --- | ---: | ---: | ---: | ---: |
| Briar / Tenth `(-263.58, -386.76)` | 4/4 | 12.40 s | 4.04 s | 0 |
| Rimway Slip `(-1050, 110)` | 7/7 | 16.29 s | 5.90 s | 0 |
| Quay `(-1300, -190)` | 4/4 | 17.14 s | 9.70 s | 0 |

Each seeded run also compares state hashes with reversed seed insertion.
Separate 30-second ambient runs at Briar and Rimway reverse the actual
activation scan and compare state every step, including delayed drivers.
These are bounded tests, not a claim of zero congestion across the city.

The dense signal-box test now requires zero contacts rather than requiring a
collision to occur. It still exercises the production broadphase, queue
discharge, reroutes and clearance watchdog. Dedicated impact tests continue
to require collision response.

The final gate passes 9 of 10 selected suites:

```sh
/Users/andrewbaker/.codex/skills/apricot-traffic-engineering/scripts/run_focused_gate.sh \
  traffic_behavior_tests lane_graph_tests traffic_runtime_tests \
  traffic_determinism_tests traffic_junction_tests route1_traffic_tests \
  road_fixture_tests city_roads_tests road_ribbon_tests traffic_bench
```

**Remaining failure:** `traffic_junction_tests`, at the Sycamore/Spine
`(950, 200)` healthy-lead wait assertion. Current result: 29 transitions,
49.61-second longest healthy stop; the limit is 48 seconds. Recompiling the
original HEAD traffic and lane-graph implementations separately reproduces
the failure at 28 transitions and 51.80 seconds. The assertion is retained.
The run stops there, so its later Nickel/Collector case is not reached.

`apricot` and the macOS app bundle build successfully. Isolated clear/noon
runs at Briar and Rimway, session seed 905, each rendered 6,000 frames
(about 50 seconds of simulation) with clean GL queues. The screenshots were
inspected in the renderer, and the current app window was inspected at Rimway.
An additional 300-frame Quay run verifies the plain side-road STOP presentation
with a clean GL queue.
These stationary views do not establish whole-city driving or pedestrian QA.
Startup streaming warnings remain; they are not treated as a performance pass.

- [Briar all-way stop](../build/traffic-realism-qa/briar-stop.png)
- [Rimway yield](../build/traffic-realism-qa/rimway-yield.png)
- [Quay side-road stop](../build/traffic-realism-qa/quay-stop.png)

Reproduce the yield view with an isolated save:

```sh
/Users/andrewbaker/.codex/skills/apricot-runtime-qa/scripts/isolated_smoke.sh \
  build/bin/apricot 6000 --start-at -1070 84 --start-heading 211 \
  --start-driving --road-start --daylight --clear --seed 905 \
  --screenshot build/traffic-realism-qa/rimway-yield.png
```

## Overtaking and lane changes

Implemented 2026-09-11 (PENG-47). A civilian stuck behind a stationary
obstruction on open road — a dead engine, the player's parked car — now gets
past it. On a road with two lanes each way it changes into the neighbour
lane, outboard side first, when the neighbour lane's nearest car is far enough
ahead and behind (the lifted MOBIL gap-acceptance kernel, four-second
time-to-collision both ways) and the change is worth at least 2 m/s. On a
single-lane two-way road it waits out its own patience (cautious drivers
never do), holds 10 m back so it has room to swing out, and borrows the
oncoming lane only when nothing is coming within four seconds at the combined
closing speed. Queues at signals, stops and yields are never overtaken, and
the wait behind one never counts.

Every decision reads frozen state and identity-keyed rolls, and both moves go
through the same clearance sweep and reservation set as the police maneuvers.

### Validation

`civilian_maneuver_tests` runs the production Crowd on synthetic roads: a
Street pass around a wreck (stand-off 15 m centre to centre, widest excursion
2.40 m, largest per-step move 0.043 m), an oncoming car pinned 50 m ahead at
24 m/s closing that refuses the pass for 30 s and a pass within 30 s of its
removal, two signalled Arterials with a queued leader that never accrues wait
or gets passed across two full cycles, an Arterial lane change with re-home
onto the inner lane (largest step 0.052 m), and both moves bit-identical in
reversed update order. `emergency_traffic_tests`, `traffic_determinism_tests`,
`traffic_runtime_tests`, `traffic_behavior_tests` and `police_runtime_tests`
are unchanged and green. Not yet feel-checked in the app; that is the next
step after this lands.

## Parked cars

Since 2026-09-11 (PENG-48, PENG-49) the ambient kerbside parking population
is drawn (through the same rig recipe as moving traffic, no lights) and is
solid to AI drivers: a parked car reaching into the lane is followed to a
stop or overtaken, and a kerb-clear one is passed at cruise with no speed
dip. `civilian_maneuver_tests` pins both cases on a Street, with the driver's
footprint never overlapping the parked body. The app renders 149 parked rigs
around Briar / Tenth with a clean GL queue. Kerbside bays remain Streets-only
(`parked_lane_clearance_m` 1.00 m); Arterials are a follow-up.

**The game treats them as solid again.** It stopped for a while, and the
reason was worth writing down. With the obstacle switch on, the in-app
`--police-pursuit-check` failed: on Sycamore Loop the civilian in front of the
cruiser could not pull onto a kerb full of parked cars, stopped in its lane,
and the cruiser sat 69.8 m from the player for the full sixty seconds. With
the switch off the same run passes in 15.2 s, so `src/app/world.cpp` set
`ambient_parked_obstacles = false` and AI cars went back to driving through
parked bodies.

That was the right call against a deadlock and the wrong one to keep. The
deadlock had three causes and none of them was the kerb being full:

1. **The bay gate measured a body that was not there.** `parked_lane_bay()`
   used the nominal 0.95 m `parked_half_width_m` while the runtime hazard test
   used real footprints up to 1.15 m. 643 of the island's 2,500 parked cars —
   every parked box truck — reached into a box truck's corridor while the code
   called the bay kerb-clear. The gate now reads
   `widest_parked_half_width_m()`; the bay population is unchanged (436 bays,
   2,500 cars), the claim is just true now.
2. **The clearance sweep then refused every escape.** It pads the body by
   0.28 m, which against a legally parked car is not a margin but a veto on
   the lane the car is already sitting in — so it vetoed the overtake, the
   bypass and the nudge alike, and standing still became the only admissible
   state. The recovery ladder despawned the car 35 s later rather than ever
   planning a move. The sweep now excuses **scenery** (parked bodies and
   abandoned cars — never a car that merely has zero speed this step) when the
   conflict is present at the lane baseline too and the body clips the
   corridor by less than the pad.
3. **And the arcs that did get planned trapped the car anyway** — merging back
   onto the next parked bumper, where any yaw clips it, or being declined for
   want of a textbook 16 m run near a junction. `lateral_arc_run()` fits the
   arc's end to the kerb, the nudge takes the road that is actually there, and
   an arc that cannot advance for a second is abandoned so the driver gets its
   recovery clocks back.

Measured after, on the authored map: the real-city soak goes from two
permanently wedged box trucks and a 28 s worst jam to **zero wedged and 5.4 s**;
Sycamore Loop with app settings goes from 63 to 47 cars stopped at t=120 s and
**5.59 to 6.10 m/s mean speed**. `--police-pursuit-check` PASSES with the
obstacles on, officer on the player at **26.0 s** against 15.2 s with them off.
Those 11 s are what parked cars cost — a full kerb leaves a civilian nowhere to
pull over, so the siren waits behind it — and they are a better price than
every AI car in the game driving through every parked body.
`tests/parked_corridor_tests.cpp` pins all of it.

## Jam recovery, evaporation, and the horn

Since 2026-09-11 (PENG-50, PENG-51). A car that cannot get past a stationary
obstruction — the oncoming lane never clears and the road has no neighbour
lane — waits out its patience and then edges round the obstruction on its
own half of the road if there is room (nudge); if there is not, and it is
more than 60 m from the player, it is retired after thirty seconds
deep-blocked, permanently. Near the player it waits. Behind a queue at a
signal nothing of this happens. Drivers also honk at the player: once, on
the step the player-hazard kernel first asks for it, and not again within
four seconds; the horn plays through a second audio predicate that
outranks frustration honks and ignores signal phase.

### Validation

`civilian_maneuver_tests`: a Street wreck with the oncoming lane also
blocked retires the wedged car at 37 s (never before the 30 s ceiling) with
one jam despawn and one retired identity; the same car 30 m from the player
is still there after 100 s; the retired departure never returns through 30 s
of ambient refresh; a parked car reaching 0.5 m into the lane with the
oncoming lane blocked is edged past by a nudge with no overlap; a driver
behind the player's stopped car honks once at step 31 and not again within
the interval, on the same step in two crowds. `traffic_horn_audio_tests`:
a cut-off horn plays with no patience wait and no repeat; four simultaneous
player horns share the voice budget and the 0.65 s spacing. Not yet
feel-checked in the app.
