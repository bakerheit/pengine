# Pinatty Regional Hospital: Emergency and Service Plan

## Status and scope

This is the integration brief for the four-story hospital superblock. It does
not change the campus source, road graph, tests, or the parent campus brief.
The parent change owns the continuous four-story shell and removal of the old
internal street segments. This sidecar owns how emergency receiving and back
of-house service should fit that new mass.

The design assumes:

- all clinical hospital mass follows the live `kHospitalFloorCount = 4`,
  `kHospitalFloorHeightM = 3.4`, and `kHospitalHeightM = 13.6` constants; the
  main roof finishes around 14.15 m above the 12 m Pinatty plate, while the
  clerestory/mechanical crown may reach about 16.7 m without becoming a fifth
  occupied story;
- no public or AI-traffic road crosses the hospital interior;
- the two-block garage remains on the south row, west of the service court;
- short ambulance and service aprons are site fixtures, not through streets;
- every coordinate below is in `kHospitalSite` local space and is a target for
  the parent geometry pass, not a claim that the current source already has it.

## Site frame and street edges

`kHospitalSite` uses the Pinatty Row basis: local +X is east, local +Z is south,
world +Z is south, and the whole site is yawed -6 degrees. Keep all emergency
geometry in site-local coordinates and transform it through the shared
`StartSite` basis.

The old four-row block rhythm puts the perimeter road centerlines at:

| Edge | Existing road | Road id | Site-local centerline |
| --- | --- | ---: | ---: |
| North | Tenth Street | 40 | z = -31 m |
| West | Bellweather Road | 22 | x = -46 m |
| East | Juniper Avenue | 25 | x = 230 m |
| South | Sixth Street | 36 | z = 217 m |

The former crossing alignments inside the parcel are Rook Lane at x = 46 m,
Pinatty Row at x = 138 m, Ninth Street at z = 31 m, Eighth Street at z = 93 m,
and Seventh Street at z = 155 m. Remove only their campus-crossing segments;
keep their ids stable outside the superblock. Reclaimed corridors may become
building links, planted courts, pedestrian paths, or emergency hardstand, but
must not remain drivable shortcuts.

## Operating layout

```text
                         TENTH STREET / NORTH
       ambulance in ->  [one-way bypass + four covered bays]  -> out
                         [direct trauma doors] [walk-in ER]
    +---------------------------------------------------------------+
    | ED / RESUS / IMAGING       | PUBLIC INTAKE                    |
    | straight trauma spine      | waiting / security / triage      |
    |----------------------------+----------------------------------|
    | four-story diagnostic, treatment, inpatient and support mass |
    |        courtyards and foot paths; no internal roads           |
    |---------------------------------------------------------------|
    | two-block public garage    | loading court | waste | oxygen   |
    +---------------------------------------------------------------+
                         SIXTH STREET / SOUTH
```

Use four independent perimeter access systems. The north ambulance loop uses
Tenth Street. The walk-in ER uses the Tenth Street sidewalk but no vehicle curb
cut. Public garage traffic stays on the south/west perimeter. Loading, waste,
and medical gas use the southeast service court from Sixth Street. None of
these aprons connects through to another edge.

## Emergency receiving zone

### Geometry targets

| Element | Site-local target | Clear size / purpose |
| --- | --- | --- |
| Ambulance entrance throat | x = 48 m at north edge | 6.0 m one-way curb cut; right turn from eastbound Tenth Street |
| Ambulance exit throat | x = 136 m at north edge | 6.0 m curb cut; merge back eastbound; no public drop-off use |
| One-way bypass lane | x = 54-130 m, z about -20.5 m | 4.2 m clear lane; local +X flow; 7.5 m target curb radii |
| Covered bay strip | x = 61-119 m, z = -16.0 to -9.5 m | four parallel ambulance positions beside the clinical face |
| Canopy | x = 59-121 m, z = -23 to -8 m | 4.6 m minimum clear height; 0.35 m roof depth; no column in door-swing zones |
| Trauma doors | north clinical face near x = 88-102 m | two 2.4 m clear automatic openings with flush thresholds |
| Walk-in ER entry | x = 148-178 m on north face | separate vestibule and 2.4 m protected walk from Tenth Street |

