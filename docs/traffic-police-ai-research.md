# Traffic and police AI — how the reference games do it, and what to take

Research pass, 2026-09-10. Sources are the GTA III / San Andreas
decompilations, GTA V's shipped data files and native docs, CD Projekt's
Cyberpunk 2.0 police talk, Need for Speed's heat model, and the open-source
traffic simulators (CARLA, SUMO, UE5 Mass Traffic, movsim). Each claim is
tagged with where it came from; anything not read from source or a data file
is marked *inferred*. The "where we stand" section was checked against the
tree on the same day, not against the docs.

**Licensing, first.** re3 and gta-reversed are reverse-engineered Take-Two
code and have been DMCA'd. They are cited here as *design* reference — the
numbers and the shape of the state machines — and nothing from them may be
copied into this tree, not code, not comments, not constant tables verbatim.
CARLA (MIT) and OpenSteer (MIT) can be adapted with attribution. SUMO
(EPL-2.0) and movsim (GPL-3) should be read for the algorithm and
re-implemented; the IDM and MOBIL formulas themselves are published papers.
The LSPDFR/FiveM police mods either carry no licence or GPL-3; same rule.

---

## 1. Where we stand

The short version: the *junction* layer is already better than anything in
GTA III/SA, the *pursuit driving* is real, and the gaps are everywhere else.

What exists and works (all verified in `src/traffic/crowd.cpp`,
`src/traffic/emergency.cpp`, `src/game/police_*.h`):

- Closed-form phantom population, activate 220 m / retire 320 m, identity
  `(lane_key, slot, generation)`, no RNG stream. This is the design from
  `docs/design/pinatty.md` §7.2 and it landed.
- Car following: linear constant-headway rule plus a kinematic braking cap
  (`crowd.cpp:3017-3050`), profile mix 18/60/16/6 cautious/normal/impatient/
  aggressive, frustration that tightens headway up to 15%.
- Junctions: two-phase signals on one shared clock, real stop dwells, yield
  gap acceptance with acceleration prediction, right-of-way ordered
  committed → arrival → priority → ETA → identity, downstream storage
  admission, stall watchdog. Right-of-way is decided by time windows, which is
  what SUMO does; GTA III has nothing here at all.
- Police: LOS-gated witnessing of red lights, stop signs, speeding, armed
  threat, cruiser hits; heat 0–15 mapped to 5 levels; dispatch from resident
  patrols in a 55–135 m ring; lane-graph pursuit with signal override, queue
  jump, ram, turnaround, overtake; free-drive pursuit on the real vehicle
  physics inside 22 m; officers dismount, chase on foot, shoot an armed on-foot
  player, arrest at 2 m for 3 s.

What is missing, in the order it will be noticed by a player:

| # | Gap | Evidence |
|---|---|---|
| 1 | **No search / last-known-position.** Pursuers route to the player's live position every tick regardless of LOS. LOS only gates witnessing, heat hold, free-drive engagement and shooting. | `crowd.cpp:1724-1737` |
| 2 | **No lane changes, no overtaking.** `LaneGraph::neighbour()` has zero callers; `overtake_gap_acceptable` and the four `go_around_*` kernels are tested and unwired. A civilian behind a stopped car waits forever. | `lane_graph.cpp`, `city/traffic_ai.h` |
| 3 | **No blocked-recovery ladder, no jam evaporation.** `recovery_plan` (nudge → reverse → three-point → give up) and `traffic_should_despawn_jam` are unwired. Civilians never reverse. | `city/traffic_ai.h` |
| 4 | **Roadblocks are not wired.** `roadblock_layout` / `roadblock_breached` / `roadblock_dispatch_step` exist and are pinned; `roadblock_select_site` was never lifted; `block_quality` is authored on every edge and feeds nothing. | `police_ai.h:497`, `src/city/README.md:341` |
| 5 | **Wanted levels are nearly undifferentiated.** Level changes only `target_units = min(8, level+2)`, response cadence, `+0.6·level` on cruise, `ram_min_wanted = 2`, and hazard-braking suppression above 2. No escalation content. | `crowd.cpp:1623-1737` |
| 6 | **Police never spawn.** Dispatch converts resident patrols (`patrol_fraction = 0.12`). If none is inside the 220 m bubble, nothing arrives. `police_response_gate_step` (radio delay) is unwired. | `crowd.cpp:1667` |
| 7 | **Crimes are labels.** All nine `WantedSystem::Crime` kinds route through one `add_heat`; no per-crime weight, no witness-report delay, no civilian phone-in. | `wanted_system.cpp` |
| 8 | **Ambient parked cars are invisible to AI drivers.** Only player-parked cars are hazards. Mitigated by `parked_lane_clearance_m`, not solved. | `crowd.cpp` (`ambient_parked_`) |
| 9 | No honk at the player (`honk_should_fire` unwired); no drive-away from gunfire; `person_brake_accel` / `recoil_step` unwired so cars brake for peds with the smooth follow law, not a hard stop. | `city/traffic_ai.h` |
| 10 | Officers only shoot an armed, on-foot player. A stopped driver is never pulled out. | `police_combat.h` |
| 11 | `police_ram_verdict` is duplicated inline in `police_offenses.h:279-292` with a different closing threshold (1.0 vs 3.0 m/s). The pinned kernel does not cover the production path. | |
| 12 | `per_slot_speed = false`: every phantom on a lane shares one speed, so nothing ever closes on anything. Pinned as intended in `traffic_determinism_tests`. Fine today; it becomes a limitation the day overtaking exists. | `ambient.h` |

