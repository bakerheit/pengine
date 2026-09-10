# Police pursuit and traffic stops

Engaged cruisers can leave their usual lane path to turn mid-block, pass a
blocked car, and approach a stopped suspect near the roadside. Short steering
curves join the car's actual pose to a safe place on the road. Distant pursuit
still uses the lane graph. These are local road/shoulder maneuvers, not routes
through buildings or across arbitrary terrain.

The cruiser slows before reversing, uses the opposing outer lane for turning
room, and checks the whole move against oncoming traffic. Freeways and one-way
roads forbid mid-block reversal. A cruiser already committed to a junction
finishes that turn; ordinary intersection controls remain in effect. When a
local move is unsafe, it waits or continues toward a connected junction.

A passing move includes leaving the lane, passing the obstruction, and merging
back. It checks several lateral clearances, including a small move around a
car that has already pulled aside. Against a stopped suspect, the pass ends
behind the suspect with braking room. At wanted levels 1–2, normal pursuit also
matches the player's speed instead of deliberately ramming.

Traffic reacts to approaching, engaged police within 60 m, on the same road
level and travel corridor. Drivers slow, steer toward the curb when safe, and
stop to leave room. They finish committed junction movements first. The move
must leave enough road for a later merge; a driver cannot park against the
next junction gate. Once the responder passes, a stable 1–2 second delay and
a fresh gap check govern the return to traffic. Idle patrols and officers
already on foot do not request passage.

Maneuvers check actual vehicle footprints, parked cars, pedestrians, world
props, grade separation, and ground beneath the body. They stay on the
carriageway or a supported shoulder. No forward motion means no sideways
movement. Impact displacement stays separate from deliberate steering, and a
nudged yielding car merges from its actual position instead of snapping back.
Decisions read frozen poses; simultaneous paths reserve space in stable
`(lane_key, slot)` order. The lane graph remains the route anchor, while
rendering, collision, and officer doors use the actual steered pose.

When the suspect stops, the officer uses the existing door and character
sequence. A cruiser held in a queue for at least two seconds may send its
officer to a stopped suspect within 45 m; otherwise the usual 32 m approach
range applies. Officers do not exit during an active steering maneuver.

## Verification

The new `emergency_traffic_tests` suite covers the 60 m response radius, inactive
patrols, grade separation, pull-aside/pass/rejoin, blocked shoulders, committed
junctions, oncoming traffic, reversed update order, props, people, missing
ground, freeway restrictions, stopped vehicles, stopping behind a suspect,
room for merging before a junction, and recovery after a shove.

Ten focused suites passed on 2026-09-09: `emergency_traffic_tests`,
`police_runtime_tests`, `police_officer_tests`, `police_arrest_tests`,
`lane_graph_tests`, `traffic_runtime_tests`, `traffic_determinism_tests`,
`traffic_ai_tests`, `police_ai_tests`, and `route1_traffic_tests`.
The sim purity guard and `git diff --check` also passed. The focused pull-over
scenario recorded no collisions and a largest per-step move of 0.041 m.
The logs are in `build/emergency-traffic-qa/complete-tests.log`.

Run the authored-city check with an isolated save:

```sh
/Users/andrewbaker/.codex/skills/apricot-runtime-qa/scripts/isolated_smoke.sh \
  build/bin/apricot 16000 --police-pursuit-check --seed 905 \
  --screenshot build/emergency-traffic-qa/complete \
  --log build/emergency-traffic-qa/complete.log
```

The check stages only the player, 50 m behind an existing patrol, and engages
that unit through the production attributed-hit response API. All police and
civilian motion comes from the live simulation. It captures the turn, civilian
yield, cruiser pass, officer exit, and civilian rejoin. After the officer reaches
the player, it stands down the response and waits for traffic recovery if needed.

The final live run selected cruiser `528280977536/10` on Sycamore Loop.
It began a mid-block U-turn after 1.91 seconds, reversed by 4.1 seconds,
passed a yielding van, stopped 8.83 m behind the player, and had its officer
approach by 16.93 seconds. The van moved 2 m toward the curb and completed its
safe rejoin at 16.31 seconds, before stand-down. The passing and rejoin images
were inspected. The process exited successfully with a clean GL error queue.
Streaming spikes remain; this was a behavior check, not a performance pass.
Evidence is in `build/emergency-traffic-qa/complete.log` and its PNG captures.

A fresh run of the broader `traffic_junction_tests` still matches the existing
congestion failure; see `build/emergency-traffic-qa/junction-tests.log`.
The earlier routing-only pass established that pre-existing congestion-bound
failure in `traffic_junction_tests` at `(950, 200)`: 29 transitions and a 49.61 s
longest healthy-lead wait. The original Crowd, compiled separately, produced
the same result. That baseline is recorded in
`build/police-pursuit-qa/baseline/junction-test.log`.
