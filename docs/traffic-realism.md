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
