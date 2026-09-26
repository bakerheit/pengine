# pengine-apricot — working agreement

A **C++17 / OpenGL 3.3** game engine, and the pilot game being built on it.

**The pilot world has two states:** O'Haven, whose GTA-style authored city
Pinatty is rebuilt from `probablecause`, and Florangia, its low subtropical
neighbor to the southeast. Pinatty's city design is at
[`docs/design/pinatty.md`](docs/design/pinatty.md); Florangia's terrain brief is
[`docs/design/florangia.md`](docs/design/florangia.md).

**Its map and roads came first, and much has landed on top of them since.**
`src/city/` holds the ten district polygons with their character parameters,
the landmark table, the terrain operators that `height_at()` evaluates
(PENG-41) and the road network — 92 spines, 52.8 km of centreline, in
`roads.h` — all `constexpr` C++, not a data
file, and the argument for that is in `src/city/map.h`. `map_spines()` feeds
`RoadGraph` → the ribbon bake → six uploaded layers, and the app draws them.
**This paragraph used to say traffic, police, missions and buildings did not
exist and that `src/city/`'s decision layers were a library nothing called. That
stopped being true, and it stayed on the page long enough to mislead agents into
disbelieving working systems.** Today: `src/traffic/` simulates a live ambient
population that consumes `city/traffic_ai.h`; `game/wanted_system.h` consumes
`city/police_ai.h`, and officers witness offences, pursue, fire and arrest;
`game/delivery_mission.h` is wired into the app; `game/molotov.h` and
`game/fire.h` put a second weapon in the wheel and burning ground under it;
Rook's Auto Repair resprays any drivable car and loses the cops if none saw the
pull-in (`game/respray_shop.h`, `--paint-check`); and
a growing set of authored buildings are enterable — the bar, pawn shop, gun store, gas store, quickbite,
repair shop, bank vault and a furnished house among them, each with its own
suite. `game/climb.h` vaults the player over a wall or fence up to 1.30 m when
it has standing room on top and somewhere to land, and the Sycamore Loop back
gardens are fenced for it.

**What is still NOT there is worth naming precisely, because that is the part
that keeps expiring.** There is no mission *campaign* — one delivery mission and
a cutscene system, not a structure of jobs; the Sycamore Loop target house has
no scripting (`docs/neighborhood-backlog.md`); the gun store sells only the
pistol and its ammunition (`game/gun_store_shop.h`, `--gun-store-check`), and
the wheel equips only what you own. Check the tree before describing any of
it, in either direction: the design document is still a design document, not a
description of the code.

There used to be a sample game, Apricot Rally — a time trial with a checkpoint
route, lap timing and a ghost car. It was a placeholder and it was deleted in
PENG-23. If you find a reference to a route, a gate, a lap, a split, a best lap
or a ghost anywhere in this tree, it is a leftover: it is not a feature, and it
is not a plan.

This file loads into every session and subagent, and `AGENTS.md` is a symlink
to it so Codex and other agents read the same rules. It is the rules of the road;
it links out to the detail rather than restating it.

Read [`docs/architecture.md`](docs/architecture.md) before your first
substantial change. It states each design rule together with what it cost to
learn, which is the part that stops you from "simplifying" one of them.

**Several agents work this repo at once, on different modules.** Everything
below is shaped by that: stay inside your module, never edit a build file
another module owns, and never document a feature you have not run.

---

## The gate

```sh
cmake -S . -B build            # configure (first run clones dependencies)
cmake --build build -j         # build (-Werror is on for our targets)
tools/ci.sh                    # THE GATE: guards + configure + build + ctest
```

**Run `tools/ci.sh` before every push. Never push red.** The `pre-push` hook
enforces it for main: a push runs the gate unless that exact tree already
passed, and a push from a dirty checkout or of a commit that is not HEAD is
refused ([`docs/versioning.md`](docs/versioning.md)). It runs five steps in
this order, and the order is deliberate:

1. `tools/guard_sim_purity.sh` — the architecture guard. First, because it is
   instant, and a compile error you hit in thirty seconds is cheap.
2. `tools/guard_lightbar_profiles.py` — the police lightbars, which are spread
   across a mesh, `lit.frag`, a `*_profiles.inc` and `emergency_lighting.h`
   with nothing forcing the four to agree. Text only, like the purity guard, so
   it needs no build and no cooked assets.