`src/traffic/README.md:247-250` still says emergency yielding is not wired.
It is. That line should go.

---

## 2. How the reference games do it

### 2.1 GTA III / Vice City / San Andreas (decompiled source)

This is the only Rockstar traffic code anyone has read, and it is the recipe
GTA V still visibly follows.

**Two simulation tiers.** Ambient cars are `STATUS_SIMPLE`, moved
kinematically along a curve between path nodes. They become `STATUS_PHYSICS`
only when they collide, turn around, are told to swerve, or sit still for
30 s. Most traffic never runs a physics step. Our Analytic → Integrating →
free-chase ladder is the same idea with a purer first tier.

**Spawn ring is directional and off-screen.** `GenerateOneRandomCar`
alternates between 120 m ahead inside a ±45° cone and 40 m to the side
outside it. Faster player → more weight on the far-ahead cone, narrowed to
±30°. Rejects: anything within 10 m, no path to the player, wrong way on a
one-way, dead end. Removal: 50 m if off-screen, 130 m if on-screen, with a
fade instead of a pop. **Both are keyed to the camera**, which is exactly the
thing `pinatty.md` §7.2 bans. Our fixed 220/320 m rings around the sim-side
player are the right answer; what we can borrow is the *shape*, weighting
instantiation toward the player's velocity so the road ahead is populated
first.

**Jam evaporation.** An off-screen car more than 25 m away, stationary for
5 s and not waiting at a light, is deleted. Cyberpunk 2077 does the same. Our
equivalent must be "beyond N m from the player" rather than "off-screen".

**Car following.** An 11 m scan box; each vehicle ahead is projected along
relative velocity and a proximity 0..1 scales cruise speed. Speed integrates
at +0.05/frame and −0.5/frame. Crude. Ours is better.

**Deadlock breakers.** Any physics car with a mission stationary for 2 s
reverses for 750–1500 ms and honks. Two cars nose-to-nose for 15 s: the
lower-address one creeps at cruise/5 for 1 s. These two rules are why GTA III
traffic never permanently wedges.

**Next node.** Random wander with lane gating: leftmost lane may turn left,
rightmost may turn right, no U-turns, no dead ends. 25% chance per node to
shift ±1 lane if the segment is over 14 m. No overtaking in III; GTA V adds
it as a driving-style flag.

**Traffic lights.** One global 16.4 s clock, two phase groups, no junction
controller. Our two-phase 12 s clock is the same design. GTA III has **no**
right-of-way at uncontrolled junctions; the OBB projection resolves it.

**Sirens.** `MakeWayForCarWithSiren`: cars within `45·speed + 20` m ahead
and aligned swerve for 2 s. Ours (60 m radius, pull aside, merge back with
gap check) is more careful.

