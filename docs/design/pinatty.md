# Pinatty — the map

The city for the pilot game on `pengine-apricot`. A rebuild of *Probable Cause*'s
world, not a port: the old design and algorithms are reference, the old code is
not coming across.

**The one idea this document exists to defend.** Apricot has no asset pipeline
and ships no asset files, and Pinatty is a specific authored place you can learn
by heart. Those look like they contradict. They do not, because the map is
authored as a **skeleton** — district polygons, road spines, terrain operators,
landmark placements, character parameters — and every building, kerb, lamp post
and blade of grass is **generated deterministically from it**. The skeleton is
compiled C++ data, so there is still nothing to ship, nothing to load and
nothing to go missing. The city is a fixed *function*, not a random draw and not
a shipped mesh.

Everything below that carries a number is either measured — with the command
that measured it — or explicitly flagged as an estimate. Where something is not
knowable without building it, it says so instead of guessing.

**Status: design only.** No code under `src/` exists for any of this yet.

---

## 0. Two corrections to the brief, up front

**The size target is arithmetically impossible as stated.** "~16 km² of land" on
"a 4 km × 4 km island" cannot both be true: a 4 km × 4 km box *is* 16 km², so 16
km² of land means zero water, which means no island. Measured on the retuned
generator (§1), land occupies 39–46% of its bounding box across three seeds. To
land 16 km² of *land* you need a **6.0 km × 6.0 km world box** holding an island
about 5.5 km across. Pick one:

| Option | Box | Land | Cost |
|---|---|---|---|
| **Recommended** | 6.0 × 6.0 km | 15.3–16.6 km² measured | 2.25× the chunks of a 4 km box |
| Alternative | 4.0 × 4.0 km | ~7 km² | Half of San Andreas, and countryside gets squeezed out first |

This document assumes 6.0 km.

**The slope figure in the brief is wrong.** The current generator does not top
out near 29°. Measured over its island at 4 m sampling:

```
$ probe_current 1600 4 12648430
height max         98.0 m at (164, 268)
slope  p50/p90/p99 14.4 / 33.4 / 50.7 deg   max 71.4 deg
land slope <5deg   9.0%   <10deg 30.6%   >25deg 21.3%
```

Peaks at 91–98 m across two seeds — that part of the brief is right. But the
**maximum slope is ~70°**, and only 9–17% of the land sits under 5°. Designing a
city against "29° maximum" would have produced a map that does not fit on the
terrain.

---

## 1. The island

### 1.1 Shape and scale

| Property | Value | Why |
|---|---|---|
| World box | 6144 m × 6144 m | 96 × 96 chunks exactly, at `kChunkMetres` = 64 |
| Island span | ~5.5 km across | Leaves ~300 m of open water inside the box on every side |
| Land area | ~16 km² | Measured 15.3–16.6 km² over three seeds |
| Origin | Downtown, `(0, 0)` | +X is east, +Z is south, +Y is up |
| Highest ground | ~135 m at Ferrone Hill, NE quadrant | Tall enough to see the whole city from |
| Sea floor | −33 m at the box edge | Bounded by water, never by an invisible wall |

The land distribution is deliberate and it is the map's whole personality:

- **A broad low plain** across the west and centre, 5–25 m, holding downtown, Old
  Town, the Flats and the suburbs. Cities are built on flat ground because
  building on flat ground is cheaper, and a city laid on rolling terrain reads as
  a mistake even to a player who could not say why.
- **One commanding massif** in the north-east, rising from 40 m to ~135 m over
  about 1.2 km. This is the landmark, the vertical-chase district, and the place
  you go to look at the map with your eyes instead of the pause menu.
- **A second, gentler upland** in the south-west, 25–80 m — countryside, quarry,
  farm tracks. Not a second city. Its job is to make the drive between
  settlements take long enough to feel like leaving.
- **The coast bites in three times**: the harbour inlet on the west (deep enough
  for ships, and the reason the docks exist), the Kessel Channel cutting the
  north off from the centre (the reason there is a bridge), and a shallow bay in
  the south-east that Camber Point spits out into.

### 1.2 What the generator has to change

Apricot's `height_at()` already does fbm + gated ridged fbm + a radial falloff,
which gives land bounded by sea for free. It is tuned for a rally island a fifth
of Pinatty's size. Here is what it measured before and after a candidate retune,
run on a scratch copy — the real repo was not touched:

```
$ probe_pinatty 3000 6 3735928559
land area          15.30 km2   (42.4% of box)
height max         126.0 m at (594, -1788)
slope  p50/p90/p99 3.0 / 11.4 / 36.6 deg   max 68.8 deg
land slope <5deg   73.1%   <10deg 87.7%   >25deg 3.2%
```

| Constant | Today | Pinatty | Reason |
|---|---|---|---|
| `kIslandRadiusMetres` | 1400 | **2750** | The 16 km² target. Everything else follows from this one |
| `kContinentMetres` | 900 | **1850** | The header states the ratio: roughly a third of island diameter. Leave it at 900 against a 5.5 km island and you get six separate highland lumps instead of one backbone — the exact failure its own comment warns about |
| `kFeatureMetres` | 96 | **240** | 96 m hills are the right size to rally over and the wrong size to lay a street on. Broader landforms give long, drivable grades |
| `kRidgeMetres` | 260 | **420** | Steepness is amplitude over horizontal run. Longer run, gentler faces, one massif instead of an alpine range |
| `kHeightMetres` | 150 | **190** | Buys a ~135 m summit while the shore band stays the same fraction |
| `kShoreFalloffStart` | 0.45 | **0.66** | More of the disc at full strength. This is what turns a 39% land fill into a 46% one |
| `kCoastWarpMetres` | 260 | **560** | The coastline must wander by hundreds of metres, not tens, or the harbour has to be entirely carved by hand |
| `kIslandPlatform` | 0.21 | **0.235** | Just *above* `kShoreLevel` instead of just below. The rally island's shallow inland lagoons are charming; in a city they are potholes the size of a block |
| `kLowlandSpan` / `kMountainSpan` | 0.42 / 0.58 | **0.30 / 0.70** | Flattens the ordinary ground and concentrates the relief where it is gated on. Must still sum to exactly 1 — that is what keeps the field inside [0,1] without a clamp |
| `kLowlandContinentShare` / `kLowlandHillShare` | 0.55 / 0.45 | **0.70 / 0.30** | Less local bumpiness underfoot |
| `kSpineStart` / `kSpineFull` | 0.35 / 0.72 | **0.55 / 0.85** | Gates the mountain harder, so it is genuinely absent from 90% of the map |
| `kHomeRadiusMetres` | 380 | **delete it** | See below |

