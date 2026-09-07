# Miandi streets and buildings master plan

Miandi is Apricot's loud, hot, coastal city at the southeast tip of Florangia.
It should read as South Florida within one drive from Florangia Regional
Airport: flat bright light, palms and water, pastel low-rise hotels, dense glass
towers, colorful street commerce, marinas, service alleys, and sudden changes
from wealth to working waterfront. The city is inspired by Miami without
copying a real building, business, logo, or exact street plan.

This document is the build contract. Current authored truth remains in
`src/city/miandi_layout.h` and `src/city/roads.h` until each package below is
integrated and validated.

## 1. Player-facing promise

The first 90 seconds after the highway arrival should sell five things:

1. The skyline is visible before the player reaches the grid.
2. Biscayne Boulevard opens onto palms, broad crossings, and tower podiums.
3. One westbound turn reaches tight storefront blocks and outdoor street life.
4. One eastbound turn reaches pastel hotels, a promenade, and the ocean edge.
5. Continuing south reaches freight sheds, cranes, marina slips, and a causeway.

Miandi must not feel like scattered boxes on a grass plate. Every first-wave
building gets a street-facing entrance, a paved pedestrian route, a believable
service side, and enough neighboring mass to form a block wall.

## 2. Fixed world frame

- World origin: `(7500, 8400)`.
- Local `+X` is east. Local `+Z` is south.
- First-wave envelope: world `X 6600..8400`, `Z 7700..9100`.
- Finished street and parcel elevation: `8.0 m` unless a later bridge or canal
  package explicitly owns a different profile.
- Street centerlines, sidewalks, lots, entrances, terrain support, collision,
  map footprints, and traffic lanes must all use the same authored coordinates.

The coast bends around the east and south sides. The first wave keeps the
existing dry plate so the city is immediately playable. The canal/causeway
terrain cut is a later package and may not strand first-wave entrances.

## 3. Street plan

The base grid uses 200 m blocks. Existing roads are kept, but four missing
north-south streets and one northern cross street close the network. Exact
shared control points matter: visual crossings without graph junctions are not
acceptable.

```text
                                   NORTH / -Z

 world X       6900      7100      7300      7500      7700      7900      8100
               Palm      Solana     Mango      Biscayne  Royal Palm Seabreeze Ocean
                 |          |          |          |          |          |        |
 Z 8000  Gateway +----------+----------+-----arrival----------+----------+--------+
                 |          |        / |          |          |          |        |
                 |          +-------/--+----------+----------+----------+        |
 Z 8200  Calle   +----------+----------+----------+----------+----------+--------+
                 |  CO-1    |  CO-2    |  BF-1    |  BF-2    |  OD-1    | beach  |
 Z 8400 Bayfront +----------+----------+----------+----------+----------+--------+
                 |  CO-3    |  CO-4    |  BF-3    |  BF-4    |  OD-2    | beach  |
 Z 8600  Coral   +----------+----------+----------+----------+----------+--------+
                 |  PS-1    |  PS-2    |  PS-3    | causeway |  CI-1    | coast  |
 Z 8800 Port Sol +----------+----------+----------+----------+----------+---bend
                 |  PS-4 / working quay           |  CI-2 / marina and islands
 Z 9000          +---------------------------------+-----------------------------
```

### 3.1 Road schedule

