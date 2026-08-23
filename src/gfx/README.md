# src/gfx — the only code allowed to call GL

Everything in this engine that issues a GL call lives here. If you find
yourself wanting a GL type in `src/scene/`, `src/terrain/` or any other sim
module, the design is wrong, not the rule — the link graph will not let you do
it anyway, and `tools/guard_sim_purity.sh` will say so before the compiler
does.

The boundary is plain data. `ChunkMesh` (sim-side vertex and index arrays)
crosses into `Mesh::upload()` and becomes a GPU resource; the sim never learns
that happened. `Scene::cull()` hands out opaque integer ids that only this
module dereferences.

## The one invariant: pair every delete with its hook

`gl_state` is a bind cache. Bind through it and redundant
`glBindTexture` / `glUseProgram` / `glBindVertexArray` calls are dropped, which
matters because a redundant bind is a driver round trip for nothing and a frame
that draws a thousand batches does a lot of nothing.

**GL object ids are recycled.** Windows drivers hand a freshly deleted id
straight back to the next `glGen*`. The cache then sees "id 7 is already
bound", skips the bind, and the new texture never arrives.

The symptom is **black textures on PC while macOS looks perfect**, because
Apple's GL recycles ids lazily. It costs a day every time somebody rediscovers
it, it cannot be detected from inside the cache, and it will not reproduce on
the machine you develop on.

So every `glDelete*` in this module pairs with the matching hook, immediately,
before the id can be reissued:

```cpp
glDeleteBuffers(1, &vbo_);
gl_state::on_buffer_deleted(vbo_);   // not optional, not later
vbo_ = 0;
```

`Shader::destroy()` and `Mesh::destroy()` already do this and are the pattern
to copy. Call `gl_state::invalidate_all()` after any third-party code — the
overlay backend, a capture tool — has bound things behind the cache's back, and
on context recreation.

One subtlety worth knowing: binding a VAO invalidates the cached
`GL_ELEMENT_ARRAY_BUFFER` slot, because a VAO carries its own element-buffer
binding. Caching through it would skip a genuinely needed rebind.

## Status — most of this module is contract only

Read this before you "fix" something here in passing.

- **`gl_state`** — real and complete. Also tracks skipped-bind counts for dev
  instrumentation.
- **`camera`** — real and complete. Pure maths, holds no GL object. It lives
  here rather than in `core` on purpose: letting sim code hold a camera is how
  "just cull against the camera" turns into gameplay that depends on where the
  player was looking.
- **`shader`** — **stub.** Every function is deliberately inert except
  `destroy()`, which is real because GL handle ownership is worth having correct
  from the start. Do not implement these one at a time in passing: a
  half-built `Shader` that reports `valid()` is worse than one that reports
  nothing. When it lands, it must report the driver's info log on failure — a
  silent compile failure renders black, which is the hardest possible thing to
  diagnose. Ticket: renderer.
- **`mesh`** — **stub.** Move and destroy are real, for the same ownership
  reason; `upload()`, `draw()` and `draw_instanced()` are inert. `Mesh` is
  move-only because copying would duplicate GL handles and the second
  destructor would delete geometry the first copy is still drawing. Ticket:
  renderer.

There is no render pass yet. `src/app/app.cpp` clears to a flat sky colour and
draws the debug overlay; the terrain and vehicle passes go between those two.
The sky colour is deliberately not black, because a black clear is
indistinguishable from a window that failed to present anything at all.

## What the renderer will consume

The batching contract is already settled and lives sim-side, so implement
against it rather than inventing a second one:

- `Scene::cull()` returns survivors **sorted by batch key**. That sort is the
  only reason contiguous runs exist to collapse.
- `plan_draw_batches()` partitions that list into instanced runs and plain
  stretches. The renderer **executes** the plan; it does not make one.
- `batch_key()` excludes `tint` and `uv_scale` because those ride per-instance
  attributes. Anything added to `Renderable` that must differ per node goes into
  the per-instance payload, **not** into the key — putting it in the key
  degrades batching to one draw per object while still appearing to work.

Design rules and their costs: [`docs/architecture.md`](../../docs/architecture.md).