**`kHomeRadiusMetres` must go.** It lifts a 380 m dome of terrain at the world
origin so a random seed cannot drop the car in a lagoon. Pinatty's origin is
downtown, so that dome would not be a safety net — it would *be* the terrain
under the financial district, a 42%-of-headroom bulge nobody authored. An
authored map does not need a spawn guarantee, because the spawn is authored.

**The seed lottery is the reason this is authored at all.** Same constants,
different seeds:

| Seed | Land | Peak | Median slope | Land under 5° |
|---|---|---|---|---|
| `0xDEADBEEF` | 15.30 km² | 126 m | 3.0° | **73.1%** |
| `0xC0FFEE`-ish (12648430) | 16.55 km² | 135 m | 9.4° | **30.6%** |
| `777` | 16.21 km² | 134 m | 7.5° | **40.2%** |

Buildable flat ground swings by a factor of 2.4 on seed alone. You cannot build a
city on a coin flip, which is precisely why the map is authored and the seed is
pinned as part of it.

### 1.3 Terrain operators — how authored intent reaches the height field

Retuning constants gets you a plausible island. It does not get you a flat
downtown, a harbour deep enough for a ship, or benched terraces on a hillside.
Those come from **terrain operators**: a small authored table of shapes, each
with a rule, evaluated analytically inside `height_at()` after the noise and
before the metre mapping.

```
h = map_to_metres( apply_ops( noise_shape(x, z), x, z ) )
```

Five kinds, and only five:

| Op | Does | Used by |
|---|---|---|
| `Flatten` | Pulls the field toward a target height, feathered over a margin | Downtown, the airfield, dock aprons |
| `Bench` | Flattens to the nearest of N terrace levels | Ferrone Hill's mansion plots |
| `Carve` | Pushes the field *down* to a target, below sea level if asked | The harbour inlet, the Kessel Channel |
| `Mound` | Raises toward a target | The stadium berm, the quarry spoil heap |
| `Grade` | Flattens along a road spine's swept corridor to that spine's own vertical profile | Every road, so no road ever climbs at 30° |

Rules that keep the ops honest:

- **Pure, like everything else in `terrain/`.** No state, no cache, no clock. The
  op table is `constexpr`; evaluating it is arithmetic.
- **C1 at the edges.** Every op feathers with smoothstep, never with `max()`
  against a floor. A hard floor draws a visible contour line around the flattened
  area on every single seed — the `kHomeRadiusMetres` comment already learned
  this and says so.
- **Ops compose in table order, and the order is the authored decision.** A carve
  after a flatten is a dry dock; a flatten after a carve is a filled-in one.
- **`Grade` reads the spine, not the other way round.** The road's vertical
  profile is authored once on the spine; the terrain conforms to it. The
  alternative — draping roads onto whatever the noise did — is how *Probable
  Cause* ended up with a `SidewalkField` reconciling two disagreeing ground
  heights after the fact.

**The cost, stated plainly.** `height_at()` is called 4225 times per chunk for
the mesh, four more times per vertex for normals, and again by every physics
ground query. A naive loop over ~40 ops adds ~40 shape tests to each of those.
The fix is a **static bucket grid** over the op table — 128 m cells across the
world box is 48 × 48 = 2304 buckets, a few kilobytes, built once from the
`constexpr` table, deterministic and I/O-free. With it, a typical sample touches
0–2 ops. Without it, expect `height_at()` to get several times slower, which
lands on chunk build time and on physics simultaneously. **I have not built this
and have not measured it — treat the "0–2 ops" as the design intent, not a
result.**

---

## 2. The districts

Ten, plus the countryside they sit in. The test each one has to pass: **it must
chase differently from its neighbours.** A district that plays like the one next
door is filler with a different texture.

### 2.1 At a glance

