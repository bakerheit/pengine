# pengine-apricot

A small **C++17 / OpenGL 3.3** game engine built around one idea: **the
simulation is a pure function, and the hardware is somewhere else entirely.**
Ships with a sample game, **Apricot Rally** — a time trial on a seed-derived
procedural island.

apricot is the successor to `pengine`, the engine behind two shipped games. It
keeps what those paid for and deliberately changes three things; see
[What changed from pengine](#what-changed-from-pengine).

> **Status: 0.1.0, early.** The architecture is settled and enforced by the
> build. Several modules are contract-and-stub while they are written in
> parallel. This README says which is which, and never the other way round.

## The three ideas

**The link graph is the architecture.** `apricot_sim` links glm and Threads,
and nothing else — no windowing, no GL, no audio device. `apricot_host` is
everything that touches hardware. Dependencies flow sim → host → exe, and
`tools/guard_sim_purity.sh` fails the build before a compiler ever runs if that
stops being true. Every test links only `apricot_sim`, so "keep GL out of the
logic layer" is structural rather than aspirational.

**Determinism is the testing strategy.** The sim steps at a fixed 120 Hz
consuming a POD `InputFrame`. Record those frames as a tape and you have a
ghost car in-game *and* a bit-exact headless regression test, from one
mechanism. The world is a pure function of a 64-bit seed; there is no `rand()`
and no time-seeding anywhere.

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

  --verbose      log at debug level
  --log FILE     also append the log to FILE
  --version      print the version and exit
  --help         this text
```

**What you get today:** a real window with a GL 3.3+ core context, the fixed-step
loop running the rally sim at 120 Hz, and a debug overlay showing frame rate,
steps owed this frame, the interpolation alpha and the sim step index. **There
is no render pass yet** — the world is simulated but not drawn, so the frame is
a flat sky colour behind the overlay. The renderer is a separate ticket.

### Tests

```sh
tools/ci.sh    # purity guard + configure + -Werror build + headless ctest
```

That is the gate. Every suite is headless — no window, no GL context, no audio
device — because every suite links `apricot_sim` and only `apricot_sim`.

## Controls

Mapped in `src/platform/input.cpp`. The mapping is real; how much of it the sim
currently *acts on* is not, so both columns are here.

| Input | Intent | Works today |
|---|---|---|
| `A` / `D` | steer left / right | the sim integrates a rate-limited steer angle; nothing draws it yet |
| `W` | throttle | read into the tape, no motion yet |
| `S` | brake | read into the tape, no motion yet |
| `Space` | handbrake (analogue) | read into the tape, no motion yet |
| `R` | respawn | mapped, not consumed yet |
| `C` | cycle camera | mapped, not consumed yet |
| `P` | pause | mapped, not consumed yet |
| `LShift` / `LCtrl` | shift up / down | mapped, not consumed yet |
| `Return` | accept | mapped, not consumed yet |
| Mouse | look | accumulated only while mouse-look is on; nothing turns it on yet |
| `Esc`, `Ctrl+Q`, `Cmd+Q` | quit | yes |

Throttle, brake and handbrake do nothing to the car because `step_vehicle()` is
still a placeholder that integrates gravity and rests the chassis on the
terrain — see the vehicle-dynamics ticket. They are recorded into the replay
tape correctly regardless, because the tape stores intent, not motion.

## Repo map

```
CMakeLists.txt   three targets, pinned dependencies, strict-warning flags.
                 Declares no sources — the modules do that.
VERSION          semver, single source of truth. Flows into project() and
                 the APRICOT_VERSION macro logged on the first line of output.

src/
  core/          InputFrame (the replay format), the fixed-step clock,
                 deterministic hashing and RNG, AABB/frustum/transform maths,
                 logging, asset-root resolution.            -> apricot_sim
  scene/         node storage, world transforms, frustum + distance culling,
                 draw-batch planning. Plans draws, never issues them.
                                                            -> apricot_sim
  terrain/       the height field (a pure function, not a file), chunk
                 meshing, and the residency streamer.       -> apricot_sim
  physics/       terrain collision and vehicle dynamics. Every step function
                 is pure in (state, input, collider, dt).   -> apricot_sim
  game/          rally rules: checkpoint route, lap timing, the replay tape.
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

tools/
  ci.sh                  the gate: guard, configure, -Werror build, ctest
  guard_sim_purity.sh    the architecture, enforced. Runs first.
tests/                   headless suites; each links apricot_sim only
docs/architecture.md     the design rules, each with what it cost to learn
```

There is no `assets/` directory yet, and by design there is very little to put
in one: the terrain is a function, every sound is synthesised, and there is no
mesh on disk. `core/asset_root.h` resolves a root for the things that genuinely
cannot be computed — shader source and an overlay font — when the renderer
needs them.

## What changed from pengine

pengine shipped two games (`probablecause`, a GTA-style open world;
`backroomsrewind`, procedural horror) and everything in it survived contact
with real gameplay. apricot keeps its rules — they are restated with their
costs in [`docs/architecture.md`](docs/architecture.md) — and changes three
things on purpose.

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
and recording that stream gives ghost replays and bit-exact headless regression
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
