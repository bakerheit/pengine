# `src/road/` — the road network

Sim-side, and **it owns no GPU resource of any kind**. That is the whole
structural point of this module and it is stated first because the alternative
is what killed the code this replaces.

`probablecause`'s `RoadNetwork` is a *rendering object*: 1,038 lines holding
five uploaded meshes, five textures and a `render()` method. Its geometry and
its hardware handles were born in the same class, so there was no seam to cut
along and none of it could be ported. Here the seam is cut at birth. Everything
in this directory is a pure function from data to data; the host layer uploads
the arrays and owns the handles. `tools/guard_sim_purity.sh` enforces it, and
every suite in `tests/road_*.cpp` runs headless because of it.

---

## The four pieces, and what each one owes its caller

| File | Answers |
|---|---|
| `road_class.h` | What is a road of this class? Width, lanes, sidewalks, junction control, grip. One constexpr table. |
| `road_graph.{h,cpp}` | Where do these spines meet? Nodes, edges, junctions. |
| `ribbon.{h,cpp}` | What does it look like, and what does the car touch? Six plain meshes, plus collision derived from them. |
| `lane_graph.{h,cpp}` | Where is this car, where should it be, and where may it go? |

They compose in that order and only in that order. Nothing upstream knows about
anything downstream.

---

## The two rules this module inherits, and what they cost

### 1. THE WIDTHS ALREADY CARRY THE +2 m. DO NOT APPLY IT AGAIN.

`kRoadClasses` in `road_class.h` is Freeway 30, Arterial 22, Street 14, Alley 6,
Dirt 9. Four of those five come straight from the reference's table **after** its
global +2 m widen (PCG-177), which is what `docs/design/pinatty.md` §3 adopted
because it already felt right.

The reference nearly shipped that widen twice — once at authoring time and again
at load time — which compounds silently and turns every 14 m street into a 16 m
one. apricot has no road file to load and no migration path, so the *only* way
back into that bug is somebody reading "+2 m founder pass" in a document and
helpfully applying it to these numbers. `tests/road_graph_tests.cpp` pins all
five as literals for exactly that reason: changing one has to be deliberate.

The same chain carries a second rule. **An authored width override is never
snapped toward its class width.** PCG-170 did snap, and it silently rewrote
hand-authored 8 m streets to 16 m; it was reverted. `RoadSpine::width_m` is used
exactly as given, pinned by `test_width_override_is_never_snapped`.

### 2. There is no junction splay or flare, and there must not be one by accident.

The PCG-168/170 splay/flare chain was reverted on feel, not on correctness. It
is not reimplemented here. Junction geometry is a trimmed plate plus corner
fills, and nothing widens an approach as it nears a node. **Do not resurrect
splay without a plan to feel-check it in game** — no test can tell you a
junction reads right.

---

## Collision comes out of the bake. That is a signature, not a promise.

```cpp
RoadCollision build_road_collision(const RibbonBake& bake);
```

It takes the **baked mesh** and not the graph, so it is structurally incapable
of re-deriving the surface — the same trick `build_chunk_collision(const
ChunkMesh&)` uses, for the same reason. This repo has found the "collision
disagrees with what draws" bug twice already (heights and normals, then
materials), and both times the cause was a second implementation of something
that already existed.

`tests/road_ribbon_tests.cpp` carries the negative control that makes this real
rather than stylistic: it moves every baked vertex up 5 m and requires the
collision to move up 5 m. A re-deriving implementation passes every other test
in the file and fails that one.

The chain runs all the way down. The ribbon drapes on `mesh_height_at()` — the
**drawn** terrain triangle — and not on `height_at()`, the continuous field
underneath it. Those differ by centimetres on a grade, and that is enough for a
car to sink into a hill it is visibly resting on. Measured across 676
carriageway vertices over real terrain, the drape error is `0.000000000 m`; the
same check over all 65,514 draped vertices of the shipped map, in
`tests/city_roads_tests.cpp`, holds to a millimetre and fails if one vertex
does not.

Kerb risers are excluded from collision **by layer, not by testing a normal**.
They are vertical faces; nothing rests on them, and letting one into a set whose
contract is "the normal points up" is how that contract stops meaning anything.

---

## The six layers

One per material the host layer has to bind.

| Layer | Surface | In collision? |
|---|---|---|
| `Carriageway` | marked asphalt | yes |
| `Unpaved` | packed earth | yes |
| `Walk` | raised concrete slabs | yes |
| `Kerb` | the vertical faces closing those slabs | **no** |
| `Plate` | junction asphalt, unmarked | yes |
| `Crosswalk` | zebra bands | yes |

