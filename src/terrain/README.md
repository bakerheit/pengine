# `src/terrain/` — the world, as a function

The terrain is not a file and there is no heightmap on disk. `height_at()` *is*
the terrain, evaluated on demand, and everything else here is a consequence of
that: chunks are regenerable rather than storable, a save file is a seed, and
physics can ask about ground no chunk has ever meshed.

The headers carry the per-decision reasoning. This file exists for the one thing
no header owns, because it spans four of them: **what happens when a chunk
changes level of detail**, which touches `chunk.cpp`, `streamer.cpp`,
`gfx/renderer.cpp` and `app/world.cpp` at once.

---

## Levels are a subset, not a simplification

A chunk at level `L` samples every `1 << L`-th point of the **same global
lattice**, at absolute world coordinates. `kChunkQuads` is 64, a power of two, so
every level divides it exactly and a coarse chunk's vertices are a *strict
subset* of the fine chunk's — the same `height_at()` at the same coordinate,
bit for bit.

| Level | Spacing | Verts | Bytes | Ring (default) |
|---|---|---|---|---|
| 0 | 1 m | 4481 | 320 KB | 256 m |
| 1 | 2 m | 1217 | 86 KB | 640 m |
| 2 | 4 m | 353 | 25 KB | 1280 m |
| 3 | 8 m | 113 | 8 KB | 2304 m |

Nothing is filtered, averaged or decimated. **A coarse chunk asks the same pure
function fewer questions**, and that sentence is the whole design — it is what
keeps two chunks at the *same* level closing exactly, as they did before LOD
existed, and it is what makes the crack between two *different* levels a bounded
quantity rather than an open-ended one.

Settled at spawn, the shipping rings hold 4053 chunks in **79.6 MB** — 49 / 268
/ 940 / 2796 by level. The same 4053 chunks at full detail would be 1.30 GB.

## Skirts, and why not a stitched row

Two levels do not share every coordinate along a common edge, so there is a real
crack. Every chunk hangs a **skirt**: a vertical curtain dropped from the
perimeter vertices it already has. It *adds* geometry and modifies none, which
is what keeps `docs/architecture.md`'s "never average across a boundary" literal
rather than merely respected.

A stitched transition row closes the crack exactly and costs the purity:
`build_chunk()` would take its four neighbours' levels as arguments, and every
chunk on a ring boundary would need a full rebuild whenever a *neighbour*
changed ring — a large multiplier on churn at exactly the radius where the
player is moving fastest.

**The depth measurement is the part that was wrong first, and it is worth
knowing which way.** Measuring relief between *adjacent* perimeter vertices
under-sizes the **fine** side, not the coarse one: a level 0 chunk has 1 m steps,
so its skirt came out short — but what it has to hide is a level 3 neighbour's
8 m chord. Measuring relief over the coarsest neighbour's cell width instead
fixed it, and let the safety factor drop from 3.0 to 1.5 while coverage
improved. `tests/terrain_lod_tests.cpp` prints the margin left at every pairing
rather than asserting a skirt exists; worst crack 1.803 m against a thinnest
margin of 0.489 m.

The probe chunks sit on the map's **carve** operators on purpose. They were
originally a scatter of coordinates over the noise, and when `src/city/`'s
terrain operators landed underneath them every one came out on a flattened
district plate — every skirt on its 0.50 m floor, the suite passing while
measuring nothing. A seam test on flat ground is a seam test that has stopped
working, and it does not announce it.

## A level change is a refit

When a chunk crosses a ring, the streamer does **not** destroy and rebuild it.
The terrain node is re-pointed at the new mesh in place, so the chunk is on
screen at its old level right up to the frame it is on screen at its new one.
Rebuilding through create/remove would put a chunk-sized hole under the player
for the length of the activation.

Scatter is pure in `(seed, coord)` and does **not** depend on level, so a chunk
changing level either keeps exactly the props it had or crosses
`max_scatter_lod` and gains or loses all of them. There is no partial reshuffle
to reconcile.

## Who owns the mesh

The streamer is sim-side and never sees a GPU handle. It reports `MeshId`s that
nothing references any more through `take_released_meshes()`, which **drains**:
the ids are handed over exactly once, and the host frees them.

Three things end up on that list, and the third is the one that gets forgotten:

1. an evicted chunk's mesh,
2. the old mesh of a chunk that changed level,
3. **a dropped delivery** — the host already uploaded it, and nothing else in
   the system knows it exists.

`take_released_meshes()` drains rather than exposing a const list because both
`step()` and `deliver()` fill it, in either order relative to the host's frame.
A "read it after `step()`" contract would silently miss everything `deliver()`
added.

## Two guarantees the player would notice breaking

**The ground under the car is always level 0.** Physics reconstructs the level 0
lattice analytically (`mesh_height_at`) and never consults the streamer, so a
coarser chunk under the car means resting on a surface that is not drawn. The
level 0 ring is clamped to at least `kMinLevelZeroRingChunks` = 2, because ring
membership is a circular test and a radius of 1 leaves the four **diagonal**
neighbours at level 1 — and a car near a chunk corner has wheels in one.

**Collision never comes from a coarse mesh.** `build_chunk_collision()` refuses
a `lod > 0` mesh loudly and returns nothing, and reads only the ground index
span so a vertical skirt face cannot enter a set whose contract is that normals
point up.

## Anything draped on the terrain has a range

Roads, props and decals are placed on the *level 0* drawn surface. Where the
ground under them is drawn coarser they are no longer the same surface, by a
measured amount over 160k samples:

Over Marrow's quarry, 60 m cut into a 120 m hill:

| Level | Mean | Worst |
|---|---|---|
| 1 (2 m) | 0.002 m | 0.324 m |
| 2 (4 m) | 0.010 m | 0.677 m |
| 3 (8 m) | 0.036 m | 1.020 m |

`mesh_height_at_lod()` exists to produce that table and **is not a ground
query** — passing it a non-zero level and using the answer for contact
re-creates the downsampled-collision bug by hand. The answer to the disagreement
is a draw distance, not a tolerance: road ribbons stop at 640 m, the outer edge
of the level 1 ring.

## Not done

- **Meshing is single-threaded.** `build_chunk()` is pure and thread-safe
  precisely so it need not be, and the budgets make it survivable — steady state
  at 40 m/s wants about 5.6 chunks a second against a far higher ceiling. A
  thread pool added before anyone measured wanting one would be a thread pool
  nobody measured.
- **No terrain splat shader.** `TerrainVertex` carries four-way
  `material_weights` and the lit shader does not read them, so terrain draws as
  one tiled diffuse rather than blended rock/gravel/grass/sand.
- **No terrain operators for road corridors.** Until the height field itself
  knows where a road bed is, every level disagrees about it and the draw
  distance above is the mitigation.