| # | District | Is | Roads | Density / heights | Surfaces | Landmark |
|---|---|---|---|---|---|---|
| 1 | **Vellum Row** | Financial core | Grid, 92 × 62 m blocks, 6° off north, one-way pairs, alleys | 0.86 coverage, 24–88 m | Asphalt, concrete walk, glass | **Trinity Tower** (88 m) |
| 2 | **Halloway Square** | Civic — police HQ, courthouse, hospital | Radial off one plaza, 4 arms | 0.55 coverage, 18–40 m | Flagstone plaza, asphalt | Courthouse dome + the **plaza steps** |
| 3 | **Saltmarsh** | Old town, pre-grid | Organic, 6–9 m lanes, no through route | 0.92 coverage, 8–22 m | Cobble, brick, no kerb | **Fishmarket clock** |
| 4 | **Ostend Docks** | Container port, warehouses | Arterial spine + service stubs, dead ends at quays | 0.40 coverage, 6–30 m | Concrete apron, rail inlay, gravel | Four **gantry cranes** |
| 5 | **Kepler Flats** | Refinery, rail yard, scrap | Wide arterials, level crossings, dirt spurs | 0.30 coverage, 4–36 m | Cracked asphalt, dirt, ballast | The **flare stack** (burns at night) |
| 6 | **Ferrone Hill** | Wealth on the massif | Switchbacks + cul-de-sac, one paved way up | 0.22 coverage, 6–14 m | Asphalt, retaining concrete, rock | **Ferrone Mast** (60 m on 135 m) |
| 7 | **Nickel Heights** | Suburbs | Cul-de-sac loops off two collectors | 0.35 coverage, 4–9 m | Asphalt, lawn, driveway concrete | The **water tower** |
| 8 | **The Strand** | Beachfront hotels | One boulevard, 2.2 km, almost no junctions | 0.50 coverage, 12–45 m | Asphalt, sand, boardwalk | **Pier and Ferris wheel** |
| 9 | **Camber Point** | Airfield on a spit | Runway + perimeter road, one causeway in | 0.10 coverage, 4–18 m | Runway concrete, grass, sand | **Lighthouse** + control tower |
| 10 | **Marrow** | Quarry and farms (in the countryside) | Dirt, one paved link | 0.04 coverage, 4–12 m | Dirt, gravel, grass, rock | The **quarry face** and its haul ramp |
| — | *The Meadows* | Countryside connecting all of it | Two paved rural roads, a web of dirt | ~8 km², sparse | Grass, dirt, rock | Windbreak tree lines |

### 2.2 What each one is FOR

This is the column that matters. If a district does not answer it in one line, it
should not be in the map.

1. **Vellum Row — the canonical grid chase.** Every intersection is four choices,
   so escape is about *reading* the pursuit, not out-driving it. Signals matter.
   Alleys are the pressure valve and the cops know them too. This is where the
   game teaches you that a right turn at speed costs you three seconds.
2. **Halloway Square — heat generation and consequence.** Police HQ means the
   response time here is the shortest on the island; the radial plan means every
   arm is covered from the centre. It is also the mission hub, so you are forced
   to come back to the most dangerous place you know. Pedestrian-dense: the plaza
   is where a stray shot costs you two stars.
3. **Saltmarsh — the district that rewards memory.** Lanes too narrow for a
   cruiser to swing, no through route the minimap can helpfully draw, and four
   deliberate cut-throughs a local knows and the AI never takes. This is the
   "lose them because you know where you are" district, and it only works if the
   layout is fixed forever.
4. **Ostend Docks — straight-line speed inside a trap.** Long open runs between
   container stacks let you build real speed, and then the quay ends in water.
   Chases here are decided by whether you know which stub is a dead end. Container
   stacks make hard cover, and the crane rails make jumps.
5. **Kepler Flats — hazards as terrain.** Level crossings that a train genuinely
   closes, tank farms you do not want to shoot near, dirt spurs that let a
   pursuit off-road. The one district where the *world* is chasing you too.
6. **Ferrone Hill — vertical.** Switchbacks turn a chase into a series of
   commitments; a missed hairpin is a 30 m drop, not a scrape. One paved road in
   makes it the best roadblock in the game — and the unmarked fire road out is the
   single most valuable piece of local knowledge Pinatty has to teach. It is also
   the observation deck: from the mast you can see every other landmark, which is
   how the player builds their mental map.
7. **Nickel Heights — dead ends punish panic.** Wide, fast, inviting, and about a
   third of the loops go nowhere. Long sight lines mean the police keep contact
   even when they lose ground. The correct play is to *leave*, and the district is
   built to make leaving feel too slow.
8. **The Strand — the top-speed stretch.** One road, 2.2 km, gentle curve, sea on
   one side. Nowhere to turn off means nothing to think about except throttle —
   and it makes a roadblock genuinely frightening, because there is no alternative
   route to fall back to. Every map needs one place where the answer is just speed.
9. **Camber Point — the escape hatch and the arena.** The plane is here, which
   makes the causeway the highest-value chokepoint on the island. The runway is
   also the flattest large open surface in the world: it is where vehicle handling
   gets tested, where stunt jumps land, and where a shootout has no cover at all.
10. **Marrow — off-road, where the cruiser cannot follow.** Grip changes under
    you, the quarry has a spiral ramp with a drop on the outside, and the dirt web
    has no signage. The countryside around it exists so that the distance between
    districts is *felt*; a city where every district touches every other is a
    theme park, not a place.

### 2.3 Density, population and heat

Per-district scalars, all authored, all consumed by generation and by the sim:

| District | Traffic | Peds | Parked cars | Patrol density | Response (s) |
|---|---|---|---|---|---|
| Vellum Row | 1.4 | 2.2 | 0.9 | 1.2 | 6 |
| Halloway Square | 1.1 | 2.6 | 0.7 | **2.0** | **3** |
| Saltmarsh | 0.5 | 1.8 | 0.6 | 0.6 | 11 |
| Ostend Docks | 0.6 | 0.3 | 0.4 | 0.5 | 14 |
| Kepler Flats | 0.7 | 0.4 | 0.3 | 0.4 | 15 |
| Ferrone Hill | 0.3 | 0.4 | 0.5 | 0.9 | 12 |
| Nickel Heights | 0.8 | 0.9 | 1.2 | 0.7 | 10 |
| The Strand | 1.2 | 1.6 | 1.0 | 0.8 | 9 |
| Camber Point | 0.2 | 0.2 | 0.2 | 0.6 | 16 |
| Marrow / Meadows | 0.15 | 0.05 | 0.05 | 0.2 | 22 |

These are first-pass feel numbers, not results. They are in the table precisely
so they can be tuned in one place by someone with a controller in their hands.

---

## 3. The road hierarchy

Five classes. Four of the five widths come straight from *Probable Cause*'s
`ROAD_TYPES` table (Highway 30, Avenue 22, Street 14, Dirt 9) — that is already
the +2 m founder pass and it already feels right, so there is no reason to
relearn it. Alley is new: there is no 6 m class in the reference table.

