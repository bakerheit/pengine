# Vellum Regional Hospital Campus

Vellum Regional is a four-story ring-and-spine hospital west of the east-side
construction district. It still occupies the approved three-by-three clinical
superblock, south garage row, and separate north visitor lot, but the old nine
identical boxes are gone.

## Clinical plan

The replacement shell has five connected departmental volumes:

- a long north public/diagnostic bar facing Tenth Street;
- a west inpatient bar facing Bellweather Road;
- an east surgery/emergency bar facing Juniper Avenue;
- a south support bar facing the garage and service court;
- an east-west clinical spine joining the centre of the plan.

The spine separates two genuinely open daylight courts. The north court is a
quiet healing garden with a meandering route, horticultural table, water rill,
and fitted mosaic. The south court is a rehabilitation garden with a different
zig-zag route, parallel bars, cadence pads, seating, and low planting. Four
supported clinical floors, real wall openings, departmental window rhythms,
sunshades, floor bands, roof screens, a clerestory, and an east-bar helipad make
the complex read as one architected hospital rather than a copied block grid.

## Access hierarchy

- Public vehicles enter a one-way northwest arrival loop through a western
  Tenth Street curb cut, follow an 8.5 m-radius swept turn into the forecourt,
  and leave through a separate eastern cut. Both mouths are clear of the
  adjacent junctions. A single porte cochere covers four passenger positions.
- The north lot's protected pedestrian spine crosses Tenth and the arrival lane
  on one clear axis into the main lobby vestibule.
- The west frontage has a step-free perimeter walk, a transit shelter, bicycle
  parking, short-stay/taxi space, and a secondary public entrance.
- Ambulances use two Juniper-side throats and a separate southbound bypass.
  Four covered bays face direct trauma doors; visitors never drive across this
  apron.
- Service traffic enters from Sixth Street into a separate southeast yard with
  three loading docks, a clear box-truck sweep, staff receiving, oxygen,
  generator/plant, and screened waste handling.
- The retained three-level south garage connects through a protected ground
  walk and its existing enclosed skybridge. The separate 146 x 38 m north lot
  still provides roughly 100 visitor spaces.

Tenth Street, Bellweather Road, Juniper Avenue, and Sixth Street stay connected
as the public perimeter. Rook Lane, Vellum Row, Seventh, Eighth, and Ninth
remain clipped through the clinical site, so there is no fake city through-road
inside the hospital.

## Runtime integration

`src/city/hospital_exterior.h` combines these current layers:

- `hospital_overhaul_massing.h`
- `hospital_overhaul_logistics.h`
- `hospital_overhaul_mobility.h`
- `hospital_overhaul_public_realm.h`
- `hospital_garage_base.h`
- `hospital_exterior_garage.h`

The north lot remains in `hospital_north_parking.h`. The integrated clinical,
site, and garage stream contains 1,798 authored pieces. Rendered solids and
collision use that same stream. The city map draws the five clinical bars as
separate footprints, leaving both courts legible instead of painting one giant
superblock rectangle. Five road-derived access meshes cut the actual perimeter
sidewalk and curb collision at the two public, two ambulance, and one service
throats.

Existing generated hospital art is reused only on its named, fitted receiver:
main-entry mural, northwest healing glass, court mosaic, Bay 2 marker,
shore-power cabinet, garage pay/control panels, and north-lot wayfinding. Plain
materials cover the repeated props. Facade, canopy, path, garage, and parking
lenses still feed real runtime lights after dusk.

## Validation on 2026-09-05

- `hospital_campus_tests`, `city_map_tests`, `city_roads_tests`, and
  `ui_flow_tests` pass.
- The hospital test pins four supported floors in all five volumes, open
  daylight courts, real lobby/trauma/loading portal cuts, one-shot texture
  receivers, unobstructed public/ambulance/service vehicle sweeps, and five
  connected perimeter curb cuts.
- Day overhead, north-arrival, and Juniper emergency screenshots were visually
  inspected. They show the two courts, direct north-lot crossing, readable
  civic entry, separate red emergency canopy, and south support/garage edge.
- A 300-frame night run averaged 116 FPS and finished with a clean GL error
  queue. The emergency canopy lights make distinct pools across the four bays.

This is a game-world hospital plan, not a construction or medical-code set.
