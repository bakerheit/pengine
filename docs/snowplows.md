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
non-solid interior cover. The same cover geometry controls surface color,
physical ground snow and local ice warnings. Exposed roofs and open parking
lots still accumulate snow.

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

This is an autonomous traffic service. The trucks cannot be entered by the
player. The active fleet follows the existing traffic streaming area; it does
not simulate a citywide depot or route schedule. Clearance is session state,
bounded to 128 merged strips of up to 96 m each, with oldest strips replaced
when full. Loading a checkpoint or starting a new game clears this history.

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