| ID | Street | Class | Required centerline and role |
| --- | --- | --- | --- |
| 222 | Biscayne Boulevard | Arterial | Existing highway continuation. Add exact diagonal junction nodes at `(7100,8040)` and `(7300,8120)` before it reaches `(7500,8200)`. Main skyline reveal. |
| 223 | Calle Ocho | Street | `Z 8200`, `X 6900..8100`; add nodes at `X 7300` and `7700`. Slow active commercial spine. |
| 224 | Bayfront Avenue | Arterial | `Z 8400`, `X 6900..8100`; add nodes at `X 7300` and `7700`. Primary east-west chase route. |
| 225 | Coral Way | Street | `Z 8600`, `X 6900..8100`; add nodes at `X 7300` and `7700`. Transition from downtown to port/causeway. |
| 226 | Port Sol Drive | Arterial | Extend `Z 8800` through `X 7700`, `7900`, and the existing south-coast node `(8000,8800)`. Freight route and southern loop. |
| 227 | Palm Avenue | Street | Extend north to `(6900,8000)`. Western edge between shops and mangroves. |
| 228 | Ocean Drive | Arterial | Extend north to `(8100,8000)`. Hotel frontage and ocean-side straight. |
| 229 | Causeway Boulevard | Street | Keep the diagonal `(7500,8600)` to `(7700,8800)` to `(8000,8800)`. This is the shortcut/chokepoint, not a fake bridge yet. |
| 230 | Gateway Drive | Street | New `Z 8000`, from `(7000,8000)` to `(8100,8000)`, with nodes at every grid avenue. It joins Biscayne at the existing arrival node `(7000,8000)`. |
| 231 | Solana Avenue | Street | New `X 7100`, `Z 8000..8800`, including exact Biscayne crossing `(7100,8040)`. |
| 232 | Mango Avenue | Street | New `X 7300`, `Z 8000..8800`, including exact Biscayne crossing `(7300,8120)`. |
| 233 | Royal Palm Avenue | Street | New `X 7700`, `Z 8000..8800`. Downtown local access and tower frontage. |
| 234 | Seabreeze Avenue | Street | New `X 7900`, `Z 8000..8800`. Hotel rear access and downtown/ocean seam. |

IDs `235..239` stay reserved for Miandi alleys, marina loops, and real bridge
spans. They should not be claimed by another district.

### 3.2 Street character

- Arterials keep the engine's 22 m, four-lane section. Streets keep the 14 m,
  two-lane section. Do not widen them again.
- Biscayne and Bayfront get regular palms, paired light poles, broad corner
  plazas, and long views. Keep fixtures outside the road ribbon and corner
  movement envelopes.
- Calle Ocho gets tighter shade, awnings, benches, café edges, and loading
  access from the rear of lots.
- Ocean Drive gets a continuous east-side promenade. Hotels face west/east
  across the road, while the beach side stays mostly open.
- Port Sol gets truck-sized curb cuts, fewer decorative objects, concrete
  barriers, steel lights, and visible service yards.
- Existing generated sidewalks and crosswalks remain the base pedestrian
  network. Authored pavement must meet them without overlap or a height seam.

## 4. Visual language

### 4.1 Palette using current finishes

| Finish | Miandi reading |
| --- | --- |
| `White` | sun-bleached stucco, hotel bands, bright roof caps |
| `WarmWall` | coral, peach, and pale sand stucco |
| `TealDoor` | aqua doors, turquoise trim, promenade accents |
| `Yellow` | butter-yellow walls, marquees, warm night accents |
| `RedTrim` | flamingo-pink/coral trim and small neon-like accents |
| `Glass` | blue-green tower glass, lobby glazing, hotel windows |
| `Concrete` | podiums, seawalls, walks, parking decks |
| `Steel` | balcony rails, canopies, cranes, service hardware |
| `DarkRoof` | shaded recesses, mechanical equipment, asphalt details |

The first wave uses geometry and color, not copied signage. A later texture
package may add original murals and hotel lettering after names are locked.

### 4.2 Architecture rules

- Ocean Drive: three-to-six storey Streamline/Art Deco-inspired shells, stepped
  parapets, horizontal eyebrows, corner fins, balcony rails, terrazzo-like
  entry pads, repeated narrow windows, and visible rooftop mechanical boxes.
- Calle Ocho: one-to-three storey party-wall shops, varied parapets, deep
  awnings, open/recessed doors, barred side windows, small courtyards, and rear
  loading clutter. No generic big-box pads.
- Bayfront: glazed residential/office shafts on masonry or concrete podiums,
  deep entrance canopies, balcony bands, parking/service backs, and varied
  crowns. Towers must not all share one height or silhouette.
- Port Sol: long low sheds, sawtooth or stepped roof rhythm, open bay gaps,
  loading aprons, cold-store equipment, cranes, pilings, and boardwalk edges.
- Airport Gateway: mid-rise hotels, rental lots, warehouses, overhead signs,
  and planted medians. This is a future wave, not part of the first dispatch.
