# Police officers, vehicle doors, and witnessed offenses

The officer keeps the cruiser's stable lane/slot identity through driving,
braking, exit, foot pursuit, return, and entry. The driver stays visible in the
Municipal Cruiser 91-C. Its authored door, open body shell, and driver window
use the same transition timing and door transform as the Workman/player path.
Glass and the solid door share the vehicle's damage state.

An engaged officer brakes near a slow or on-foot suspect. Exit requires a
stopped cruiser, room outside the junction, supported ground, and a clear door
corridor. Nearby approaching traffic can hold the door sequence. Officers
chase on foot, stop short of the suspect, and return when the chase ends or
the suspect drives away. Walking checks terrain, walls, traffic, and other
officers, including a route around the officer's own cruiser. Empty cruisers
remain braked until re-entry and door closing finish; external impacts still
move them physically.

## Offenses

- A forward front-bumper crossing of the shared signal stop line during red
  adds 1 heat only when an officer has range, field of view, and clear sight.
  Yellow, green, reverse travel, spawning, teleporting, and simply waiting at
  a red light do not count. One crossing produces one report.
- A player-caused cruiser contact at 1 m/s closing speed or more adds 2 heat
  and directly engages that cruiser, even when the player hit it from behind.
  Contact-point velocities before the impulse determine fault. Cop-initiated
  rams, tiny resting nudges, and repeated contact in one episode do not add
  heat. A repeated report needs separation and a five-second cooldown.
- Witnessing is active at zero wanted level. Officers on foot use their own
  position and facing. World geometry and active traffic bodies block sight.
  Traffic occlusion uses oriented opaque body bounds; it does not model sight
  through another car's windows.

## Arrest

During an active pursuit, an officer on foot arrests an on-foot player after
remaining within 2 m for 3 continuous seconds with clear line of sight.
Distance uses both characters' actual feet positions in all three dimensions.
The officer approaches to about 1.6 m, leaving room inside the arrest range.

Each officer has their own timer, measured in 120 Hz simulation steps.
Leaving range, losing sight, entering a vehicle, ending the pursuit, or the
officer leaving foot pursuit resets that officer's progress. Pause time does
not count. Loading and teleporting clear the timer. Officer ordering and a
change in the nearest officer do not transfer or erase another officer's hold.

Arrest fires once per pursuit, shows `ARRESTED` for four seconds, clears wanted
heat, and sends officers back to their cruisers. This event leaves the player
at their current location.

This pass uses local obstacle avoidance and parks cruisers in their lane.
It does not add pull-over planning, jail processing, weapons, or a global foot navmesh.
Active patrol cars are locked, so the existing instant theft transfer cannot
delete their seated or deployed officer.

## Supplied characters

The police cast uses the supplied `Characters_psx_01` pack. Blue Character 17
and tan Character 19 uniform textures share the proven Character 17 police
body and skeleton. Character 19's separate hair geometry is not imported.
Both looks retain a 1.76 m height while seated, walking, and transitioning.
Idle, walk, and sprint clips use the existing skeletal animation path.

Stage these local assets after the base PSX character assets are available:

```sh
python3 tools/stage_police_character_assets.py \
  --source-pack ~/workspace/Games/resources/Characters_psx_01
```

The meshes, textures, clips, and per-look provenance files stay below ignored
`assets/models/characters/psx_pack/`. Only the staging tool and integration
code are tracked.

## Reproducible checks

`police_officer_tests` covers lifecycle, mid-exit standdown, no early driving,
blocked doors, supported feet, returning around the cruiser, direct hit
response, responder limits, and reverse-scan determinism.
`police_offense_tests` covers signal phases, real authored approaches,
world/traffic occlusion, FOV/range, discontinuities, and impact attribution.
`police_character_pose_tests` samples both uniform rigs through the full
entry/exit sequence and all three locomotion clips.
`police_arrest_tests` checks the exact 2 m / 3 s boundary, timer resets,
individual officer holds, one-shot reporting, and simulation discontinuities.

The live check scripts player placement and input. The offense detector,
collision solver, police decisions, walking, and door motion run unchanged:

```sh
/Users/andrewbaker/.codex/skills/apricot-runtime-qa/scripts/isolated_smoke.sh \
  build/bin/apricot 6000 --police-officer-check --clear --daylight --seed 905 \
  --screenshot build/police-officer-qa/verified
```

It must observe a witnessed red light, an attributed cruiser hit, officer
exit, foot movement, arrest, return, entry, and resumed driving. Each phase saves a
PNG. A missed phase or failed capture makes the command fail.

## Initial officer validation on 2026-09-07

- The game, Mistral driver lab, and boat driver lab build successfully. The
  game executable and `Probable Cause.app` executable match byte for byte.
- All 10 focused suites pass: `police_offense_tests`, `police_officer_tests`,
  `police_character_pose_tests`, `police_ai_tests`, `police_runtime_tests`,
  `wanted_system_tests`, `vehicle_driver_pose_tests`,
  `traffic_determinism_tests`, `traffic_runtime_tests`, and
  `audio_vehicle_runtime_tests`.
- The isolated live check passes the entire sequence in 2,000 simulation
  steps and 1,905 rendered frames, with a clean GL error queue. The check
  exits early once all phases and captures succeed. Screenshot stalls and
  streaming warnings remain, so this is behavior evidence, not a performance
  benchmark.
- The simulation purity guard passes all 195 checked files.

Local evidence is retained in `build/police-officer-qa/`: `gameplay.log`,
`focused-tests.log`, `driver-labs-build.log`, and the `verified.*.png`
captures. Exit, foot pursuit, re-entry with the door open, and the seated
driver after departure were inspected in the actual game renders. Blue and
tan character walk captures are in the `characters/` subdirectory.

## Arrest validation on 2026-09-07

After adding arrest, the game rebuild and all seven affected suites pass:
`police_arrest_tests`, `police_officer_tests`, `police_offense_tests`,
`police_runtime_tests`, `wanted_system_tests`, `traffic_determinism_tests`,
and `traffic_runtime_tests`. The officer lifecycle test also verifies that
an officer reaches the arrest radius naturally. The simulation purity guard
passes 196 files, and the app bundle matches the game executable.

The live sequence now includes a real arrest instead of scripted standdown.
It passes after 2,345 simulation steps and 2,017 rendered frames, with one
arrest report and a clean GL error queue. The `ARRESTED` banner, cleared wanted
stars, and nearby officer were visually checked in the captured game frame.
Evidence is in `build/police-officer-qa/arrest-tests.log`,
`arrest-gameplay.log`, and `arrest.*.png`. Repeat the command above with
`--screenshot build/police-officer-qa/arrest` to reproduce these captures.
