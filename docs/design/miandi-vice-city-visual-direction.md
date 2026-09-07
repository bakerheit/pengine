# Miandi: sun-faded resort city, 1986

## Vision

Make Miandi feel like a believable South Florida city with the bold, readable
silhouettes and warm-day/cool-night contrast associated with Vice City. This is
an original district: retain Miandi's names, geography, roads, and assets.
The target is architectural credibility at driving and walking distance, not
photorealism or a replica of Rockstar's map.

## Approved venue names and identities

The user approved this naming/identity pass after the street-front correction.
These are original working venue identities, not claims of trademark clearance.

| Former name | Current name | Authored direction |
| --- | --- | --- |
| Coral Crown | The Bellmar | Pink Deco; old money and faded glamour |
| Blue Heron | The Maravelle | Pale seafoam/ivory walls, elegant cyan-lit beachfront hotel |
| Sunwave Hotel | The Palmera | Buttercream resort, pool terrace and cabana bar |
| Prism Works | Club Mirage | Smoked windows, bright cyan name, late-night warehouse club |
| Sol Social Club | Club Candela | Warm terracotta/amber neighborhood Latin nightlife |
| Palma Dance Hall | Tropico Ballroom | Older cream-trimmed dance hall with a broad amber marquee |

Mariposa Motor Lodge retains its name and design. This pass preserves parcel
locations, real doors, collision, the 8 m beachfront setbacks and service routes.
No new interiors, music, activity simulation or weathering textures are implied.

`src/city/miandi_venue_identity.h` holds canonical display names, uppercase sign
copy and direction. Building plans, authored piece labels, night-light labels,
access references and design notes use the new names. Legacy C++ symbols and
header filenames remain stable internal IDs; they are not display copy.

The original tube alphabet is shared in `src/city/miandi_neon_sign.h`, extended
with the missing D/G/I/M/P/V glyphs. East and north facades use the same lettering
with an explicit facing transform. New Palmera, Mirage, Candela and Tropico signs
are real non-solid geometry, mounted above existing doors/awnings or the roof.
The longer Maravelle name uses 2.1 m letters so it fits its 25 m backing.

Venue-specific tints retain non-emissive walls, give Mirage smoked glazing, and
differentiate Maravelle, Palmera, Candela and Tropico. Existing actual night-light
sources remain; tube emissive color does not stand in for lighting the buildings.
The full map has six named venue markers anchored to real roof centers. The
minimap uses those same anchors and keeps the existing POI toggle/bearing rules.

### Naming-pass validation

Rebuilt `build/bin/apricot` and `build/bin/apricot_map_lab`. All ten focused
suites pass: `miandi_ocean_drive_tests`, `miandi_calle_noche_tests`,
`miandi_prism_works_tests`, `miandi_sunwave_hotel_tests`,
`miandi_resort_frontage_tests`, `miandi_presentation_tests`,
`miandi_night_lighting_tests`, `miandi_integration_tests`,
`building_access_tests`, and `minimap_tests`. Regressions cover exact display
copy, complete oriented lettering, sign-backing fit, distinct non-emissive
palettes, and preserved lobby, sidewalk and service access. This is not a
full-suite result.

Inspected real captures under `build/qa/miandi-venue-names/`:
`hotels-day.png`, `hotels-night.png`, `hotels-map.png`, `mirage-day.png`,
`palmera-day.png`, `candela-tropico-day.png`, and
`candela-tropico-night-wide.png`. The hotel night capture uses
`hotels-night-final.log`; other final captures have matching log stems.
The game captures use isolated saves and bounded 300-frame runs, all exiting
successfully with clean GL queues. They still report 145–146 streaming spikes
over 4 ms per run; this pass does not claim a performance or road-flicker fix.
The map capture verifies all six named venue markers. Visual checks are static,
with access traversal tested headlessly using the real character controller.
The user's running game was left untouched and needs a restart to load this
rebuilt version.

## Research and translation

