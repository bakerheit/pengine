# pengine-apricot

A small **C++17 / OpenGL 3.3** game engine built around one idea: **the
simulation is a pure function, and the hardware is somewhere else entirely.**

apricot is the successor to `pengine`, the engine behind two shipped games. It
keeps what those paid for and deliberately changes three things; see
[What changed from pengine](#what-changed-from-pengine).

**The pilot world has two states:** O'Haven, whose GTA-style authored city
Pinatty is rebuilt from `probablecause`, and Florangia, a low subtropical state
southeast of it. Pinatty's city design is in
[`docs/design/pinatty.md`](docs/design/pinatty.md); Florangia's terrain pass is in
[`docs/design/florangia.md`](docs/design/florangia.md).

> **O'Haven has roads and its first roadside block; Florangia has its first
> land and biome pass.** `src/city/`
> is real: the map — ten
> district polygons with their character parameters, thirty landmarks, and the
> terrain operators the height field evaluates (PENG-41) — and the road network,
> 99 authored spines and 53.6 km of centreline that bake into 310,160 triangles
> and draw. You can drive from any district to any other; a headless suite does
> exactly that with the real vehicle, five times, across the island. The spawn
> now has a solid gas station, a two-storey U-shaped motel, a three-storey
> apartment block, and a small drive-through restaurant. Nearby traffic now
> follows the real lane graph, makes deterministic right-of-way decisions,
> obeys working traffic lights and stop signs, brakes for the player, and is
> solid to the player. Play now starts on foot beside the car, with a third-person
> character controller, enter/exit flow, and deterministic pedestrians drawn
> from eight stable civilian looks. The pack rigs use the original Probable
> Cause breathing, walking, and sprinting clips with continuous skeletal
> interpolation.
> Police, missions, and the wider building pass
> are still design only, so keep reading that document as a plan for those. There was previously a placeholder sample game,
> Apricot Rally, a time trial with checkpoints and lap timing; it was deleted in
> PENG-23 because it was scaffolding that read as design.

> **Status: 0.1.0, early.** The architecture is settled and enforced by the
> build. What runs today is an engine and a streamed procedural world, not a
> game. This README says which is which, and never the other way round.

## The three ideas

**The link graph is the architecture.** `apricot_sim` links glm and Threads,
and nothing else — no windowing, no GL, no audio device. `apricot_host` is
everything that touches hardware. Dependencies flow sim → host → exe, and
`tools/guard_sim_purity.sh` fails the build before a compiler ever runs if that
stops being true. Every test links only `apricot_sim`, so "keep GL out of the
logic layer" is structural rather than aspirational.

**Determinism is the testing strategy.** The sim steps at a fixed 120 Hz
consuming a POD `InputFrame`. Record those frames as a tape and you have replay
in-game *and* a bit-exact headless regression test, from one mechanism. The
world is a pure function of a 64-bit seed; there is no `rand()` and no
time-seeding anywhere. The tape and its version live in `core/replay_tape.h`,
not in a game, so the guarantee outlives whatever is built on top —
`tests/sim_determinism_tests.cpp` includes nothing from `game/` and did not move
a line when the sample game was deleted.

**Modules own their build.** The root `CMakeLists.txt` declares three
source-less targets and `add_subdirectory`s. Each `src/<mod>/CMakeLists.txt`
attaches its own files with `target_sources()`, so agents working different
modules never open the same build file.

The reasoning behind each of these, and what it cost to learn, is in
[`docs/architecture.md`](docs/architecture.md).

## Build and run

Requires **CMake ≥ 3.24** and a C++17 compiler. Dependencies (SDL2, glm, glad2,
stb, miniaudio, Dear ImGui) are fetched at configure time at pinned versions —
a fresh clone plus a configure is the whole setup. There are no vendored blobs
and no system packages.

```sh
cmake -S . -B build        # configure (first run clones dependencies)
cmake --build build -j     # build (-Werror is on for our targets)
./build/bin/apricot        # run
```

The first configure takes a few minutes because it clones SDL2. Subsequent ones
are seconds.

### Asset labs

The **Marlin Sprint 22** is a fictional cream/seafoam speedboat moored at the
Ostend small-craft landing, world `(-2046, 0, -603.4)`. Use F1 → Teleport →
Ostend Boatworks, walk beside the cockpit and press E (controller A) to board.
W/S or the driving triggers control forward/reverse; A/D or left stick steers.
Space (handbrake binding) slows the boat; R recovers it to its home berth.
Stop beside a clear dock or shore and press E to exit. Open-water exits are
blocked until swimming exists. Handling uses fixed-step water drag, lateral
slip, shallow-water/hull collision and restrained visual bobbing—not car physics.
Boarding and exit take three seconds, with staggered steps over the gunwale;
movement is locked until the animation finishes. The supplied player character
stays visible in the helm seat, with fitted bent legs, wheel-reaching hands,
small steering/idle motions and the same rocking transform as the boat.
`apricot_boat_driver_lab --view cockpit --screenshot build/marlin-pilot.png`
checks the production renderer; `--transition enter --sequence build/marlin-entry`
captures the animation. `boat_driver_pose_tests` validates seat fit, preserved
limb lengths, smooth transitions and hull-relative attachment.
The parked boat stays in place. It is not a road-car menu entry.
Its joined 912-triangle hull includes an open cockpit, seats, a split windshield
and swim step, with one 256x256 RGBA pixel atlas. Regenerate with
`python3 tools/make_marlin_sprint_assets.py` (`--blockout` for the shape review).
The editable Blender source and cooked body stay under ignored
`assets/models/vehicles/marlin_sprint/`; the UV guide, four-view preview and
structural fit report are written to `build/marlin-sprint-*`.

The **Ostend Boatworks dock** is authored in `src/city/marina.cpp`: a 32 m
timber main pier, two berthing fingers, and a connected service deck with an
open-front shed. Its 511 pieces include 133 deck boards, piled supports,
cleats, rope coils, rubber fenders, ladders, life rings, power pedestals,
workbenches, shelving, cargo crates, drums and four working warm downlights.
The Marlin's original berth remains clear. `marina_tests` checks support and
character clearance along the main pier, both fingers and the shed approach.
Regenerate the four dock textures with `python3 tools/make_marina_textures.py`.
For a shore-side view, run with `--start-at -2033 -600 --start-heading 100`;
add `--night` to inspect the lights. `--boat-check --frames 300` exercises real
boarding, dock exit, departure, steering, braking and unsafe-exit rejection.
`boat_tests` covers deterministic handling and collision without a window.

`apricot_asset_lab` previews cooked assets through the same `.emesh` reader,
texture loader, shaders, scene culling, and renderer used by the game. Use it
for static meshes and textures.

The pistol wheel now feeds a timed weapon-use state and an upper-body pose layer.
Hold Tab/LB, select the pistol, then hold RMB/LT or toggle Q to aim, LMB/RT to fire one shot
per press, and R/X to reload. The first uncaptured left click captures the
mouse. While armed, the controller's left stick moves the player; the triggers
are reserved for aim/fire. Aiming uses a shoulder camera and slower movement.
The magazine holds 12 rounds with 48 in reserve; reload takes 1.35 seconds.
The gun has slide recoil, an empty-mag slide lock, muzzle flash and magazine
movement. The character keeps its locomotion while raising both hands to aim,
kicking back on shots, and reaching for the magazine during reload.

The outlined crosshair follows the shoulder view; aiming tightens the view and
keeps the player's body facing the shot. Shots trace from the camera and then
the gun muzzle so nearby cover still blocks them. Pedestrian hits show a red
hit marker, knock the target down, and emit the original Probable Cause-style
short blood spray. Pedestrians recover through their existing activity system;
this does not add permanent deaths, vehicle damage, ammo pickups, or checkpoint
persistence for ammo. World hits show brief surface sparks.
Pistol shots use the original Probable Cause
`Glock17_Shoot_004.wav` recording (stereo, 44.1 kHz), with generated PCM as a
missing-file fallback. Reload feedback remains generated. Sample bounds are
tested, but live listening is not part of the automated check.
`--weapon-check --frames 900 --daylight --clear --screenshot build/weapon-check`
exercises equip/cancel/unarmed, aim, semi-auto fire, reload, held-R repeats,
and blocked wheel clicks, then Q aim, a flick-and-click hit on a real spawned
pedestrian, blood expiry, a ground hit, and Q release. Captures happen before
buffer swap and include aim and body-hit views.
The focused suites are `weapon_wheel_tests`, `weapon_use_tests`,
`weapon_pose_tests`, `weapon_visual_pose_tests`, `weapon_aim_tests`,
`weapon_hit_tests`, `blood_particles_tests`, and `ped_impact_pose_tests`.

**The characters have thirteen clips**, not three: `idle` `walk` `sprint`
`pistol_idle` `pistol_walk` `pistol_run` `punch_left` `punch_right`
`hit_by_car` `die_forward` `die_backward` `stand_up` `jump`. They are lifted
from Probable Cause's cooked set rather than re-cooked — Characters_psx 1.1
changed the FBX rest-space axes, and re-cooking a locomotion clip from it
rotates every pose onto its back. `assets/models/` is **gitignored**, so a
fresh clone stages them with:

```sh
python3 tools/lift_character_animations.py          # verify and publish
python3 tools/lift_character_animations.py --check  # verify only
```

`src/app/character_animation.h` is the registry and the state machine — idle
(with hash-derived variety), walk, run, jump, punch, flinch, knockdown, downed,
get-up and die, crossfaded between. It is header-only and app-side, so
`character_animation_tests` drives the real thing headless. It is also the
caller `src/city/character_punch.h` and `src/city/character_getup.h` were
written for; `CharacterAnimInput` is the plain-data seam a pedestrian or the
player fills in.

```sh
cmake --build build --target apricot_asset_lab apricot_character_lab -j

# Play the production player rig, texture, shader, and walk clip.
./build/bin/apricot_character_lab \
  --asset-dir assets/models/characters/psx_pack/player_male_01 \
  --clip walk --height 1.76

# Drive the REAL state machine through a scripted timeline: idle, walk,
# sprint, two alternating jabs, a flinch, a knockdown, the prone hold, the
# get-up, and a death. Prints every state change and every punch contact.
./build/bin/apricot_character_lab \
  --asset-dir assets/models/characters/psx_pack/player_male_01 --states

# Repeatable renderer capture or bind-pose inspection.
./build/bin/apricot_character_lab \
  --asset-dir assets/models/characters/psx_pack/player_male_01 \
  --clip walk --height 1.76 --frames 61 \
  --screenshot build/character-lab-player-walk.png
./build/bin/apricot_character_lab \
  --asset-dir assets/models/characters/psx_pack/player_male_01 \
  --clip walk --bind --frames 1

# Texture-only inspection keeps the image's real aspect ratio.
./build/bin/apricot_asset_lab --texture assets/textures/example.png
```

The character lab uses the production dual-quaternion skinning shader. Use A/D
to rotate, Q/E to zoom, Space to pause, the arrow keys to scrub, and R to reset.
The ground axes make facing explicit: red is +X, green is +Y, and blue is
Apricot forward (-Z). Under `--states` the timeline runs at a fixed 60 Hz, so
`--frames N` lands on second `N/60` and `--screenshot` is repeatable at a chosen
moment. A single-clip preview obeys the registry's root policy, so a death clip
carries the body forward instead of collapsing on the spot.

**In game, F (or controller X) throws a bare-fist jab** while on foot and
unarmed. It is presentation only today: the swing, the alternating fists and
the one-shot contact latch all run, and nothing takes damage yet.

```
apricot 0.1.0

  --verbose       log at debug level
  --log FILE      also append the log to FILE
  --frames N      render N frames, print a summary, then exit
  --no-instancing start on the naive per-node draw path
  --warp-every N  teleport across the island every N frames
  --start-at X Z   start at a world position for visual QA
  --version       print the version and exit
  --help          this text
```

**What you get today.** A title/pause menu and full Pinatty city map, plus a
drivable car on **streamed procedural terrain with Pinatty's road network on
it**, under a moving sky and a debug overlay. The sim runs at a fixed 120 Hz. The terrain loads and
unloads around the car in four level-of-detail rings out to 2.3 km, with props
scattered on the near two. The roads are baked once at startup into six meshes
by material: measured on an M5, 129,484 triangles for 7.86 MB of vertex data,
and the whole thing runs at **3.80 ms a frame (263 FPS) over 1199 frames**.

The moving sun and moon light every authored model and terrain mesh. At dusk
the player car adds two soft-edged headlights that follow its interpolated pose,
light the road surface and fade out automatically under the visible daytime sky.

There are no missions yet. The player car uses the wheel-less Car 5 body from
the Probable Cause alpha plus four copies of its shared wheel model.
Those wheels follow the real suspension, steer at the front and spin from the
sim-owned wheel state. Active traffic uses the alpha's Car 5, Car 8, and
ambulance bodies with the same independent wheel setup. It follows the authored
lanes, reads driver-specific yellow lights and stop dwell times, yields to
cross traffic, slows for turns, brakes for the player's predicted path, and
collides with the player if avoidance fails. Those impacts now move both
vehicles: traffic can be shoved and yawed off its lane, then settles back into
flow by slowing and steering forward instead of sliding back to its old pose.
Wall, player-versus-traffic, and traffic-versus-traffic impacts also accumulate
in six persistent body regions. The shared vertex shader crushes the struck
front corner, rear corner, or door area per car while the independently driven
wheels keep their real suspension pose.
AI traffic also has deterministic body-to-body collision now, and cars claim a
signal box at a geometry-derived stop line so conflicting approaches cannot
flood it. The line expands with the widest road through each junction, keeping
the whole car outside even at the 22 m arterial intersections. Cars
wait outside a junction until their destination lane has room for the whole
vehicle, then try an open alternate turn after a driver-specific patience delay
instead of feeding a permanent jam.
Cars following the exact same movement use a conservative moving-leader
projection, so a green queue can enter as a platoon instead of waiting one full
signal cycle per car; stopped downstream traffic still blocks admission.
Once admitted, traffic stays committed until its rear clears that same computed
box: it ignores later signals while leaving, low-speed rubbing no longer re-arms
the crash pause, merge conflicts are reserved one at a time, and a stalled car
gets a GTA-style clearance throttle rather than parking in the intersection.
Trees remain three-box stand-ins; where they grow is real. The 420 m disc and
1400 boxes that used to be here were deleted in PENG-27.

`--frames N` runs the whole thing with nobody at the keyboard and reports what
it drew, which is how the numbers below were produced rather than remembered:

```
$ ./build/bin/apricot --frames 1500
GL 4.1 core (Apple M5), window 2560x1440 drawable
world: seed 0x00000000DEADBEEF, rings 4/10/20/36 chunks
       (256/640/1280/2304 m), scatter to level 1
cold fill: 2 steps, 71.1 ms, 29 chunks, 8.9 MB of terrain
map 0x00000000DEADBEEF, run seed 0xA5EED0FFC0FFEE11,
    car spawned at 12.56 m (ground 12.00 m)
quit after 711 sim steps (5.92 s of sim time), 1500 frames
last frame: 4672 visible nodes, 8 batches (7 instanced), 1070 draw calls,
            4672 instances, longest run 896, 2136 binds skipped
terrain: 4053 chunks resident (lod 49 / 268 / 940 / 2796), 4061 meshes,
         79.6 MB of vertex data
costs: cull 0.547 ms (peak 1.651), meshing 0.06 ms (peak 12.50),
       last fill 71.1 ms in 2 steps
streaming spikes over 4.0 ms: 2 of 1500 frames
GL error queue clean for the whole session
```

1500 frames in 6.3–6.7 s wall is **4.2–4.5 ms a frame** at 2560×1440 with vsync
off (the spread is run-to-run on the same binary, not a range across machines).

The 79.6 MB is the point of the LOD rings: those same 4053 chunks at full detail
would be 1.30 GB. One run of 896 instances collapsing into a single draw is the
batching working on the scattered props; press `F7` (or pass `--no-instancing`)
to watch that collapse to one draw per node: 4672 draws instead of 1070, and the
same 1500 frames take 14.8 s instead of 6.7 s. Terrain chunks each carry a
unique mesh and so are one draw each by construction, which is most of that
1070.

The two spikes are both on the first budgeted frame after a fill and are the
process committing pages as it grows from 8.9 MB toward 80 MB; removing the
streamer's per-step allocation did not move them, so that attribution is a
best explanation rather than an isolated cause. Steady-state meshing is 0.06 ms.

`--warp-every N` teleports across the island on a timer, which is how the
fill-before-resume path gets exercised without a human remembering to press
`F8`. Thirteen warps in one run: every one filled in 2 steps and 73–78 ms and
resumed on full-detail ground, with resident memory flat at 67–89 MB.

What is deliberately *not* there — no transparency pass, no shadows, no shader
hot-reload, no terrain splat shader — is listed in
[`src/gfx/README.md`](src/gfx/README.md).

### Tests

```sh
tools/ci.sh    # purity guard + configure + -Werror build + headless ctest
```

That is the gate. Every suite is headless — no window, no GL context, no audio
device — because every suite links `apricot_sim` and only `apricot_sim`.

## Controls

Mapped in `src/platform/input.cpp`, with a gamepad path alongside the keyboard.
The mapping is real; how much of it anything *acts on* is not, so both columns
are here.

| Input | Intent | Works today |
|---|---|---|
| `A` / `D` | strafe or steer left / right | **yes** — camera-relative movement on foot; progressive, rate-limited steering with Ackermann front-wheel angles in the car |
| `W` | walk forward or throttle | **yes** — camera-relative movement on foot; engine torque through the gearbox in the car |
| `S` | walk backward or brake / reverse | **yes** — camera-relative movement on foot; braking then reverse in the car |
| `Space` / controller left-stick click | jump on foot | **yes** — tap to jump; air movement, ceiling collision, and animated landing |
| `Space` (driving) | handbrake (analogue) | **yes** — shrinks rear grip, breaks the back loose |
| `LShift` / `LCtrl` | sprint or shift up / down | **yes** — Shift sprints on foot; both keys operate the manual gearbox in the car |
| `E` / controller `A` | enter / steal / exit road vehicle | **yes** — approach either front door of a stopped car; occupied traffic is taken over; moving or obstructed exits are blocked |
| `F1` | developer menu | **yes** — pauses play and opens a GTA-style trainer menu; Classic GTA (VC / SA feel) is the default, Driving Mechanics can switch the player live between all nine handling styles, and Vehicle can repair the current car in place |
| `F2` | report a bug to Codex | **yes** — type a report; Apricot attaches the game-window screenshot and exact world position |
| `F3` | toggle debug stats | **yes** — the large profiling panel starts hidden and can be shown or hidden during a drive |
| `F7` | toggle instancing | **yes** — the batching A/B, handled outside `InputFrame` on purpose |
| `F8` | teleport across the island | **yes** — evicts the world, refills the near ring before resuming. Outside `InputFrame` for the same reason as `F7` |
| `Esc` / `B` | back / pause | **yes** — closes the map, resumes from pause, or opens pause while driving |
| `Ctrl+Q`, `Cmd+Q` | quit | **yes** |
| `R` | respawn | **yes on foot** — returns the player beside the car |
| `C` | cycle camera / map layer | **yes** — cycles near, chase and far distances; in the map, cycles Explore, Roads and Places (controller Y) |
| `P` / controller Start | pause | **yes** — freezes sim time and opens resume, map, restart, and title options |
| `M` / controller Back | city map | **yes** — full-width atlas with smooth vector coastlines, terrain contours, cased roads, readable labels, building footprints, location icons, and live player heading. Wheel or `+/-` zooms; WASD/stick or mouse drag pans; Return/A recentres. See [map viewer notes](docs/map-viewer.md) |
| `B` | look back | **yes** — instant rear view, clean return on release |
| `Return` / `A`, `Backspace` / `B` | accept / back | **yes** — keyboard, gamepad, and mouse menu navigation |
| Mouse | look | **yes** — left-click captures; free third-person look on foot and auto-centring orbit in the car |

Road-vehicle entry is door-based, with a 1.65 m reach and a 1.5 m/s maximum
entry/exit speed. Taking traffic keeps its model, paint and dent stamps and
retires its exact AI identity. Previously driven cars remain parked, solid and
re-enterable for the session; traffic brakes for them. This first pass uses
instant transitions, without driver-pullout animations, theft-specific police
responses, save persistence, or airplane boarding/flight. Bank interactions
still take priority when standing at a bank interaction point.

`build/bin/apricot --frames 2400 --vehicle-entry-check` exercises takeover of a
real stopped traffic car, preserved paint/damage, parked-car re-entry and
blocked/safe exits. It exits with failure if the flow never completes. The
headless `vehicle_interaction_tests` and traffic runtime suite also cover door
reach, speed/roof restrictions, collision activation and no AI respawn.

Steering, throttle, brake, handbrake and both shift edges are pinned by
`tests/vehicle_tests.cpp` against real terrain — the car settles on its springs,
transfers load under braking and cornering, keeps an ordinary powered turn
under two degrees of measured body slip, recovers after a handbrake slide, rights
itself when flipped, and takes deterministic health damage at the actual body
region that struck a solid prop instead of driving through it. Front and rear
anti-roll bars couple the independent struts without making landings harsher.

The chase camera keeps its focus on the centre of the chassis, pulls back and
widens its FOV with speed, anticipates turning, supports held look-back, and raycasts against terrain and
authored buildings so it pulls in before clipping through them. Damage is shown
in the HUD and as persistent six-region dents on the Car 5 body, with an impact
flash/camera kick and weaker headlights as health drops. AI sedans, trucks, and
ambulances use the same per-car deformation state without cloning their shared
meshes. The separately moving wheel models remain driven by their suspension,
steering and spin state; body damage never deforms or scales the wheels.

The remaining unconsumed row is honest rather than aspirational: it is recorded
into the replay tape correctly, because the tape stores intent, not motion.

## Repo map

```
CMakeLists.txt   three targets, pinned dependencies, strict-warning flags.
                 Declares no sources — the modules do that.
VERSION          semver, single source of truth. Flows into project() and
                 the APRICOT_VERSION macro logged on the first line of output.

src/
  core/          InputFrame and the ReplayTape that carries it (the replay
                 format, and its version), the fixed-step clock, deterministic
                 hashing and RNG, AABB/frustum/transform maths, logging,
                 asset-root resolution.                     -> apricot_sim
  scene/         node storage, world transforms, frustum + distance culling,
                 draw-batch planning. Plans draws, never issues them.
                                                            -> apricot_sim
  terrain/       the height field (a pure function, not a file), chunk meshing
                 at four levels of detail with skirts, and the residency
                 streamer that decides which chunks exist, at what level, and
                 when to let go of them.                    -> apricot_sim
  road/          authored spines -> a planar road graph, a ribbon bake that
                 emits PLAIN vertex arrays and owns no GPU resource, and the
                 lane graph traffic and police drive on.    -> apricot_sim
                 (see src/road/README.md)
                 meshing, and the residency streamer. height_at() evaluates
                 city/'s terrain operators as its last step.
                                                            -> apricot_sim
  city/          PINATTY'S MAP AND ROADS, as constexpr C++ tables: district
                 polygons and character parameters, landmarks, the five terrain
                 operators (Flatten, Bench, Carve, Mound, Grade), and the 92
                 authored road spines. Every road corridor operator is DERIVED
                 from the road table rather than written beside it. Not a data
                 file, on purpose — see src/city/map.h.     -> apricot_sim
  traffic/       the ambient population: the analytic phantom schedule every
                 lane carries, and the bounded active set instantiated out of
                 it on approach.                            -> apricot_sim
                 (see src/traffic/README.md)
  physics/       terrain collision and vehicle dynamics. Every step function
                 is pure in (state, input, collider, dt).   -> apricot_sim
  game/          the pilot game's sim-side rules. Currently ONE file:
                 conditions.{h,cpp}, deterministic time-of-day and weather
                 feeding VehicleTuning::grip_scale. Pinatty lands here.
                                                            -> apricot_sim
  audio/         SPLIT. mixer.h + synth.cpp are pure maths  -> apricot_sim
                 device.cpp + miniaudio_impl.c own hardware -> apricot_host
                 (see src/audio/README.md)
  platform/      the window, the GL context, and the translation of raw device
                 events into InputFrame.                    -> apricot_host
  gfx/           the only code in the engine allowed to call GL: the bind
                 cache, shaders, meshes, camera, and the upload seam for
                 road/'s ribbon bake.                       -> apricot_host
                 (see src/gfx/README.md)
  app/           the frame loop, the debug overlay, and world.{h,cpp} -- the
                 host half of streaming: build, upload, deliver, free. Wiring
                 only; it holds the program's ONE wall clock.
                                                            -> apricot (exe)
  main.cpp       argument parsing and not much else.

assets/                  GLSL plus cooked legacy vehicle bodies and paint.
tools/
  ci.sh                  the gate: guard, configure, -Werror build, ctest
  guard_sim_purity.sh    the architecture, enforced. Runs first.
tests/                   headless suites; each links apricot_sim only
docs/architecture.md     the design rules, each with what it cost to learn
docs/design/pinatty.md   the pilot game's map. Its section 3 road hierarchy is
                         implemented in src/road/ and authored in src/city/;
                         the map tables and the road network are written and
                         wired. Basic traffic and the opening buildings are
                         live; police, missions, and the wider city build are not.
```

`assets/` keeps runtime files grouped by kind: GLSL under `shaders/`, cooked
vehicle geometry under `models/vehicles/`, and matching paint under
`textures/vehicles/`. Terrain remains procedural while authored scenery uses
project-local albedo maps, player acceleration uses recorded WAVs without a
procedural vehicle bed, and the glyph atlas is still drawn in code. See
[`assets/README.md`](assets/README.md) for layout and imported-asset provenance.

## What changed from pengine

pengine shipped two games (`probablecause`, a GTA-style open world;
`backroomsrewind`, procedural horror) and everything in it survived contact
with real gameplay. apricot keeps its rules — they are restated with their
costs in [`docs/architecture.md`](docs/architecture.md) — and changes three
things on purpose.

(Pinatty is a rebuild of `probablecause`'s world, not a port. Its design and
algorithms are reference material; none of its code is coming across, and
several of its systems — shared `mt19937` streams, a `std::time` seed, a
wall-clock read below the frame loop — are things apricot bans outright.
`docs/design/pinatty.md` §7.2 names them.)

**1. Module-owned CMake fragments, instead of one root file.** pengine keeps
every target and all 27 `add_test` blocks in one 486-line root file, and
probablecause's has grown to 781 lines and 32. Not huge — but every agent
adding a file edits the same one. Here the root declares three empty targets
and each module attaches its own sources, so two agents working different
modules never open the same build file. Registering a test is one line.

Measured on this repo: seven agents built seven modules in parallel and the
only file they ever collided on was `tests/CMakeLists.txt`, which is shared by
design. Zero conflicts in any module fragment.

**2. A sim/host split enforced by the link graph, instead of by convention.**
pengine was one library, so a test could link anything and "keep GL out of the
logic layer" was a rule people had to remember. Conventions erode one innocent
`#include` at a time; by the end `tools/ci.sh` carried a hand-maintained
`EXCLUDE` list of suites that could not run headless. Here `apricot_sim`
physically cannot see the hardware layer, a text-search guard catches the
attempt before the compiler does, and "test the real producer" is the only
thing the build permits.

**3. Determinism as the testing strategy, not just a nice property.** pengine
had a fixed timestep and latched input edges. apricot takes the same idea and
makes it the *product*: the sim consumes a POD `InputFrame` at a fixed 120 Hz,
and recording that stream gives in-game replay and bit-exact headless regression
from one mechanism. Accounting that used to be incidental is now pinned by
tests — the accumulator is held in step units so the arithmetic is exact, the
step clamp discards rather than repays, and golden hash values make any change
to world generation a deliberate, visible decision.

That third one has already earned itself. `hash_coord(0, 0, 0)` returned zero,
because splitmix64's finaliser has zero as a fixed point — so the terrain
lattice was degenerate at the world origin, which is where every test, every
default-constructed state and every fresh run begins. A gamma offset fixes it;
a regression test and a set of golden values keep it fixed. Nothing about
driving the game would ever have found that.