3. configure
4. build under `-Werror` (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion
   -Wsign-conversion` on our targets; fetched dependencies are exempt)
5. `ctest` — every suite headless: no window, no GL context, no audio device

**Steps 1 and 2 are guards, not a dumping ground.** They earn their place
before `configure` only because they check things no compiler and no headless
suite can see: a banned word in a comment, and four files that must agree
across the CPU/GPU boundary. Anything a test can prove belongs in `ctest`.

There is no `EXCLUDE` list and there must never be one. If a suite cannot run
headless, the logic under test is in the wrong library.

Registering a test is one line in `tests/CMakeLists.txt`:
`apricot_add_suite(my_thing_tests)`.

**Assertions in tests must not compile out.** The default build type is
`RelWithDebInfo`, which defines `NDEBUG`, under which `<cassert>`'s `assert()`
is a no-op — a suite using it does not fail, it *lies*. Use `REQUIRE`,
`REQUIRE_MSG` and `REQUIRE_NEAR` from `tests/test_assert.h`. There is no
`<cassert>` in the test tree and there should never be one.

---

## Versioning

**Every commit on main bumps `VERSION`, and you pick the level.** The
pre-commit hook in `.githooks/` bumps a patch by default; `tools/ci.sh`
installs the hooks. Say what the change *means*, not how big it is:

```sh
git commit -m "..."                  # patch: fixes, tuning, docs, tooling, refactors
BUMP=minor git commit -m "..."       # minor: something new a player can see, hear or do
```

**major** is the human's call until 1.0. Changing `kReplayTapeVersion` forces
at least a minor, and the hook raises it for you. Worktree branches do not bump
per commit; they take one bump when they land, and a plain `git merge` into
main is refused, so land with `git merge --no-ff --no-commit <branch>` then
`git commit`. Never `--no-verify` past it. The levels, the landing recipes and
what each hook does are in [`docs/versioning.md`](docs/versioning.md).

---

## The module boundary

Three targets. Dependencies flow **sim → host → exe**, never back.

| Target | Links | Holds |
|---|---|---|
| `apricot_sim` | glm, Threads. **Nothing else, forever.** | `core` `scene` `terrain` `physics` `game`, plus `audio/mixer.h` + `audio/synth.cpp` |
| `apricot_host` | `apricot_sim`, SDL2, glad, ImGui, miniaudio | `platform` `gfx`, plus `audio/device.cpp` + `audio/miniaudio_impl.c` |
| `apricot` (exe) | `apricot_host` | `app` + `main.cpp` |

`src/game/` is where Pinatty's rules landed, and it has filled up: about forty
files covering the wanted system, police offences/arrest/combat, weapons, the
character, vehicle interaction and transitions, the delivery mission, cutscenes,
save games, the map and minimap, snow and weather hazards, and still
`conditions.{h,cpp}`, the deterministic weather that feeds
`VehicleTuning::grip_scale`.

**Each module owns its own `CMakeLists.txt`** and attaches its files with
`target_sources()`. The root `CMakeLists.txt` declares three source-less
targets and nothing else. Do not add sources at the root; do not edit another
module's fragment. This is the mechanism that lets several agents work at once
without conflicting, and it stops working the moment somebody centralises
"just this one file".

### The purity guard, and its one sharp edge

`tools/guard_sim_purity.sh` greps every sim source for
`glad`, `SDL`, `<GL/`, `miniaudio`, `imgui`.

**It is a plain text search, so it trips on those words in comments too.** That
is intended, not a false positive. Sim code should not need to talk about the
windowing library either. **If a sim-side file needs to refer to the other
side, write "the host layer" or "the platform layer".** Never name the banned
libraries in sim-side source — not in code, not in a comment, not in a string
literal. (Markdown files are not scanned; this file and `docs/` may name them
freely.)

The guard also fails if:

- a sim module's `CMakeLists.txt` mentions `apricot_host` — a module can attach
  itself to the wrong target and slip straight past a text search;
- an expected sim-side file is **missing**. `src/audio/` is a split module, so
  its sim-side files are listed by name in the guard. If you rename one, update
  `SIM_FILES` in the same commit. A guard that silently stops checking things
  is decoration.

**When the guard fires, the fix is essentially never to loosen the guard.**
Move the code into `src/platform/` or `src/gfx/` and pass plain data across the
boundary. `core/input_frame.h` is the shape to copy.

---

## Rules that constrain what you may edit

These are the ones most easily broken by a change that looks harmless. Full
reasoning in [`docs/architecture.md`](docs/architecture.md).

- **`InputFrame` is the replay tape format.** POD, standard layout, no padding,
  all asserted at compile time. Append fields at the **end** and bump
  `kReplayTapeVersion` (`core/replay_tape.h`, next to the `ReplayTape` it
  versions). Reordering or resizing an existing field, or renumbering a
  `ButtonBit`, silently invalidates every recorded tape — and the failure reads
  as "the physics changed". Both the format and the version are pinned in
  `tests/sim_determinism_tests.cpp`; the version is a golden value, so bumping
  it is a deliberate act with a stated cost.
- **No wall clock below `App`.** `App::run()` owns the program's only clock.
  Sim functions take `dt` as a parameter. The first `std::chrono` call added
  under that line ends replay, and the symptom arrives a minute later as a
  desync.
- **No `rand()`, no `random_device`, no global generator state, no
  time-seeding, anywhere.** Procedural content derives from `hash_coord()`, not
  from a sequential stream — stream values depend on pull order, and pull order
  is exactly what streaming takes away from you.
- **Changing the hash or the RNG means regenerating the golden values in
  `tests/rng_determinism_tests.cpp`,** which invalidates every existing tape and
  save seed. That is the real cost of the change. The test exists so you pay it
  on purpose.
- **Input edges clear on step count, never per frame.** `consume_edges()` is
  called only when `tick.steps > 0`. A render frame can owe zero sim steps, and
  clearing the latched mask on such a frame drops the press outright. It is
  frame-rate dependent, so it will not reproduce on your machine. Pinned by
  `tests/fixed_step_tests.cpp`.
- **Every `glDelete*` pairs with the matching `gl_state::on_*_deleted()`,
  immediately.** GL ids are recycled; a stale "already bound" entry silently
  no-ops the next bind. You get black textures on PC while macOS looks perfect.
  See `src/gfx/README.md`.
- **Collision derives from the geometry that draws.** Never a parallel
  re-derivation, never a downsampled copy. The solid the car touches must be
  the surface the player sees.
- **Terrain height and chunk meshing are pure.** No caching inside them, no
  statics, no clock. A cache with a stale entry turns a pure function into a
  desync that surfaces an hour in.

---

## Verification — before you call something done

The expensive failure mode is **"ships green but broken in-game."**

1. **Build clean, test green.** `tools/ci.sh`, all five steps. Never push red.
2. **Test the real producer, not just consumers.** A consumer test with
   hand-built inputs passes happily while the real producer feeds garbage. The
   sim/host split exists so you *can* instantiate the real system in a headless
   test — a real `VehicleState` stepped against a real `TerrainCollider`, a real
   `Scene` culled by a real `Frustum`. Do that instead of hand-rolling inputs.
3. **Rebuild before a feel-check, and confirm your change actually linked.** A
   stale binary wastes time twice: once looking broken, once looking fixed.
4. **Feel-check anything perceivable.** If a player could see, hear or feel it,
   run it and watch it. **A passing test is necessary, never sufficient.**
   Determinism tests prove a drive is reproducible; they cannot tell you the
   car is fun to drive, that the engine note sounds like an engine, or that a
   landmark reads from far enough away to navigate by.
5. **`--frames` runs are UNATTENDED, and must stay that way.** A frame-limited
   run creates its window hidden, keeps the process out of the desktop's
   foreground and never captures the OS cursor — see `platform/foreground.h`.
   That is not tidiness: several of us run `--weapon-check`, `--molotov-check`,
   `--house-check` and the rest all day on a machine somebody is also using,
   and a window that jumps in front of them and swallows the pointer for twenty
   seconds is the most disruptive thing this build does to a human. Screenshots
   are unaffected — they are read back from the GL drawable, which is real
   whether or not the window is on screen. If you need to watch a capped run,
   pass `--attended`; do not reach for it in a scripted check, and do not add a
   new path that shows a window or calls `SDL_SetRelativeMouseMode` directly.
6. **Verify count and usage claims.** Before writing "N call sites" or "X is
   unused", read the header and grep with word boundaries.
7. **Never document a feature as working unless you ran it.** Much of this tree
   is contract-and-stub while modules land in parallel. If it is a stub, either
   leave it out of the docs or name it as not yet implemented, with its ticket.

---

## Docs

Tickets live on the **coregoo** board (project `PENG`).

- `docs/architecture.md` — engine-wide design rules. Every rule states the
  reason **and what it cost**. Match that register; a rule without its cost is
  a rule someone will delete.
- `src/<mod>/README.md` — module docs, co-located with the code, **only where a
  module has genuinely non-obvious structure**. Today: `src/audio/` (the one
  split module), `src/gfx/` (the bind-cache invariant, the mesh generation tag,
  and what is still a stub), `src/terrain/` (what happens when a chunk changes
  level of detail, which spans four files and no header owns), `src/city/` and
  `src/road/`. **Docs are earned, not scaffolded — no empty shells.**
- `README.md` — what the engine is, how to build and run it, the repo map, and
  an honest account of what is and is not working yet.

**Keep the status notes honest as modules land.** Every "not implemented yet"
in these docs is a claim with an expiry date. When you finish the thing, delete
the note in the same commit — a doc that under-claims goes stale just as fast
as one that over-claims, and it teaches people to stop believing the docs.
