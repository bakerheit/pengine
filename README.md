# pengine-apricot

**Probable Cause** is the open-world crime game being built on Apricot.
Start with the [Design & Lore documentation](docs/README.md) for the game's
direction, world, characters, businesses and vehicle makers. This README
covers the engine, build and feature-specific development workflows.

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

> **Development status, source review 2026-09-12:** the checkout contains an
> open-world game slice with authored cities and interiors, on-foot and vehicle
> traversal, traffic, police response, combat, an opening scene, one delivery
> mission and checkpoint saves. It does not yet contain a mission campaign.
> See [Game design](docs/design/README.md) for the current scope and open
> decisions, and the linked feature records for their dated runtime evidence.
> This overview is not a fresh validation of all those systems.

The former Apricot Rally sample was removed in PENG-23. Its time trials,
checkpoints and ghost car are not the design for Probable Cause.

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

### Windows

Windows builds are cross-compiled here with MinGW-w64 (`brew install
mingw-w64`); the Windows machine needs no toolchain.

```sh
tools/build_windows.sh          # build-win/dist/ProbableCause-<VERSION>-Windows-x64.zip
tools/build_windows.sh --push   # ...and unpack it on the Windows test PC's Desktop
tools/build_windows.sh --test   # run every headless suite on that PC
```

The package is `apricot.exe`, static and needing no DLLs, beside a copy of
`assets/`, so packaging needs the gitignored `assets/models/` here. The PC is
reached over SSH at `$PC_HOST` or the gitignored `tools/.pc_host`, with the key
`~/.ssh/pcgame_winbox`; the script's header has the rest.

**What has run on Windows** (Windows 10, Radeon RX 580): the exe starts and
reports its version, and 219 of the 220 suites `--test` sends pass. The one
that fails is `vehicle_driver_pose_tests`: cruiser 91-C's exit pops the right
hand 7.6 cm in one tick. The Mac passes it only because Apple clang fuses
multiply-adds and x86-64 gcc does not; a Mac tree configured with
`-DCMAKE_CXX_FLAGS=-ffp-contract=off` reproduces the Windows numbers exactly,
which is the quick way to chase a Windows-only failure. **Nobody has played the
game on Windows yet:** its window, rendering, input and audio are unseen there.

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

The **Halberd Gunship** is a low-poly attack helicopter parked on the east
apron at Halberd Field, the north-shore military air station, world
`(-360, 9.1, -2084)`. Use F1 -> Teleport -> Halberd Field, or drive in through
the main gate off the Yard Road. Walk to the port side of the cockpit and press
E (controller A) to board; the rotor spools for about two seconds before it
will lift. SHIFT/CTRL are the collective (climb/descend), W/S the cyclic (nose
down to fly forward, nose up to slow), A/D the pedals (yaw on the spot). R
resets it to its stand. Land and come to a stop to get out.

It is not car physics and not `game/aircraft.h` either. Thrust points along the
machine's OWN up axis, so tilting is how it translates and there is no stall
speed to fall out of; neutral collective auto-trims to hold the altitude it has
at whatever attitude it is in, which is the forgiving part. Holding descend
lands it, because the descent rate is deliberately kept inside the gear's
limit. What ends badly is arriving with the speed still on, putting it in the
sea, or letting the 14 m rotor disc find something the fuselage would have
cleared -- the disc reaches 7 m out, where there is no airframe at all, and it
is swept for collision separately.

The airframe and its rotor are two meshes, two materials and two scene nodes:
the disc is a single alpha-cut quad cooked recentred on its hub, so the host
spins it about its own Y axis without the helicopter following it round.
Unlike every other vehicle here it is converted rather than authored --
`python3 tools/cook_psx_helicopter.py --source <zip>` turns the supplied OBJ
into `.emesh`. Provenance, hashes and an unfilled licence gap are recorded in
`assets/textures/vehicles/psx_helicopter/SOURCES.md`. The stand, entry point
and compound collision are constants in `src/city/halberd_helicopter.h`.
`--helicopter-check --frames 120` exercises real boarding, apron exit,
re-entry, a vertical takeoff against the station's own blast walls and masts,
a rejected airborne exit, and a landing. `helicopter_tests` covers the hover
trim, the spool gate, the pedal turn, the touchdown limits and the rotor-rim
sweep headlessly; `north_airbase_tests` checks the stand is on paving with a
clear disc.

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

