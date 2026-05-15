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

The engine uses custom binary formats (`.emesh` for meshes, `.eskel` for skeletons, `.eanim` for animations). Source art (`.obj`, `.fbx`) is converted into these formats by the `meshconv` tool — but you don't normally run it directly. **Cooking is wired into the build graph: `cmake --build build` cooks anything stale.** Edit a source file, rebuild, and the cooked output regenerates.

To add a new asset, append one line to `cmake/CookedAssets.cmake` in the right section:

```cmake
# Static mesh (vehicle, weapon, world prop):
cook_static_mesh("vehicles/Vehicles_psx/Car NN/CarN.obj" "vehicles/carN")

# Skinned mesh (character or animation with embedded skeleton):
cook_skinned_mesh("characters/Characters_psx/Models/Male/Character_NN.fbx" "characters/ped_NN")

# True animation-only source (Mixamo in-place export, no mesh):
cook_animation("characters/Characters_psx/Animations/Foo.fbx" "characters/foo")
```

The next build picks it up.

If you ever need to invoke `meshconv` by hand (e.g., to inspect what it produces from a single file), the binary is at `./build/bin/meshconv` and takes `<source> <output_basename>`. Output goes under `assets/models/`, not next to the source.

For axis requirements, the bone-name matching gotcha, intent-function selection, and other asset-cooking gotchas, see [`docs/assets.md`](docs/assets.md).

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

- **Long first build.** SDL2 and Assimp build from source on the first configure. Expect 5–10 minutes the first time, fast thereafter.
- **Limited test coverage.** The project currently has a single ctest target (`traffic_ai_tests`). Adding tests is encouraged but not yet a project norm. If you're touching something tricky, a test alongside the change is welcome.
- **Platform support is macOS-first.** Linux is expected to work but is exercised less often. Windows is untested.
- **Source art is checked into the repo.** This keeps the asset pipeline simple for now but makes the repo heavier than typical. A migration to Git LFS is on the table.

## Contributing

The project is small and the team is informal. If you're picking up work, the best path is to talk to the team first — there is ongoing context that's not yet written down, and you'll save yourself rework.