| Class | Width | Lanes/dir | Junction control | Where |
|---|---|---|---|---|
| Freeway | 30 m | 3 | Grade-separated, ramps only | Route 1 only |
| Arterial | 22 m | 2 | Signals | District spines, inter-district links |
| Street | 14 m | 1 | Signals at 4-way, none at minor T | Everywhere urban |
| Alley | 6 m | 1, no sidewalk | None | Saltmarsh, Vellum Row, Docks |
| Dirt | 9 m | 1 | Stop signs | Meadows, Marrow, the fire road |

**Route 1 (the Rimway) is deliberately not a ring.** It runs Camber causeway →
The Strand → the south edge of Vellum Row → over Ostend Docks on an elevated
deck → Kepler Flats → the Kessel Bridge → the foot of Ferrone Hill, and it
**stops there**. A closed ring means the answer to every chase is "keep going".
An open one means that at both ends you have to make a decision, in traffic, at
speed. The gap in the ring is the design.

### 3.1 Chokepoints, and what each one is for

| Chokepoint | Links | Why it plays differently |
|---|---|---|
| **Kessel Bridge** | Centre ↔ Kepler Flats / Ferrone Hill | The only vehicle crossing of the channel. 640 m of deck with no exits: once you commit, both ends are known. The premier roadblock site, and the reason boats matter |
| **Harbour Tunnel** | Ostend Docks ↔ Vellum Row | Enclosed. No line of sight from above, no minimap detail, and — later — no helicopter. The *stealth* chokepoint: you go in visible and come out having chosen one of two exits |
| **The Shoulder** | Everything ↔ Ferrone Hill | One paved road up, 11 switchbacks, 9% grade. Block it and the hill is sealed to anyone who does not know about the fire road. Uphill chases favour whoever has the power; downhill ones favour whoever is braver |
| **Camber Causeway** | Island ↔ the airfield | 900 m, two lanes, water on both sides, no shoulder. A single stopped vehicle closes it. It guards the plane, so it is the most contested 900 m on the map |
| The four Saltmarsh crossings | Vellum Row ↔ Saltmarsh | *Soft* chokepoints — four narrow bridges over the creek, all passable, none fast. Chases here fragment rather than stop |

**What this means for police.** *Probable Cause*'s roadblock system already walks
up to 12 hops forward along the straightest continuation of the player's lane and
stages 2–3 cars at 120–200 m ahead (`RoadblockTuning` in `police_ai.h`). That
logic transfers directly and gets much better here, because Pinatty has real
topology: on the Kessel Bridge or the Causeway a block is genuinely
unavoidable, in Vellum Row it is a suggestion, and in Saltmarsh the site
selector will usually fail to find a wide-enough span at all — which is the
correct outcome, arrived at by geometry instead of by a special case.

**Roadblock placement should be authored-aware, not purely derived.** Give each
spine edge a `block_quality` byte: 0 means "never stage here" (a tunnel, an
intersection you cannot see into), 255 means "this is what this road is for".
Derived-only selection will eventually stage two cruisers across a hairpin at
the top of the Shoulder and kill the player with a cutscene they cannot avoid.

---

## 4. Landmarks and legibility

On a 5.5 km island you navigate by silhouette. Three rules:

**1. Two landmarks visible from anywhere on land.** Not a hope — a constraint to
be *tested*. A headless test samples the road network at 100 m intervals, raises
an eye point 1.6 m, and asserts at least two entries of the landmark table are
above the horizon and unoccluded. That test is cheap, it is pure, and it will
catch the day somebody lowers the hill by 20 m.

**2. Landmarks are ranked by how far they read.**

| Tier | Reads from | Members |
|---|---|---|
| **Island** (visible from everywhere) | 4+ km | Ferrone Mast (195 m ASL), Trinity Tower (100 m ASL), the Kepler flare (night) |
| **District** (tells you which district) | 1–2 km | Gantry cranes, Ferris wheel, water tower, courthouse dome, stadium bowl, lighthouse, quarry face, Kessel Bridge towers |
| **Corner** (tells you which block) | 100–300 m | Fishmarket clock, the Halloway steps, the neon on the Vellum Row cinema, six authored murals |

**3. The skyline is district-typed, not noise-typed.** Each district's building
height envelope is narrow enough that its silhouette is recognisable: Vellum Row
is a bell curve peaking at its centre, Saltmarsh is a flat 8–22 m mat, Nickel
Heights is a uniform 4–9 m carpet, the Docks are low sheds punctuated by four
tall cranes. Get this right and a player at 800 m knows which way they are facing
before they read a single sign. Get it wrong — sample heights uniformly per
building — and every district looks like static.

Supporting the silhouette: per-district **material palette** (Saltmarsh cobble
and brick, Vellum Row glass and pale concrete, Kepler Flats rust and cracked
asphalt) and per-district **street furniture kit** (the lamp posts differ, the
bins differ, the road markings differ). Those are cheap, they are authored as one
enum each, and they do more for legibility per byte than anything else on this
list.

---

## 5. The authored definition

### 5.1 What it is, and why it is not a data file

**The map is C++ source data — `constexpr` tables compiled into `apricot_sim`.**

That decision deserves its argument, because "authored map" instinctively means
"map file", and a map file is the wrong answer here:

- **Apricot ships no asset files, deliberately.** A `.map` file would be the
  first, and the first one is the one that makes the second one reasonable.
- **World generation currently cannot fail.** It is arithmetic. Add a loader and
  it can now fail at runtime — file missing, file truncated, file from an older
  build — and every caller of `height_at()` inherits a failure mode it has no
  way to handle.