**The weapon wheel has three slots**: unarmed straight up, the pistol down and
to the right, the molotov down and to the left. Keys `1`, `2` and `3` pick them
without the mouse. **It equips only what you own:** a new game has bare hands and
molotovs, and the pistol shows `LOCKED` until you buy it at Brassline Arms on
Sixth Street ($500, plus $50 boxes of 24 rounds up to 240 spare; see
[the gun store](docs/design/gun-store.md)). It feeds a timed weapon-use state and an upper-body pose layer.
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
hit marker and emit the original Probable Cause-style short blood spray.

**Everybody in Pinatty carries the same hundred points** — the player, the
crowd and the officers — on the one scale in
[`src/city/body_damage.h`](src/city/body_damage.h). Three centre-mass pistol
rounds kill, or four punches; being run over kills at thirty miles an hour and
bruises below it; falls and crashes have their own curves. A round that does
not kill wounds: the victim flinches and runs, and carries the missing health
until they leave the city. A round that does kill leaves a body, and the body
stays where it fell until the streamer retires it — there is no get-up. Killing
an officer is permanent too, and his cruiser is abandoned where he parked it.

Dying is a state the player spends three seconds in: control is frozen, the
body plays the same authored fall a shot pedestrian does, WASTED holds for the
whole of it, and the respawn puts you beside your car at full health with the
pursuit cleared and the pistol holstered. You keep the weapons you own and the
pistol keeps the rounds it had, so bought ammunition is not refilled by dying;
molotovs, which nobody sells, come back full. Player health is not saved;
the current checkpoint does preserve vehicle damage and mechanical condition
(see [Saved games](docs/save-games.md)). A respawn is always beside the current
vehicle — there is no hospital routing yet. World hits show brief surface sparks.
Pistol shots use the original Probable Cause
`Glock17_Shoot_004.wav` recording (stereo, 44.1 kHz), with generated PCM as a
missing-file fallback. Reload feedback remains generated. Sample bounds are
tested, but live listening is not part of the automated check.
`--damage-check --frames 2400 --screenshot build/damage-check` drives the whole
damage model in the real game: three aimed rounds into a real spawned
pedestrian (wound, wound, kill), the body still on the pavement eight seconds
later, then the player killed — frozen, WASTED, and respawned whole. It exists
because the headless suites cannot see a corpse standing back up or a banner
playing over a world the player is already driving around in, and both of those
broke at some point while every test passed.

**The player carries a wallet.** The cash is on the HUD, a green figure in a
plate beside the clock, and any change to it flashes beside the plate for a
couple of seconds (`+$750` green, `-$400` red). A new game starts with $250.
Lou's delivery pays $750 on completion, shown on the Mission Success card. An
arrest fines $100 doubled per star ($200 at one, $3,200 at five), shown under
ARRESTED; dying costs a $250 hospital bill, shown under WASTED. A penalty takes
what the wallet holds and stops at zero, and the banner says what was billed
and what was paid. Amounts and the reasoning are in
[`docs/economy.md`](docs/economy.md); rules in `game/wallet_rules.h`
(`wallet_rules_tests`); the App side in `app/wallet_gameplay.cpp`. The F1
menu's PLAYER & VEHICLE page has ADD $1,000 CASH for testing.
`--wallet-check --frames 3000` plays and skips the real delivery cutscene,
arrests at two stars, kills the player, then arrests at five stars with less
than the fine in hand, checking the balance after each and leaving five
screenshots in `build/wallet-check.*.png`. The arrests go through the same
`arrest_player()` a real officer's arrest does, but the check does not stage
the officer — `--police-officer-check` does that. **Not there yet:** arrest
does not jail, relocate or confiscate; Brassline Arms is the only place that
takes the cash; not yet played by hand.

**Rook's Auto Repair resprays cars.** Drive into a bay, stop, press R or pad X:
the world pauses, the camera swings round to frame the car beside a paint
picker (24 presets, or a custom hue bar and saturation/value plane), and the
car wears the colour you are pointing at. Confirm and it sprays for a second.
If no cop saw you pull in, the stars go with the old paint; if one did, you get
the paint and keep the stars. The colour is a CPU recolour of only the paint
texels of whatever atlas the car wears — glass, chrome, lamps, lightbars and
livery lettering stay — driven by per-car paint profiles in
`tools/paint_profiles/`; see [`docs/architecture.md`](docs/architecture.md)
(Rendering) for why. `--paint-check --frames 18000` does it all in the real
game, every car driven in from the forecourt and every booth action a pushed
key or click: profile stats against the real PNGs, the lot hint, an unseen
roll-in, getting out and back in, the booth (sim paused, key repeat ignored,
cancel from its button, preset and custom drags), a respray that clears three
stars, a cancel by rolling, a stolen Car 8 that keeps its livery and resprays
into its own slot, a save/load round trip, a pull-in watched by a real
cruiser, both police liveries with their lightbars on, a car swap mid-spray,
and a firetruck in bay two. Nine screenshots land in `build/paint-check.*.png`;
add `--night` to see the lamp and lightbar glow over a respray. Not yet tried by
hand on a pad, and its sounds have not been listened to.

