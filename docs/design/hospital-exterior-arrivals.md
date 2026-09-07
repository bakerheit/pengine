# Vellum Regional Hospital Exterior Arrivals

## Scope and integration contract

`src/city/hospital_exterior_arrivals.h` is a header-only detail sidecar for the
north public entrance and the north emergency arrival. It exposes exactly:

```cpp
inline std::vector<StartPart> bake_hospital_exterior_arrivals();
```

The returned 147 pieces use `kHospitalSite` local coordinates and are ready for
the parent to append after `bake_hospital_campus()`. The sidecar does not change
roads, the base hospital shell, world material routing, runtime lights, tests,
or map data.

The design keeps Apricot's low-poly language while adding the layered reads a
close-range open-world landmark needs: projecting fascia, underside structure,
real column silhouettes, framed glazing, protected furniture, small operational
hardware, material changes, and deliberate wear on one project-bound prop.

## Authored totals

| Zone | Pieces | Purpose |
| --- | ---: | --- |
| Main arrival | 67 | Canopy, vestibule, pedestrian table, tactile paving, furniture, bike rack, planters, curb and drainage |
| Emergency arrival | 80 | Full four-bay canopy, trauma-door frame, one-way loop and throats, bay markings, shore power, stretcher storage, impact protection, drainage and wall fixtures |
| Total | 147 | Arrival structure, fitted operational props, circulation paint, and light anchors |

The count comes from useful repeated systems rather than hidden filler. Thin
paint, mullions, lenses, foliage, handles, drains, and tactile tiles are
non-solid. Canopy shells, columns, bollard bodies, bench contact volumes,
planters, curb segments, cabinet bodies, and rack anchor rails are solid where
their visible mass should stop a vehicle or pedestrian.

## Main public arrival

The main pavilion is centred at local `(0, 0)` with its north wall near
`z = -18`. Tenth Street's 14 m carriageway and two 3 m walks fill the ribbon
to the local south edge at `z = -21`. The sidecar layers an 18 x 7.2 m canopy
over the smaller base canopy, but every low solid stays south of that edge.
Two columns at `x = +/-7.7`, `z = -20.30` keep the centre open and give the
cantilevered roof a credible load path. Five underside ribs and five separately
named light lenses break up the large soffit.

The central route is deliberately plain:

- a 4.8 m-wide pedestrian table continues from the Tenth Street walk edge at
  `z = -21` toward the doors;
- the low solid-clear zone is `x = -2.4..2.4`, `z = -21..-17.6`;
- four tactile tiles mark the road-side transition without collision;
- bollards begin at `x = +/-5`, outside the accessible route;
- benches, planters, and the bike rack sit behind the road ribbon on the outer
  thirds of the frontage;
- split curb pieces leave a broad centre opening instead of making a hidden
  vehicle trap.

The existing entrance remains the real opening. Added mullions, two sliding
glass leaves, kick plates, and a transom are visual-only so thin frame boxes
cannot snag the player or refill the doorway.

## Emergency arrival

The emergency pavilion is centred at local `(92, 0)`. Its 64 m red canopy keeps
the existing 5.7 m clearance and gains a roof cap, front and side fascia, six
underside ribs, and six downward lens fixtures. The door face is split into two
trauma leaves inside the existing opening; all thin frame parts remain
non-solid.

Four ambulance positions are centred at `x = 68.5`, `82.5`, `96.5`, and
`110.5`. Their boundary paint, stop bars, and trench drains are non-solid. A
one-way bypass runs along their Tenth Street edge between separate 6 m entrance
and exit throats, and remains free of low solid sidecar props,
and the trauma opening from `x = 88.6` to `95.4` has no low solid blocker. Hard
props sit against the facade and outside that door box:

- four shallow shore-power cabinets;
- two stretcher cabinets at the outer ends;
- four crash bollards protecting only the equipment edges;
- four wall-pack bodies with separate light lenses;
- four transverse drain sections at the bay edge.

No cable or hose crosses the pavement. Those can be added later only as
retracted wall-mounted details.

## Texture receiver

The only generated receiver is:

- exact piece: `hospital arrival emergency shore power cabinet fitted face`;
- centre: `(84.5, -18.37)`;
- bottom: `0.57 m`;
- size: `0.60 m wide x 0.90 m high x 0.03 m deep`;
- asset: `assets/textures/world/hospital/arrivals/emergency-shore-power-cabinet-face-generated.png`;
- mapping: full image once over UV `[0,1] x [0,1]`, white tint, clamped edges;
- forbidden: repeat, crop, mirror, atlas packing, or routing to the three blank
  cabinet faces.

The source is opaque 1024 x 1536 RGB, exactly matching the receiver's 2:3
aspect ratio. It has no text, logo, medical cross, watermark, or environmental
lighting. Full prompt and provenance are recorded in
`docs/assets/hospital-exterior-arrivals-generated.md`.

## Runtime light handoff

Every visible arrival or canopy emitter lens is named exactly
`hospital arrival canopy light lens`. There are 15: five at the main canopy,
six at the emergency canopy, and four on the emergency wall packs. The parent
must route those pieces to real downward runtime lights. The white finish alone
does not light the pavement, player, or vehicles.

## Validation handoff

The integrated hospital test instantiates all 147 pieces, checks positive
dimensions, the one fitted receiver, 15 exact-name light lenses, four bay stop
bars, eight bay boundaries, both loop throats, and six eastbound arrows. The
same vector now feeds rendered geometry, collision-bearing fixtures, fitted
texture routing, and real night lights. Street-level daylight and night runtime
passes confirm that the expanded canopy and receiving apron render cleanly.

Driving every bay with the longest ambulance and testing the trauma doors on
foot remain separate interaction QA; the current pass proves authored exterior
geometry and clear visual circulation, not an ambulance mission system.