**Wanted (III → SA).** One `chaos` scalar. Crime weights: hit ped 5, speeding
5, run red 10, steal car 15, run over ped 18, shoot ped 30, hit cop 45, shoot
cop 80. Level thresholds 40/200/400/800/1600/3200 in III, 50/180/550/1200/
2400/4600 in SA. Crimes enter a queue and are reported 500 ms later, deduped
by type and target. Decay: once per second, only if no police within 18 m,
chaos −1 (−2 indoors or in the desert in SA). III blocks decay above one
star entirely; you drop to one star by Pay 'n' Spray or bribe.

**Per-level table (III).** Max cop cars 1/2/2/2/3/3, max cops 1/3/4/6/8/10,
roadblock density 0/0/4/8/10/12. Pursuit mission by level: 1★ always BLOCK,
2★ 25% RAM, 3★ 50%, 4★+ 75%. Speed: 1★ 25, 2★ 34, then 0.9 / 1.2 / 1.25 /
1.3 × top speed. Cops are allowed to be faster than physics.

**Pursuit state machine (`CCarAI`).** `RAMPLAYER_FARAWAY` (A* to the
player's nearest node) → `_CLOSE` at 30 m (direct steering) → back at 50 m.
CLOSE aims at the player's flank offset by both half-widths, and when within
`12·speed + 2` m fires a 250 ms hard turn into them — a crude PIT.
`BLOCKPLAYER` steers at `player + clamp(vel, 0.13)·60` (an intercept ~8 m
ahead) and switches to handbrake-stop inside 5 m. Player stopped for 2.5 s
with a cop within 10 m → occupants leave the car and the cop ped busts on
foot. Pursuit is dropped if `distance · playerSpeed > 4` or beyond 30 m.

**Roadblocks (`CRoadBlocks`).** Candidate nodes are pre-tagged "big road".
Each frame checks 1/16 of them; when the player enters 80 m and a per-level
density roll passes, `floor(roadWidth / (carWidth + 0.25))` cars are placed
across the road at ±90° with ±0.38 rad jitter, abandoned, sirens on half.
Only fires at 3★+.

### 2.2 GTA V (data files and natives; internals not public)

- **Population** is `popcycle.dat`: per zone type × 2-hour slot, hard caps
  `#Cars #Peds #parked` plus weighted vehicle groups, e.g. downtown 04:00
  `cars 20 … VEH_MID 60 VEH_RICH 8 VEH_POOR 20 VEH_POLICE 2`. Our
  district table (`pinatty.md` §2.3) is the same idea minus time of day.
- **Path nodes** (`.ynd`) carry per-node `Speed {Slow, Normal, Fast, Faster}`,
  `Density 0–15`, `IsJunction`, `Highway`, `Tunnel`, `DeadEndness`, and
  special types `StopSign`, `TrafficLightJunctionStop`, `Caution`,
  `PedNodeRoadCrossing`, `ParkingSpace`, `EmergencyVehiclesOnly`. Links carry
  lane counts each way and `Shortcut`. Our `Lane` has the equivalents except
  a per-lane `Density`, which we get from the edge.
- **Driving style is a bitmask** (`eVehicleDrivingFlags`): StopForVehicles,
  StopForPeds, SwerveAroundAllVehicles, SteerAroundStationaryVehicles,
  SteerAroundPeds, SteerAroundObjects, StopAtTrafficLights,
  GoOffRoadWhenAvoiding, AllowGoingWrongWay, Reverse, UseShortCutLinks,
  ChangeLanesAroundObstructions, ForceStraightLine … Normal traffic is
  `786603`; pursuit is `786468` (drops StopForVehicles, StopForPeds,
  StopAtTrafficLights). One mask per driver replaces a pile of special-case
  booleans.
- **Wanted.** `dispatch.meta` thresholds 50/180/550/1200/3800. Detection
  radius per star 145 / 227 / 340 / 510 / 850 m. Escape timer per star
  30 / 37.5 / 45 / 56 / 82.5 s in the file (wikis say 30/45/60/75/90; the file
  is primary). The timer runs only while **no officer has LOS**; a sighting
  resets it and re-centres the search (`SET_PLAYER_WANTED_CENTRE_POSITION`,
  `REPORT_POLICE_SPOTTED_PLAYER`). Units are ordered to the wanted centre,
  not to the player (*inferred* from the natives). The radar draws per-unit
  vision cones; the stars flash while searching.
- **Dispatch.** Every unit type spawns ≥ 200 m from a player on foot, ≥ 350 m
  from one in a car. Units per star: 1★ 4 cars; 2★ 6; 3★ 8 cars + heli +
  **2 roadblocks**; 4★ 6 + 6 SWAT + 4 roadblocks + 2 helis; 5★ same plus
  boats. Dispatch types include `PoliceRoadBlock` with its own vehicle set
  (`POLICE_ROAD_BLOCK_CARS`, `_SPIKE_STRIP`).
- **Witnessing.** One star needs a cop to see it *or* a civilian to phone it
  in; out-of-sight theft and assault get reported by bystanders. Two peds hurt
  in a short window is an instant star.
- **Tactics.** `TASK_VEHICLE_CHASE` with behaviour flags: 1 aggressive ram,
  2 ram attempts, 8 box-in with occasional PIT, 16 mild ram, 32 stay-back.
  1★ arrest at gunpoint; 2★ shoot to kill; 3★ roadblocks and spike strips,
  helicopter; 4★ NOOSE; stationary in a car at 3★+ → dragged out.

### 2.3 Cyberpunk 2077 2.0 (GDC 2025, "Heat, MaxTac and Blockades")

The cleanest modern design. Three states (relaxed / alerted / combat), heat
picks strategies, and each unit gets **one of seven Pac-Man-style
strategies**: drive-to-player, drive-away, patrol quadrant, **intersection
trap** (flank at the next junction on the player's route), get-to-player-
from-anywhere, **search around the crime scene**, search-from-anywhere.
Spawns come from graph-based lane discovery with async spawn-point
generation. Design rule they state outright: **always leave one road to
freedom**. That rule and the ghost-strategy assignment are the two things
worth stealing.

### 2.4 Need for Speed: Most Wanted (2005) and Hot Pursuit (2010)

Heat 1–6 escalates unit types and tactics (rolling roadblock → PIT →
roadblocks → spike strips → heli → heavy SUV blocks). Two meters: **evade**
fills while out of sight of every engaged unit, then a **cooldown** bar
runs; any sighting resets to pursuit. Hiding spots drain cooldown faster.
Busted fills while stationary and surrounded. Hot Pursuit 2010 replaces
busting with a damage bar. The evade → cooldown split is the same thing as
our `lose_track_window` → `heat_decay_rate`, made visible.

### 2.5 Watch Dogs 2

Civilian reactions come from a spreadsheet **reaction matrix** of ~700 rules
keyed on activity, an emotion FSM (relaxed → anxious → angry, with cooldown),
fuzzy personality weights, stimulus type and intensity. That is our
`DriverProfile` × `PedDisposition` × hazard classification, if it were data.
Not a priority; noted because the *emotion with cooldown* model is what our
`delay_seconds` frustration already is.

---

## 3. Open-source code worth reading

| Project | Licence | What to read | The idea |
|---|---|---|---|
| **CARLA Traffic Manager** (`LibCarla/source/carla/trafficmanager/`) | MIT | `CollisionStage.cpp`, `TrafficLightStage.cpp`, `MotionPlanStage.cpp`, `Constants.h` | Stage pipeline over frozen frames. Collision is bounding-box *negotiation*: each car sweeps a "geodesic" polygon along its path, length `2.5 + (0.36·v)²`; who yields is decided by who is deeper in the other's path and by heading angle, never by actor id. Unsignalised junctions are a FIFO deque: come to rest, then only the front may go after 2 s. Follow distance `2·v + 2` m. Lane change refused if a car is within 20 m ahead; merge point `clamp(1.5·v, 5, 20)` m down the new lane. |
| **SUMO** (`src/microsim/`) | EPL-2.0 | `MSCFModel_Krauss.cpp`, `MSLink.cpp`, `MSRightOfWayJunction.cpp` | Krauss safe-speed following (`accel 2.6, decel 4.5, tau 1.0, minGap 2.5`). Right of way from precomputed foe bitsets plus per-vehicle `{arrivalTime, leaveTime}` windows: follow the foe if you arrive ≥ 1 s after it leaves, lead if it arrives > 1 s after you leave, else blocked. An `impatience ∈ [0,1]` that grows with waiting time blends the foe's arrival toward "they'd brake for me". **This is our `traffic_approach_yields` with one addition we lack: impatience that eventually forces a gap.** |
| **UE5 Mass Traffic** (CitySample, source-available, Epic EULA) | not OSI | `MassTrafficMovement.cpp`, `MassTrafficLaneChange.cpp`, `MassTrafficIntersections.cpp` | Next lane by lowest downstream density. Follow law is a power-curve clamp: `f = clamp(rangePct(minDist, idealDist, gap))³`, `idealDist = lerp(1.5, 2.0, rf)·v`. Per-vehicle 35% speed-limit variance. Intersections are **periods** (sets of open lanes with durations) with "about to close" lanes that let committed cars through. LOD: full physics ≤ 150 units, kinematic to 200 m, off at 500 m. |
| **movsim / traffic-simulation.de** (`js/models.js`) | GPL-3 | `IDM.calcAccDet`, `MOBIL.realizeLaneChange` | Reference IDM and MOBIL implementations with defaults. Formulas below. |
| **OpenSteer** (`SteerLibrary.h`) | MIT | `steerForPursuit`, `steerToAvoidObstacle`, `steerToAvoidNeighbors` | Pursuit predicts the quarry's position `min(maxT, distance/speed · f)` where `f` depends on whether you are ahead, aside or behind it. This is the intercept lead our `police_pursuit_intercept` already does; OpenSteer's bucketed `f` is a better lead than our linear clamp. |
| **yoep/AutomaticRoadblock** (LSPDFR plugin) | GPL-3 | `RoadblockDispatcher.cs`, `PursuitManager.cs` | Roadblock distance `speed·4 s`, floor 175 m; needs speed ≥ 15; walk the road graph along heading; reject < 45 m from suspect; junction blocks need ≥ 60 m; auto-clean after 45 s or 100 m behind. Escalation by time (10 s after last deployment) and shots fired. |
| **LMSDev/lcpdfr_public** (GTA IV) | none stated | `Chase.cs`, `TaskCopChasePed.cs` | Every cop runs its own `HasSpottedChar`; `VisualLost` when the count hits zero; a lost suspect can be re-acquired only within 75 m (150 from a heli); `SearchArea(lastKnown, 200 m)`; escaped = visual lost > 800 ticks **and** outside the search radius. Disband at 250 m in vehicle. |
| **re3 / gta-reversed** | DMCA'd | `CarCtrl.cpp`, `CarAI.cpp`, `Wanted.cpp`, `RoadBlocks.cpp`, `CopPed.cpp` | §2.1. Design reference only. |
| **OpenRW** | GPL-3 | `TrafficDirector.cpp` | Max 10 cars, random node wander, spawns outside the frustum beyond half the radius. Nothing to take. |

**IDM** (Treiber). Acceleration
`a = a_max · [1 − (v/v₀)⁴ − (s*/s)²]`, desired gap
`s* = s₀ + max(0, v·T + v·Δv / (2·√(a_max·b)))`, `Δv = v − v_lead`.
Collision-free by construction; `s` is the bumper-to-bumper gap. City
defaults: `T 1.2–1.5 s, s₀ 2 m, a_max 1.4–2.5 m/s², b 2–3 m/s²`.

**MOBIL** (lane change). Safety: the new follower's acceleration after the
change must stay above `−b_safe` (4 m/s²). Incentive:
`ã_c − a_c + p·[(ã_n − a_n) + (ã_o − a_o)] > Δa_th + bias`, politeness
`p ≈ 0.2`, threshold `Δa_th ≈ 0.1–0.2 m/s²`, keep-right bias `0.3 m/s²`.
The incentive term is exactly "I would accelerate more over there", which is
also the overtaking decision.

---

## 4. What to take, ranked

Each item names the reference, our files, the determinism constraint, and
the test that should pin it. Nothing here needs a new module; it is all
wiring and one new data table.

### Police

**P1. Search mode with a wanted centre.** *(GTA V wanted centre; lcpdfr
`SearchArea`; Cyberpunk "search around crime scene".)* Keep
`last_seen_pos` and `last_seen_step` in the police context. While any unit
has LOS, the centre is the player. When none does, pursuers route to the
centre and then fan out: nearest unit holds the centre, the rest take the
junctions one hop out (Cyberpunk's patrol-quadrant / intersection-trap
strategies are a deterministic assignment by unit ordinal). Escape timer per
level, `{30, 37, 45, 56, 82}` s as a starting table, replacing the flat
`lose_track_window = 8 s`. A sighting resets the timer and re-centres.
Change `update_police_response` (`crowd.cpp:1724-1737`) to target the
centre; the routing code does not change. Pin: a player who breaks LOS and
turns off must *not* have pursuers converge on the live position; a player
who stays within the search ring must be re-acquired. This is the single
biggest gap between "chase" and "GTA".

**P2. Wire roadblocks.** *(GTA V 3★+, `RoadBlock` dispatch; re3
`CRoadBlocks`; AutomaticRoadblock placement rules; `pinatty.md` §3.1.)*
The kernels are pinned. What is missing is `roadblock_select_site`: walk the
lane graph forward from the player's lane along the straightest continuation
(`RoadblockTuning::ahead 120–200 m` already), score edges by
`block_quality`, width ≥ `min_width 7`, ≥ `junction_setback 14` from any
junction, and reject any site with no alternate exit reachable by the player
(Cyberpunk's "one road to freedom"). Instantiate 2–3 cruisers as ambient
identities with `officer.phase = Seated` and speed 0, so they are ordinary
obstacles to traffic and ordinary witnesses to the offence tracker. Identity
`hash(map_seed, edge_key, dispatch_step)`. Gate at level ≥ 3 with the
existing 25 s cooldown. Pin: on the Kessel Bridge a block is found; in
Saltmarsh the selector returns none.

**P3. A per-level profile table.** *(GTA III mission mix; GTA V
`dispatch.meta`; NFS heat.)* One `constexpr WantedLevelProfile[6]` in
`police_ai.h` holding: unit budget, response cadence, spawn allowed,
ram/block mix, pursuit speed multiplier, roadblock allowed, escape timer,
shoot-at-vehicle allowed. Suggested starting values:

| Level | Units | Cadence | Block : Ram | Speed × limit | Roadblock | Escape s | Fire at car |
|---|---|---|---|---|---|---|---|
| 1 | 2 | 1.4 s | 100 : 0 | 1.10 | no | 30 | no |
| 2 | 3 | 0.7 s | 75 : 25 | 1.25 | no | 37 | no |
| 3 | 4 | 0.7 s | 50 : 50 | 1.30 | yes | 45 | no |
| 4 | 6 | 0.4 s | 25 : 75 | 1.35 | yes | 56 | yes |
| 5 | 8 | 0.4 s | 25 : 75 | 1.40 | yes, spike | 82 | yes |

Today these are scattered literals across `crowd.cpp:1623-1737`,
`police_ai.cpp` and `emergency.cpp:370`. Pulling them into one table is the
prerequisite for P1, P2 and P4 and costs nothing.

**P4. Off-screen instantiation and a radio delay.** *(GTA V spawn ≥ 200 /
350 m; re3 8 s / 5 s cop spawn rate limits at 2★ / 3★; `pinatty.md` §2.3
per-district response seconds.)* When the dispatcher finds no resident
patrol, instantiate one on a lane in the 220–320 m annulus, outside LOS,
biased toward the player's heading, identity keyed on
`hash(map_seed, step, level, ordinal)`. Wire `police_response_gate_step`
(already written for this) with the district's `response_s` so a crime is
followed by a beat of nothing, then sirens. **Never key on the camera**: the
GTA III spawner uses camera direction and frustum, which is the exact thing
§7.2 rule 1 bans. Key on sim-side player position and velocity at step
boundaries. Pin with the radius-invariance test already in
`traffic_determinism_tests`.

**P5. Crime weights and a witness queue.** *(re3 crime table and 500 ms
report queue; GTA V civilian phone-in.)* Give `WantedSystem::Crime` a heat
weight and a report delay instead of flat `add_heat`. Reports sit in a small
queue and land after ~0.5 s; a ped or driver within a witness range can
"phone in" theft and assault at reduced heat when no officer saw it. The
tracker in `police_offenses.h` already produces per-offence events; this is
the consumer side. Pin: an unwitnessed theft with a ped within 20 m produces
one star after the delay; the same theft alone produces none.

**P6. Stopped driver gets pulled out.** *(re3 2.5 s stopped within 10 m →
occupants exit; GTA V 3★+ drag-out.)* Officers currently dismount only for
an on-foot or slow suspect and arrest only on foot. Add: player vehicle
stationary ≥ 2.5 s with an officer within 10 m → officer walks to the driver
door and the existing 2 m / 3 s arrest hold applies through the door. At
level ≥ 4 allow `police_combat` to fire at a vehicle. Small, and it closes
the "sit in the car and nothing happens" hole.

**P7. Flank-and-turn PIT at level ≥ 3.** *(re3 `RAMPLAYER_CLOSE`; GTA V
box-in flag 8.)* In free-chase, at `dist < 12·speed + 2` and roughly
aligned, aim `police_terminal_pursuit_cmd` at the target's rear quarter
offset by both half-widths and hold full lock for 0.25 s. Side is chosen by
identity hash. Our current `PoliceRam` is a lane-offset contact arc; this
is what makes 3★ feel different from 2★. Lower priority than P1–P4.

**P8. Hygiene.** Make `police_offenses.h:279-292` call `police_ram_verdict`
and pick one closing threshold. Delete the stale line in
`src/traffic/README.md:247-250`. Wire `wanted_report_blink_step` so the
stars flash while searching (GTA V does this and it is the HUD for P1).

### Traffic

**T1. Lane changes and overtaking around obstructions.** *(MOBIL; CARLA
20 m / 50 m rule; GTA V `ChangeLanesAroundObstructions`; UE5 density-based
lane choice.)* The single most visible civilian gap. `LaneGraph::neighbour`
exists and is uncalled; `overtake_gap_acceptable` and `go_around_*` are
tested and unwired; `PoliceBypass` in `emergency.cpp` is already a working
swept-footprint lateral move. Reuse that machinery for civilians with a
MOBIL-style trigger: change or overtake when the gain in follow-speed on the
neighbour lane exceeds a threshold, the new follower is not forced to brake
harder than `b_safe`, and (for a same-direction lane) the CARLA rule holds:
nobody within 20 m ahead on the target lane. Multi-lane roads (Arterial,
Freeway) get MOBIL; single-lane Streets get the opposing-lane overtake only
when the obstruction is *stationary* and the opposing bucket is clear for the
whole `28–44 m` run the police overtake already uses. Decision keyed on
`(lane_key, slot, decision_index)` like `choose_next`; evaluate on the 10 Hz
identity-phased cadence `emergency.cpp` already uses. Pin: a car behind a
stopped player on an Arterial passes within 10 s; on a Street it passes only
when the opposing lane is empty; reversed update order gives identical state.

**T2. Blocked-recovery ladder and jam evaporation.** *(re3 2 s → reverse
750–1500 ms + honk, 15 s nose-to-nose → lower id creeps; re3 / Cyberpunk
off-screen stationary 5 s → delete.)* `recovery_plan` and
`maneuver_reverse_cmd` / `maneuver_three_point_cmd` are written and pinned.
Wire them after T1 so a civilian that cannot pass can back off and try
another exit, and wire `traffic_should_despawn_jam` as "stationary 5 s,
beyond 60 m from the player, not at a control" → permanent retire. The
despawn must key on player distance, not visibility.

**T3. Parked cars as obstacles.** Put `ambient_parked_` footprints into the
frozen obstacle set the leader scan and `emergency_path_clear` read. On its
own this makes AI cars *stop* behind a badly parked car; with T1 they go
round it, which is what removes the need for the `parked_lane_clearance_m`
gate and lets Arterials have kerbside parking.

**T4. IDM as the speed law.** *(Treiber; CARLA, UE5 and SUMO all use a safe-
speed or IDM-like rule.)* Our linear headway plus kinematic cap is correct
and collision-free, but it produces a bang-bang approach and no visible
nose-dive. Replace `traffic_follow_speed_for_gap` with IDM using the OBB gap
as `s`, keep the hard positional clamp as the safety floor, and feed the
resulting deceleration to `recoil_step` so the body pitches. Profiles map
onto `(T, a_max, b)`: cautious `1.6 / 1.4 / 2.0`, normal `1.3 / 2.0 / 2.5`,
impatient `1.0 / 2.5 / 3.0`. Pin: the existing follow tests plus a
"no phantom ever overlaps its leader" check under a reversed insertion order.
Medium priority; it is a feel change, not a behaviour gap.

**T5. Honk at the player and drive away from gunfire.** *(re3 honk on
anti-stall reverse; GTA V peds flee, drivers drive off.)* `PlayerHazard::honk`
and `honk_should_fire` are computed and unused; `TrafficHornAudio` only honks
on jam frustration. Wire the player-hazard honk with its debounce. Add a
`Panic` reaction for drivers within the ped threat cone when the player
fires: cap-lift to `flee_speed_mul` and drop stop-for-player. Cheap and the
most perceivable item here after T1.

**T6. Driving-style bitmask.** *(GTA V `eVehicleDrivingFlags`.)* Pursuer
exemptions are currently scattered booleans (`run_control`, hazard-braking
suppression at level ≥ 3, `stop_completed = true` while engaged, ram
permission). A `DriveStyle` bitmask on `VehicleAgent` — StopForVehicles,
StopForPeds, StopAtControls, SteerAroundStationary, ChangeLanes,
AllowWrongWay, Reverse — set from the level profile (P3) for police and from
`DriverProfile` for civilians, gives one place to read them and makes T1/T2
gating one bit each.

**T7. Per-slot speed variance.** *(UE5 35% speed-limit variance per
vehicle; re3 per-class cruise.)* `per_slot_speed = false` is pinned as a
deliberate determinism choice: phantoms never close, so the analytic tier
never needs a follow law. Once T1 exists, that choice is what stops any car
ever overtaking. Do not flip it casually; it changes the negative-control
test in `traffic_determinism_tests`. Raise it as a decision when T1 lands.

**T8. Junctions: leave alone, one addition.** Our arbitration is already
SUMO-shaped and beyond anything in GTA III/SA. The one thing SUMO has that
we do not is `impatience`: a waiting driver's estimate of the foe's arrival
blends toward "they will brake for me", so a starved minor approach
eventually forces a gap instead of accruing `delay_seconds` forever. That is
the `(950, 200)` 49.6 s wait in `traffic_junction_tests`. Add it as a term in
`traffic_gap_margin_seconds` scaled by `delay_seconds`, capped so it never
enters on red or into a blocked exit.

### Suggested order

1. P3 (table) → P1 (search) → P4 (spawn + delay). That is the wanted system
   people will recognise, and P1 is where a chase currently feels wrong.
2. T1 (lane change / overtake) → T3 (parked obstacles) → T2 (recovery +
   evaporation). That is the civilian traffic people will notice.
3. P2 (roadblocks), P5 (crime weights), T5 (honk / flee), P6 (drag-out).
4. T4, T6, T7, T8, P7, P8 as they fit.

---

## Sources

Decompilations (design reference only): re3 GTA III
(`CarCtrl.cpp`, `CarAI.cpp`, `Wanted.cpp`, `RoadBlocks.cpp`, `CopPed.cpp`,
`TrafficLights.cpp`), gta-reversed GTA SA (`Wanted.cpp`, `CarCtrl.cpp`).
GTA V: GTAMods `Dispatch.meta` and `.ynd` pages, CodeWalker `YndFile.cs`,
`popcycle.dat`, FiveM natives (`SET_DRIVE_TASK_DRIVING_STYLE`,
`TASK_VEHICLE_CHASE`, `SET_TASK_VEHICLE_CHASE_BEHAVIOR_FLAG`,
`SET_PLAYER_WANTED_CENTRE_POSITION`, `REPORT_POLICE_SPOTTED_PLAYER`,
`ENABLE_DISPATCH_SERVICE`), grandtheftwiki "Wanted Level in GTA V" and "GTA
IV era". Cyberpunk 2077: GDC 2025 "Heat, MaxTac and Blockades" (gdcvault
1035509). Watch Dogs 2: GDC 2017 "Helping It All Emerge" (gdcvault 1024426),
Game Developer "How spreadsheets power civilian AI in Watch Dogs 2". NFS:
Most Wanted heat-level and pursuit wiki pages. Models: traffic-simulation.de
IDM and MOBIL pages, Treiber MOBIL paper. Code: CARLA
`LibCarla/source/carla/trafficmanager/` (dev branch), SUMO
`src/microsim/cfmodels/MSCFModel_Krauss.cpp` and `MSLink.cpp`, Myxcil
MassTraffic-Test mirror and Megafunk/MassSample issue 43, movsim
`traffic-simulation-de/js/models.js`, meshula/OpenSteer `SteerLibrary.h`,
yoep/AutomaticRoadblock, LMSDev/lcpdfr_public, rwengine/openrw.
