# East Arm Galleria

East Arm Galleria occupies the clear block across Halloway Square's East Arm
(road spine 51) from the Cinder Underpass terminal. It is aligned to the East
Arm segment and set back so its frontage meets the street sidewalk without
occupying either road corridor. It is one shared 148 x 170 metre retail parcel
with a 136 x 106 metre enclosed mall shell. The indoor floor is about 14,200
square metres, roughly three times the former footprint.

## Blueprint

```text
NORTH / SERVICE
┌──────────────────────── loading and plant lane ────────────────────────┐
│  BOOKSTORE ANCHOR     │ main spine │       CINEMA ANCHOR              │
│                       ├──── cross-gallery ─────────────────────────────┤
│  inline shop bays     │ atrium     │ food court and inline shop bays  │
│  COFFEE SHOP          │ main lobby │ RESTAURANT                       │
├──────── exterior door ┴── main door ┴ exterior door ──────────────────┤
│ planted arcade / pedestrian court / marked crossing                   │
│ two parking rows, through aisles, west entry and east entry           │
└──────────────────────────── EAST ARM / ROAD 51 ────────────────────────┘
SOUTH / STREET
```

The public spine is 14 metres wide and 96 metres long. A 112 metre
cross-gallery distributes people to the two rear anchors. The coffee shop and
restaurant each have an exterior door for early and late trade plus a second
door into the mall. Storefront bays use repeated wall and glass modules with
real gaps. The rear service lane is kept entirely separate from customer
parking and the public entrance.

The south side carries two parking rows, through aisles, a planted forecourt,
a marked pedestrian crossing, and two nine-metre driveway cuts. Both accesses
are explicitly constrained to the East Arm; they cannot spill into the Plaza
Ring ramps. Tenant entries are real shell gaps with threshold pieces, not doors
painted onto a continuous wall. All mall and tenant floor records register with
interior streaming and ground collision.

The exterior is organized as an early-2000s open-world landmark rather than a
single flat box: a capped mall entry tower, raised cinema anchor, stepped
bookstore roof, skylit atrium spine, deep columned arcade, storefront glazing,
palms, lamps, benches, and cafe seating. Five separate ImageGen neon
textures give each tenant its own curved tube-letter identity. Every bitmap is
cropped to the physical aspect of its facade face, so the renderer does not
stretch the lettering back into a wide, square-looking font.

The mall, tenant floors, arcade, and pedestrian court sit on a surveyed 13.82 m
commercial terrace. The East Arm may shape only the street-facing parking
transition; no other road grading corridor may enter the parcel. Concrete
retaining skirts close the two side edges and rear edge, and the full earthwork
envelope excludes procedural trees and rocks.

Validation: `east_arm_plaza_tests`, `building_access_tests`,
`authored_city_layout_tests`, a bounded runtime smoke, exterior visual QA from
the East Arm, and an interior visual QA pass down the central concourse.