A **plate is not the same thing as a junction**. Every crossing of three or more
roads gets one, and so does every degree-2 node where the road actually changes
— a sharp corner, or a step in width, surface or sidewalk — because those leave
a notch between two ribbons that do not line up. A gentle bend between two
identical roads gets a mitred cap instead, which is cheaper and lets the surface
flow through the corner. Crosswalks are emitted only at real crossings: a bend
is not a pedestrian crossing.

---

## Handing the bake to the host layer

`RoadMesh` is structurally `gfx::MeshData` — the same three members, in the same
order, over the same `TerrainVertex` the whole engine uses. It is declared here
rather than reused because `MeshData` lives in a host-side module and
dependencies flow sim → host and never back.

**The host-side owner is not in this module and was not written by this ticket**,
because `src/gfx/` is another module and its build fragment is not this one's to
edit. The seam it needs is three lines per layer:

```cpp
// host side, e.g. src/gfx/
MeshData md;
md.vertices = std::move(bake.layer(l).vertices);
md.indices  = std::move(bake.layer(l).indices);
md.bounds   = bake.layer(l).bounds;
mesh.upload(md);            // Mesh::destroy() already pairs with gl_state
```

If unifying the two types is ever worth it, the move is `MeshData` **down** into
`core/`, never `RoadMesh` up into `gfx/`.

---

## The lane graph is a contract

`lane_graph.h` is written as one. The short version:

- `pose(lane, d)` — where a car at lane-distance `d` should be. Returns
  position, tangent **and** right, because every caller that got only a position
  re-derived the heading itself and did it slightly differently each time.
- `project_onto(lane, xz)` — where a car that already knows its lane is.
  Per-step, no search.
- `nearest_lane(xz)` / `nearest_lane_along(xz, heading)` — the cold start.
  Backed by a uniform grid, not a linear scan of every lane. Use the `_along`
  form for anything with a heading: without it a car on the centre line snaps to
  whichever opposing lane is a millimetre closer and drives away backwards.
- `outgoing()`, `choose_next()`, `plan_route()` — where it may go.
- `junction_control()` and `TurnLink::priority` — whether it may go now.

**One sign convention, everywhere.** `right` is `cross(up, tangent)`, positive
lateral is to the right of travel, and `pose()`, `project_onto()` and
`Lane::lateral_offset_m` all use it. When two of them disagree, an overtake
steers into the traffic it was avoiding — so it is pinned by a round-trip test
over every lane in the fixture (worst arc error 1.5e-5 m).

**`choose_next()` takes a seed and a decision index, not a generator.** The
reference drew from two shared `std::mt19937` streams, which made an agent's
behaviour depend on how many agents had drawn before it — and how many agents
exist depends on where the player went. That is the exact failure the no-stream
rule exists to prevent. Here the choice is `hash_coord3(seed, lane.key,
decision_index)`, and `Lane::key` is derived from the **authored spine**, so it
survives a rebuild and survives reordering the spine table.

`LaneRef` is an index into one build and is not stable across a rebuild. Do not
persist one; persist `Lane::key`.

---

## What is deliberately not here

- **Pedestrian paths.** Sidewalk geometry is baked, but no walkable lane network
  is emitted. It wants its own kind flag, its own crossings at junctions and its
  own nearest-path query, and bolting it onto the vehicle graph would make every
  traffic query filter it out first.
- **Movement conflict resolution.** `TurnLink::priority` says who yields;
  deciding *which* movements conflict needs vehicle extents and reaction times,
  which is the traffic system's problem, not the geometry's.
- **Terrain operators.** `RoadStructure::Cut` and `Fill` are carried through and
  drape like `Ground`. Carving the corridor belongs to the map module, and it
  has landed: `src/city/roads.h` derives a `Grade` corridor from every road that
  needs one, so the drape now lands on carved ground and this module did not
  change a line to get it. The ribbon must never grow its own copy of the
  terrain.
- **Lane markings.** The carriageway is one material; painted lines are a
  texture question for the host layer.

## Where the spines come from

`city::map_spines()` — `src/city/roads.h` and `src/city/spines.cpp`. It is
still a **parameter** to `RoadGraph::build()` and it must stay one: nothing in
this module knows Pinatty exists, and that is what lets `tests/road_*.cpp` build
the graph out of a fixture instead of out of a city.

Measured on the real network, in `tests/city_roads_tests.cpp`:

| | |
|---|---|
| spines in | 92 |
| nodes / edges / junctions | 227 / 334 / 149 |
| carriageway centreline | 52.8 km |
| bake | 129,484 triangles, 168 plates, 544 crosswalks, 7.86 MB uploaded |

`tests/road_fixture.h` is still a hand-made spine set and still should be. It
contains one of each thing the graph has to notice — an unauthored crossing,
two T junctions, a class change at a shared endpoint, a shape point that is not
a node, and a bridge that must not drape — and the real map contains all of
those buried in ninety-odd roads, which is a worse place to debug a weld
tolerance from.