- [Rockstar's Vice City description](https://play.google.com/store/apps/details?id=com.rockstargames.gtavc.de&hl=en)
  establishes the 1980s tropical/neon setting. Our interpretation is a clear
  daytime city silhouette with concentrated nightlife landmarks after dark.
- [Miami Beach Art Deco hotels](https://www.miamiandbeaches.com/hotels/art-deco-architecture-boutique-hotels)
  describes stepped crowns, symmetry, eyebrows, portholes, curved corners,
  and glass block. Translate those into actual facade depth, repeated room bays,
  shaded entrances and differentiated hotel rooflines.
- [NPS: South Beach pastel palette](https://www.nps.gov/articles/000/color-pallets-saving-buildings-from-demolition.htm)
  explains a late-1970s/1980s revival palette drawn from local light and landscape,
  with one building's body color feeding its neighbor's trim. Use shell pink,
  buttercream, seafoam and pale aqua, with saturated color on small accents.
- [City of Miami Beach: MiMo](https://www.miamibeachfl.gov/architecture/mimo-miami-modern/)
  identifies projecting eaves, breeze blocks, broad glass, tropical forms and
  large signs. Keep this as the motor-lodge/resort vocabulary rather than
  making every place another Deco hotel.

The resulting design decisions below are our synthesis, not claims about an
exact historic block. Contemporary reference photos are shape/material studies;
new LED billboards, modern luxury branding and present-day nightlife trends do
not define the period.

## What the current build lacks

The inspected `build/qa/miandi-80s/coral-day.png` has legible neon but sparse
room windows, broad blank sidewalls, thin unsupported-looking balcony rails,
and a bare grass forecourt. Buildings read as decorated boxes. The surrounding
context blocks are even coarser. Four existing hotel wall-wash lights already
work; adding more neon alone will not solve those daytime problems.

## District rules

1. Large scale: strong roof silhouettes, varied heights, ordinary buildings
   between landmarks, street-facing public fronts and discreet service backs.
2. Medium scale: plausible room/window spacing, projecting sills and shade
   ledges, layered balconies, framed entries and grounded cafe terraces.
3. Small scale: awning stripes, metal rails, planter rims, vents and drainpipes.
   Detail stays clustered at entrances and roof/service edges.
4. Ground: paved arrival courts meet the existing sidewalk; palms and furniture
   frame the route. Retain at least the existing 4.8 m hotel lobby paths and
   the west service lane. No new objects on road ribbons or at door thresholds.
5. Materials: subtle plaster/concrete texture at physical scale; differentiated
   glass and painted metal. No procedural random dirt sprayed over everything.
6. Night: retain pink/cyan tube signs and real bounded light washes. Windows,
   entrances and signs should form a readable hierarchy. Daytime walls stay lit
   by the normal renderer, not made emissive to disguise shading issues.
7. Budget: use the existing reusable meshes/materials, cap repeated detail,
   preserve collision/render parity and compare the real runtime counts.

## First implementation wave: three Terra / medium workers

| Worker | Owns | Concrete output |
| --- | --- | --- |
| A: hotel architecture | `src/city/miandi_ocean_drive.h`, `tests/miandi_ocean_drive_tests.cpp` | Bellmar/Maravelle room rhythms, sills, window reveals, supported balconies, sidewall detail, distinct Deco profiles; preserve signs and real doors |
| B: resort forecourt | new `src/city/miandi_resort_frontage.h`, new `tests/miandi_resort_frontage_tests.cpp` | tiled terrace panels, cafe tables/chairs, striped shade, planter edges, benches and deterministic palm anchors within the existing hotel parcel |
| C: ordinary city fabric | `src/city/miandi_context.h`, `tests/miandi_context_tests.cpp` | detail only existing low-rise blocks 0/1/2 with windows, storefront bays, shades, roof/service features; no new parcels or changes to replacement registry |

Main agent owns material/mesh integration, shared build files, access integration,
this document, regression checks, and real day/night captures. Workers have
disjoint write sets and preserve all existing dirty checkout changes. They do
not edit global lighting, terrain, roads, coastline or shared renderer files.

### Shared geometry contract (updated for street-front correction)

- Hotel local origin `(8000,8300)`, ground `8 m`, lot `166 x 150 m`,
  local lot center `(3,0)`: west edge remains `-80`, east edge is `86`.
- Coral shell: x `28..78`, z `-54..-4`; entrance approach centered z `-29`.
- Heron shell: x `36..78`, z `4..52`; entrance approach centered z `28`.
- Public sidewalk seam local x `86`; west service lane local x `-90..-54`.
- B exports `bake_miandi_resort_frontage()` returning `BuildingPiece`s in this
  same site frame. No duplicate broad slab under existing lobby walks/cafe.
- B exports `kMiandiResortPalms`: records with `centre` (Vec2), `height_m` and
  `yaw_deg`. Palm meshes use the existing renderer's real palm prototypes;
  foliage is not a stack of boxes. Main adds exact trunk collision separately.
- Architecture, furniture and palms share an 8 m-deep terrace. Keep both lobby
  corridors and the continuous outer walking strip clear; overhangs and neon
  must stop before the public sidewalk at x `86`.
- Existing paths/thresholds/door gaps remain testable. Decorative overhangs and
  small details are visual-only; large ground obstacles have matching collision.

## Next waves after this vertical slice

1. Expand the same vocabulary to Palmera and Mariposa, then clubs: MiMo eaves
   and breezeblock courts, distinctive club entrances and layered service alleys.
2. Fix coastline/promenade elevation and road/terrain contact before extending
   beachfront lots. Existing promenade support mismatch and reported road
   flicker must not be hidden by adding more props.
3. Add original material atlases, subtle weathering and contextual signage where
   geometric detail alone does not hold up close.
4. Tune local activity and atmosphere after geometry: pedestrians, parked cars,
   occupied windows, warm sunset and modest neon bloom, with a measured budget.

## Acceptance / evidence

- Deterministic bakes, bounded pieces and no context/hero duplication.
- Clear lobby/service routes with actual character-collision checks and a
  blocked-route negative control for the new forecourt.
- Daylight hotel frontage, oblique view showing sides, night sign readability,
  and a context street approach inspected in the rebuilt game.
- Focused suites and isolated 300-frame runtime; report streaming spikes and
  any untested live traversal separately from visual/build success.

## Implemented first wave

Three workers used `gpt-5.6-terra`, reasoning `medium`, on disjoint files.
Main reviewed their output and requested corrections for buried/detached side
windows, the initially blank east context frontage, and overly blocky furniture.

- Both hotels have layered multi-storey front/end-wall room bays, correctly
  oriented open balcony rails and floors, Deco trim and supported lobby floors.
  Original neon names and hotel blades remain. The sign backings are smaller.
- Three low-rise blocks have 150 detail pieces each; total context bake is 519.
  These context buildings are still closed shells, not new enterable interiors.
- Five terrace panels, three planter/bench groups, four cafe tables with
  chairs, striped shade and six palm anchors fill out the existing hotel lot.
  World combines the separate architecture/frontage bakes before rendering and
  collision. Access roads and map building footprints did not move.
- Main reused existing stucco, concrete and metal textures with physical-scale
  UVs and a local pastel palette. Ordinary surfaces remain non-emissive. Palms
  use existing Florangia meshes and matching tapered trunk collision.
- Actual character travel exposed the old Maravelle door offset: its opening
  was centered at local z=30 while its walkway was z=28. The opening now aligns
  with the approach. Both hotels have tested entry/exit and sidewalk routes.

Validation: rebuilt `apricot`; all six focused suites pass:
`miandi_ocean_drive_tests`, `miandi_context_tests`,
`miandi_resort_frontage_tests`, `miandi_presentation_tests`,
`miandi_integration_tests`, and `miandi_night_lighting_tests`.
The new forecourt test uses `step_character`, actual baked oriented collision,
the runtime's ground-piece predicate, both lobby round trips, the service lane,
and a blocking-gate negative control. It does not claim upstairs access.

Runtime captures and logs are under `build/qa/miandi-vc/`, using isolated saves,
300 frames, `--clear`, a separately parked player car and `--daylight`/`--night`.
Inspected final captures: `heron-day.png`, `coral-night.png`, `context-day.png`.
All three exited successfully with clean GL queues. They reported respectively
299/300/300 draw calls, 104/102/104 mean FPS, and 140/143/145 streaming spikes
over 4 ms out of 300 frames. These are bounded startup observations, not a
performance pass or proof of driving, crowds or full-city stability. The live
screenshots were static visual checks; route traversal was proven headlessly
with the real character controller rather than by interactive input.
The additional `hotel-corner-day.png` confirms the corrected side-window
placement and roof/rail depth: clean GL, 297 draws, 87 mean FPS, 83.21 ms worst,
146 streaming spikes. That slower sample reinforces that performance remains
open, despite the focused geometry tests passing.
The design remains an initial slice: the 200 m grid is still coarse, the current
palm prototypes are visibly low-poly, and activity, weathering, detailed material
atlases and coastline correction are future work. The OD-1 setbacks were
subsequently corrected below; Palmera and the other parcels have not moved.

## Street-front correction

The user found the beachfront hotels too far from Ocean Drive. Bellmar's
door was 89 m from the public sidewalk and Maravelle's was 42 m away. Move the
entire Bellmar shell/details 81 m east and Maravelle 34 m east, giving both
an 8 m setback. Their facade planes now align at world x `8078`; the public
sidewalk remains x `8086` and Ocean Drive's center remains x `8100`.

- Rebuild the front lawns as five narrow terrace panels, edge-to-edge with the
  two shortened lobby walks. No overlapping road slabs or terrain changes.
- Move the corner cafe to local z `-73..-57`, clear of the moved Coral shell.
  Its four tables, chairs, canopy/posts and stripes move together. Furniture
  stays off the continuous outer walk; all six real palm anchors are relocated.
- Move the four hotel wall washes to world x `8084`, still facing the walls.
  The existing roof-derived map footprints and baked collisions follow the move.
- Keep Seabreeze's west service connection, coastline and other venues intact.

Validation: rebuilt `build/bin/apricot`; the same six focused suites above pass.
New assertions pin the actual shell planes, map roof extents, 8 m walks, cafe
clearance, sidewalk seams, full transformed overhang bounds, and continuous
frontage pavement. Real `step_character` tests cover both lobby round trips,
136 m of frontage in both directions, the cafe aisle and service lane, plus a
blocked-gate control. These tests include conservative palm-trunk envelopes;
the renderer retains its narrower exact tapered/bent trunk collision.

Inspected day/night captures: `build/qa/miandi-street-front/street-day.png` and
`street-night.png`, with matching logs. Both isolated 300-frame runs exited 0
with clean GL queues, 302 draw calls and 2212 visible nodes. Day/night mean FPS
was 103/101; streaming spikes over 4 ms were 141/143. This proves the new visible
placement, not a performance fix or interactive full-city traversal. The user's
already-running game was left alone and needs a restart to load the new layout.