**Rook's also fits car bombs.** Stopped in a bay you pulled into, press K and
the car is rigged. The trigger is refused anywhere on Rook's lot; off it, K sets
the bomb off from wherever you are, including the driver's seat. The car goes
up a metre, comes down charred, crumpled and dead, and the ground under it
burns with the molotov's fire; bodies within ten metres are hurt with falloff
and thrown (inside 3.5 m they die), and the city charges arson-level heat plus
each body. Nobody gets back into a burnt-out car, and a respawn after sitting
on your own bomb steps clear of the flames. One bomb at a time: rigging a second
car disarms the first. Rules in `game/car_bomb.h` (`car_bomb_tests`); the App
side in `app/car_bomb_gameplay.cpp`. `--car-bomb-check --frames 3600` runs both
detonations in the real game, leaves nine screenshots in
`build/car-bomb-check.*.png`, and lights a fire under a real pedestrian and
requires it to bite them. **Not there yet:** Rook's does not charge for the
bomb, so it is free; there is no pad binding; the fireball's flames and smoke
are cards off the molotov's flame atlas (smoke is that take tinted to soot, as
there is no smoke sheet yet) and its debris is still small boxes; no explosion
recording ships (it layers the crash, glass and molotov whoosh, and plays
`assets/audio/weapons/runtime/car_bomb_blast.wav` if one appears); the blast
does not touch traffic or parked cars; and none of it has been played by hand.

**Every `--frames` run is unattended.** The window is created hidden, the
process never enters the desktop's foreground and the OS cursor is never
captured, so a scripted check running on a machine somebody is also using does
not steal their keyboard or their pointer. Screenshots are unaffected: they come
off the GL drawable, which is real either way. `platform/foreground.h` records
why neither SDL hint that looks like it does this actually does it on macOS.
Pass `--attended` to watch a capped run instead.

`--weapon-check --frames 900 --daylight --clear --screenshot build/weapon-check`
exercises equip/cancel/unarmed, aim, semi-auto fire, reload, held-R repeats,
and blocked wheel clicks, then Q aim, a flick-and-click hit on a real spawned
pedestrian, blood expiry, a ground hit, and Q release. Captures happen before
buffer swap and include aim and body-hit views.
The focused suites are `weapon_wheel_tests`, `weapon_use_tests`,
`weapon_pose_tests`, `weapon_visual_pose_tests`, `weapon_aim_tests`,
`weapon_hit_tests`, `blood_particles_tests`, and `ped_impact_pose_tests`.

**The molotov is the second weapon, and it is a thrown one.** Five bottles, one
arm, one bottle at a time: aim with RMB/LT or Q, throw with LMB/RT, and the hand
is visibly empty for half a second before the next one is up. The bottle is the
contoured glass soda bottle off the Quequis House kitchen shelves, cooked out of
the supplied GLB by [`tools/cook_molotov_bottle.py`](tools/cook_molotov_bottle.py)
into a tracked header — a hundred and four triangles, painted bottle green, with a burning rag
in the neck. A hip throw lands about seven metres out and an aimed one about
eleven; the arc is lofted on purpose, because a bottle thrown down the exact
camera ray lands at your feet.

Where it breaks, the street catches. [`src/game/fire.h`](src/game/fire.h) is a
bounded grid of burning cells — 64 of them at 1.1 m — that spreads outward from
the bottle for up to five metres, burns for seven to twelve seconds a cell and
goes out. Whether a patch catches is `hash_coord3()` of that patch's own
coordinates, so the same bottle in the same place burns the same shape every
run. Flames will not climb a wall, catch on ground that is not there, or take
on a parked car, and they refuse ground at or below sea level; lighting one
costs 3.5 heat the moment it catches, with no witness needed. Standing in it takes a bite of health every 0.85
seconds, scaled by how hot the patch is (four bites at full heat) — and that
goes for pedestrians and police officers too, on one shared beat
(`game/fire_harm.h`, `fire_harm_tests`). A bitten survivor panics and runs; a
body drops where it stood; every wound and death is charged like any other the
player causes. People already on the floor are not burned. The flames are billboards off a 132-frame sprite
atlas, two cards per cell on different frames, plus one tiled spot light for the
whole fire. Glass, whoosh and the crackle loop are recorded takes; see
[`assets/audio/weapons/SOURCES.md`](assets/audio/weapons/SOURCES.md).

