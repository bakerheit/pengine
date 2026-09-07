# Vellum Regional Hospital four-story superblock

## Scope and source snapshot

This is an architecture sidecar for the parent hospital rebuild. It does not
change runtime geometry, roads, collision, tests, map data, or
`docs/design/hospital-campus.md`.

The live `src/city/hospital_campus.h` snapshot uses nine 54 x 38 m hospital
blocks on a 92 x 62 m grid. Their centres are `(0, 0)` through `(184, 124)` in
hospital-site coordinates. The two-block garage is centred at `(46, 186)` and
is 146 x 38 m. The hospital site origin is approximately world
`(-38.081, -331.897)` on the Vellum Row 6-degree basis.

The proposed occupied superblock keeps the existing nine block centres and the
outer buildable envelope:

- Site-local shell envelope: `x = -27..211`, `z = -19..143` (238 x 162 m).
- North edge: Tenth Street. South edge: the reclaimed Seventh Street corridor
  across the garage columns, with the surviving road continuing east.
- West edge: Bellweather Road. East edge: Juniper Avenue.
- Removed internal segments: Rook Lane and Vellum Row at local `x = 46` and
  `x = 138`; Ninth and Eighth Streets at local `z = 31` and `z = 93`; and
  Seventh Street across the two hospital-to-garage columns.
- Keep Tenth, Bellweather, and Juniper as full perimeter streets. The surviving
  eastern Seventh segment still serves the regular block.

The four-story rule applies to every occupied clinical pavilion and connector.
The current three-deck garage remains a separate parking structure; it does not
need a fake occupied fourth floor.

## Massing

Keep the nine existing block-sized pavilions as the readable base module. This
avoids replacing the district with one featureless 238 x 162 m slab and gives
the parent a clean migration path from the current loops.

Use one common four-story datum across all nine pavilions:

| Element | Site-local height |
| --- | ---: |
| Ground floor | `0.20..4.40 m` |
| Level 2 | `4.40..7.80 m` |
| Level 3 | `7.80..11.20 m` |
| Level 4 | `11.20..14.60 m` |
| Roof slab | `14.60..14.90 m` |
| Main parapet | top at `15.80 m` |

The ground floor gets the extra height needed for lobbies, imaging, emergency
receiving, and service clearances. Upper levels use the existing 3.4 m city
floor rhythm. There is no inpatient tower. A non-occupied stair overrun,
helipad deck, or screened plant enclosure may rise above the parapet without
reading as a fifth floor.

Connect the pavilions with four-story links in the former rights-of-way:

- Six east-west links bridge each 38 m gap between columns. Each is about
  `38 x 16 m`, centred at local `x = 46` or `138` and at row centre
  `z = 0`, `62`, or `124`.
- Six north-south links bridge each 24 m gap between rows. Each is about
  `18 x 24 m`, centred at column `x = 0`, `92`, or `184` and at local
  `z = 31` or `93`.
- Keep the four former street intersections at `(46, 31)`, `(138, 31)`,
  `(46, 93)`, and `(138, 93)` open to the sky. Their full available void is
  roughly 38 x 24 m; a 4 m planted edge leaves a useful 30 x 16 m court.

This lattice makes every wing weather-connected on every floor while retaining
four real daylight courts. The courts are pedestrian gardens and light wells,
not leftover asphalt, parking, or disguised internal roads. Ground-floor
galleries can open to them; upper links should stay solid enough for bed moves
and staff circulation.

## Wing roles and circulation logic

The program should read from the street even before interiors are built:

- North-west pavilion `(0, 0)`: main public lobby, registration, pharmacy, and
  visitor services. Use a double-height lobby inside the four-story shell.
- North-centre pavilion `(92, 0)`: emergency department and ambulance
  receiving, retaining the current broad red canopy and clear apron.
- North-east pavilion `(184, 0)`: trauma/imaging support below the rooftop
  helipad, with a direct protected route to emergency.
- Middle row: diagnostics, operating/procedure support, central sterile, and
  the main bed/service transfer spine. This is the least public band.
- South-west and south-centre pavilions: outpatient clinics, rehab, staff
  support, and the garage arrival lobby.
- South-east pavilion: loading, kitchens, stores, waste, and plant access on
  Juniper Avenue. Keep service traffic out of the public
  and ambulance forecourts.
- Levels 3 and 4: inpatient floors arranged around the four courts. Put family
  rooms and day spaces at outer corners; put support rooms against connector
  knuckles so blank walls occur inside the campus, not on street facades.

Use two circulation systems that meet but do not collapse into one corridor:

1. A public loop links the main lobby, outpatient lobby, court galleries, and
   garage arrival. It should always offer a visible next destination rather
   than ending at an opaque core.
2. A bed/service spine runs through the middle column and the two central
   north-south links. It connects emergency, imaging, procedures, inpatient
   floors, loading, and plant without crossing the public waiting zones.

Provide at least four lift/stair knuckles near the inner courts, with the two
central knuckles carrying bed-sized lifts. Add protected stairs at the four
outer corners so no long wing depends on a single central escape route. Doors,
floors, thresholds, stairs, and lift lobbies need real openings and support
surfaces in any later enterable implementation.

## Entrances and vehicle edges

- Main entrance: north face of the north-west pavilion on Tenth Street. Use a
  12 m clear glazed opening, an 18 m canopy, and a forecourt deep enough for a
  short drop-off without putting cars on the pedestrian axis.
- Emergency: north face of the north-centre pavilion. Retain the 27 m red
  canopy as the only red-dominant facade element. Separate ambulance movement
  from the main visitor drop-off with curbs, bollards, and a planted median.
- Ambulatory/women-and-children entrance: north-east pavilion. Give it a teal
  canopy and direct access to the east public core without copying the main
  entrance.