The coordinates deliberately reuse the mouths of the removed north-south
alignments for the two ambulance curb cuts without preserving either road
through the site. Put a raised curb, planters, or fixed bollards immediately
south of the loop ends so traffic cannot continue into the old grid.

### Approach and circulation

1. An eastbound ambulance on Tenth Street turns right at the west throat,
   crosses one flush sidewalk table, and enters the one-way lane.
2. It selects Bay 1-4 without reversing. Four 10.5 x 4.0 m painted vehicle
   footprints sit parallel to the facade; target bay centers are x = 68.5,
   82.5, 96.5, and 110.5 m.
3. A 4.2 m bypass stays clear on the north side of occupied bays. A departing
   ambulance noses into that lane, continues east, curves north, and turns
   right back onto eastbound Tenth Street.
4. The loop ends at the exit. Curbs and bollards prevent it from becoming a
   player shortcut or a replacement for the removed internal roads.

Bay 2 and Bay 3 are primary stretcher bays. Bay 1 handles queueing and routine
transfers. Bay 4 gets a small separate vestibule for contaminated, combative,
or isolation arrivals. Keep all four usable by the largest ambulance mesh plus
open rear doors: design around a 7.5 x 2.5 m vehicle envelope with a 3.0 m clear
rear work apron and 1.2 m clear along the patient side.

The canopy underside should be no lower than 4.6 m above pavement. Put columns
only along the outer north edge and at the two canopy ends. Keep a continuous
2.4 m gurney strip between parked vehicles and the facade. Drain the canopy to
the outer edge; no downpipe, wheel stop, signpost, or kerb may sit in that strip.

### ER intake and trauma route

Ambulance doors and the public walk-in door are separate openings with separate
vestibules. The public path starts east of the ambulance exit and never crosses
the loop. Give the public vestibule a visible security desk, then a triage point,
waiting area, and a controlled door into treatment. Do not force walk-in
patients through a vehicle bay.

Behind Bay 2/3, preserve this direct clinical sequence:

```text
rear ambulance doors -> covered 3 m apron -> 2.4 m receiving doors
                     -> 3.6 m clean receiving corridor
                     -> resus rooms -> CT / trauma lift -> OR and ICU
```

- Keep the path from ambulance threshold to the first resuscitation room at or
  below 18 m and to CT/trauma lift at or below 28 m.
- Use no pinch point under 3.0 m on the stretcher route and no hard 90-degree
  turn before resus. Corners need a 2.4 m inside turning radius.
- Give each receiving door a 3.0 x 3.0 m clear maneuver box on both sides. Door
  leaves, crash bars, props, and waiting NPCs stay outside it.
- Put decontamination and the Bay 4 isolation vestibule off the route, not in
  it. Their drainage and exhaust are visual/service systems; they do not share
  the public waiting air intake.
- Put the trauma lift directly behind imaging. Target a 2.4 m door and a 3.0 x
  4.0 m clear car so a stretcher and staff can turn without clipping.

### Four-story clinical stack

| Level | Emergency relationship |
| --- | --- |
| 1 | Ambulance receiving, walk-in triage, resus, trauma rooms, CT/radiology, decon, security, and clean/soiled holding. This is the only level that opens to the bay apron. |
| 2 | Operating rooms, post-anesthesia care, ICU, blood storage, and sterile support directly on the trauma-lift core. |
| 3 | Inpatient rooms and step-down care. Keep bed-lift landings aligned with the same core. |
| 4 | Inpatient rooms plus staff/on-call space. Mechanical exhaust stays away from the north intake and southeast oxygen compound. |

Repeat the structural/window rhythm on all four levels so the whole mass reads
as four stories. The emergency canopy is a one-story projection, not a short
hospital wing. Keep the helipad decision separate; if retained, its trauma-lift
landing must join this same protected core instead of using an exterior route.

## Loading and service court

Put the back-of-house yard in the otherwise open southeast cell, approximately
x = 142-211 m and z = 160-205 m. It is screened from the public garage and has
one 8.0 m two-way gate from Sixth Street around x = 176 m. The gate terminates
in the yard; it does not connect north into the clinical courts or west into
garage circulation.

