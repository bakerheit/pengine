# Asset cooking

This is the reference for cooking source art into the engine's binary formats. For a quickstart, see the "How to cook an asset" section in the [README](../README.md). This document covers the full procedure, conventions, and the silent-failure modes worth knowing about before you start.

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

`meshconv` reads a source file via Assimp and writes one or more engine binary formats next to a basename you provide:

- Static mesh source → `.emesh`
- Rigged mesh source → `.emesh` + `.eskel` + `.eanim`
- Animation-only source (no mesh, e.g. Mixamo in-place animation) → `.eanim` only

Invocation:

```sh
./build/bin/meshconv <source_file> <output_basename>
```

The basename has no extension. `meshconv` appends `.emesh`, `.eskel`, `.eanim` as appropriate.

## Procedure by asset type

### Static mesh (vehicle, weapon, world prop)

```sh
./build/bin/meshconv assets/vehicles/Vehicles_psx/Car\ 08/Car8.obj assets/models/vehicles/car8
# Produces: assets/models/vehicles/car8.emesh
```

### Rigged character

```sh
./build/bin/meshconv assets/characters/Characters_psx/hero.fbx assets/models/characters/hero
# Produces: assets/models/characters/hero.emesh
#           assets/models/characters/hero.eskel
#           assets/models/characters/hero.eanim
```

All three files must land in the same directory with the same basename for the engine to load them as a set.

### Animation-only file

For Mixamo-style in-place animation FBX exports that have no mesh:

```sh
./build/bin/meshconv assets/characters/Characters_psx/walking.fbx assets/models/characters/walking
# Produces: assets/models/characters/walking.eanim
```

The resulting `.eanim` can be applied to any character whose skeleton has matching bone names — see the bone-name gotcha below.

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

There's no headless verifier tool. The realistic verification ladder, in order:

**1. Read `meshconv`'s stdout.** The tool prints what it saw in the source and what it wrote:

```
meshconv: scene meshes=1 animations=0
  mesh[0]: 'Body' verts=2104 faces=3680 bones=0
meshconv: wrote assets/models/vehicles/car8.emesh (static) 2104 verts 11040 idx 1 submeshes
```

If `bones=0` on something you expected to be rigged, your source was exported posed rather than rigged. If vertex or face counts look wildly off, the source is wrong. Catch these before launching.

**2. Watch for errors at game startup.** The loaders validate file magic and version on every load. A truncated or stale-format cook produces an error line in the terminal:

```
[ERROR] mesh.cpp:185  Mesh: bad magic/version in assets/models/vehicles/car8.emesh
```

The `[ERROR]` prefix and `<file>:<line>` are added by the logger. Grep for `[ERROR]` in your game output if you want to catch these cleanly.

**3. Launch and look.** No way around this for visual correctness. There is no preview tool.

## "Did we re-cook?"

The engine has no automated stale-content detection. If you edit a source `.obj` and forget to re-cook, the game loads the old `.emesh` with no warning. The diagnostic is mtime comparison:

```sh
# Flag any source file newer than the cooked-output directory
find assets/vehicles assets/characters assets/weapons -name "*.obj" -newer assets/models -print
find assets/vehicles assets/characters assets/weapons -name "*.fbx" -newer assets/models -print
```

Granularity is directory-level, not perfect, but it surfaces "you edited source and forgot to cook" cases quickly.

If something looks wrong in-game, this is the first thing to check. Wiring cooking into the build graph is tracked as PBD-003 and will eliminate this class of problem.

## Other things worth knowing

**Adding a vehicle requires a code change.** After cooking the `.emesh`, append a `CarModelDef` row to `CAR_MODELS[]` in `src/game/car_models.cpp`. The comment at the top of that file is the source of truth for the procedure.

**Vehicle paint textures are not cooked.** Paint variants are plain PNGs loaded at runtime. Drop new PNGs into the per-car source folder and list them in the paint array in `car_models.cpp`. `meshconv` doesn't touch them.

**Paint PNGs share a UV layout with the body mesh.** A new paint that doesn't match the existing UV unwrap will look garbled. This is a content rule, not engine-enforced.

**Weapons are static meshes.** "Weapon animations" like `pistol_idle.eanim` are actually character animations that hold the weapon — they belong to the character skeleton, not the gun.

**World models use a separate registry.** Adding a model to the world also requires editing `assets/world/streets.ide`, loaded at startup by `ModelRegistry::load_ide`. This is a GTA-III-lineage Item Definition format with its own conventions, not covered in this document.

**Multi-submesh static models need `PreTransformVertices`.** This is handled by `meshconv` automatically. If you ever see a static multi-part model exploding apart in-game with each piece at a different origin, that's the flag (or its absence) at work. See the comment in `tools/meshconv/main.cpp` for the why.