- **A file needs a parser, a schema, a version, and a migration path.** All four
  are real code with real bugs, bought to solve a problem — hot reload — that a
  30-second incremental rebuild also solves.
- **Compiled data gets the build's guarantees for free**: `-Werror`, designated
  initialisers that fail to compile when a field is renamed, `static_assert` on
  invariants (polygons wound the same way, district ids dense and unique, spine
  node references in range), and the same golden-value determinism tests that
  already pin the noise constants.
- **It is still perfectly diffable and human-editable.** It is text in git. A
  district's character is 30 lines you can read in a code review, and changing a
  block size shows up in `git diff` as a changed block size.

The honest cost: **no live editing.** A designer cannot drag a road and watch it
move. The mitigation — and this is a later ticket, not a hidden dependency — is a
dev-only overlay that *dumps* the current table plus in-session tweaks back out
as source you paste in. The source stays the truth; the tool is a printer.

### 5.2 Files and size

```
src/game/pinatty/
  map.h            types: District, SpineNode, SpineEdge, TerrainOp, Landmark
  districts.cpp    ~10 district entries              est. ~330 lines
  spines.cpp       ~60 nodes, ~90 edges              est. ~180 lines
  terrain_ops.cpp  ~40 operators                     est. ~150 lines
  landmarks.cpp    ~30 landmarks                     est. ~90 lines
  palettes.cpp     material + furniture kits         est. ~120 lines
  map.cpp          index build, queries, validation  est. ~300 lines
```

**Estimated total: ~1200 lines, ~45 KB of data.** The worked example below is 32
lines, counted, which is where the district figure comes from; the rest are
proportional estimates and not measurements. For scale, that is smaller than *Probable
Cause*'s `road_network.cpp` alone (1038 lines), and the whole of Pinatty fits in
less text than one of its systems.

### 5.3 A district entry, written out

```cpp
// src/game/pinatty/districts.cpp
inline constexpr District kVellumRow = {
    .id       = DistrictId::VellumRow,
    .name     = "Vellum Row",

    // Boundary polygon, world metres, counter-clockwise, convex not required.
    // Coarse on purpose: this is the district's JURISDICTION, not its outline.
    // The visible edge is where its generated blocks stop, which is decided by
    // the spines that cut through it.
    .boundary = {{-470.0f, -520.0f}, { 560.0f, -470.0f}, { 610.0f,  300.0f},
                 { 180.0f,  430.0f}, {-430.0f,  360.0f}},

    .blocks   = {.pattern      = BlockPattern::Grid,
                 .grid_deg     = 6.0f,      // off north: nothing real is square
                 .block_m      = {92.0f, 62.0f},
                 .street       = RoadClass::Street,
                 .one_way_pair = true,      // alternating N/S and E/W pairs
                 .alley_odds   = 0.75f},    // per block, hash-keyed

    .build    = {.coverage   = 0.86f,       // fraction of a lot built on
                 .setback_m  = 1.0f,
                 .height_m   = {24.0f, 88.0f},
                 .height_ramp = HeightRamp::PeakAtCentre,  // makes a skyline
                 .footprint_m = {14.0f, 34.0f},
                 .facades    = FacadeKit::Downtown},

    .ground   = {.road = Surface::Asphalt, .walk = Surface::Concrete,
                 .verge = Surface::None,   .kerb_m = 0.12f},

    .props    = {.kit = PropKit::Downtown, .density = 1.0f},
    .pop      = {.traffic = 1.4f, .ped = 2.2f, .parked = 0.9f},
    .heat     = {.patrol = 1.2f, .response_s = 6.0f},
};
```

Thirty-two lines (counted, not guessed), and every one of them is a decision
somebody made on purpose.
Nothing in it names a mesh, a texture or a file.

### 5.4 A road spine, written out

Spines are the roads that are *authored*. Everything below street level is
generated.

```cpp
// src/game/pinatty/spines.cpp
inline constexpr SpineNode kNodes[] = {
    {.id = N::KesselS,  .pos = { -180.0f, -1180.0f}, .y_m = 14.0f, .kind = NodeKind::BridgeHead},
    {.id = N::KesselN,  .pos = { -120.0f, -1820.0f}, .y_m = 14.0f, .kind = NodeKind::BridgeHead},
    {.id = N::HillFoot, .pos = {  640.0f, -1520.0f}, .y_m = 22.0f, .kind = NodeKind::Junction},
    {.id = N::Shoulder1,.pos = {  900.0f, -1420.0f}, .y_m = 48.0f, .kind = NodeKind::Bend},
};

inline constexpr SpineEdge kEdges[] = {
    // The Kessel Bridge. Deck height is authored, not draped: a bridge that
    // follows the terrain is a causeway, and the channel underneath is 26 m of
    // authored carve that a drape would happily drive into.
    {.a = N::KesselS, .b = N::KesselN, .cls = RoadClass::Freeway,
     .shape = Shape::Straight, .structure = Structure::Bridge,
     .deck_y_m = 26.0f, .block_quality = 255},

    // The Shoulder's first switchback. `bulge_m` is the quadratic control
    // offset perpendicular to the chord — one float instead of a control
    // point, so a hairpin is legible in the diff.
    {.a = N::HillFoot, .b = N::Shoulder1, .cls = RoadClass::Street,
     .shape = Shape::Arc, .bulge_m = -70.0f,
     .structure = Structure::Cut, .grade_max = 0.09f, .block_quality = 40},
};
```

An edge is: two node ids, a class, a shape (`Straight` / `Arc` with one bulge /
`Poly` with up to four interior points), a structure (`Ground` / `Bridge` /
`Tunnel` / `Cut` / `Fill`), a maximum grade, and a `block_quality`. That is
enough to express every road in Pinatty that a human should be deciding, and
short enough that ninety of them fit on two screens.

