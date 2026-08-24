# pengine-apricot

A small **C++17 / OpenGL 3.3** game engine built around one idea: **the
simulation is a pure function, and the hardware is somewhere else entirely.**

apricot is the successor to `pengine`, the engine behind two shipped games. It
keeps what those paid for and deliberately changes three things; see
[What changed from pengine](#what-changed-from-pengine).

**The pilot game is Pinatty** — a GTA-style open-world crime game, a rebuild of
`probablecause` in an authored 16 km² island city. The map design is
[`docs/design/pinatty.md`](docs/design/pinatty.md).

> **Pinatty is design only. There is no code under `src/` for any of it.** Do
> not read that document as a description of this tree. There was previously a
> placeholder sample game, Apricot Rally, a time trial with checkpoints and lap
> timing; it was deleted in PENG-23 because it was scaffolding that read as
> design.

> **Status: 0.1.0, early.** The architecture is settled and enforced by the
> build. What runs today is an engine and a demo scene, not a game. This README
> says which is which, and never the other way round.

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

```
apricot 0.1.0

  --verbose       log at debug level
  --log FILE      also append the log to FILE
  --frames N      render N frames, print a summary, then exit
  --no-instancing start on the naive per-node draw path
  --version       print the version and exit
  --help          this text
```

**What you get today.** A window with a GL 3.3+ core context, the fixed-step loop
at 120 Hz, a drivable car on procedural terrain under a moving sky, and a debug
overlay. There is no game on top of it: the world is a demo scene — a displaced
ground plane and 1400 boxes — built to prove the renderer batches, and it is
marked for deletion in `src/app/demo_scene.h`.

`--frames N` runs the whole thing with nobody at the keyboard and reports what
it drew, which is how the numbers below were produced rather than remembered:

```
$ ./build/bin/apricot --frames 1200
GL 4.1 core (Apple M5), window 2560x1440 drawable
shader 'shaders/lit_instanced.vert + shaders/lit.frag' linked
shader 'shaders/sky.vert + shaders/sky.frag' linked
shader 'shaders/precip.vert + shaders/precip.frag' linked
shader 'shaders/hud.vert + shaders/hud.frag' linked
hud: 384x144 procedural glyph atlas, 95 glyphs, no font files
demo scene: 1402 nodes (1400 boxes over 420 m, 5 materials)
quit after 213 sim steps (1.77 s of sim time), 1200 frames
last frame: 364 visible nodes, 5 batches (3 instanced), 5 draw calls,
            364 instances, longest run 128, 11 binds skipped
last frame: hud 50 quads in 1 draw(s), rain 1050 drops / 1050 quads
GL error queue clean for the whole session
```

Five draw calls for 364 visible nodes is the batching working; press `F7` (or
pass `--no-instancing`) to watch that collapse to one draw per node. What is
deliberately *not* there — no transparency pass, no shadows, no shader
hot-reload — is listed in [`src/gfx/README.md`](src/gfx/README.md).

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
| `A` / `D` | steer left / right | **yes** — rate-limited steer angle, smoothed in the physics |
| `W` | throttle | **yes** — engine torque curve through the gearbox to the driven wheels |
| `S` | brake | **yes** |
| `Space` | handbrake (analogue) | **yes** — shrinks rear grip, breaks the back loose |
| `LShift` / `LCtrl` | shift up / down | **yes** — manual gearbox, with a shift cooldown |
| `F7` | toggle instancing | **yes** — the batching A/B, handled outside `InputFrame` on purpose |
| `Esc`, `Ctrl+Q`, `Cmd+Q` | quit | **yes** |
| `R` | respawn | mapped and latched into the tape; **nothing consumes it.** The rally's `step_rally` used to, and went with it |
| `C` | cycle camera | mapped, not consumed. The camera is a fixed chase cam |
| `P` | pause | mapped, not consumed |
| `B` | look back | mapped, not consumed |
| `Return` / `Backspace` | accept / back | mapped, not consumed — there is no menu |
| Mouse | look | left-click captures the cursor and motion accumulates into `look_dx/dy`; the chase camera does not read it |

Steering, throttle, brake, handbrake and both shift edges are pinned by
`tests/vehicle_tests.cpp` against real terrain — the car settles on its springs,
transfers load under braking and cornering, slides and can be caught, rights
itself when flipped, and does not drive through a solid prop.

The unconsumed rows are honest rather than aspirational: every one of them is
recorded into the replay tape correctly, because the tape stores intent, not
motion. Wiring them up is the pilot game's job.

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
  terrain/       the height field (a pure function, not a file), chunk
                 meshing, and the residency streamer.       -> apricot_sim
  road/          authored spines -> a planar road graph, a ribbon bake that
                 emits PLAIN vertex arrays and owns no GPU resource, and the
                 lane graph traffic and police drive on.    -> apricot_sim
                 (see src/road/README.md)
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
                 cache, shaders, meshes, camera.            -> apricot_host
                 (see src/gfx/README.md)
  app/           the frame loop and the debug overlay. Wiring only; it holds
                 the program's ONE wall clock.              -> apricot (exe)
  main.cpp       argument parsing and not much else.

assets/shaders/          GLSL. The only shipped asset files in the tree.
tools/
  ci.sh                  the gate: guard, configure, -Werror build, ctest
  guard_sim_purity.sh    the architecture, enforced. Runs first.
tests/                   headless suites; each links apricot_sim only
docs/architecture.md     the design rules, each with what it cost to learn
docs/design/pinatty.md   the pilot game's map. Its section 3 road hierarchy is
                         implemented in src/road/; the map tables are not
                         written and nothing else of Pinatty exists.
```

`assets/` holds **nine GLSL files and nothing else**, which is the point rather
than a gap: the terrain is a function, every texture is generated, every sound
is synthesised, the glyph atlas is drawn in code and there is no mesh, no image
and no audio file on disk. `core/asset_root.h` resolves a root for the shader
source, which is the one thing that genuinely cannot be computed.

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