- Mangrove Edge: stilted low buildings, screened porches, boat sheds, canals,
  sea grape/mangrove planting, and narrow roads. This waits for terrain/water.
- Causeway Islands: motels, villas, pools, seawalls, and marina courts. This
  waits for real bridge and shoreline geometry.

Every roof needs signs of heat and storms: parapets, drains, vents/HVAC boxes,
antennae, hurricane shutters, or shade canopies. Blank flat roofs are not done.

## 5. First-wave block schedule

### 5.1 CO: Calle Ocho quarter

The four blocks between Palm, Mango, Calle Ocho, and Coral Way form one dense
low-rise quarter. `CO-1` is the signature block built first.

| Block | Center | Content | Frontage/access |
| --- | --- | --- | --- |
| CO-1 | `(7000,8300)` | Sol Café, Mercado Miandi, cigar workshop, corner music bar, shaded pocket plaza | Public doors face Calle Ocho and Solana. Rear service lane remains clear. |
| CO-2 | `(7200,8300)` | bakery, pharmacy, two apartments over shops, domino tables under a canopy | Doors face Calle Ocho; small plaza opens toward Mango. |
| CO-3 | `(7000,8500)` | nightlife row, mural wall, fenced service court | Doors face Bayfront; deliveries face Palm. |
| CO-4 | `(7200,8500)` | stucco apartments, laundromat, courtyard parking | Doors face Bayfront/Coral; driveway avoids both intersections. |

First build target: one `160 x 150 m` site centered at `(7000,8300)`. Preserve
at least 14 m from street centerline to solid walls, a 4 m clear public walk,
and a 6 m rear service passage. The block needs at least four distinct facades,
six real door gaps, awnings, rooftop clutter, and one obvious corner landmark.

### 5.2 BF: Bayfront Core

Bayfront is the skyline, but podiums and public space matter as much as height.

| Block | Center | Content | Height target |
| --- | --- | --- | --- |
| BF-1 | `(7400,8300)` | Miandi Exchange: office tower, parking/service podium, small forecourt | 85-105 m |
| BF-2 | `(7600,8300)` | Crown Residences: narrow residential shaft, balconies, lantern crown | 105-125 m |
| BF-3 | `(7400,8500)` | Bayfront civic plaza, low cultural hall, palms and fountain | 12-24 m |
| BF-4 | `(7600,8500)` | Sol Financial: glass tower over active corner lobby | 75-95 m |

First build target: BF-2 centered at `(7600,8300)` on a `160 x 150 m` site.
It needs a collision-backed podium, a real recessed entrance gap, lobby glazing,
two tower setbacks, horizontal balcony/spandrel bands, a distinct crown, roof
plant, and a clear service/back side. The skyline must read from the diagonal
Biscayne arrival without blocking the street.

### 5.3 OD: Ocean Drive strip and promenade

Buildings occupy the west side of Ocean Drive, between Seabreeze and Ocean.
The land east of Ocean Drive is public promenade, palms, low pavilions, and
open coast. Do not put a second wall of buildings on the beach side.

| Block | Center | Content | Height target |
| --- | --- | --- | --- |
| OD-1 | `(8000,8300)` | The Bellmar, smaller The Maravelle, corner café court | 16-25 m |
| OD-2 | `(8000,8500)` | The Palmera, motor court, pool court, nightlife frontage | 12-22 m |
| Promenade north | `(8222,8300)` | broad walk, palms, benches, shade pavilion, low seawall | under 5 m |
| Promenade south | `(8222,8500)` | broad walk, palms, exercise court, beach access gaps | under 5 m |

First build target: OD-1 centered at `(8000,8300)` on a `160 x 150 m` site,
plus a separate `216 x 150 m` non-building promenade strip east of Ocean Drive.
Its west edge is the exact east sidewalk edge at `X 8114`, never the road
centerline. The two hotel
facades need different silhouettes and palettes. Include real doors, a lobby
walk, stepped parapets, eyebrows, fins, balcony rails, rooftop equipment, and a
clear rear/service edge on Seabreeze.

### 5.4 PS: Port Sol working waterfront

