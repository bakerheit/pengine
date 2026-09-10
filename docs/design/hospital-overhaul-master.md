# Pinatty Regional Hospital overhaul master contract

This contract coordinates the hospital overhaul. Coordinates are local to
`kHospitalSite`: `+x` is grid east, `+z` is grid south, world `+Z` is south,
and the whole campus follows the existing six-degree downtown grid yaw.

## Fixed site and urban edges

- Clinical campus envelope: `x[-27,211]`, `z[-19,143]`, four occupied floors.
- North visitor lot: keep the separate two-block lot at `x[-27,119]`,
  `z[-81,-43]`, across Tenth Street.
- South garage: keep the three-level garage at `x[-27,119]`, `z[167,205]`.
- Keep Tenth Street, Bellweather Road, Juniper Avenue, and Sixth Street as the
  connected public perimeter. Internal streets remain removed.
- Reuse the existing garage, north-lot, furniture, planting, lighting, and
  fitted texture props. New texture art is outside this pass.

## Professional planning concept

Replace the nine identical boxes with one legible four-story clinical complex:

- North public/diagnostic bar: `x[-15,204]`, `z[-8,34]`.
- West inpatient bar: `x[-15,45]`, `z[34,138]`.
- East surgery/emergency bar: `x[144,204]`, `z[34,138]`.
- South support bar: `x[45,144]`, `z[100,138]`.
- Central clinical spine: `x[45,144]`, `z[60,78]`.
- North healing court: `x[45,144]`, `z[34,60]`.
- South healing court: `x[45,144]`, `z[78,100]`.

The bars form an articulated perimeter with two daylight courts and a clear
east-west clinical spine. They must not be rendered as one giant solid box.
Each volume gets real facade depth, floor bands, window rhythm, roof screens,
and a clear departmental identity while sharing one material family.

## Separated circulation

- Public: main lobby and covered drop-off face northwest. Separate entry and
  exit curb cuts meet Tenth away from its Bellweather and Juniper junctions;
  the north-lot crossing lands directly on the lobby walk.
- Emergency: ED occupies the northeast/east bar. A one-way ambulance loop
  uses Juniper-side entry and exit throats, four covered bays, and direct
  trauma doors. No visitor car route crosses the ambulance apron.
- Service: loading, oxygen, waste, plant access, and staff receiving use the
  southeast service court between the hospital, garage, Juniper, and Sixth.
  Service traffic never crosses the public forecourt or ambulance bays.
- Parking: visitors use the north lot and the existing south garage. Keep the
  garage skybridge and ground walk connected to real south-bar openings.
- Walking: preserve continuous perimeter sidewalks, a protected north-lot
  crossing, step-free lobby routes, two courtyard loops, and a separate
  garage-to-hospital path.

## Work layers and ownership

- `hospital_overhaul_massing.h`: complete replacement clinical shell and
  structural landmarks only.
- `hospital_overhaul_logistics.h`: ED ambulance loop, service court, loading,
  utilities, and operational safety props.
- `hospital_overhaul_mobility.h`: public arrival loop, walking links, north
  crossing, transit/curb treatment, and parking wayfinding.
- `hospital_overhaul_public_realm.h`: facade hierarchy, courtyards, planting,
  furniture, signs, roofline polish, and light lenses.
- Parent integration owns `hospital_campus.h`, `hospital_exterior.h`, shared
  world/map/light routing, road graph edits, and tests.

## Acceptance rules

- Render geometry and solid collision come from the same integrated part
  stream. Doors, vehicle throats, service lanes, and court walks stay clear.
- The result remains a four-story hospital. Roof plant and helipad structures
  may rise above the occupied roof but do not create a fifth floor.
- Main entry, ED, ambulance receiving, parking, and service access must be
  readable from street level without relying on map labels.
- Existing fitted generated textures stay mapped only to their named receiver
  faces. Do not tile them across unrelated geometry.
- Named light lenses must still create real runtime lights after dusk.
- Focused geometry/road/map tests, a bounded runtime smoke, and day/night
  screenshots are required before calling the overhaul complete.
