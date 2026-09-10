# Pinatty Regional Hospital overhaul: logistics layer

## Scope

`src/city/hospital_overhaul_logistics.h` owns the east-side emergency receiving
route and the southeast back-of-house yard for the hospital overhaul. All
coordinates are local to `kHospitalSite`; local `+x` is grid east, local `+z`
is grid south, and the parent applies the shared six-degree downtown yaw.

This layer does not change the road graph, hospital shell, map, light routing,
or collision integration. It is designed to be appended to the shared
`StartPart` stream so the same solid records can feed visible geometry and
collision.

## Emergency receiving

The ED occupies the overhaul's east clinical bar at `x[144,204]`, `z[34,138]`.
Emergency receiving sits between that facade and Juniper Avenue:

| Element | Site-local envelope | Purpose |
| --- | --- | --- |
| Bay hardstand | `x[204,220]`, `z[40,118]` | Four west-facing ambulance positions and rear-door work aprons |
| One-way bypass | `x[220.5,227.5]`, `z[44,122]` | Seven-metre clear southbound moving lane |
| Entry throat | centre `(225,42)`, 10 x 7.2 m | Separate Juniper-side entry and north turn apron |
| Exit throat | centre `(225,124)`, 10 x 7.2 m | Separate Juniper-side exit and stop bar |
| Covered bays | centres `z=52,70,88,106` | Four 4.8 x 12.5 m marked positions |
| Trauma approach | centre `(208,79)`, 8 x 3.6 m | Flush route from primary bays to two sliding leaves |

Ambulances enter at the north throat, travel south on the outer bypass, back
west into a bay, and return to the same southbound lane before exiting. Canopy
columns sit at `x=219.7` on bay divider lines, outside the bypass. Paint and
drains are flush. No wheel stop, bollard, equipment cabinet, or canopy post is
placed in the bypass or trauma approach.

The canopy provides 5.24 m clear height beneath its fascia. It covers the full
vehicle depth and stretcher work zone, has visible steel ribs, and uses fourteen
pieces named exactly `hospital arrival canopy light lens` so parent integration
can attach real downward lights.

The trauma vestibule is represented by two non-solid glass leaves and a flush
3.6 m approach. The massing layer must preserve a real opening in the east wall
at `z[76.6,81.4]`; these visual leaves do not cut a solid wall by themselves.

## Emergency props and fitted art

- Four shallow shore-power cabinets sit against the ED facade, outside the
  trauma-door rectangle. Only Bay 2 has the exact fitted receiver
  `hospital arrival emergency shore power cabinet fitted face`; the other
  three faces use ordinary steel.
- The square Bay 2 panel uses the exact receiver
  `hospital emergency bay-two marker face` once. It is a 1.20 x 1.20 m
  east-facing plane over a 1.34 m backing.
- Two stretcher cabinets are protected by crash bollards at the ends of the
  receiving strip. A parked transfer trolley is non-solid set dressing outside
  the primary door route.
- Each bay has a flush drain and the bypass has a continuous trench drain.
  Direction arrows and the exit stop bar are non-solid paint.

No new raster assets are created. The two named fitted faces reuse the existing
hospital textures only on their intended receivers.

## Southeast service yard

The service yard is a 90 x 62 m hardstand at `x[129,219]`, `z[144,206]`. Its
only general vehicle throat is centred at `(184,210)` toward Sixth Street.
It does not connect to the ambulance exit or Juniper-side emergency lane.

The sixteen-metre gap from the ambulance envelope at `z=128` to the service
hardstand at `z=144` contains planting and a solid screen near the east edge.
The garage edge at `x=119` remains separate from the service screen at
`x=126.2`.

| Zone | Placement | Operational intent |
| --- | --- | --- |
| Box-truck sweep | `x[148,199]`, `z[148,203]` | Collider-free central maneuver box, marked only with flush paint |
| Loading docks | `x=158,174,190`, south facade | Three raised platforms, dock seals, bumpers, drains, and wall-supported shelters |
| Staff receiving | `x=135`, south facade | Separate 3 m walk, protected door, canopy, and bollards |
| Oxygen compound | centre `(211.5,163)` | Open steel cage, bulk vessel, manifold, reserves, gate, placard, and impact posts |
| Generator/plant | centre around `(135,180)` | Housekeeping slab, standby set, louvers, exhaust, switchgear, and bollards |
| Waste enclosure | centre `(210,194)` | Washable slab, three opaque walls, open west gate, sorted containers, hose point, and drain |

Truck traffic enters from Sixth, reaches the clear centre, backs north to one
of three loading docks, and leaves through the same controlled throat. Raised
dock geometry ends north of the truck-sweep box. Staff use a separate walk west
of the loading positions. The oxygen and generator zones occupy opposite yard
edges, and the waste compound is isolated at the southeast corner.

The oxygen cage is open above and has an outward-presented gate. Its authored
placement is more than five metres from the loading doors and well separated
from waste, generator exhaust, and the ambulance route. This is a fictional
game layout, not a substitute for local medical-gas or fire-code review.

The waste gate faces west into the service court. Container doors therefore do
not face the staff receiving walk, and collection traffic does not reverse
across the receiving threshold. Screening walls on the west and east edges hide
plant and waste from the garage and street while leaving the Sixth Street throat
open.

## Parent integration checklist

- Append `bake_hospital_overhaul_logistics()` once and remove the superseded
  emergency pieces from the old campus/arrival streams.
- Cut real east-wall trauma openings at `x=204`, `z[76.6,81.4]` in the massing
  layer. Do the same for the three dock doors and staff receiving door along
  the south facade.
- Cut two Juniper curb/sidewalk throats at local `z=42` and `z=124`, and one
  Sixth Street service throat at local `x=184`. Preserve road IDs and validate
  actual vehicle trajectories through the final road ribbons.
- Keep the ambulance bypass and service truck-sweep constants free of solid
  parts when combining all overhaul layers.
- Route every `hospital arrival canopy light lens` to a real runtime light.
  Keep the two fitted texture names mapped exactly once each.
- Register every solid piece from the combined stream with the same transform
  used for rendering, including the hospital site's yaw.
- Verify ambulance approach, bay maneuvering, stretcher travel, box-truck dock
  approach, and service exit in the running game. A compile or bounded smoke
  alone does not prove those routes usable.

## Local validation

- Standalone C++17 syntax check:
  `c++ -std=c++17 -Isrc -x c++ -fsyntax-only -include city/hospital_overhaul_logistics.h /dev/null`
- Header syntax passed on 2026-09-05.
- A compiled geometry check baked 236 parts and passed: four bay boundaries
  and stop bars, a 16 m emergency/service gap, one Bay 2 fitted face, one
  shore-power fitted face, 22 runtime light receivers, zero low solid parts in
  the ambulance bypass, and zero low solid parts in the box-truck sweep.
- Full integration, collision registration, road-ribbon cuts, runtime driving,
  and day/night visual checks remain parent-owned because this fork may edit
  only the logistics header and this note.
