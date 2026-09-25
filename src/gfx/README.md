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

`tint`, `uv_scale`, and the six-region vehicle deformation payload ride the
instance rather than a per-draw uniform because `scene/draw_batch.h`
deliberately keeps them out of the batch key. Nodes differing only in colour,
tiling, or crash damage still collapse into one draw. Promote any of them to a
uniform and batching degrades to one draw per object while continuing to look
like it works.

Vertex attribute locations: `0` position, `1` normal, `2` uv, `3` reserved for a
tangent, `4-12` transform/colour/tiling, and `13-15` regional vehicle damage.
Location 3 stays empty so adding normal mapping later does not renumber every
shader in the engine.

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

The same shared include also evaluates the player car's two headlight spots.
`PlayerCarVisual` builds their world-space pose from the same interpolated
chassis transform used for the model, and the renderer uploads the rig once per
frame. An intensity of zero is the daylight off path; there is no second lit
material variant to drift out of sync.

Traffic headlights use CPU-built 64-pixel tiles with 24 depth bands and
variable-length buffer-texture lists. All relevant cars can contribute; there
is no four-car budget. The same lists light static geometry and skinned people
without splitting draw batches. See `docs/traffic-headlights.md` for the
shadowless-lighting limits and the frozen-scene GPU A/B benchmark.

Weather layers *onto* that env and is an **exact no-op at zero** — bit for bit,
pinned by `tests/sky_env_tests.cpp`. The app then adds draw-distance haze as a
separate world-scale layer: clear weather keeps a long view, while weather pulls
the fully opaque horizon inward. Keeping those jobs separate means storm tuning
cannot quietly drift the clear-day lighting model.

### Precipitation and cloud are two axes, not one

`WeatherParams::overcast` is **deck opacity**; `rain` and `snow` are how much
water is falling through it. They used to be summed into one cloud figure, which
made the sky a function of the downpour: asking for heavy rain closed the deck
whether you wanted it or not, so a bright sunshower was not a look this engine
could express. Precipitation now only holds the deck to a **minimum**
(`kPrecipCloudFloor`), bounded so that **precipitation alone can never hide the
sun** — only `overcast` can. `DevWeatherPreset::Sunshower` exists to keep that
honest; if it ever stops looking bright, the two axes have been welded back
together.

The deck's bite is weighted by the sun as well as by its own thickness
(`deck_transmission`): a low sun's light takes a longer slant path through the
same cloud, so one storm reads differently at noon, at dusk and at midnight. A
single flat multiplier gave the same mid-grey at every hour, and heavy weather
used to *lift* screen brightness, because the deck colour sat above the sky it
hung under. Both are pinned in `tests/sky_env_tests.cpp`.

Colours under weather derive from the environment's **own luminance**
(`deck_grey`) rather than from absolute constants. An absolute grey reads as one
flat tint at every hour, and the tell was a foggy midnight painting a horizon
band brighter than the sky above it.

## Headless-testable by design

`sky_env.h`, `rain_field.h`, `glyph_atlas.h`, `primitives.h` and `instance.h`
are header-only and contain **no GL include**, so the headless suites exercise
the real generators the renderer uploads rather than a hand-written copy of
them. Keep them that way; the moment one needs a GL type, the thing that needed
it belongs in a `.cpp` next door.

## Status

Implemented and exercised: `gl_state`, `Shader`, `Texture` (procedural plus PNG
paint for authored models), `Mesh` including cooked static `.emesh` geometry and
the instanced attribute stream, `Camera`, `Sky`, `Precipitation`, `Hud` and
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

## Paintable materials, the one exception to append-only

Append-only means a material, once added, is never freed and never changes.
**Paintable materials are the one exception to the second half** — their
texels may be rewritten; they are still never freed. `add_paintable_material(w,
h)` mints one, starting as opaque white so an unwritten one draws its tint
rather than black. `update_paintable_material()` rewrites it: same GL object,
same `MaterialId`, `glTexSubImage2D` when the size and RGBA8 storage match and
`glTexImage2D` when they do not, the whole mip chain regenerated every time.
There is no `glDelete*` anywhere on that path, so the recycled-id hazard above
never opens while scene nodes hold the handle.

`update_paintable_material()` refuses every id `add_material()` returned, with
an error and no change. That refusal is the point. Materials are shared by
handle: every traffic car of one paint variant draws through a single id,
picked from its model's paint table by hash. A respray that rewrote a shared id
would repaint every car of that variant in the city in the same frame, and it
would be reported as a traffic bug. So the check lives in gfx itself rather
than in the caller's good intentions — and the converse is the caller's:
**a paintable id must never go into a table that traffic, or any other shared
owner, draws from.** One car, one paintable id.

What it costs:

- A paintable material holds its full RGBA storage and mip chain from the
  moment it exists, painted or not: a third of a MiB at 256², 5.3 MiB at 1024².
  The table never frees, so a pool that needs N of them allocates N up front and
  keeps them.
- Every rewrite regenerates the whole mip chain. Fine for one car at a respray;
  wrong for anything that wants to repaint per frame.
- A texture keeps no CPU copy of its pixels once uploaded, so a recolour has to
  start from its own decode. `decode_rgba_file()` is that decode, and it is the
  very function `load_file()` calls, so composited texels land one-for-one on
  the stock paint instead of mirrored across the atlas. The price is one extra
  copy of the image out of the decoder's buffer on every `load_file()`.

The respray pool at Rook's Auto Repair (`app/vehicle_paint_materials.h`) is the
one consumer: twelve paintable materials made at startup, the booth's preview
slot rewritten at most once per rendered frame, owned slots only while no body
wears them.

## HUD gradients cost vertices, never a draw call

`Hud::gradient_triangle()` and `Hud::gradient_rect()` put a colour on each
corner and ride the same path as every other HUD primitive: the solid atlas
block, the CPU clip rect, the single draw in `end()`. A colour picker is a few
hundred vertices in the batch, not a second pass and not a texture.

**A four-corner blend is not something a triangle can draw.** The GPU
interpolates each triangle linearly, and a bilinear blend has a `u·v` term no
pair of triangles reproduces. A colour picker's saturation/value plane — white,
the hue, black, black — drawn as one quad is wrong through its middle by a
quarter of full scale. So `gradient_rect()` cuts the rect into `cols` × `rows`
cells, each exact at its own corners, and the worst error falls with the square
of the cell count. Measured on that plane against a true bilinear blend, using
the triangulation `hud.cpp` actually submits:

| cells | worst error, in 8-bit levels |
|---|---|
| 1×1 | 63.75 |
| 2×2 | 15.94 |
| 4×4 | 3.98 |
| 8×8 | 1.00 |
| 16×16 | 0.25 |
| 32×32 | 0.06 |

A GL readback of a real `Hud` pass agreed to within 8-bit rounding (8×8 read
1.47 levels worst, 32×32 read 0.56), with no crack pixels at any grid size.
8×8 is the knee: one level at worst, 384 vertices. Counts clamp to 1..32 (6144
vertices, about 190 KiB of that frame's upload), and a zero-area rect queues
nothing.

Every grid line is computed from its own index as `i / n` through `glm::mix`,
never by accumulating a step, so neighbouring cells share bit-identical edges
and the last cell lands exactly on `max`. An accumulated step leaves hairline
cracks the backdrop shows through.

The respray colour picker is the consumer: its hue bar is 36 two-colour
segments, exact because the hue's kinks fall on sixths, and its saturation/value
plane is one 12×12 `gradient_rect()`.