- Secondary public entrance: Bellweather Road at the middle-west pavilion for
  transit and walk-up arrivals.
- Staff/garage arrival: south-centre pavilion. Add one enclosed 4.5 m-wide
  bridge across the reclaimed Seventh corridor at local `x = 92`, connecting
  the garage top
  deck near local `y = 7.5 m` to hospital Level 3 at `y = 7.8 m`. The shallow
  transition can sit within the bridge floor. Keep bridge clearance and
  collision explicit.
- Loading and utilities: south-east corner only, entered from Juniper Avenue.
  Screen the dock from the courts and public doors, but do not
  close the truck turn envelope with decorative walls.
- Garage vehicle entry: prefer the south face toward Sixth Street so queues do
  not block the hospital/garage pedestrian crossing in the former Seventh corridor.

## Facade system

The building should read as one hospital, not nine copied boxes. Use the same
floor bands and bay spacing everywhere, then change depth and material by wing.

- Primary bay: 4.5 m. Express a 0.55-0.65 m pier between broad window pairs.
- Floor bands: 0.30-0.40 m deep at `y = 4.40`, `7.80`, `11.20`, and `14.60 m`.
  They must cast real shadows at game distance.
- Base: 0.9 m of robust concrete. Ground-level glass starts above the base
  except at actual door openings.
- Public north/west faces: warm off-white masonry or ceramic panels, recessed
  blue-grey glass, steel bands, and teal entry frames.
- Clinical south/east faces: more opaque concrete, grouped three-bay windows,
  and deeper fins for sun and service screening.
- Court faces: lighter teal spandrels and larger glazing. Keep the four courts
  visually related without applying one repeated texture across them.
- Emergency red is reserved for the ambulance canopy, door line, and small
  wayfinding accents. Do not stripe the whole campus red.
- Mullions, piers, frames, parapets, and recesses should be authored geometry.
  Do not bake fake windows or structural depth into a generic wall swatch.

At the outer corners, recess the top floor by 1.5-2.0 m for a lighter silhouette
while keeping it an occupied fourth story. At connector ends, use vertical
teal fins to make the stitching legible and break the 238 m frontage into
recognizable wings.

## Main-entry landmark panel

Use the generated texture only on one named authored plane:

- Piece name: `hospital main entry mural face`.
- Position: site-local centre `(x = -13.0, z = -19.28)`, on the north/local
  `-Z` face of the north-west pavilion.
- Size: `9.0 m wide x 6.0 m high x 0.06 m deep`.
- Vertical placement: bottom at `y = 4.80 m`, top at `y = 10.80 m`.
- Asset: `assets/textures/world/hospital/facade/vellum-main-entry-mural-face-generated.png`.
- UVs: map the complete RGB image once over `[0,1] x [0,1]`; clamp edges; white
  tint; no repeat, crop, atlas packing, or mirroring.
- Edge treatment: recess the face 0.05-0.08 m inside a separately authored
  0.18 m metal or masonry reveal. Do not crop off the texture's teal border.

The 1536 x 1024 source is exactly 3:2, matching the 9 x 6 m plane. Its large
teal stepped beacon and exact `VELLUM REGIONAL HOSPITAL` copy are readable from
Tenth Street and distinguish the low hospital from the tall commercial skyline.
The small red centre accent ties to emergency without using a protected
red-cross emblem.

## Roof and skyline

- Retain the helipad over the north-east pavilion, raised just above the
  14.9 m roof slab. Keep its deck, edge lights, access stair, and protected
  emergency route; move the current low-wing pad up to the common roof datum.
- Keep a clear helipad approach sector and a plant-free safety zone around the
  deck. Do not let parapets, light poles, or decorative fins intrude into it.
- Concentrate major air-handling plant above the middle-centre and south-east
  pavilions. Use two or three grouped volumes rather than sprinkling small boxes
  across every roof.
- Set plant at least 6 m behind public parapets. Screen it with 3-4 m teal-grey
  louver walls that share the 4.5 m facade rhythm.
- Put exhausts, kitchen vents, and waste/service plant on the south-east roof,
  downwind visually from the main and emergency forecourts.
- Add a non-occupied glazed stair lantern above the main lobby and two slim teal
  fins at the north-west corner. These, the mural, emergency canopy, and
  helipad form the campus identity without restoring a tower.
- Emissive-looking strips are not lighting. Entry canopies, courts, bridge,
  helipad, and loading areas need real runtime light sources in a later pass.

## Authoring and acceptance notes

- Remove internal road ribbons, sidewalks, curbs, lane graph edges, traffic
  fixtures, terrain operations, and collision before adding connector support.
  Hiding only the asphalt will leave invisible traffic and collision through
  the building.
- Keep perimeter road IDs and street geometry stable. Verify the truncated
  Rook Lane, Vellum Row, Ninth Street, and Eighth Street endpoints do not leave
  dead lane nodes inside the site.
- Generate render and collision geometry from the same hospital description.
  Connector floors, court walks, entry thresholds, and the garage bridge all
  need support geometry; access ramps must use their actual triangles.
- Replace the old nine-lot map treatment with the pavilion-and-link footprint,
  while keeping the garage visually separate and the former Seventh corridor
  legible as hospital grounds.
- Update the current hospital tests: the nine-floor inpatient tower assertions
  will be obsolete. Add checks for the four-story datum, 12 links, four open
  courts, real entry gaps, helipad height, road removal, perimeter-road
  clearance, and bridge clearance.
- Runtime QA should approach from Tenth, Bellweather, Juniper, and the surviving
  eastern Seventh stub;
  walk main-entry-to-core and garage-to-lobby routes; drive the emergency and
  loading paths; and inspect the mural, courts, roof plant, helipad, and real
  light coverage by day and night.
