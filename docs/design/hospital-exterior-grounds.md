# Vellum Regional Hospital exterior grounds

## Scope

`src/city/hospital_exterior_grounds.h` is a header-only landscape sidecar for
the four open-air courts and the pedestrian ground around Vellum Regional
Hospital. It exposes exactly:

```cpp
inline std::vector<StartPart> bake_hospital_exterior_grounds();
```

All positions are local to `kHospitalSite`. The module does not alter roads,
the hospital shell, the garage, world integration, shared material routing, or
tests. The parent should append the returned pieces after
`bake_hospital_campus()` so both use the same site transform.

## Layout and circulation

The four courts remain the approximately 38 x 25 m open intersections between
the linked hospital pavilions:

| Court | Site-local centre | Character |
| --- | ---: | --- |
| Public arrival court | `(46, 31)` | shade trees, benches, bike-adjacent social space |
| Quiet healing court | `(138, 31)` | botanical mosaic, shallow water rill, paired seating |
| Rehab court | `(46, 93)` | clear crossing spine with planted sensory edges |
| Staff court | `(138, 93)` | shaded seating and durable planting |

Every court has a continuous 4 m cross-shaped walk. Three low planter islands
occupy quadrants outside that cross; the fourth quadrant stays open for
seating. Trees and path lights sit inside the planted setbacks, never in the
walk. The cross routes remain clear to the open spaces between adjacent wings
and connectors.

Two 3.2 m perimeter walks follow the west and east pavilion walls without
reaching Bellweather Road or Juniper Avenue. The north frontage is deliberately
left alone because the live main-entry and emergency canopies already own that
narrow area. This avoids squeezing landscape props into ambulance movement or
adding paving on top of existing entry slabs.

The former Seventh Street corridor is not rebuilt as a road. Two east-west
walk slabs stop at the skybridge axis, while one 3.2 m north-south route stays
clear under the elevated bridge. Two shallow rain gardens and three inverted-U
bike stands furnish the leftover ground without entering the garage envelope
or its Bellweather/Sixth vehicle routes.

## Authored detail

The sidecar contains 178 purposeful pieces:

- 19 walk surfaces across the courts, perimeter, and skybridge connection;
- 12 concrete planter bodies with soil and two shrub clusters each;
- eight low-poly trees, each built from one solid trunk and two crossed
  non-solid crown masses;
- eight three-piece benches and four two-piece bins;
- ten path lights with separate base, shaft, and lens geometry;
- three three-piece bike stands;
- two four-piece rain gardens;
- one four-piece healing focal assembly: backing, fitted mosaic face, basin,
  and water plane;
- four flush slot drains.

The repeated pieces establish a consistent civic kit, while tree scale/crown
angle, planter rotation, and court programming break up copy-paste rhythm.
Foliage stays coarse and silhouette-led to match Apricot's low-poly/PSX visual
language.

## Collision and support contract

| Element | `solid` | Reason |
| --- | --- | --- |
| Low planter/rain-garden body | yes | collider matches the visible curb volume |
| Tree trunk | yes | narrow collider matches the visible trunk |
| Canopy, shrubs, rushes | no | soft foliage must not become invisible walls |
| Bench plinth, seat, back | yes | visible furniture should stop characters and cars |
| Bin body | yes | visible waist-high obstacle; lid is visual only |
| Light base and shaft | yes | collider matches visible hardware; lens is visual only |
| Bike posts and rail | yes | each narrow metal member owns only its visible volume |
| Healing mosaic backing and rill basin | yes | substantial visible masonry/metal volumes |
| Mosaic face, water, drains | no | receiver/finish planes must not snag movement |
| Paving | no | thin finish layers inherit support from the existing campus/terrain plate |

No solid part overlaps a court's 4 m cross route or the 3.2 m skybridge ground
walk. All south-green solids stop before the garage north face at `z=167.5`.
No piece reaches a perimeter carriageway, restores a removed road, or creates a
new vehicle through-route.

## Material and lighting integration

Parent integration should give `hospital grounds tree canopy`,
`hospital grounds shrub cluster`, and `hospital grounds rain garden rushes`
muted, non-emissive foliage tints rather than treating teal as painted metal.
Soil uses the existing dark finish and paving uses the existing concrete.

Map
`assets/textures/world/hospital/grounds/healing-garden-botanical-mosaic-face-generated.png`
exactly once to the named `hospital grounds healing mosaic face`. The receiver
is a 3.60 x 2.40 m vertical plane on the north-facing side of the healing-court
backing. Set UV scale to `(1, 1)`; do not tile, crop, reuse, or multiply its
authored color by another tint.

Every visible emitter is named exactly `hospital grounds path light lens`.
Those lens boxes are non-solid presentation geometry. The parent must add real
runtime lights at the ten lens positions; emissive tint by itself cannot light
the paths or a pedestrian. The generated mosaic is neutral albedo and must not
be made emissive.

## Parent integration and QA

1. Include `city/hospital_exterior_grounds.h` and append the returned vector to
   the existing hospital `StartSite` draw/collision path.
2. Load and route the generated mosaic only for
   `hospital grounds healing mosaic face`, with one-shot UVs.
3. Route foliage names to low-saturation greens and the exact path-light lens
   name to emitter geometry plus ten real night lights.
4. Keep terrain/scatter exclusion over the four courts and the reclaimed
   Seventh corridor so procedural props cannot appear in a walk.
5. Verify the four cross routes and the skybridge ground walk on foot. Check
   the garage approach separately with the player vehicle.
6. Inspect daylight close views for planter/tree silhouette and the mosaic's
   one-shot mapping. Inspect a clean night run on foot, with the car parked far
   enough away that headlights do not fake path-light coverage.

Local sidecar checks compile the header in C++17, enforce the 100-180 piece
budget, assert one mosaic receiver, assert ten exact lens names, and check that
no solid part enters the four court crosses or the skybridge ground walk.