Terrain ops are the same shape — a polygon or a swept corridor, a kind, a target
height, a feather distance.

---

## 6. What generation derives

Given the tables, in order. Every step is a pure function of
`(map tables, kMapSeed, the thing being generated)`, and every step keys its
entropy on a **stable authored identity** — never on a counter, never on
iteration order, never on how the player approached.

| # | Step | Derived from | Keyed by |
|---|---|---|---|
| 1 | **Height field** | Noise constants + terrain ops | `(x, z)` |
| 2 | **Spine geometry** | Spine edges tessellated at 4 m, vertical profile from node heights and `grade_max` | edge id |
| 3 | **District street grid** | Block params, clipped to the boundary polygon and to any spine crossing it | district id + block index |
| 4 | **Block polygons** | The closed faces of the street graph | canonical block corner |
| 5 | **Lots** | Recursive split of a block along its street frontage until each lot fronts a street and fits `footprint_m` | `hash_coord(block corner, split depth)` |
| 6 | **Building footprints** | Lot inset by `setback_m`, area scaled to `coverage` | lot id |
| 7 | **Building heights** | Sample of `height_m` shaped by `height_ramp` and distance to district centre | lot id |
| 8 | **Facade parameters** | Window pitch, floor height, colour drawn from `FacadeKit` | lot id |
| 9 | **Sidewalks and kerbs** | Carriageway width + `SIDEWALK_WIDTH_M`, offset from the same centreline the mesh uses | edge id |
| 10 | **Surface painting** | Distance to nearest road segment, from the spatial index | `(x, z)` |
| 11 | **Street furniture** | Placed along road edges at an authored pitch per `PropKit` | `(edge id, index along edge)` |
| 12 | **Vegetation and rocks** | Existing `scatter_chunk()`, suppressed inside district polygons | chunk coord |
| 13 | **Lane graph** | Two directed lanes per carriageway, offset `width/4`, junctions merged by proximity — *Probable Cause*'s `LaneGraph::build_from_freeform` is the right algorithm | edge id + side |

### 6.1 Two things that must change from the reference implementation

**Street furniture cannot ride the scatter grid.** `scatter_chunk()` places at
most one prop per 4 m cell on a chunk-aligned lattice, which is exactly right for
trees on a hillside and exactly wrong for lamp posts, which need to be at a
regular pitch *along a road* and offset a fixed distance from its kerb. Furniture
is placed per road edge — key `(edge id, index)` — and the placement is
clipped to the chunk being built, so a lamp near a boundary is emitted by exactly
one chunk. Two prop systems, one grid-keyed and one edge-keyed, and they must not
be merged for tidiness.

**Surface classification cannot be rasterised at startup.** *Probable Cause*'s
`SidewalkField` bakes a coverage grid from the drawn road triangles, which is a
sound answer at its world size and impossible at Pinatty's: 6144 m at the ~0.5 m
resolution a kerb needs is 12288² ≈ 151 million cells. Pinatty answers the same
question analytically — "how far is this point from the nearest road segment, and
which side of the kerb line is it on" — through the same static spine index the
terrain ops use. Same single source of truth, no bake, no memory, and it works in
chunks that have never been meshed. This is the piece I would build first,
because everything from grip to footstep sounds to skid-mark colour hangs off it.

---

## 7. The hard parts

### 7.1 Streaming at 16 km²

**First, a correction to the brief.** It says the `Streamer` only *plans*
evictions and no consumer exists. That was true when `docs/architecture.md` was
written; it is not true of the code. `Streamer::evict()` sweeps the resident map,
the delivered queue, the half-activated chunk and the in-flight set, and ends
with `scene.remove_many(doomed_scratch_)` — the bulk consumer, implemented, with
the ordering hazard (a stale eviction meeting a fresh reload) already reasoned
about in the header. `architecture.md` still refers to `pending_evictions()`,
`mark_evicted()`, `mark_resident()` and `max_loads_per_update`, none of which
exist in `streamer.h` today. **The doc is stale, not the code.** Someone should
fix the doc; it is not a Pinatty ticket, but it will mislead whoever reads it
next.

**Measured costs.** Real `build_chunk` and `scatter_chunk` out of
`libapricot_sim.a`, 128 chunks, `-O2`, this machine:

```
$ chunkbench 128
build_chunk    2.317 ms  (4225 verts, 8192 tris each)
scatter_chunk  0.187 ms  (102.2 props each)
```

Real `Scene::cull()` against a 6 km lattice of static nodes:

```
$ cullbench
nodes   20000   visible    1805   cull 0.154 ms/call
nodes   60000   visible    5360   cull 0.278 ms/call
nodes  200000   visible   17921   cull 1.449 ms/call
```

What those numbers say:

- **Steady-state driving is fine.** At 40 m/s you cross a 64 m chunk every 1.6 s;
  the leading edge of a `load_radius = 4` ring is 9 chunks, so ~5.6 chunks/s ≈ 13
  ms/s of meshing — about 1.3% of one core. The instance budget has similar
  headroom: 384 instances/step at 120 Hz is 46,000/s against a demand near 900/s.
  **Steady state is not the problem and should not be optimised.**
- **View distance is the problem.** `load_radius = 4` is 256 m. Pinatty's whole
  legibility argument rests on seeing a landmark 2–4 km away. A 2.5 km
  full-detail ring is **4794 chunks** — at 294 KB and 2.317 ms each, that is
  **1.34 GB of vertex data and 11.1 seconds of single-threaded meshing.** Not a
  tuning problem, a missing feature. **Terrain LOD is a hard prerequisite for
  this map**, not a polish item — three or four rings at 1/4, 1/16, 1/64 vertex
  density. (I first wrote ~1900 chunks here from a bad mental estimate; the
  arithmetic is `π · (2500/64)²` and it is 2.5× worse than I guessed.)
