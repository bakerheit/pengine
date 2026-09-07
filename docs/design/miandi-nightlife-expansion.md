# Miandi culture and nightlife expansion

This is the second authored Miandi wave. It replaces four coarse context blocks
with original, usable places informed by Miami and South Florida architecture.
It does not copy a real hotel, club, mural, logo, or exact street plan.

## 1. Research takeaways

### Tropical Art Deco / Ocean Drive

Miami Beach describes its Art Deco fabric as symmetrical, geometric, and
streamlined, with stepped vertical emphasis, bold signs, terrazzo, glass block,
and neon accents. The district works as a continuous street wall rather than a
single isolated landmark. The familiar pastel palette was derived from sun,
sky, sand, ocean, and tropical colors, with one facade color feeding the trim
of the next building.

Miandi translation:

- three-to-six-storey hotel fronts, not giant podium slabs;
- rounded corners, eyebrows, stepped parapets, fins, and narrow window rhythm;
- shell pink, seafoam, aqua, cream, sunrise orange, and restrained black trim;
- readable vertical hotel pylons and horizontal neon bands;
- outdoor tables and entry courts between the facade and sidewalk;
- visible rear service, rooftop vents, and storm-ready parapets.

Sources: [City of Miami Beach Art Deco guide](https://www.miamibeachfl.gov/architecture/art-deco/),
[National Park Service on the South Beach pastel streetscape](https://www.nps.gov/articles/000/color-pallets-saving-buildings-from-demolition.htm).

### Miami Modern / roadside resort culture

MiMo grew with postwar tourism and automobile culture. Its useful visual kit is
not just "mid-century": open motor courts, flat roofs with deep projecting
eaves, breeze-block screens, large glass areas, curved or sharply geometric
forms, tropical motifs, terrazzo-like paving, and oversized neon marquees.

Miandi translation:

- a U-shaped two-storey motor lodge around a real parking court;
- exterior galleries and stairs implied with solid slabs and light rails;
- a breeze-block screen represented by repeated, non-solid geometric units;
- a pool court separated from vehicle circulation;
- a tall roadside pylon that reads from Bayfront and Seabreeze;
- wide colored downlights that put real pools of light on paving at night.

Sources: [City of Miami Beach MiMo guide](https://www.miamibeachfl.gov/architecture/mimo-miami-modern/),
[City of Miami MiMo Historic District](https://www.miami.gov/My-Government/Departments/Planning/Historic-Preservation-Main-Page/MiMo-Historic-District).

### Calle Ocho / living Latin neighborhood

Calle Ocho's identity is social and mixed-use: small businesses, ventanitas,
cigar work, domino games, murals tied to community history, outdoor patios,
live bands, dancing, food, and nightlife share a low-rise street. The important
part is the sequence of public thresholds and gathering spaces, not decorative
stereotypes pasted onto generic boxes.

Miandi translation:

- several narrow storefront identities and actual door gaps;
- one cafecito window, a shaded domino patio, and a small music courtyard;
- a stage shell and dance floor that are visible from the public approach;
- layered awnings, shutters, tile-color bands, rooftop fans, and service clutter;
- original abstract mural reliefs, never portraits or copied cultural symbols;
- service access kept apart from the player-facing Bayfront frontage.

Sources: [Greater Miami Convention & Visitors Bureau Calle Ocho guide](https://www.miamiandbeaches.com/things-to-do/attractions/explore-calle-ocho-in-little-havana),
[GMCVB on Calle Ocho storefront and ventanita culture](https://www.miamiandbeaches.com/things-to-do/shopping/tour-miami-s-famed-shopping-streets).

### Wynwood / adaptive-reuse arts nightlife

Wynwood's present character grew from reused garment warehouses and factories,
with galleries, bars, food, murals, and activity spilling through alleys and
sidewalks. New work is reviewed for compatibility with that arts identity, and
the district has explored slow shared streets to add pedestrian public space.

Miandi translation:

- retain a broad, low warehouse silhouette and sawtooth/monitor roof rhythm;
- cut real public doors into the old shell instead of applying fake door decals;
- use original geometric color panels and dimensional wall relief as mural art;
- make the side alley a deliberate arrival and queue space, not leftover asphalt;
- preserve a fenced loading/service court behind the venue;
- combine a gallery/event room, main club, and food-yard canopy on one block.

Sources: [Wynwood BID neighborhood history](https://wynwoodmiami.com/our-story/),
[City of Miami Wynwood NRD-1 overview](https://www.miami.gov/My-Government/Departments/Planning/Urban-Design-Main-Landing-Page/Neighborhood-Revitalization-Districts/Neighborhood-Revitalization-District-1-NRD-1).

## 2. Fixed build area

All four sites sit on Miandi's existing 200 m grid at `8.0 m` ground elevation.
Each solid stays within a `160 x 150 m` parcel. Thin light tubes, mural relief,
glass, water, rails, foliage, paint, and paving are non-solid. Every visible
shell and collision box comes from the same deterministic bake.

| Package | Context index replaced | World center | Core identity |
| --- | ---: | --- | --- |
| N1 Calle Noche | 3 | `(7000, 8500)` | Latin social club row and music court |
| N2 Club Mirage | 4 | `(7200, 8500)` | adaptive-reuse warehouse club and art yard |
| H1 Mariposa Motor Lodge | 7 | `(7800, 8500)` | MiMo motel, motor court, pool, neon pylon |
| H2 Palmera Hotel | 8 | `(8000, 8500)` | Deco hotel, cabana court, lobby club frontage |

No package may edit `miandi_context.h`, `world.cpp`, `game_ui.cpp`,
`building_access.h`, either CMake file, or this plan. Those shared changes are
owned by final integration.

## 3. Package contracts

### N1: Calle Noche social clubs

- Owns `src/city/miandi_calle_noche.h` and
  `tests/miandi_calle_noche_tests.cpp`.
- Site center `(7000, 8500)`, size `160 x 150 m`, ground `8.0 m`.
- Build three distinct low-rise fronts: Club Candela, Tropico Ballroom, and
  a late-night cafecito counter. Names are original Miandi names.
- Add at least five real door gaps, one service gate, a covered domino patio,
  open music court, stage shell, dance floor, deep awnings, rooftop fans/HVAC,
  and an original geometric mural wall.
- Public frontage and non-solid walk run north to Bayfront Avenue's outer
  arterial sidewalk edge at world `z=8414`. A separate six-metre service route
  exits west to Palm Avenue's outer street sidewalk edge at world `x=6910`.
- Add visible cyan, pink, and amber pieces whose names contain `miandi neon` so
  integration can make them emissive. Neon trim is never solid.
- Target `70..220` baked pieces and maximum occupied height `16 m`.

### N2: Club Mirage warehouse club

- Owns `src/city/miandi_prism_works.h` and
  `tests/miandi_prism_works_tests.cpp`.
- Site center `(7200, 8500)`, size `160 x 150 m`, ground `8.0 m`.
- Build a reused garment warehouse with a recognizable industrial shell,
  roof monitors or sawtooth rhythm, a gallery entrance, a separate club door,
  queue rails, a food-yard canopy, and a fenced rear loading court.
- Add an original geometric mural made from colored, shallow, non-solid panels;
  no lettering copied from Wynwood and no real artist's work.
- Public routes run north to Bayfront's outer sidewalk edge at world `z=8414`.
  The loading route runs south to Coral Way's outer sidewalk edge at world
  `z=8590`; neither may be pinched by solids or presented as a through road.
- Add violet, aqua, and warm-white pieces named `miandi neon`; keep them
  non-solid and backed by real venue lights during integration.
- Target `65..210` baked pieces and maximum occupied height `18 m`.

### H1: Mariposa Motor Lodge

- Owns `src/city/miandi_mariposa_motel.h` and
  `tests/miandi_mariposa_motel_tests.cpp`.
- Site center `(7800, 8500)`, size `160 x 150 m`, ground `8.0 m`.
- Build a two-storey U-shaped MiMo lodge around a marked motor court, with a
  separate fenced pool deck, exterior gallery slabs/rails, breeze-block screen,
  projecting eaves, lobby door gap, room-door rhythm, roof plant, and a tall
  original `MARIPOSA`-free abstract pylon silhouette. Geometry can sell the
  sign before custom lettering exists.
- Public lobby walk reaches Bayfront's outer sidewalk edge at world `z=8414`.
  The seven-metre driveway reaches Seabreeze Avenue's outer sidewalk edge at
  world `x=7890`. Pool and pedestrian space stay out of its sweep.
- Add aqua, coral, and warm-white `miandi neon` pieces, all non-solid.
- Target `90..260` baked pieces and maximum occupied height `14 m` excluding a
  pylon capped below `24 m`.

### H2: Palmera Hotel and cabana club

- Owns `src/city/miandi_sunwave_hotel.h` and
  `tests/miandi_sunwave_hotel_tests.cpp`.
- Site center `(8000, 8500)`, size `160 x 150 m`, ground `8.0 m`.
- Build one four-to-six-storey Deco hotel plus a lower cabana club wing. Include
  a real recessed lobby door, separate club door, stepped center bay, rounded
  corner reading, horizontal eyebrows, balcony rails, narrow windows, terrazzo-
  like entry slab, pool/cabana court, rooftop vents, and storm parapets.
- Both public routes reach Ocean Drive's outer arterial sidewalk edge at world
  `x=8086`. A seven-metre service lane reaches Seabreeze's outer street
  sidewalk edge at world `x=7910`. Keep all three clear and non-solid.
- Use a coordinated pastel body/trim sequence and pink, blue, and warm-white
  `miandi neon` pieces. Do not copy any real Ocean Drive facade or sign.
- Target `100..300` baked pieces and hotel roof below `26 m`.

### L1: real night-light data

- Owns `src/city/miandi_night_lighting.h` and
  `tests/miandi_night_lighting_tests.cpp`.
- Define a small GL-free authored record for fixed Miandi venue spotlights and
  a deterministic array covering all four new parcels.
- Use `16..28` lights total. Each record includes world position, normalized
  world direction, linear RGB, range, power, and outer cone cosine.
- Favor downward or facade-washing wide cones with bounded `8..22 m` range.
  Avoid upward sky beams and avoid any light whose range reaches a road beyond
  the venue frontage.
- Power is multiplied by runtime night level during integration. Zero night
  level must produce strict zero power.

## 3.1 Detail pass: street-level read

The second detailing pass keeps the major massing and road connections intact.
It adds only parcel-contained geometry that earns its place in a close street
view:

- Calle Noche gets colored facade pilasters and fins, blade signs, furnished
  domino seating, mosaic planters, a DJ plinth, and suspended neon strings.
- Club Mirage gets repeated brick piers, glazed monitor ends, gallery/club art
  plinths, food-yard stools and pendants, dock bumpers, wheel guides, mural
  frames, and monitor-edge neon.
- Mariposa gets a porte cochere, room-number fins, gallery neon, pool chaises,
  umbrellas, towel cabinet, and a low pool ribbon; all keep clear of the lobby
  walk and Seabreeze driveway.
- Palmera gets vertical Deco ribs, a lobby canopy and terrazzo star, pool
  chaises and umbrellas, cabana curtains/bar stools, plus pool and cabana neon.

The authored light set grows from 20 to 24 short, bounded cones: one added
detail light for each parcel. These illuminate the new strings, monitor, pool,
and cabana features without reaching the surrounding road grid.

## 4. Shared integration

### Ocean Drive: 1980s neon revision

Bellmar and Maravelle now use a dedicated hot-pink/cyan neon palette,
with original slanted tube lettering spelling the full hotel names and
double-sided vertical HOTEL blades. Layered pale cores sit within the colored
tubes. Paired horizontal bands, tall corner outlines, stepped parapet crowns,
and entrance canopies give the lights architectural shapes to follow.
Ocean Drive's stucco/trim uses local pastel colors, independent of the rest of
Miandi's shared finishes. The header remains the source of visible geometry.

The four existing Ocean Drive lights now sit in front of the walls and aim
back/down at the stucco, alternating cyan and pink. Total Miandi light count
stays at 28. The wall-intersection regression checks that their center rays
hit a facade above ground within their 22 m range. New trim/signs are non-solid;
lobby paths, service access, and coastline are unchanged. Roof vents have been
put back over their roofs and projecting eyebrows aligned with the east faces.

Visual evidence is captured from the actual rebuilt game under
`build/qa/miandi-80s/`, using an isolated save and a car parked away from the
player so its headlights do not light the facade. Focused validation covers
`miandi_ocean_drive_tests`, `miandi_night_lighting_tests`, and
`miandi_integration_tests`. This revision does not resolve the previously
observed offshore promenade terrain mismatch or the older road-flicker report.

### Runtime wiring

After the packages return:

1. Mark context indices `3`, `4`, `7`, and `8` as replaced, while retaining the
   center registry for map/testing history.
2. Bake each package once into world draw and collision from its authored site.
3. Register ground-floor shells as map footprints; omit trim, pools, and signs.
4. Add one service access record per parcel without generating fake frontage.
5. Make every `miandi neon` piece emissive in the ordinary world material path.
6. Convert L1 records to the existing tiled `TrafficSpotLight` path each frame,
   scaling power by visible night level. These are real bounded lights; emissive
   trim alone is not accepted as illumination.
7. Update Miandi part counts/logging and add all focused suites to CMake.

## 5. Acceptance checks

- Four detailed sites replace, rather than overlap, their context massing.
- Solids remain inside parcels and clear all roads, sidewalks, entrances,
  driveways, pools, queue lanes, and service routes.
- Each club/hotel has a distinct silhouette, palette, threshold, and back side.
- Daylight reads as a cultured coastal district without relying on glow.
- At night the tubes visibly emit and the tiled lights illuminate nearby walls,
  paving, pools, and entry courts without lighting half the city.
- A player can drive Bayfront, Coral, Palm, Seabreeze, and Ocean Drive without
  road tearing, colliding with frontage props, or entering fake pavement.
- Focused package, context, integration, building-access, road/ribbon, terrain,
  render-geometry, and 300-frame runtime smoke checks pass.
- Capture one elevated daytime overview and at least two street-level night
  frames, including Ocean Drive and the west nightlife blocks.
