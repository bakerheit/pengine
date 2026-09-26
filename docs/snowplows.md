# Snowplows

Snowpack of 2 cm dispatches up to four municipal plows into active paved-road
traffic. They work at up to 6 m/s, follow the normal traffic controls and
collision rules, and keep their truck identity when snowfall stops. New
dispatch stops below 8 mm. Later storms can dispatch fresh trucks.

The 2.7 m front blade clears only the truck's actual consecutive travel,
leaving an 8 mm layer. Cleared paths reveal the road material, remove the
physical snow bump and improve the player's snow grip and rolling resistance.
Rain and other weather penalties still apply. Tire marks use local snow cover.
Continued snowfall refills cleared paths at one quarter of the normal net
accumulation rate; thaw keeps its normal rate. Local depth targets the current
main snow depth, including changing weather and pinned dev snow. The road
remains visibly distinct until refill reaches that level, instead of becoming
fully white at 10 cm in a much deeper pack. Height checks keep road levels
separate.

Accumulation is excluded beneath authored roofs and ceilings, including
non-solid interior cover. Open-sided roofs are the exception: a cover whose
name marks it as a canopy, carport, shelter, awning, gazebo, portico or
pergola (`city/precipitation_cover.h`) lets snow drift in from its edges. The
drift is full at the roof line and fades to a 5% dusting over a reach of 0.6 m
per metre of headroom, capped at 4 m, so the Halloway fuel canopy (5.25 m)
drifts about 3 m in. Every other roof is treated as a building and leaves its
floor bare; where roofs overlap, the most sheltering one wins. One function,
`assets/shaders/snow_drift.glsl`, is compiled into both `lit.frag` and
`physics/snow_shelter.h`, so surface color, physical ground snow, tyre grip,
tire marks and local ice warnings all follow the same exposure. Exposed roofs
and open parking lots still accumulate snow.

Vehicles carry their own snow (`game/vehicle_snow_load.h`) rather than reading
the ground under them. A load settles while the roof is exposed and snow is
falling, up to the open-ground cover times the roof's exposure; it holds under
cover and on plowed roads, sheds above 9 m/s down to a 0.35 floor, and melts
slowly in the dry, faster with a running engine or a heatwave. The player car
and moving traffic are stepped at the fixed sim step; ambient parked cars wear
the steady state at their roof. The player car is seeded from where it starts,
a stolen traffic car keeps its load, and `--vehicle-snow-load L` starts the
player car as though it had just driven in from open weather. A parked copy
left behind when the player switches cars keeps the load it had at that moment.

Vehicle windshields retain two clear wiper sweeps, with snow around the seals
and curved upper edges. Source-mesh profiles cover all 26 current car/truck
models, including painted windows on older traffic meshes. Named transparent
windshields use their own pane bounds. Hoods and roofs retain ordinary snow;
motorcycle screens do not gain car wiper patterns. This is a persistent visual
sweep detail, not an animated wiper control.

`vehicle_snow_mesh_tests` checks actual vehicle geometry, separate panes,
articulated bodies and protected hood/roof faces. `vehicle_snow_shader_tests`
executes the shader's mask formula and checks plow windshield tags. These plus
the snowplow visual, sky and rain suites passed. Inspected production-loader
close-ups of Car 5, Pip, Mistral and Hauler show clear sweeps under full snow;
the Car 5 dry baseline retains its normal finish. A 300-frame blizzard game
run also completed with a clean graphics error queue. Evidence is under
`build/qa/snow-windshields/`. Reproduce a close-up with:

```sh
build/bin/apricot_mistral_driver_lab --player-car --car car5 \
  --snow-cover 1 --view windshield --frames 4 \
  --screenshot build/qa/snow-windshields/car5-snow.png
```

Interior exclusion is covered by `snow_shelter_tests` and
`snow_shelter_render_tests`, including actual Cloggers geometry, rotated roofs,
stacked levels and dense roof overlap. Seven focused suites passed for this
change. Inspected 300-frame blizzard runs with 0.8 m main snow confirmed a clear
restaurant floor and snowy outdoor parking, both with clean graphics error
queues. Images and logs are in `build/qa/snow-interiors/`.

`snow_drift_tests` walks the real Halloway canopy, pins the packed shader grid
to the CPU exposure, checks tyre depth follows it, and samples every authored
interior floor in the building-access inventory (58 floors) to prove none
receives drift. `vehicle_snow_load_tests` steps a car through the real canopy.
A before/after at the pumps:

```sh
build/bin/apricot --frames 240 --start-driving --start-at 9.84 -12.15 \
  --start-heading 6 --weather snow --snow-depth 0.3 --daylight \
  --vehicle-snow-load 1 --screenshot build/qa/snow-shelter/after-canopy-car.png
```

The municipal fleet is an autonomous traffic service; those trucks cannot be
entered by the player (the Grazer and Workman plow trucks below can). The active fleet follows the existing traffic streaming area; it does
not simulate a citywide depot or route schedule. Clearance is session state,
bounded to 128 strips of up to 96 m each, with oldest strips replaced when
full. Loading a checkpoint or starting a new game clears this history.