- **LOD reopens the seam guarantee.** Today seams close *exactly* because
  neighbouring chunks evaluate `height_at()` at identical coordinates. Two
  different LODs do not share those coordinates and there will be cracks. The fix
  is skirts or an explicitly stitched transition row — and the existing rule
  stands: **never average across the boundary.** Averaging hides the crack and
  keeps the cause.
- **The budget needs a second denomination.** `max_instances_per_step` correctly
  budgets node creation. It does not budget *vertices*, and with LOD a chunk's
  mesh cost stops being constant. Both budgets, spent independently, with the
  chunk the player is standing in jumping the queue ahead of the ring.
- **The cull scan holds up better than I expected.** A flat `std::vector` scan
  costs 0.28 ms at 60k nodes — call it 1.7% of a 60 Hz frame. It degrades to 1.45
  ms at 200k, and the survivor sort (survivors are sorted by batch key, then id)
  grows with the visible count on top. **Recommendation: cap simultaneously
  resident static instances near 60k and get there with draw-distance tiers, not
  with a BVH.** Adding a broadphase now would be optimising a thing that is not
  yet slow. Revisit if the number goes past ~100k.
- **The cold fill and the teleport are unhandled.** 50 chunks at 2.3 ms is 115 ms
  of meshing at startup and again on every mission warp or respawn across the
  island. There is no "fill before resume" path today, so a warp drops the player
  into void for a fraction of a second and then hitches. This needs an explicit
  mode, and it is a Pinatty blocker in a way steady-state streaming is not.

**Honest caveats.** All three benchmarks are one machine (Apple silicon, `-O2`),
single-threaded, with synthetic node layouts and no dirty transforms. `build_chunk`
was measured at the *current* terrain tuning; the retune in §1.2 adds no octaves
so it should be within noise of the same, but I did not measure the retuned
version through `build_chunk`. And there is no render pass in apricot yet, so
**the GPU cost of ~18k visible instances is entirely unknown** — the batcher
collapses by mesh and material, and a city has few distinct meshes, so it *ought*
to collapse well. I have not run it. Do not plan around that hope.

### 7.2 Traffic and pedestrians at city scale

**The reference implementation cannot come across, and it is worth knowing
exactly why.** *Probable Cause* keeps at most 75 cars and 75 peds alive, spawns
them in a 140 m ring, despawns at 200 m, and **has no AI LOD at all** — distant
agents simply do not exist, and everything alive is simulated at full rate. That
is a legitimate design. It also:

- draws from two shared `std::mt19937` streams (`0xCABBA9E`, `0x9EDA11E`), so an
  agent's behaviour depends on how many draws happened before it;
- seeds one path from `std::time(nullptr)` (police-station cruiser stalls);
- keeps *every* lane "loaded" forever and does O(lane_count) linear scans several
  times per frame, plus up to 24 more per spawn attempt.

Every one of those is banned outright in apricot — no sequential generator state,
no time seeding, no wall clock below `App`. So this is a rebuild, not a port, and
the interesting question is what replaces it.

**The trap, stated precisely.** "Only simulate what is near the player" is not
by itself non-deterministic: the player's position is a pure function of seed and
inputs, so anything keyed off it is too. Four specific things break it, and they
are what to actually defend against:

1. **Keying off the camera instead of the player.** The camera is a render-side
   object, free-look, updated at frame rate. LOD keyed to where you are *looking*
   makes the sim depend on the display. Keying off the sim-side player position at
   step boundaries does not.
2. **Transitioning on frames instead of steps.** A 144 Hz machine changes LOD at
   different moments than a 60 Hz one. LOD transitions happen on step counts,
   like input edges already do.
3. **Promotion is not reversible.** A cheaply-advanced car promoted to full
   simulation is not in the state it would have been in had it been simulated all
   along. This is the real one, and no amount of care makes an integrated
   approximation match an integrated truth.
4. **Population-dependent randomness.** Any shared RNG stream makes agent N's
   behaviour depend on how many agents exist — which depends on LOD — which
   depends on where the player went. This is exactly the failure the "no
   sequential stream" rule already exists to prevent.

**What I would build: an analytic ambient population.**

Distant traffic is not simulated cheaply. It is not simulated *at all* — it is
**defined**. For each directed lane, an authored-density-scaled number of phantom
slots exist. Slot `k` on lane `L` has a departure step and a nominal speed
derived from `hash_coord(kMapSeed, laneId, k)`, so its position at sim step `t`
is a **closed-form function of `t`**. No integration, no history, nothing to
lose.

- When the player comes within the active radius, a phantom is **instantiated**
  into a real agent at exactly the position and velocity its closed form gives at
  that step. Arriving from the north and arriving from the south instantiate the
  identical car, because the closed form does not know you were there.
- When a real agent leaves the radius it is **retired permanently**, and the
  lane's phantom schedule carries on as if it had never existed. It does *not*
  demote back to phantom, because a car you rammed cannot be described by a
  closed form any more. The player-visible consequence — "the wreck is gone when I
  come back" — is what this genre does anyway.
- Per-agent randomness is keyed `hash(kMapSeed, laneId, slot, decision_index)`.
  Never a stream. The active set iterates in a fixed order by that stable id, not
  by hash-map order.
- Density comes from the district table (§2.3) via the lane's authored
  `traffic` / `ped` scalars, so a district is busy because it was *authored*
  busy, not because a spawner happened to land there.

**Cap and radii, first pass:** 120 vehicles and 150 pedestrians active, vehicles
in a 220 m ring despawning at 320 m, peds 110 m / 160 m. Larger than the
reference because Pinatty's sight lines are longer and an empty arterial reads as
broken. These are guesses to be tuned, and they are the numbers I would least
trust in this document.