Port Sol begins south of Coral Way and gets rougher toward the coast. It must
feel operational, not like decorative dock props dropped beside offices.

| Block | Center | Content | Vehicle logic |
| --- | --- | --- | --- |
| PS-1 | `(7000,8700)` | cold store, reefer yard, gate hut | truck entry from Coral; exit to Palm |
| PS-2 | `(7200,8700)` | produce warehouse, loading bays, forklift lanes | wide curb cut on Coral |
| PS-3 | `(7400,8700)` | fish market hall, seafood sheds, worker parking | public side on Coral; service side south |
| PS-4 | `(7100,8920)` | marina office, boardwalk, slips, cranes, net yard | loop from Port Sol Drive; water edge later |

First build target: PS-3 centered at `(7400,8700)` on a `160 x 150 m` site.
Build a public fish market frontage and a separate truck/service apron with at
least three loading bays, open bay gaps, roof vents, cold-store equipment,
barriers, and one crane/gantry silhouette. Keep a 4.5 m pedestrian route from
Coral Way to the market door and a 9 m truck route to the rear apron.

## 6. Future-wave districts

These are planned now so first-wave work does not block them.

- Airport Gateway occupies `X 7000..7900`, `Z 7750..8200`. Keep the diagonal
  Biscayne approach visually open. Mid-rise mass should frame, not hide, the
  downtown skyline reveal.
- Mangrove Edge occupies the land west of Palm Avenue. Do not pave the whole
  fringe; reserve continuous wetland bands and two future canal corridors.
- Causeway Islands occupy the southeast bend beyond Coral Way. Keep IDs
  `235..239` and the eastern `Z 8800..9050` terrain free for bridge, marina, and
  island access work.

## 7. Public realm and environmental detail

The city-wide street kit should provide reusable, cheap parts:

- paired palm trunk/crown pieces at 24-32 m spacing on major boulevards;
- simple steel light poles, warm lamp boxes, benches, trash cans, and bollards;
- concrete planters and low walls that never enter the sidewalk centerline;
- bus shelters on Biscayne and Bayfront, clear of intersections;
- storm-drain grates and utility boxes at block edges;
- parking stripes, wheel stops, loading markings, and service gates inside
  parcel sites rather than painted across road ribbons;
- original district pylons for Calle Ocho, Ocean Drive, and Port Sol only after
  their names and sightlines pass a runtime check.

Vegetation should cluster, not form a perfect grid. Use palms for skyline
rhythm, denser broad-leaf planting in courtyards, and preserve open views at
corners. No tree, sign, planter, or light may block a driveway or lane sweep.

## 8. Gameplay and circulation checks

- A complete road-graph route must exist from Florangia Highway to every new
  road and back out without reversing.
- Right-side traffic must stay on the rendered ribbon through every new
  intersection. Geometric crossings require shared graph nodes.
- The main loop is Biscayne -> Calle Ocho -> Ocean Drive -> Port Sol Drive ->
  Palm Avenue -> Calle Ocho. Causeway Boulevard is the diagonal shortcut.
- Each first-wave parcel needs one measured sidewalk entrance. Port Sol also
  needs a separate truck path. Hotel and tower service access should come from
  the quieter avenue, not the arterial front door.
- Collision must come from the same baked pieces as the visible shell. Thin
  trim, glass overlays, paint, water, and foliage stay non-solid.
- Door gaps must be large enough for the character controller and reachable
  from the public sidewalk without clipping a wall, planter, or curb.
- Maintain chase readability: no prop forest at junctions, no dead-end service
  yard presented as a through road, and no unmarked drop into future water.

## 9. Implementation packages

The first dispatch is intentionally split into non-overlapping files. Workers
must not edit integration files, CMake files, this plan, or another package.

### Package S1: connected street grid

- Owns: Miandi road records `222..234` in `src/city/roads.h`, plus
  `src/city/miandi_streets.h` and `tests/miandi_streets_tests.cpp`.
- Builds: exact graph nodes in section 3, street constants, and a small reusable
  boulevard fixture kit with collision-safe offsets.
- Does not own: district buildings, world integration, map UI, CMake.

### Package B1: Calle Ocho signature block