Place the dock wall on the north side of the court around z = 164 m. Two loading
bays centered near x = 160 and 178 m handle linen, food, pharmacy, and a medium
box truck. Give each a 3.6 m clear opening, a 4.0 x 4.0 m dock pad, a sealed dock
canopy, and a 15 m unobstructed maneuver apron. A third 3.6 m ground-level door
handles vans and facilities carts. Keep the outer truck turning radius at or
above 12 m and test the actual longest service vehicle, not only a bounds box.

Model the service chain as distinct clean and dirty destinations:

- clean loading enters a receiving room and service lift without crossing the
  ED corridor;
- soiled linen and regulated waste leave through a separate dirty hold;
- food delivery does not share the waste threshold;
- dock bumpers, wheel guides, and a 1.2 m raised staff refuge keep people out
  of the truck crush zone;
- no loose pallet, bin, or parked van may occupy the fire lane or gate throat.

## Waste handling

Use an enclosed waste compound near the southeast corner of the service court,
target x = 194-208 m and z = 184-202 m. Give it a 2.4 m opaque wall, lockable
vehicle gate, washable slab, covered clinical-waste hold, separate general and
cardboard containers, hose cabinet, drain, and two crash bollards. Container
doors face the service court so the collection vehicle never backs across the
staff door.

Keep a 3.0 m clear working strip in front of every container, 1.2 m beside the
staff gate, and at least 3.0 m from doors or outdoor-air intakes. The enclosure
must not share a fence line, gate, or impact path with medical oxygen service.

## Medical oxygen service

Put the medical-gas compound on the east service edge, target x = 198-211 m and
z = 160-176 m, with its own short maintenance access from Juniper Avenue. It is
not part of the loading court and cannot be used for parking. Include a bulk
vessel or cylinder manifold, reserve rack, valve cabinet, fill connection,
protective bollards, open metal fence, locked outward-opening gate, warning
placards, and one downward service light.

For game layout, reserve at least 5.0 m from the oxygen equipment to building
openings, air intakes, waste/combustible storage, idling vehicles, and ignition
props; keep a 3.0 m clear gate apron and 1.0 m inspection path around the
equipment. These are conservative authored-space targets, not a real-world
code certification. If this fictional layout is ever used as an architectural
reference, local medical-gas and fire rules must replace them.

## Exterior prop schedule

| Exact proposed part name | Count | Size / placement | Collision and material intent |
| --- | ---: | --- | --- |
| `hospital emergency bay canopy column` | 6 max | outer north edge and canopy ends | Solid steel; keep outside vehicle and gurney envelopes. |
| `hospital emergency crash bollard` | 12-16 | 0.16 m diameter, 1.05 m high | Solid; 1.2 m pedestrian gaps; do not form a car-trapping invisible wall. |
| `hospital emergency bay number face` | 4 | 0.75 m square, one over each bay | Full-face graphic or simple authored numeral; no repeat. |
| `hospital emergency bay-two marker backing` | 1 | 1.30 x 1.30 x 0.10 m at Bay 2 | Solid or non-solid backing mounted above impact height. |
| `hospital emergency bay-two marker face` | 1 | 1.20 x 1.20 m north-facing quad, lower edge 2.35 m | Use the generated PNG once with white tint; non-solid face. |
| `hospital emergency stretcher cabinet` | 2 | 1.2 x 0.45 x 1.5 m against facade | Solid; outside door maneuver boxes. |
| `hospital emergency shore power cabinet` | 4 | one between bay positions | Solid shallow box; hoses/cables stay flush, never across the route. |
| `hospital emergency wheel stop` | 8 | paired only where they cannot catch a gurney | Solid low curb; omit from rear work apron. |
| `hospital emergency curb stripe` | as needed | red/white edge bands at loop only | Decal/non-solid; not a substitute for curbs or bollards. |
| `hospital service dock bumper` | 4 | two per raised dock | Solid rubber blocks; align with tested truck rear envelope. |
| `hospital service waste enclosure wall` | 3 + gate | southeast court | Solid; preserve the full collection maneuver path. |
| `hospital oxygen cage` | 1 | east service compound | Solid posts/fence; open top and visible equipment silhouette. |

The selected fitted texture is
`assets/textures/world/hospital/emergency/ambulance-bay-2-marker-face-generated.png`.
It belongs only on `hospital emergency bay-two marker face`. Do not route it to
all signs and do not use repeating world-material UVs. The exact generation
record is in `docs/assets/hospital-emergency-props-generated.md`.