`--molotov-check --frames 1300 --screenshot build/molotov-check` equips the
molotov off the wheel, turns to the open street, throws one, and watches the
fire light, spread, burn the player who walks into it, and die back. Along the
way it asserts that the fire landed on the ground the player is standing on
rather than on the roof behind the wall it broke against — a bug this check
found — and that all three recorded takes reached the mixer, because a
recorded-only clip that fails to load is silent and silence is the one bug a
screenshot cannot show. Add `--night` to see the fire's own light on the road;
the check honours it instead of forcing daylight. Eight screenshots, because no
headless suite can see a flame. The focused suites are `molotov_tests`,
`fire_tests`, `weapon_wheel_tests` and `weapon_pose_tests`.

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
unarmed. The swing, the alternating fists and the one-shot contact latch run on
the render clock, and the contact spends 25 points off whoever is within an
arm's length — the same query the pistol makes, so a fist and a round cannot
disagree about who is standing in front of you. Four jabs kill somebody
untouched; fewer if they have already been shot.

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

### Reading back a session's frame rate

Recording is **off by default** and turned on from the developer menu:
**`F1` → FRAME LOGGING**. It writes a file and costs a little every frame, so a
session nobody asked to profile should not be paying for one. Each switch-on
starts a fresh file — appending would put two unrelated stretches of play under
one summary, and the percentiles of a blend are nobody's percentiles. The
`REC nnn` badge appears under the clock while it is running.

`--perf-record` records from launch instead, and `--perf-log FILE` does the
same to a chosen path.

When recording, it writes one CSV row per frame, numbered and never overwritten,
because the interesting session is always the one that already happened. The
files go next to the save game:

```
~/Library/Application Support/Bakerheit/Probable Cause/perf/session-NNN.csv
```

That folder and not `build/`, because it is writable no matter how the app was
launched. The default used to be `build/perf` resolved against the WORKING
DIRECTORY, which is right exactly once — typing `./build/bin/apricot` from the
repo root. Launch `Probable Cause.app` instead, the normal way anyone starts a
game here, and the working directory is `/`: the recorder wrote nothing, said
nothing visible, and a real play session was lost. The startup line now prints
the absolute path for the same reason.

Nothing needs to be enabled and nothing needs to be running alongside; play,
quit, then read it back:

```sh
tools/perf_report.sh              # the newest session
tools/perf_report.sh --worst 40   # more of the slow frames
```

The report opens with the recorder's own trailer — percentiles, the dip count,
the worst frames, the blocks they happened on, and the **phase breakdown**:

```
# phase              all      dips
# sim               0.90      1.70      the fixed-step loop
#   traffic         0.58      1.12        world_.step_traffic + collisions
#   police          1.52      3.98        offences, arrest, wanted
#     visible       1.51      3.97          visible_police() line of sight
#     context       0.00      0.00          the three set_police_* handoffs
#     other         0.00      0.00          the residual inside the block
#     calls          4.1       5.1          visible_police() calls per frame
#   character       0.11      0.30        the on-foot character step
#   other           0.27      0.49        the residual inside the step
# world             0.34      0.30      terrain streaming and meshing
# visual            1.61      2.23      building scene nodes from sim state
# scene             0.07      0.09      Scene::update()
# render            5.16      7.01      render(), CPU side, swap excluded
# swap              3.91     10.53      blocked on the display — vsync lives here
# unaccounted       0.11      3.30      the residual, and the honesty check
# gpu               7.38     10.01      concurrent with the CPU, not a slice of it
```

The indented rows **partition** their parent rather than adding to it, and each
`other` is the residual that keeps the split honest — the same rule at every
level. `calls` is a count rather than a duration because the same work repeated
five times a step and the same work made five times slower are
indistinguishable in a millisecond column.

**`swap` and `unaccounted` are the two that decide where to look.** Time in
swap is not work, it is the CPU blocked waiting for the display: when a dip is
mostly swap, every CPU phase above it was already fast enough and `gpu` is the
number that matters. And `unaccounted` is the residual the phases could not
explain — it exists so the breakdown can never quietly stop adding up. The
first version of this recorder logged only `cull` / `mesh` / `light` / `fill`,
which on a real 31 ms dip frame summed to 1.35 ms and left 96% of the frame
unexplained; detail that does not add up to the whole is not detail, it is a
decoy.