- Owns: `src/city/miandi_calle_ocho.h` and
  `tests/miandi_calle_ocho_tests.cpp`.
- Builds: CO-1 as specified in section 5.1.
- Does not own: roads, terrain, world integration, map UI, CMake.

### Package B2: Bayfront signature tower

- Owns: `src/city/miandi_bayfront.h` and
  `tests/miandi_bayfront_tests.cpp`.
- Builds: BF-2 as specified in section 5.2.
- Does not own: roads, terrain, world integration, map UI, CMake.

### Package B3: Ocean Drive signature block

- Owns: `src/city/miandi_ocean_drive.h` and
  `tests/miandi_ocean_drive_tests.cpp`.
- Builds: OD-1 and the north promenade strip as specified in section 5.3.
- Does not own: roads, terrain, world integration, map UI, CMake.

### Package B4: Port Sol fish-market block

- Owns: `src/city/miandi_port_sol.h` and
  `tests/miandi_port_sol_tests.cpp`.
- Builds: PS-3 as specified in section 5.4.
- Does not own: roads, terrain, world integration, map UI, CMake.

### Package M1: first-pass context infill

- Owns: `src/city/miandi_context.h` and
  `tests/miandi_context_tests.cpp`.
- Builds: twelve occupied but intentionally lower-detail parcels around the
  hero blocks: local centers `(-300,-100)`, `(-100,-100)`, `(300,-100)`, all
  six `Z 100` block centers from `X -500..500`, and `(-500,300)`,
  `(-300,300)`, `(300,300)`.
- Character: pastel shop/apartment rows to the west, varied podium-and-shaft
  massing downtown, one low civic/plaza block, and working sheds in Port Sol.
- Constraint: every parcel stays inside a `160 x 150 m` block envelope. It may
  establish skyline and street wall, but it may not fake doors or claim the
  finish level of CO-1/BF-2/OD-1/PS-3.
- Does not own: roads, finished parcels, terrain, world integration, map UI,
  CMake.

### Package I1: integration after workers return

- Main integration owns `src/city/miandi_layout.h`, `src/app/world.cpp`,
  `src/app/game_ui.cpp`, `src/city/building_access.h`, and CMake registration.
- Remove or suppress rough massing only where a finished package replaces it.
- Bake each package once, use the same result for draw/collision, register map
  footprints, and keep each site's max draw distance high enough for skyline
  or approach reading.

## 10. Definition of done

Static and focused checks:

- all road IDs are unique and every intended crossing is a shared graph node;
- all new road ribbons sit on supported terrain and remain inside Florangia;
- parcel solids stay inside their site and clear road/sidewalk envelopes;
- every package has a valid building plan or a deterministic bake with bounded
  part count;
- door, sidewalk, driveway, and truck-route checks pass;
- visible/collision parity is pinned for every solid piece;
- the Miandi map footprint uses finished ground-floor shells, not roof trim.

Runtime checks:

1. Drive Florangia Highway into Miandi in daylight and confirm the skyline
   reveal, road continuity, signs, and no terrain/ribbon tearing.
2. Drive the full city loop in both directions with traffic enabled.
3. Walk from the sidewalk through the Calle storefront, Bayfront lobby, Ocean
   hotel lobby, and Port Sol market entrance.
4. Repeat Ocean Drive, Bayfront, and Calle Ocho at night to judge window,
   marquee, and street-light readability.
5. Capture approach, street-level, and elevated overview frames. A clean build
   or headless GL run alone does not prove the city reads correctly.
6. Run the focused Miandi suites, road/lane/ribbon suites, building access,
   terrain collision, render geometry, and a bounded 300-frame game smoke.

## 11. Rollout order

1. Land S1 and confirm the complete drivable grid.
2. Land B1, B2, B3, and B4 as separate authored sites.
3. Add the twelve M1 context parcels so the hero blocks sit inside a readable
   city rather than an empty grass grid.
4. Integrate the finished sites/context and replace conflicting rough boxes.
5. Validate day, night, driving, walking, map, collision, and terrain support.
6. Only then cut canals, promote Causeway Boulevard to real bridge spans, and
   build Airport Gateway, Mangrove Edge, and Causeway Islands.
