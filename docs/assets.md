# Asset cooking

Reference for cooking source art into the engine's binary formats. For a quickstart, see the "How to cook an asset" section in the [README](../README.md).

**Cooking is automated.** `cmake --build build` runs the cooker against any source whose cooked output is missing or older than the source — you don't normally invoke `meshconv` directly. The manifest of "which sources get cooked" lives in [`cmake/CookedAssets.cmake`](../cmake/CookedAssets.cmake); adding a new asset means appending one line there. This document covers the conventions, the intent-function choice, and the silent-failure modes that still apply.

## Directory conventions

Source art and cooked output live in separate trees. **Cooked output does not go next to the source.**

```
assets/
  characters/Characters_psx/        Character source files (FBX/OBJ + textures)
  vehicles/Vehicles_psx/Car NN/     Per-car source folders (OBJ body + paint PNGs)
  weapons/<weapon name>/            Weapon source files
  models/
    characters/                     Cooked: .emesh + .eskel + .eanim
    vehicles/                       Cooked: .emesh only
    weapons/                        Cooked: .emesh only
    test_scene.emesh                The world test scene (top-level)
```

If you cook output into the source tree, the engine won't find it and `git status` will look strange. Cooked output belongs under `assets/models/`.

## What `meshconv` does

`meshconv` reads a source file via Assimp and writes one or more engine binary formats. The shape of the output is determined by what's in the source:

- Source with a single static mesh, no bones → `.emesh`
- Source with a skinned mesh + bones (+ optional animation) → `.emesh` + `.eskel` + `.eanim`
- Source with no mesh, only animation channels (Mixamo "in-place" export) → `.eanim`

Invocation when running by hand (rarely needed):

```sh
./build/bin/meshconv <source_file> <output_basename>
```

The basename has no extension. `meshconv` appends `.emesh`, `.eskel`, `.eanim` as appropriate.

## Adding an asset

Append one line to `cmake/CookedAssets.cmake` in the section that matches the source's shape. Three intent functions exist:

```cmake
# Static mesh — single .emesh output. Used for vehicles, weapons, world props.
cook_static_mesh("vehicles/Vehicles_psx/Car NN/CarN.obj" "vehicles/carN")

# Skinned mesh — produces .emesh + .eskel + .eanim. Used for characters AND for
# animation FBX exports that ship an embedded skinned mesh (most Mixamo exports
# from the Characters_psx pack fall here).
cook_skinned_mesh("characters/Characters_psx/Models/Male/Character_NN.fbx" "characters/ped_NN")

# Animation-only — produces .eanim. Used for Mixamo "in-place" exports that
# carry no mesh data at all (the Pistol_Handgun Locomotion Pack files).
cook_animation("characters/Characters_psx/Animations/Foo.fbx" "characters/foo")
```

Source paths are relative to `assets/`; output basenames are relative to `assets/models/` and have no extension. The next `cmake --build build` cooks the new entry.

**How to pick the right intent function:** if you're unsure, run `meshconv` by hand once against your source and look at its stdout. If it writes a `.emesh` and prints `bones=N` with N > 0, use `cook_skinned_mesh`. If `bones=0` with a mesh present, use `cook_static_mesh`. If meshconv writes only `.eanim` (no `.emesh`), use `cook_animation`. The choice declares CMake's output set — undeclaring an output file means CMake won't track it for cleaning or dependency invalidation.

## ⚠️ Source files must be Y-up

The engine is Y-up, right-handed (standard OpenGL convention). **`meshconv` applies no axis conversion.** Vertex coordinates are written through from the source file as-is.

- **Blender:** Set "Forward: -Z Forward, Up: Y Up" in the FBX/OBJ export dialog. Default Blender export is Z-up and will produce models lying on their side in-game.
- **Maya:** Default FBX export is Y-up. Works out of the box.
- **Units:** The engine uses meters. Blender's default meter units match. If your source uses a different unit scale, apply scale in the source tool before export.

There is no engine-side rescue for an axis or scale mismatch. If your model looks sideways or wrong-sized in-game, the source export is the problem.

## ⚠️ Bone names must match exactly

Animation channels are bound to skeleton bones by exact string match. **A bone-name mismatch produces no error — the animation just silently fails to play.**

The classic case: Mixamo exports use bones named `mixamorig:Hips`, `mixamorig:Spine`, etc. If your character's skeleton uses bones named `Hips`, `Spine`, etc., the animation will load successfully and apply to nothing.

When adding a character from a new source, audit the bone naming before cooking animations. Either rename bones in the source tool to match your skeleton, or stick to one source family per character.

The animations already in `assets/models/characters/` work because their source files share a naming scheme. New sources do not get this for free.

## Verification

No headless verifier tool exists. The realistic verification ladder:

**1. Watch `meshconv`'s output during the build.** CMake runs the cooker as part of `cmake --build`; lines like

```
meshconv: scene meshes=1 animations=0
  mesh[0]: 'Body' verts=2104 faces=3680 bones=0
meshconv: wrote ...car8.emesh (static) 2104 verts 11040 idx 1 submeshes
```

scroll past during build. If `bones=0` on something you expected to be rigged, your source was exported posed rather than rigged. If vertex or face counts look wildly off, the source is wrong. Build with `-v` (verbose) or scroll back through the build output to see them.

**2. Watch for errors at game startup.** The loaders validate file magic and version on every load. A truncated or version-mismatched cook produces:

```
[ERROR] mesh.cpp:185  Mesh: bad magic/version in assets/models/vehicles/car8.emesh
```

The `[ERROR]` prefix and `<file>:<line>` are added by the logger. Grep stderr for `[ERROR]` in your game output if you want to catch these cleanly. Should be rare now that cooking is automated — used to be the symptom of a stale cook, but stale cooks no longer exist by construction.

**3. Launch and look.** No way around this for visual correctness. There is no preview tool.

## Other things worth knowing

**Adding a vehicle requires a code change.** After cooking the `.emesh`, append a `CarModelDef` row to `CAR_MODELS[]` in `src/game/car_models.cpp`. The comment at the top of that file is the source of truth for the procedure.

**Vehicle paint textures are not cooked.** Paint variants are plain PNGs loaded at runtime. Drop new PNGs into the per-car source folder and list them in the paint array in `car_models.cpp`. `meshconv` doesn't touch them.

**Paint PNGs share a UV layout with the body mesh.** A new paint that doesn't match the existing UV unwrap will look garbled. This is a content rule, not engine-enforced.

**Weapons are static meshes.** "Weapon animations" like `pistol_idle.eanim` are actually character animations that hold the weapon — they belong to the character skeleton, not the gun.

**World models use a separate registry.** Adding a model to the world also requires editing `assets/world/streets.ide`, loaded at startup by `ModelRegistry::load_ide`. This is a GTA-III-lineage Item Definition format with its own conventions, not covered in this document.

**Multi-submesh static models need `PreTransformVertices`; rigged sources cannot have it.** `meshconv` decides per-source: it probes the file first without `PreTransformVertices`, detects whether any bones exist, and only re-imports with `PreTransformVertices` if the source is purely static. If you ever see a static multi-part model exploding apart with each piece at a different origin (the Glock 17 was the canonical case), the probe-then-decide logic isn't picking up the static-ness correctly. If you see a character cook as `(static)` with `bones=0` when the source is rigged, the probe import isn't seeing the bones. See `tools/meshconv/main.cpp` for the implementation.