**The two tests that actually catch a broken LOD.** Both are headless and both
belong in the suite from day one, because retrofitting them after the fact means
finding out the sim was never deterministic:

1. **Radius invariance.** Play the same input tape twice with different streaming
   and AI-activation radii. Assert bit-identical vehicle state, wanted level and
   agent-population hash at every step. This catches anything that leaks
   "how much was resident" into the sim.
2. **Camera invariance.** Play the same tape twice with the camera forced to
   different orientations. Assert bit-identical sim state. This catches anything
   keyed off the view instead of the player — the mistake that is easiest to make
   and hardest to see.

**What I cannot answer without building it.** Whether 120 active vehicles at 120
Hz fits the frame budget alongside terrain streaming — apricot has no vehicle
model beyond gravity and a terrain rest, so there is no per-car cost to
extrapolate from. Whether the phantom schedule *looks* right, which is a
feel-check with a controller and not a test. And the lane count: Pinatty's
urbanised area is roughly 5.5 km², which at typical block pitch is on the order
of 5,000–10,000 directed lanes — an estimate from block geometry, not a measured
figure, and the design should not depend on which end of that range it lands.

---

## 8. What this changes about apricot's architecture

One consequence deserves to be stated on its own, because it contradicts a rule
currently written down as settled.

`docs/architecture.md` says: *"The seed is the world identity. A save file is a
seed... Nothing about the world needs to be serialised because nothing about the
world is authored."*

**For the pilot game that stops being true**, and pretending otherwise will cause
a confusing bug later. Pinatty splits one seed into two:

- **`kMapSeed`** — a pinned constant in the map tables. It selects the noise
  detail *under* the authored skeleton. Changing it changes the city, which
  invalidates tapes and saves exactly like changing the hash does, and it should
  be as hard to change as the golden values are.
- **The run seed** — still per-session, still in the tape, but it now identifies
  the *session*, not the world. Mission shuffles, ambient variation, anything that
  is allowed to differ between two visits to the same corner.

The world still needs nothing serialised: the map tables are in the binary. But
"the seed is the world" becomes "the *build* is the world", and that is a
different sentence with different consequences for what a save file has to
contain.

---

## 9. Open questions

Things I could not settle from reading, and would rather leave written down than
invent an answer to.

1. **Which seed.** `0xDEADBEEF` measures best on the retuned constants (73% of
   land under 5°, a clean NE massif, a broad western plain), but that is against
   *candidate* constants measured on a scratch copy. The seed must be re-chosen
   by measurement once the terrain ticket lands, and then pinned like a golden
   value. Treat the candidate as a starting point, not a decision.
2. **Interiors.** Nothing above says whether buildings are enterable. If any are,
   lot generation has to reserve door positions on the street frontage, which is
   a constraint on step 5 that is much cheaper to add now than later.
3. **Water as a surface.** `Surface` has four members (Rock, Gravel, Grass,
   Sand) and the enum is append-only by contract. A harbour city wants boats,
   shallows and a swimming state. That is at least one new `Surface` member and a
   physics conversation, and it is not in this design.
4. **Bridges and tunnels versus a height field.** A height field cannot overhang,
   and `normal_at()`'s positive-Y invariant depends on that. Bridge decks and
   tunnel bores are therefore *not* terrain — they are generated geometry with
   their own collision, and the terrain underneath them is carved as if they were
   not there. Someone has to decide how the vehicle ground query picks between
   the deck and the ground 26 m below it. That is a physics ticket and it blocks
   the Kessel Bridge.
5. **The train.** Kepler Flats' level crossings are only a hazard if something
   crosses them. A single scheduled train on a fixed loop is closed-form and
   deterministic and cheap. It is also entirely absent from every estimate above.

---

## 10. Appendix — how the numbers in this document were produced

Four throwaway rigs, written for this design pass and living in `/tmp/pinatty-probe/`.
None of them is in either repo, none is committed, and neither repo was modified
to run them. They are listed so the numbers can be re-derived rather than
believed.

| Rig | What it does |
|---|---|
| `probe.cpp` | Samples `height_at()` / `normal_at()` over a box and reports land area, peak, and height/slope percentiles |
| `map.cpp` | Coarse ASCII plan view of the island, so the coast and the high ground can be *looked at* before districts are drawn on them |
| `chunkbench.cpp` | Times the real `build_chunk()` and `scatter_chunk()` out of `libapricot_sim.a` |
| `cullbench.cpp` | Times the real `Scene::cull()` against N static nodes spread over a 6 km lattice |

Built against the real tree for the "today" figures:

```sh
clang++ -std=c++17 -O2 -ffp-contract=off \
  -I pengine-apricot/src -I pengine-apricot/build/_deps/glm-src \
  -o probe_current probe.cpp pengine-apricot/src/terrain/heightmap.cpp
```

The retuned figures in §1.2 come from a **scratch copy** of `heightmap.{h,cpp}`
and `noise.h` under `/tmp/pinatty-probe/inc/`, with the constants rewritten by
`tune.sh` and compiled from there. The exact set measured:

```
kFeatureMetres 240   kHeightMetres 190   kShoreLevel 0.22
kContinentMetres 1850   kRidgeMetres 420   kIslandRadiusMetres 2750
kShoreFalloffStart 0.66   kCoastWarpMetres 560   kIslandPlatform 0.235
kLowlandSpan 0.30 / kMountainSpan 0.70
kLowlandContinentShare 0.70 / kLowlandHillShare 0.30
kSpineStart 0.55 / kSpineFull 0.85
```

**One caveat on those measurements:** they were taken with `kHomeRadiusMetres`
still at 380, i.e. with the spawn-lift dome still in place. §1.2 recommends
deleting it. Removing it will change the terrain within 380 m of the origin —
which is the middle of downtown — so the land-area and slope figures should be
re-measured after that change rather than carried forward.