Then a second-by-second timeline, and the slowest frames with each one's phases
side by side. Rows also carry position, district, mode, draw calls, chunks
built, car and NPC counts, so "why was it slow *there*" is usually one line
wide.

A **`REC nnn`** badge sits under the in-game clock whenever a session is being
recorded, naming the file it is writing to. It is there because a recorder you
cannot see is one you cannot trust: without it, "did that run get logged?" is
only answerable after quitting, which is exactly too late. Pressing `F4` turns
it green and counts the mark, so the keystroke has visible confirmation rather
than going into a void.

Press **`F4`** while playing whenever the frame rate stumbles. The recorder
cannot tell which dips a player actually felt, and that turns out to be the
column worth having: the mark is written with the position and the worst frame
of the previous three seconds, because a stutter is noticed after it happens.

Columns are addressed **by name** from the header, never by position, so a new
column can be added to the recorder without the report tool silently
misreading every older log on disk.

`--perf-log FILE` chooses the path, `--perf-spike-ms N` moves the dip threshold
(default 20 ms — one frame under 50 fps), and `--no-perf-log` turns it off. The
recorder is deliberately not the `--log` logger: that one flushes every line,
which is correct for events and would itself cause dips at 120 Hz.

A file runs about 250 bytes a frame — roughly 11 MB for ten minutes at 120 Hz.
They accumulate, including from bounded `--frames` QA runs; delete the folder
when the sessions in it stop being interesting.

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
| `F1` → FRAME LOGGING | start/stop the frame recorder | **yes** — off by default; each switch-on opens a fresh session file |
| `F1` → PLAYER & VEHICLE → GOD MODE | player takes no damage | **yes** — one early return at `damage_player()`, the door every source of harm already goes through |
| `F1` → PLAYER & VEHICLE → VEHICLE GOD MODE | no impact damage or dents | **yes** — zeroes `impact_damage_per_mps` and `body_damage_gain` on the per-step tuning. Prevents new damage; it does not repair existing damage (REPAIR does that) |
| `F1` → PLAYER & VEHICLE → WANTED LEVEL → NEVER WANTED | police never engage | **yes** — `WantedSystem::set_enabled(false)`; switching it on mid-chase ends the pursuit rather than freezing it |
| `F4` | mark the moment | **yes** — writes "I felt that" into the session's performance log, with the position and the worst frame of the previous three seconds. Press it when the frame rate stumbles; the recorder does the rest. The `REC` badge under the clock turns green and counts the mark |
| `F7` | toggle instancing | **yes** — the batching A/B, handled outside `InputFrame` on purpose |
| `F8` | teleport across the island | **yes** — evicts the world, refills the near ring before resuming. Outside `InputFrame` for the same reason as `F7` |
| `Esc` / `B` | back / pause | **yes** — closes the map, resumes from pause, or opens pause while driving |
| `Ctrl+Q`, `Cmd+Q` | quit | **yes** |
| `R` | respawn | **yes on foot** — returns the player beside the car |
| `C` | cycle camera / map layer | **yes** — cycles near, chase and far distances; in the map, cycles Explore, Roads and Places (controller Y) |
| `P` / controller Start | pause | **yes** — freezes sim time and opens resume, map, restart, and title options |
| `M` / controller Back | city map | **yes** — full-width atlas with smooth vector coastlines, terrain contours, cased roads, readable labels, building footprints, location icons, and live player heading. Wheel or `+/-` zooms; WASD/stick or mouse drag pans; Return/A recentres. See [map viewer notes](docs/map-viewer.md) |
| `B` | look back | **yes** — instant rear view, clean return on release |
| `V` / controller D-pad down (driving a plow truck) | raise / lower the blade | **yes** — a sim input on the replay tape (`kBtnPlowBlade`, tape v9): the blade takes 0.85 s to travel and clears snow only while it is down on the road. See [snowplows](docs/snowplows.md) |
| `H`, `J` (driving) | horn; siren or folding top | **yes** — `H` honks the seated vehicle's own horn. `J` toggles the siren and lightbar in a Municipal cruiser, and raises or lowers the Vesper Mistral's canvas top over 2.4 s. Both are presentation, handled outside `InputFrame` for the same reason as `F7` |
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
                 Every commit on main bumps it: docs/versioning.md.

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
  guard_lightbar_profiles.py  police lightbars: mesh, shader, profile
                         table and lamp origins held to each other.
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
