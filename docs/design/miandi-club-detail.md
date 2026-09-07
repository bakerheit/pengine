# Miandi venue detail: the seven layers, worked on Club Mirage

Miandi's venues were authored in one wave of four parallel packages
([`miandi-nightlife-expansion.md`](miandi-nightlife-expansion.md)), then given
names and neon in a second
([`miandi-vice-city-visual-direction.md`](miandi-vice-city-visual-direction.md)).
That second document already stated the problem it did not solve:

> Buildings read as decorated boxes. The surrounding context blocks are even
> coarser.

This is the pass that fixes it, on one building, so the method can be judged
before it is spent five more times. **Club Mirage is the reference. The seven
layers below are the transferable part; the metre values in
[`src/city/miandi_prism_works.h`](../../src/city/miandi_prism_works.h) are only
how those layers land on that block.**

Nothing here is a texture or a shader change. Every layer is authored geometry
and authored light through the existing creator, because that is what the
other five venues can be given without new systems.

---

## 1. What the captures actually showed

Before touching anything, the club was photographed from all four streets, at
noon and at midnight, in the real running game. The set is under
`build/qa/mirage-before/` and it is the whole argument for what follows.

| Capture | What it showed |
|---|---|
| `front` | A flat brick plane. The 0.14 m piers were invisible. A **lawn** ran from the kerb to the wall, crossed by two concrete ribbons. |
| `west` | 70 m of blank wall over 30 m of lawn on Solana Avenue. |
| `east` | 70 m of near-black wall over 30 m of lawn on Mango Avenue. |
| `south` | Same, on Coral Way. |
| `plan` | The block is **grass with a shed on it**. Every neighbouring block has more ground treatment than the finished one. |
| `night` | Sign legible, building black. Four of the six authored lights aimed down and *north* — away from the building — so they lit empty ground. |

Two of those are not detail problems at all, and finding them is the reason to
photograph before authoring:

- **The block had no ground.** No amount of facade work fixes a club standing
  on a lawn.
- **Club Mirage was not on Miandi's paint kit.** `world.cpp` applied
  `miandi_presentation()` to Ocean Drive and the context massing only. On the
  plain path the shell took a flat finish tint at no texture scale, so *one
  brick shed* read maroon from Bayfront, grey from Solana and black from
  Mango. Every venue package outside Ocean Drive has this bug today.

---

## 2. The seven layers

Apply them in order. Each one is cheap only because the one before it is done;
skipping to layer 5 is how you get a decorated box with a nice bin store.

### Layer 1 — ground before building

**A city block has no lawn on it.** Pave the parcel to the kerb seam and let
each quarter say what its side of the block is for. Concrete where people
walk, asphalt where vehicles turn.

The kerb seam is not the parcel edge. It is the road's outer sidewalk edge:
`carriageway/2 + SIDEWALK_WIDTH_M`, which is 10 m from a Street centreline and
14 m from an Arterial. Stopping 3 m short leaves a ribbon of grass that reads
as an authoring mistake from a moving car, and it did — the first fix round
here stopped at ±87 and had to be pushed to ±90.

Layer the surfaces by height, never coplanar: base paving at `bottom 0.0,
height 0.10`, and the routes already authored on top at `height 0.12`. Two
centimetres is invisible and it makes z-fighting impossible.

### Layer 2 — four faces, four jobs

A parcel on a grid has as many public faces as it has streets. Mirage has
four, and had designed two.

Give every face a job and **at least one real opening**. Mirage's west wall got
a fire door onto the alley; the rear wall got a kitchen door onto the food yard
and a crew door onto the loading court — two rooms this plan already had and
could not be entered from. Then give all four the same base/shaft/cap grammar
so the block reads as one building from any street, and let them diverge in
what hangs on that grammar.

The east wall was also changed from steel cladding to brick. It is the same
shed; cladding one elevation of it turned a whole street frontage black.

### Layer 3 — depth over decal

**Articulation is measured in the shadow it casts.** The first pass's piers
were 0.14 m proud and did not exist at street distance. This parcel now pins a
floor — `kMiandiMirageReliefDepthM` = 0.30 m — and the suite fails any piece
calling itself a pilaster, base course, cornice, string course, attic or portal
jamb that is shallower.

Mirage's front got eleven 1.1 m × 0.44 m brick pilasters on the bay lines,
stepping around every opening rather than crossing one; a 1.10 m base course in
three runs so the doors keep their gaps; a string course at window-head height
and a cornice under the roof parapet. Eleven verticals read as a fence until
two horizontals cross them.

**Articulation and a mural compete for the same wall.** The Solana elevation
is the mural face, so it spends its depth on the base, the cornice, the
downpipes and the roof ladder and leaves the mural field flat. Pick one per
face.

### Layer 4 — the threshold is a room

An entrance is a sequence, not a doorway. Mirage's club door is now six moves
over thirty metres: lot, bay stripes, walk, queue pen under a canopy, rope
line, brick portal, door — with a raised attic over it and a sign gantry above
that, so the sequence is legible from the far kerb as well as from the rope.

The gallery keeps the factory's own threshold instead: an undemolished loading
dock, now a terrace with steps and a pipe rail beside its door. Two doors on
one building should not arrive the same way.

One trap: **a door gap with no room behind it shows the daylit floor slab
straight through the wall.** Both public doors got a dark baffle a few metres
in. It is two boxes and it is the difference between a nightclub and a hole.

### Layer 5 — servicing, told rather than hidden