Every blade lays its strips through `PlowSweep` (`game/plow_blade.h`): one live
strip, extended each step while the path the blade actually scraped stays
within 4 cm of its centre line, and a new strip where it would not. A straight
pass is one strip; a turn is a chord every metre or so. Before this the
municipal fleet submitted a segment per step and the field merged only runs
straight to a degree, so every junction turn cost dozens of strips: a snow run
at the default start filled all 128 in 20 s. The same run now holds 44 after
33 s, with more road swept.

## Plow trucks

Two drivable plow trucks: **RODEO → GRAZER 4X4 PLOW** and **HARROW → WORKMAN
PLOW** in the dev menu, or `--player-car rodeo_grazer_plow` /
`harrow_workman_plow`. Each is its base truck (same body, doors, glass, seat,
paint and plate mounts) carrying a plow kit and a roof service light bar, with
its own heavier, front-loaded tune.

The kit is procedural geometry in `app/plow_kit_mesh.h`, fitted at load to the
truck's real cooked body: the receivers come out under the bumper and run back
to the first body part behind it, the headgear stands clear of the grille and
bumper at every height, the plow lamps sit above the hood line, and the bar's
feet rest on the roof skin under each foot. The moldboard is a curved sheet
with ribs, back channels, a bolted cutting edge on the road, a rubber
deflector, trip springs, angle rams and blade guides. `V` / D-pad down raises
and lowers it. The bar double-flashes left and right on the sim step whenever
someone is in the cab; the plow lamps glow with the headlights.

The blade the sim scrapes with and collides with is the drawn blade:
`app/plow_kit.h` pins the edge position, width, lift and reach, and
`plow_kit_tests` rebuilds the kit on the real bodies and fails if they drift by
a centimetre. The car-car footprint grows forward by the blade's reach
(`VehicleTuning::car_collision_front_extension`), and three discs along the
blade face stop the truck at a wall instead of letting the blade sink into it.

`--plow-check` drives a Grazer plow down Cloggers' frontage with the blade
down, lifts it, backs up the next lane and pushes a second pass; it fails if
the blade cleared less than 12 m.

```sh
build/bin/apricot --frames 2200 --seed 7 --plow-check \
  --screenshot build/qa/plow-trucks/plow-check.bmp
```

## Lot plow crews

Once 3 cm of snow is down, three crews (Grazer and Workman plows, chosen per
session seed with `hash_coord`) work downtown commercial lots: the gas, motel,
apartment, Cloggers, bank, laundry, pawn, bar and gun-store lots are the
candidates. `game/lot_plow.h` plans the biggest clear rectangle of each lot
from its authored parts (walls, pumps and columns, and kerbs such as the pump
islands, which are authored walkable), plows it in adjacent lanes, lifting and
backing up to the next lane between pushes, and parks with the blade down when
the lot is done or the snow is gone. A truck stops for any person, car or the
player in the box its blade or tailgate is about to sweep, and abandons a pass
after 8 s blocked rather than push through. Trucks are kinematic: solid to the
player, but they do not react to being rammed, the cab has no driver, and a
lot finished in a session is not replowed until the snow melts away and a new
storm arrives.

`lot_plow_tests` plans every candidate lot and runs three crews through a real
clearance field on the map terrain. `--lot-plow-check` follows the crew nearest
the start and fails if the crews swept less than 20 m:

```sh
build/bin/apricot --frames 1800 --seed 7 --lot-plow-check \
  --screenshot build/qa/plow-trucks/lot-plow-check.bmp
```

## Checks

`snow_clearance_tests`, `snowplow_service_tests`, `snowplow_traffic_tests`, and
`snowplow_visual_tests` cover local depth, reaccumulation, physical road
contacts, service lifecycle, deterministic traffic, and actual truck geometry.

For a live check that follows a real working plow:

```sh
build/bin/apricot --frames 600 --weather snow --snow-depth 0.18 \
  --snowplow-check --daylight --start-at 950 200 \
  --start-player-at 950 200 --road-start \
  --save-file /tmp/apricot-snowplows-qa.save \
  --screenshot build/qa/snowplows/working-plow.png
```

The check fails if less than 10 m was actually swept or no clearance remains.
Inspect the screenshot as well: counters alone cannot establish visible road
clearing.

Validated with ten focused suites and inspected 600-frame snow / 300-frame dry
runs. The snow run swept 125.9 m with four units; dry weather had no units or
clearance. Both runs had clean graphics error queues. Streaming spikes remain;
these checks are not a performance sign-off.

The broader `traffic_junction_tests` wait bound at `(950, 200)` still fails.
An isolated build with the snowplow traffic changes removed reproduced the
same 49.61 s healthy lead wait, so this is an existing traffic congestion issue.
QA logs and the inspected image are under `build/qa/snowplows/`.


For refill visual QA, add `--snowplow-refill-seconds N` to the bounded command.
It advances only the actual driven clearance footprints through N seconds of
current weather before the final screenshot. With `--weather blizzard
--snow-depth 0.8`, 6272 seconds reaches roughly half the main depth; 13000
seconds completely refills it. This is an explicit snapshot setup; normal play
always uses fixed simulation steps.
