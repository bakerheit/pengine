# `src/gfx/` — the renderer

The only code in the engine allowed to call OpenGL. Everything here is
host-side; nothing in `core/`, `scene/`, `terrain/`, `physics/` or `game/` may
include a header from this directory.

## The bind-cache invariant

**`gl_state` is the only thing that binds.** A grep for
`glBindTexture|glUseProgram|glBindVertexArray|glBindBuffer|glActiveTexture`
across `src/` returns hits in `gl_state.cpp` and in comments, and nowhere else.
Keep it that way.

**Every `glDelete*` pairs with its `gl_state::on_*_deleted()` hook, immediately.**
GL object ids are recycled. Windows drivers hand a freshly deleted id straight
back to the next `glGen*`; the cache then sees "id 7 is already bound", skips
the bind, and the new object never arrives. You get black textures on PC while
macOS — which recycles lazily — looks perfect. There is no way to detect the
mistake from inside `gl_state`, and it will not reproduce on the machine where
it gets written.

Two consequences that are less obvious:

- **`bind_texture` only skips when the ACTIVE UNIT matches too.** That is not a
  missed optimisation. It buys a guarantee: when the call is skipped, the GL
  active unit *is* that unit and the texture *is* bound to it, so an immediately
  following `glTexImage2D` or `glTexParameteri` lands on the intended object.
  `Texture`'s generators depend on it.
- **Anything third-party that binds behind the cache's back must be followed by
  `invalidate_all()`.** Today that is the ImGui backend, both at init and after
  every `RenderDrawData` — see `app/overlay.cpp`.

## Layout contracts that span three files

`gfx/instance.h` pins the per-instance record. Change it and you must change
all three in the same commit:

1. `struct InstanceData`
2. `Mesh::upload_instances()`'s attribute wiring in `mesh.cpp`
3. `assets/shaders/lit_instanced.vert`'s `layout(location = ...)` inputs

`tint` and `uv_scale` ride the instance rather than a per-draw uniform because
`scene/draw_batch.h` deliberately keeps them out of the batch key, so nodes
differing only in colour or tiling still collapse into one draw. Promote either
to a uniform and batching degrades to one draw per object while continuing to
look like it works.

Vertex attribute locations: `0` position, `1` normal, `2` uv, `3` reserved for a
tangent, `4-12` the instance block. Location 3 stays empty so adding normal
mapping later does not renumber every shader in the engine.

## Shaders

`Shader` resolves `#include "file.glsl"` relative to the including file and
keeps a **line map** while it does. That map is why a compile error inside the
shared `lighting.glsl` reports `lighting.glsl:37` instead of an offset into the
concatenated blob — a line number that points at the wrong file is worse than no
line number, because you believe it.

On failure the driver's info log is logged *and* the offending source lines are
quoted, `valid()` stays false, and a rebuild over a live `Shader` leaves the
previous working program intact.

## Sky drives all lighting

`compute_sky_env(time_of_day)` produces one `SkyEnv` that the sky pass, every
lit shader (via the shared `apply_lighting` GLSL include) and the rain all read.
There is no second place to set a light direction.

Weather and fog layer *onto* that env and are **exact no-ops at zero** — bit for
bit, pinned by `tests/sky_env_tests.cpp`. Not "visually identical": if the clear
day drifts by an ulp every time somebody tunes a storm, there is no frame anyone
can point at where it broke.

## Headless-testable by design

`sky_env.h`, `rain_field.h`, `glyph_atlas.h`, `primitives.h` and `instance.h`
are header-only and contain **no GL include**, so the headless suites exercise
the real generators the renderer uploads rather than a hand-written copy of
them. Keep them that way; the moment one needs a GL type, the thing that needed
it belongs in a `.cpp` next door.

## Status

Implemented and exercised: `gl_state`, `Shader`, `Texture` (procedural only —
there is no image loader and no image file on disk), `Mesh` including the
instanced attribute stream, `Camera`, `Sky`, `Precipitation`, `Hud` and
`Renderer`.

Not done, deliberately:

- **No shader hot-reload.** The scaffold mentioned one; it is not written.
- **No transparency pass.** Everything lit is opaque; rain and the HUD do their
  own blending inline.
- **No shadows, no normal mapping, no post-processing.**
- **No terrain splat shader.** `TerrainVertex` carries four-way
  `material_weights` and the lit shader does not read them, so terrain draws as
  one tiled diffuse rather than blended rock/gravel/grass/sand. Attribute
  location 3 is reserved for a tangent, so wiring it needs a location and a
  fragment change together. Not started.

## Freeing a mesh, and the generation tag

`Renderer`'s mesh table **frees now** (PENG-28). It did not used to, and the
reason it gave was sound: a recycled `MeshId` aliasing a live scene node draws
one chunk's geometry where another's belongs, and the symptom points nowhere.
Streaming removed the option — a 2.5 km ring is thousands of chunk meshes and
they turn over continuously — so the aliasing is made *unrepresentable* rather
than merely unlikely.

A `MeshId` is **slot | generation**, not an index:

| bits | meaning |
|---|---|
| 0-15 | slot into the mesh table |
| 16-31 | generation, bumped on every free |

A handle held across the free of its slot resolves to `nullptr`, draws nothing,
and **logs**. That is a bug you can find. Slot `0xFFFF` is never issued, so a
real handle can never collide with `kInvalidId`. A slot whose generation would
wrap is retired instead of reused.

`Mesh::destroy()` already pairs every `glDelete*` with its
`gl_state::on_*_deleted()` hook, which is why `remove_mesh()` does not
re-litigate the bind-cache invariant — it delegates to the thing that already
obeys it.

**Materials are still append-only, deliberately.** Nothing streams them: every
terrain chunk shares one material and the road layers share six, all created at
startup. A free path for a table that never grows would be untested code
guarding a case that does not occur.