## Safety and gameplay clearances

| Check | Minimum authored clearance |
| --- | ---: |
| Ambulance bypass lane | 4.2 m horizontal, 4.6 m vertical |
| Ambulance bay | 10.5 x 4.0 m vehicle footprint |
| Rear stretcher work apron | 3.0 m |
| Continuous gurney route | 3.0 m; 3.6 m preferred corridor |
| Public accessible walk | 2.4 m, with flush crossing table |
| Canopy column to open ambulance door | 1.2 m |
| Curb-cut sight triangle | 6 x 6 m, free of props over 0.75 m |
| Fire/service lane | 6.0 m clear width |
| Box-truck maneuver apron | 15 m depth, 12 m outer turning radius |
| Waste container working face | 3.0 m |
| Oxygen equipment working path | 1.0 m around equipment; 3.0 m gate apron |
| Hydrant / fire connection | 1.0 m clear radius and unobstructed street sightline |
| Exterior egress landing | 3.0 x 3.0 m, no props or parking |

Collision must come from the same authored bay, curb, canopy, door, dock, cage,
and enclosure geometry that is rendered. Aprons and flush sidewalk tables need
support surfaces; do not approximate a sloped access piece with a solid box.

## Night readability

The emergency entrance needs three readable scales:

1. At 100-150 m, a red roofline `EMERGENCY` wordmark and the bright canopy
   underside identify the north frontage.
2. At 40-80 m, red curb bands, four bay numbers, and the one-way opening geometry
   explain the loop before the turn.
3. Under the canopy, the Bay 2 marker, door glazing, and a continuous lit gurney
   strip guide the final approach.

Use actual runtime lights. Emissive-looking red or white material does not light
the pavement, ambulance, or player. Suggested authored sources are six downward
canopy spots with overlapping pools, two wall lights at the trauma doors, one
light at the walk-in vestibule, low shielded fixtures at both curb cuts, two
dock floodlights, and one downward oxygen-service light. Keep the ambulance
lane neutral white, public walk-in slightly warm, and service court cooler and
dimmer. Avoid pulsing red/blue lights on the fixed building; save that signal
for vehicles.

The generated marker PNG is opaque RGB. Its high-contrast letters can support
an emissive material later, but it is not an emission mask and does not replace
the nearby real light source. Keep the center clean and put wear only at the
edge so the copy survives mipmapping.

## Integration and acceptance checks

- Preserve the four perimeter road ids and delete/cut only the old segments
  inside the superblock. Confirm that the removed road ribbons, lane graph,
  sidewalks, curbs, traffic routes, and map lines all disappear together.
- Author explicit pavement/support for the ambulance loop and service court.
  Neither apron should register as a general road or spawn ambient traffic.
- Cut only the two Tenth Street ambulance openings, the Sixth Street service
  gate, and any separately approved garage opening. Keep all other perimeter
  sidewalks and curbs continuous.
- Route the fitted texture by the exact receiver name above and map UV 0-1 once
  over the north-facing square. Use white tint and no texture repeat.
- Give canopy, loading, and oxygen fixtures real runtime light entries. Check
  them on foot at night with the player car and its headlights left elsewhere.
- Drive the longest ambulance through entry, every bay, bypass, and exit with
  no reverse. Park vehicles in Bays 1 and 3 as blockers and repeat the Bay 2/4
  routes.
- Walk a stretcher-sized probe from each bay door to resus, CT, and the trauma
  lift. Add a blocked-door negative control and confirm solid props stop travel.
- Drive the longest service truck through the Sixth Street gate, dock it, and
  leave forward. Confirm it cannot reach the ambulance loop or garage.
- Check the 6 x 6 m curb-cut sight triangles, pedestrian path, all door landing
  boxes, and the oxygen/waste separation in shared world coordinates.
- Run day, rain, and `--night` screenshots from westbound and eastbound Tenth
  Street plus an on-foot canopy view. A clean GL queue alone does not prove the
  signs, turns, or gurney clearances read correctly.

This plan intentionally leaves code, collision wiring, road edits, tests, and
the parent campus document to the integration owner.