A club this size runs on deliveries, bins, a transformer and a kitchen extract.
Hiding all of it is what makes an authored building feel like a facade on a
film lot. Mirage's Mango Avenue elevation now carries a dock canopy, roll-up
guides, two bins in a corral, a fenced transformer, pallets, a gas bottle rack
and yard hatch markings.

**Fence the yard, but at waist height.** A 2.2 m slab is a wall, and a wall
hides the very thing the face exists to show. A 1.05 m base with a rail on
posts fences the yard and still lets the street see into it.

### Layer 6 — the roof is the fifth face

The shell is 8 m tall and 110 m long, which is one flat line from every point
on Bayfront Avenue. A raised brick attic over the entrance bay with stepped
shoulders, and a steel sign gantry above that, are what give the club a
silhouette. Ducts, cowls, a hatch and drain hoppers do the near work; the
gantry does the far work.

The rooftop mark is a **shape, not a name**: a three-tube prism outline. A
second copy of the lettering would have cost seventy glyph strokes and read
worse at distance than an outline does.

### Layer 7 — time on the building

The reuse story has to be *in* the geometry, not in a comment. Mirage carries
the frame of a sign that was removed and the unfaded rectangle of paint behind
it, two window bays panelled shut in steel while their neighbours survive,
three patched brick panels, and one bright new condenser among the old dark
roof plant.

None of this is weathering. There is no procedural dirt anywhere in it.

### And then: the light hierarchy

Four levels, one authored source each, in this order: **sign, threshold,
facade, incidental.** Mirage's six lights are now roof sign wash, club door,
north facade wash, mural court, food yard, loading edge.

Two rules learned the hard way, both already learned once on Ocean Drive and
then not applied here:

- **Mount outside the face and aim back at it.** Four of these lights pointed
  away from the building and lit bare ground.
- **Aim at wall, not at opening.** The facade wash originally sat in front of
  the gallery *door* and shone straight through the gap into the building. It
  was moved to x=-43 so its cone lands on brick, a pilaster and the base
  course, and only grazes the door reveal.

---

## 3. What it cost, measured

| | Before | After |
|---|---:|---:|
| Baked pieces | 239 | 508 |
| Collision solids | 44 | 159 |
| Tallest point | 9.95 m | 16.88 m (cap 18 m) |
| Authored night lights | 6 | 6 |
| Visible scene nodes, club front | 798 | 988 |
| **Draw calls, club front** | **300** | **298** |

The draw count did not move, and slightly fell. Every added piece is one more
instance on the shared unit-box mesh, so they merge into the existing batches
(9 instanced batches became 12). Streaming spikes over 4 ms per 150-frame run
were 4 before and 4 after. **This layer is cheap in draw calls and expensive in
scene nodes**, and that is the trade the piece budget in
`miandi_prism_works_tests.cpp` guards: 300..560, currently 508.

---

## 4. Verification

Rebuilt `build/bin/apricot` and captured the same eight views plus two
entrance close-ups from the real game, isolated save, 150-frame bounded runs,
clean exit. `build/qa/mirage-before/` and `build/qa/mirage-detail/` are the
same camera positions, so the pairs are directly comparable.

Focused suites green: `miandi_prism_works_tests`, `miandi_integration_tests`,
`miandi_night_lighting_tests`, `miandi_presentation_tests`,
`miandi_context_tests`, `miandi_ocean_drive_tests`, `building_access_tests`,
plus the full `tools/ci.sh` gate.

New regressions in `miandi_prism_works_tests.cpp`, one per layer that could
silently rot:

- the four paving quarters exist, are non-solid, reach their exact seams, and
  pass `miandi_ground_piece()` so the player walks and drives on them;
- every wall of the shell carries at least one opening;
- anything named as facade relief is at least `kMiandiMirageReliefDepthM` deep;
- the threshold sequence — queue canopy, portal, attic, gantry — is present;
- the building breaks 13 m so it still has a silhouette.

**What this pass does not claim.** No interior exists behind either public
door; the baffle is a baffle. Pedestrians, traffic and any club activity are
still absent — this is authored geometry only. The offshore promenade terrain
mismatch and the road-flicker report from the previous pass are untouched.

---

## 5. Rolling it out

Two shared changes were needed to make one venue work, and they are the
prerequisite for the rest, not part of the per-venue cost:

1. `MiandiSurface::Brick` in
   [`miandi_presentation.h`](../../src/city/miandi_presentation.h), mapped to
   the existing brick material at a 1.8 m tile. This also affects the two
   Brick-finished pieces in the context massing, which previously resolved to
   stucco.
2. `kMiandiPrismWorksSite` added to `miandi_detailed_site` in
   [`world.cpp`](../../src/app/world.cpp). **The other four venue packages —
   Calle Noche, Mariposa, Palmera, Port Sol — are still off the paint kit and
   still render on the plain path.** Adding each one is a line, and it should
   happen with that venue's detail pass, not before, because it also turns its
   authored paving into firm ground.

The per-venue order is layers 1 → 7 as written. Expect roughly 250–300 added
pieces each on this evidence.

One real constraint blocks the light layer: `kMiandiNightLights` is capped at
28 entries by `miandi_night_lighting_tests` and is at exactly 28. Mirage's pass
fitted inside its existing six by re-aiming them, which was enough here because
the hierarchy needed re-pointing more than it needed count. A venue with more
frontage than this one will not fit, and raising that cap is a deliberate
decision with a stated GPU cost — the tiled spotlight path is per-frame per
tile — not a number to nudge.
