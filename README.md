# Bulldog

A nostalgic open-world driving and on-foot game in the GTA III / Vice City / San Andreas tradition. Custom C++17 engine. Procedural city, cell-streamed at runtime. Small project, real engine.

## Tech stack

- **Language:** C++17
- **Build:** CMake (≥ 3.21)
- **Windowing & input:** SDL2 (built from source via FetchContent)
- **Graphics:** OpenGL 3.3 core, loaded via glad2 (codegen at configure time)
- **Math:** GLM
- **Audio:** miniaudio
- **Image loading:** stb
- **Offline tooling:** Assimp (used only by the `meshconv` asset tool; not linked into the runtime)

## Prerequisites

All platforms:
- CMake 3.21 or newer
- A C++17 compiler
- git (required by FetchContent)
- python3 (glad2 generates loader code at configure time)

**macOS:** Xcode Command Line Tools. The engine links against OpenGL, CoreAudio, AudioUnit, AudioToolbox, and CoreFoundation frameworks.

**Linux:** OpenGL development headers and X11 or Wayland development headers (SDL2 needs them to build from source).

**Windows:** Untested. It may build with minor work; nobody on the team has tried yet. Reports welcome.

## Build

```sh
git clone <repo-url> bulldog
cd bulldog
cmake -S . -B build
cmake --build build -j
```

**First configure takes 5–10 minutes.** SDL2 and Assimp are fetched and built from source. Subsequent configures are fast.

CMake targets:
- `pengine` — the game executable
- `meshconv` — offline asset converter (see below)
- `traffic_ai_tests` — the current test target (run with `ctest`)

Optional flag for development: `-DPENGINE_SANITIZE=ON` enables AddressSanitizer and UndefinedBehaviorSanitizer. Pass it on the configure step:

```sh
cmake -S . -B build -DPENGINE_SANITIZE=ON
```

## Run

```sh
./build/bin/pengine
```

The asset directory is resolved at compile time, so the binary can be run from any working directory.

## How to cook an asset

The engine uses custom binary formats (`.emesh` for meshes, `.eskel` for skeletons, `.eanim` for animations). Source art (`.obj`, `.fbx`) must be converted using the `meshconv` tool before it can be loaded.

`meshconv` takes a source file and an **output basename** (no extension), and writes one or more files next to that basename depending on what's in the source. **Cooked output lives under `assets/models/`, not next to the source art.**

```sh
# Static mesh (vehicle, weapon, world prop):
./build/bin/meshconv assets/vehicles/Vehicles_psx/Car\ 08/Car8.obj assets/models/vehicles/car8
# Produces: assets/models/vehicles/car8.emesh

# Rigged character:
./build/bin/meshconv assets/characters/Characters_psx/hero.fbx assets/models/characters/hero
# Produces: assets/models/characters/hero.emesh, hero.eskel, hero.eanim
```

After editing a source model, re-run `meshconv` to regenerate the cooked files — the engine will not pick up changes to source art automatically. For rigged models, all three output files (`.emesh`, `.eskel`, `.eanim`) are produced and must land in the same directory with the same basename.

For directory conventions, axis requirements, the bone-name matching gotcha, verification steps, and other asset-cooking gotchas, see [`docs/assets.md`](docs/assets.md).

> The cooking step is currently manual. Wiring it into the build graph is tracked as PBD-003.

## Directory layout

```
assets/             Source art and cooked binary assets
  characters/       Character source art
  vehicles/         Vehicle source art (OBJ + paint PNGs)
  weapons/          Weapon source art
  models/           Cooked binary assets (.emesh, .eskel, .eanim)
    characters/
    vehicles/
    weapons/
  world/cells/      Generated at runtime by the streamer (gitignored)
src/
  core/             Logging, time utilities, binary asset format definitions
  scene/            Scene graph, transforms, AABB, frustum culling
  world/            Streaming, terrain, road graph, city layout, IPL loader
  game/             Gameplay systems (traffic AI, pedestrians, police, etc.)
  ai/               Reserved for shared AI primitives (currently empty)
  physics/          Character controller, rigid body, world collision
tools/
  meshconv/         Offline asset converter
tests/              ctest targets
docs/               Reference documentation (asset cooking, etc.)
```

> Gameplay AI currently lives under `src/game/` rather than `src/ai/`. The `src/ai/` directory exists but is not yet in use.

## Runtime notes

The world streamer writes generated `.ipl` cell files into `assets/world/cells/` while the game runs. These are gitignored, but the asset tree will show as modified after a play session — this is expected.

## Known rough edges

We're being honest about the parts that are still rough so new contributors don't trip over them silently.

- **Manual asset pipeline.** After editing any source model, you must re-run `meshconv` by hand. There is no automatic re-cook on build. If something looks wrong in-game, "did we re-cook?" is the first question to ask.
- **Long first build.** SDL2 and Assimp build from source on the first configure. Expect 5–10 minutes the first time, fast thereafter.
- **Limited test coverage.** The project currently has a single ctest target (`traffic_ai_tests`). Adding tests is encouraged but not yet a project norm. If you're touching something tricky, a test alongside the change is welcome.
- **Platform support is macOS-first.** Linux is expected to work but is exercised less often. Windows is untested.
- **Source art is checked into the repo.** This keeps the asset pipeline simple for now but makes the repo heavier than typical. A migration to Git LFS is on the table.

## Contributing

The project is small and the team is informal. If you're picking up work, the best path is to talk to the team first — there is ongoing context that's not yet written down, and you'll save yourself rework.