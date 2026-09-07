# North Collector / Spine / Sycamore traffic validation

Validated 2026-09-04 against the current authored map, including the integrated Route 1 elevation/ramps and first-house detail pass.

## Junctions

- Nickel Road / North Collector: (700, 60), degree 2, uncontrolled.
- North Collector / Spine: (950, 40), degree 3, signal.
- Sycamore Loop / Spine: (950, 200), degree 4, signal. Its two loop lanes return to this same junction.
- The two signals are 160 m apart; they are separate controllers.

## Scoped changes

The earlier self-loop ownership fix, sampled swept-body conflicts, expanded acute-mouth reservation, compact turn curves, downstream-storage check, and signal arrival aging were already present and retained.

New fixes:

1. Defer phantom promotion inside the **departure** reservation as well as the incoming stop box. Cars spawned on outgoing lanes previously appeared among crossing cars without negotiating entry.
2. Allow a healthy, storage-ready lead that missed a full signal cycle to reserve time for the box to drain while its light is red. It still cannot enter until green. Otherwise fresh conflicting traffic can occupy a long acute corridor through every green available to the old queue.
3. An uncommitted car with a failed engine cannot veto a movable competitor.

The collision solver remains enabled.

## Recorded runtime

Each run uses the full authored map and TerrainGround, seed `city::kMapSeed`, 120 Hz for 120 seconds, vehicle spacing 48 m, max slots 16, activation radius 220 m, normal refresh/retirement, and no pedestrians.

| Focus | Local lane transitions before / after | Solver contact resolutions before / after | Final longest healthy lead stop |
|---|---:|---:|---:|
| Collector / Spine | 24 / 24 | 54 / 0 | 5.12 s |
| Sycamore / Spine | 29 / 26 | 90 / 69 | 45.42 s |
| Nickel / Collector | 8 / 8 | 0 / 0 | 0.00 s |

“Before” is the current-map source snapshot containing the earlier traffic fixes. The departure-spawn fix changes which scheduled cars initially appear, so population-dependent before/after values are not throughput comparisons. Cars retire and the population drains.

The 69 remaining resolutions in the Sycamore-focused run are **two same-lane rear contacts**, not 69 crashes: 68 repeated resolutions between one pair near Collector at (934, 33), from 4.40 to 5.58 s; one resolution near the southern Spine junction at (941, 369), at 11.37 s. Neither leaves a deadlock; no later contacts occurred in this 120 s run.

The departure-spawn fix alone exposed a healthy loop lead waiting 95.32 s. The drain-reservation fix reduces that exact case to 45.42 s. The loop's approximately eleven-degree return needs a 72 m reservation on each side, while its signal period is 12 s. Clearing movements can span signal changes, and queued cars can still miss greens while an admitted car finishes. This work does not promise zero congestion.

The recorded lead `528280977536/7` is stopped at 50 s, moving at 9.3 m/s through its reservation at 70 s, and has released that reservation by 72.5 s. A browser-inspected top-down replay records actual simulation positions; it is not a game renderer capture. The user's running game was not restarted.

## Regression gate

`traffic_junction_tests` verifies:

- Departing self-loop cars release the junction claim before making a whole lap.
- Two parallel approaches whose body paths cross cannot claim the acute merge together; both clear without contact.
- Fresh traffic appears outside both ends of every reserved junction corridor.
- An aged red queue can drain the box but cannot enter on red; a failed-engine lead cannot veto healthy traffic.
- Healthy lead stops remain below four cycles in the three 120-second authored-map runs, with actual local transitions required at each junction.

Negative-control builds separately reintroduced the old self-loop ownership, disabled the swept conflict cache, removed departure spawn exclusion, and removed red-phase drain reservation. Every build failed the corresponding regression.

Validation commands:

```sh
cmake --build build --target apricot traffic_junction_tests traffic_runtime_tests traffic_determinism_tests traffic_bench -j 6
ctest --test-dir build -R '^traffic_(junction_tests|runtime_tests|determinism_tests|bench)$' --output-on-failure
```

The four traffic suites passed in the saved checkout, including reversed-spawn-order determinism and the traffic benchmark. Unrelated audio tests were not changed.

## Halloway Viaduct auxiliary lanes

Validated 2026-09-05. The old diamond ramps ended on the Route 1 centreline,
so their rendered asphalt and AI links crossed three live lanes. Halloway now
has two paired auxiliary zones:

- West: Route 1 widens from 30 m / three lanes per direction to 40 m / four
  lanes over 83 m, carries the lane to the west ramp station, and reverses that
  geometry for the westbound entry merge.
- East: the 40 m auxiliary deck runs 260 m from the ramp station before a 120 m
  taper returns to the original 30 m freeway. In reverse it is the westbound
  deceleration and exit lane.

At street level, each on/off pair now meets North Arm at one shared station.
The north mouths were previously staggered 40 m apart; they now continue
straight across one centered junction at `(-45.714, 440.0)`.

The original three through-lane centres stay fixed. Ramp endpoints meet only
the correct right-side outer lane. `city_roads_tests` checks all four seams
under 1 cm, checks 3-to-4 lane geometry, and drives a production vehicle over
each ramp and taper against the real collision mesh. `route1_traffic_tests`
keeps the Halloway direction regression plus the Saltings, Fisherman's, Apron,
and Berth 2 bounded-flow cases.

The final collision pass samples the largest traffic shell over every Halloway
lane against the baked viaduct parapets. It covers 9,286 lane-metres without an
overlap. The four production-vehicle routes now complete 802 m westbound on,
631 m westbound off, 975 m eastbound on, and 492 m eastbound off with no sink.

The authored traffic run clears 33 westbound and 41 eastbound freeway-zone lane
transitions with zero collision-reaction ticks. The aligned north and south
surface terminals clear 44 and 41 transitions, also with zero reactions;
their longest healthy stops are 14.69 s and 16.68 s. A second run reproduces
the app capture population at seed 905 beside the westbound entry: zero traffic
reactions and zero player contacts over 360 steps.

Final focused gate:

```sh
cmake --build build -j6 --target road_ribbon_tests lane_graph_tests city_roads_tests route1_traffic_tests route1_elevation_tests sycamore_freeway_tests traffic_runtime_tests sim_determinism_tests apricot
ctest --test-dir build --output-on-failure -R '^(route1_elevation_tests|route1_traffic_tests|sim_determinism_tests|city_roads_tests|road_ribbon_tests|lane_graph_tests|traffic_runtime_tests|sycamore_freeway_tests)$'
```

All eight suites pass.
