# Vellum Regional Hospital: four-story interior plan

Status: planning/design sidecar for the road-free, four-story hospital revision.
This document does not change the current shell, roads, collision, runtime
materials, tests, or `docs/design/hospital-campus.md`.

## Planning basis

The current `src/city/hospital_campus.h` establishes a 3-column by 3-row
clinical grid. Each block is 54 m wide by 38 m deep. Block centres are 92 m
apart east-west and 62 m apart north-south, leaving 38 m east-west and 24 m
north-south between the wing footprints. Local +X is east, local +Z is south,
and each present entrance faces local north.

The parent shell now confirms these coordination values:

- all nine clinical wings have four occupied stories at 3.40 m floor-to-floor;
- occupied shell height is 13.60 m, with a non-occupied central clerestory and
  mechanical crown above it;
- the road-free clinical superblock is 238 m wide by 162 m deep and uses one
  continuous `hospital campus superblock lot`;
- six 39 m by 12 m `hospital east west connector core` pieces and six 12 m by
  25 m `hospital north south connector core` pieces link the wings through all
  four stories;
- emergency vehicles stay on the outer north frontage at `NC`;
- the separate three-level garage spans 146 m across the west two positions of
  the fourth row and has an enclosed skybridge whose current base is 4.20 m;
- former road gaps remain paved courts around the enclosed links rather than
  drive aisles.

This interior sidecar places service receiving on the outer south/east edge
and treats the current connector cores as circulation envelopes. Final room
partitions should use finished-floor tops at approximately 0.28, 3.68, 7.08,
and 10.48 m above site ground, matching the current 3.40 m shell cadence. The
implementation still needs explicit slabs and thresholds at levels 2-4.

## Block key

Block IDs describe physical position, not floor number.

| North to south | West column | Centre column | East column |
| --- | --- | --- | --- |
| North row | `NW` at (0, 0) | `NC` at (92, 0) | `NE` at (184, 0) |
| Middle row | `MW` at (0, 62) | `MC` at (92, 62) | `ME` at (184, 62) |
| South row | `SW` at (0, 124) | `SC` at (92, 124) | `SE` at (184, 124) |

All coordinates above are hospital-site local metres. The plan deliberately
does not redefine the site transform or lot dimensions.

## Interior kit and clearances

Use one repeatable interior planning kit in every block so walls, openings,
floor support, furnishing collision, and lights can be baked from the same
records.

- Keep finish-floor slabs inside the final exterior wall line. Every occupied
  slab, stair tread, lift threshold, and enclosed connector needs a matching
  walk-support surface.
- Use 3.0 m clear for the main public gallery, 2.7 m for bed/clinical routes,
  2.4 m for normal staff/service corridors, and 1.8 m only for short room-side
  spurs that never carry beds or opposing cart traffic.
- Use 1.40 m clear patient-room doors, 1.80 m paired doors on bed routes, and
  2.10 m clear equipment doors at imaging, surgery, and central sterile.
- Preserve a 1.80 m turning circle at public decision points and accessible
  toilets. Preserve 1.50 m clear at lift doors and both ends of paired doors.
- Keep wall-mounted props at least 2.05 m above finished floor when they project
  into a corridor. Recess extinguishers, glove boxes, and chart holders instead
  of shrinking the route with collision boxes.
- A typical single patient room is 3.60 m wide by 6.40 m deep. Accessible and
  isolation rooms are 4.20 m wide by 6.40 m deep. The bed collision envelope is
  1.05 m by 2.35 m, with 1.20 m clear on the staff side and 1.50 m clear at the
  foot.
- Use two remote enclosed stairs for every connected floor plate. The preferred
  gameplay loop is stair-capable even if lift interaction ships later.
- Keep patient-facing glazing and room doors readable, but do not make glass
  walls the only boundary. Frames, kick plates, and solid wall returns carry
  the silhouette and prevent single-sided glass from disappearing indoors.

### Typical block section

Within a 54 m by 38 m shell, reserve perimeter room bands about 6.4 m deep,
two 2.4-2.7 m corridors, and a central support/utility band. Cores and major
equipment can replace the central band where needed. Connector openings must
stay aligned floor to floor, even when the department on either side changes.
The current exterior bake supplies only the ground slab in each wing; levels
2-4 need named interior slabs before any upper room layout is traversable.

## Campus circulation

Four routes stay legible and do not borrow the removed internal roads.

### Public blue route

The main public entrance is on the north face of `NW`. A 3.0 m clear public
gallery links the lobby, outpatient services, imaging reception, public lifts,
family waiting, and the garage pedestrian arrival. It passes along department
edges and never cuts through ED treatment, operating rooms, ICU bed bays,
sterile processing, loading, or the morgue.

Ground-floor links may be enclosed galleries across the former road gaps.
Upper-floor public links are limited to destinations that need them: surgery
waiting on level 2, ICU family reception on level 3, and inpatient family
lounges on level 4. Doors at each clinical threshold make the public boundary
obvious.

### Clinical green route

A 2.7 m bed route links `NC` emergency care, `NE`/`MC` imaging, the central bed
lifts, level-2 surgery/PACU, level-3 ICU, and level-4 wards. Enclosed links are
at least 4.2 m overall so the clear route survives wall thickness, handrails,
and door leaves. No reception desk, loose chair, vending machine, or decorative
planter enters this route.

### Service grey route

Receiving begins at the outer edge of `SE`. A 2.4 m back-of-house spine runs
west through `SC` and `SW`, then uses service lifts and rear connectors to feed
every floor. Supplies, food carts, linen, equipment, and staff access use this
route without crossing the main lobby or family waiting.

### Soiled return route

Soiled instruments, waste, and deceased-patient transfer use a controlled
rear corridor from clinical rooms to `SC`/`SE`. Level 2 gets a dedicated dirty
return from OR/PACU to decontamination. It must not share a door vestibule,
lift lobby, or cart alcove with clean case carts.

## Level 1: arrival, emergency, diagnostics, and service

| Block | Program | Key layout notes |
| --- | --- | --- |
| `NW` | Main lobby, security, registration, admitting, public toilets, cafe kiosk, outpatient pharmacy | Keep a 4.8 m deep clear vestibule and a direct sightline from entry to reception and public lifts. Put seating in bounded bays, not the center path. Pharmacy queue runs along a wall and has a separate exit lane. |
| `NC` | Emergency public entry, triage, fast track, secure treatment | Public waiting and triage stay on the north edge. A controlled line separates waiting from treatment. Ambulance transfer enters a different north-side opening and proceeds straight to resuscitation without crossing seated visitors. The public gallery bypasses, rather than traverses, ED. |
| `NE` | Imaging reception, X-ray, CT, MRI, ultrasound, control rooms | Public intake sits north; secure ED access enters west. Put CT closest to the ED link. MRI gets a controlled threshold and a generous equipment/service door. Scanner bodies are solid; bores and table travel zones stay open. |
| `MW` | Ambulatory clinics and exam rooms | Two waiting pockets feed short exam-room spurs. A staff work core and clean utility run behind exam rooms. Keep public circulation on the north/west edge and staff circulation on the south/east edge. |
| `MC` | Phlebotomy, specimen receiving, clinical lab, blood bank | Public blood-draw rooms face the public route. Processing and blood storage face the service spine. A pass-through specimen point avoids public entry into the lab. |
| `ME` | Emergency observation and short-stay unit | Sixteen bed bays around two decentralized nurse stations. A 2.7 m bed route remains continuous between ED, imaging, bed lifts, and discharge. Visitor access enters through one controlled desk. |
| `SW` | Infusion, physical rehabilitation, day medicine | Put infusion chairs at the perimeter and the rehab gym in the central open zone. Equipment parks in recessed 1.2 m deep alcoves. The service spine stays behind support rooms. |
| `SC` | Decontamination, sterile receiving, bulk clean/soiled stores | Dirty receiving enters from `SE`; cleaned material moves to a separate clean side and dedicated lift to level 2. Do not connect the public gallery. |
| `SE` | Loading, dietary receiving, linen, waste hold, morgue transfer, facilities control | This is the outer service front. Use separate clean receiving and waste/morgue vestibules. Screen every line of sight from public paths. Leave a straight 2.10 m equipment route to the freight lifts. |

### Level-1 playable public route

1. Enter `NW` through the main vestibule.
2. Reach registration without walking through seating.
3. Follow the blue gallery to ambulatory care in `MW` or imaging reception in
   `NE`.
4. Return to the public lift bank for levels 2-4.
5. Exit by the same lobby or the garage pedestrian connector without entering
   a clinical or service zone.

### Level-1 emergency route

Ambulance entry at `NC` leads to resuscitation, then west/east to treatment or
directly into the secure imaging link. A critical patient can continue to the
bed lifts without touching ED waiting, the main lobby, or public lifts.

## Level 2: surgery and procedures

| Block | Program | Key layout notes |
| --- | --- | --- |
| `NW` | Surgery check-in, family waiting, consult rooms | This is the only routine public destination on level 2. The public lift opens into reception; a controlled door separates all patient preparation areas. |
| `NC` | Pre-op holding and phase-II recovery | Sixteen curtained bays, two enclosed isolation bays, medication room, clean utility, and direct bed-route access. Beds do not queue in the public lift lobby. |
| `NE` | Interventional imaging and cardiac procedure rooms | Two procedure rooms, two control rooms, preparation/recovery bays, equipment store, and a service connection to imaging below. Large equipment doors stay off the public side. |
| `MW` | Operating rooms 1-4 | Four rooms around the west half of the clean core. Each room keeps a 1.50 m clear perimeter around the table and a separate equipment parking recess. |
| `MC` | Operating rooms 5-8 and clean core | Four rooms around the east half of the clean core. Clean case carts enter from `SW`/`SC`; traffic circulates around, not through, scrub and sterile stores. |
| `ME` | PACU | Twenty monitored bays in two pods with direct sightlines from nurse stations. Bed routes connect to OR, ICU lifts, and wards. Family access stops at reception. |
| `SW` | Central sterile clean assembly and case-cart staging | Clean side only on this level. Dedicated vertical links connect to level-1 decontamination. Full carts have marked parking rectangles outside the 2.4 m route. |
| `SC` | Anesthesia pharmacy, blood issue, equipment library, clean lift lobby | Counters are solid and queues remain inside the department. Equipment charging bays are recessed and non-public. |
| `SE` | Staff lockers, on-call rooms, staff lounge, procedure plant | Staff stairs and freight lifts open here. Plant access never requires crossing PACU or clean core. |

### Clean and dirty surgery loop

- Clean supplies rise from `SC`, stage in `SW`, enter the clean core, and move
  into ORs through paired 2.10 m doors.
- Used case carts leave on the rear dirty-return corridor and descend directly
  to level-1 decontamination.
- Patients travel pre-op -> OR -> PACU -> ICU/ward by the clinical green route.
- Families remain at `NW` and never enter pre-op, the clean core, or PACU bays.

## Level 3: intensive and intermediate care

| Block | Program | Key layout notes |
| --- | --- | --- |
| `NW` | ICU family reception, waiting, consult rooms, quiet room | Public lifts open here. One staffed desk controls access to all bed pods. Waiting furniture stays in side bays with a 3.0 m route to consultation rooms. |
| `NC` | Medical ICU, 12 rooms | Two six-room pods with direct nurse-station sightlines and a central clean utility. Two rooms are sized for isolation. |
| `NE` | Surgical/neuro ICU, 12 rooms | Direct bed route to surgery/PACU lifts. Equipment alcoves hold ventilators and portable imaging without narrowing corridors. |
| `MW` | Cardiac ICU, 12 rooms | Rooms ring a central telemetry/nurse station. A controlled staff link reaches interventional imaging below. |
| `MC` | Shared ICU support and bed-lift hub | Medication, clean utility, respiratory store, staff workroom, consult space, and two 2.7 m bed-lift lobbies. No public seating in this block. |
| `ME` | Intermediate care, 16 rooms | Single rooms around two nurse stations. Use the same room module as level 4 so geometry can be reused. |
| `SW` | Therapy, staff education, simulation | Training rooms and a small mobility gym. Keep simulated clinical props out of the through corridor. |
| `SC` | Respiratory therapy, clinical engineering, equipment service | Service lifts and the grey spine serve all ICU pods. Repair benches and parked equipment are solid and live inside marked work zones. |
| `SE` | On-call rooms, staff respite, data/communications, mechanical support | Quiet staff rooms face the perimeter; equipment rooms face the service core. No public connector enters this block. |

ICU room doors are paired 1.80 m openings with a clear bed turn. The bed,
headwall cabinet, visitor chair, and equipment rail define the furnishing zone.
Wall-mounted monitors and booms are visual/non-solid; the bed, mobile cabinet,
and low sink casework are solid.

## Level 4: inpatient wards

| Block | Program | Planning capacity and notes |
| --- | --- | --- |
| `NW` | Medical ward west | 16 single rooms, one decentralized nurse station, clean/soiled utility, and a public family lounge near the lift. |
| `NC` | Medical ward central | 16 single rooms and a shared therapy/dining room. Bed traffic uses the central green route, not the family lounge. |
| `NE` | Isolation ward | 12 larger rooms, including four negative-pressure planning modules with anterooms. Pressure behavior is not implied by geometry and needs a later gameplay/visual decision. |
| `MW` | Surgical ward west | 16 single rooms, two treatment rooms, and equipment parking alcoves. |
| `MC` | Flex ward and inpatient support | 8 flex rooms plus discharge planning, case management, medication room, and the central bed-lift hub. |
| `ME` | Surgical ward east | 16 single rooms with direct bed-route access to the surgery/PACU lifts. |
| `SW` | Oncology and palliative ward | 12 larger rooms, a quiet family room, and a small consult suite. Keep lighting warmer and lower contrast than the procedural floors. |
| `SC` | Rehabilitation/long-stay ward | 14 single rooms and a protected mobility loop with wall rails. Do not place loose furniture on the loop. |
| `SE` | Overflow ward and staff support | 10 rooms, staff offices, on-call rooms, linen, and service-lift staging. Public access ends at the ward desk. |

Planning capacity is 120 single rooms on level 4, plus ICU, intermediate-care,
observation, pre-op, PACU, and procedure bays on lower floors. Counts are layout
targets, not a claim of real-world code or licensure compliance.

## Vertical circulation and connectors

### Cores

- `NW public core`: two public lift faces, one 2.0 m stair, toilets, and a
  3.0 m clear lobby on all four levels.
- `MC bed core`: two bed lifts with roughly 2.10 m by 3.00 m clear cabs, paired
  doors, and 2.7 m approach lanes. It links ED/imaging, surgery/PACU, ICU, and
  wards.
- `SC/SE service core`: two freight/service lifts, a staff stair, clean cart
  staging, and a physically separate dirty-return vestibule.
- `garage arrival`: the current skybridge is centred at local (46, 155), with
  its walking level intended around 4.2 m. It should arrive at a controlled
  public vestibule, then join the `NW` public route without entering receiving.
- Remote stairs at campus edges keep every connected floor from depending on
  one central core. Stair landings need real support and conservative collision
  around rails, not one solid bounding box around the entire flight.

### Former internal-road gaps

Do not fill the whole campus with one collision slab. The current source's
`hospital east west connector core`, `hospital north south connector core`, and
`hospital garage skybridge` are solid exterior-massing boxes. They must become
hollow wall/ceiling assemblies with real openings and per-level support; adding
decor inside the existing solid volumes would still leave them blocked. Use
named pieces for each court, gallery, connector, and bridge so render and
collision can be checked independently.

- Ground-level public galleries: 6.0 m overall, 3.0 m guaranteed clear route.
- Bed/clinical connectors: 4.2 m overall, 2.7 m guaranteed clear route.
- Rear service connectors: 3.6 m overall, 2.4 m guaranteed clear route.
- Upper bridges get floor support, side walls/frames, roof/ceiling, and real
  openings at both receiving buildings. A decorative bridge box with no door
  openings is not traversable.
- Courts remain pedestrian-only. Furniture, trees, and planters sit outside
  emergency egress and connector landing rectangles.

## Collision-ready furnishing zones

The names below are recommended stable authored names. Large fixtures carry
simple solid boxes or purpose-built collision. Small controls, cables, screens,
and overhead pieces do not.

| Named piece or zone | Nominal visible size | Collision and clearance contract |
| --- | ---: | --- |
| `hospital lobby reception desk cabinet` | 7.20 x 1.10 x 1.10 m | Solid base split into straight segments; 1.80 m public approach and 1.50 m clear around both ends. Do not use one box if the desk curves. |
| `hospital waiting chair bank` | 2.20 x 0.75 x 0.85 m | One solid low box per fixed three-seat bank; place only in marked side bays. Keep the main gallery clear. |
| `hospital imaging CT gantry` | 2.40 x 1.20 x 2.20 m | Solid outer legs/body with an open bore; do not block the bore with a whole-object box. Preserve a 1.50 m service ring and a clear table travel lane. |
| `hospital imaging table` | 0.80 x 2.40 x 0.85 m | Solid low box; align with gantry opening. |
| `hospital operating table` | 0.90 x 2.20 x 0.90 m | Solid low box with 1.50 m clear perimeter. Overhead lamp arms remain non-solid. |
| `hospital PACU bed` / `hospital patient bed` | 1.05 x 2.35 x 0.80 m | Solid bed envelope. Keep 1.20 m staff side and 1.50 m foot clearance. |
| `hospital nurse station cabinet` | department-specific | Solid base pieces. Build U/L shapes from segments so the staff opening stays open; keep 1.50 m inside turn and 2.40 m outside route. |
| `hospital bedside cabinet` | 0.55 x 0.50 x 0.85 m | Solid low box inside the room furnishing zone, never in the doorway swing or bed route. |
| `hospital bedside vital monitor body` | 0.72 x 0.18 x 0.52 m | Wall/rail mounted and non-solid to avoid shoulder snag. Geometry supplies casing, lip, and controls. |
| `hospital bedside vital monitor display face` | 0.60 x 0.02 x 0.40 m | Non-solid, flat 3:2 front face. Map `bedside-vital-monitor-display-generated.png` once with UV 0..1 and white tint; never tile it. |
| `hospital service cart parking zone` | 1.20 x 2.40 m floor mark | Keep parked solid carts fully inside. The zone itself is visual/non-solid. |
| `hospital clean/dirty utility counter` | 3.60 x 0.70 x 0.95 m | Solid base; 1.20 m staff work aisle minimum and no public approach. |

## Materials and fitted monitor face

The generated texture belongs only to
`hospital bedside vital monitor display face`. It is not a generic screen
swatch and must not be used on signs, wall panels, nurse-station dashboards, or
other equipment.

- Runtime path:
  `assets/textures/world/hospital/interiors/bedside-vital-monitor-display-generated.png`
- Native image: 1536 x 1024 RGB PNG, exact 3:2.
- Receiver face: 0.60 m wide x 0.40 m high, one full-image UV, no repeat.
- Put the face 0.011 m in front of the monitor body to avoid coplanar flicker.
- Use a white albedo tint. If emissive tint is added, keep it restrained; the
  face should read in a lit room without becoming a room light.
- The body, bezel depth, stand/rail mount, knobs, and cable sockets remain
  authored geometry. The texture is only the recessed display surface.

Exact generation provenance and prompts are in
`docs/assets/hospital-interior-props-generated.md`.

## Lighting plan

Emissive-looking lenses and the monitor texture do not light rooms. Every major
fixture group needs real runtime light entries through the existing tiled-light
path, culled by distance and active floor/block.

| Zone | Fixture cadence | Color target | Starting runtime target |
| --- | --- | --- | --- |
| Lobby/public gallery | 4.8-5.4 m centres | warm-neutral, about 3500 K | 7.5 m range, power 2.8 |
| Clinical corridors/exam | 4.2-4.8 m centres | neutral, about 4000 K | 6.2 m range, power 2.4 |
| Imaging control/scan | perimeter and task heads | neutral-cool, about 4200 K | 5.5 m range, power 2.6; no glare on displays |
| Operating rooms | 2.4 m ceiling grid plus table task light | clean neutral, about 4500-5000 K | 5.5 m range, power 3.6 |
| ICU/PACU | 4.2 m ambient plus headwall task lights | neutral, about 4000 K | 6.0 m ambient, 3.5 m task |
| Patient rooms | one ambient plus one night light | 3500 K ambient, 2700 K night | 5.8 m/power 2.2; 2.5 m/power 0.7 |
| Service/utility | 4.8 m centres | neutral, about 4000 K | 6.0 m range, power 2.3 |

Recommended stable names are `hospital interior ceiling light lens`,
`hospital interior procedure light lens`, `hospital interior headwall light
lens`, and `hospital interior night light lens`. Geometry gets an emissive tint;
runtime code gathers the same named parts into real local lights. Do not add one
ambient fill for a whole 3x3 campus. Cull by camera distance plus floor/block so
all four stories do not upload at once.

Emergency/exit fixtures should remain visible during normal daytime and night
checks, but they should not wash the corridors red. Exterior vehicle headlights
must be kept out of interior night-light screenshots.

## Authoring order

1. Re-read the parent four-story shell before implementation and lock the
   current 3.40 m deck cadence, connector openings, public entrance, ambulance
   entrance, garage arrival, and service edge.
2. Author slabs, thresholds, two remote stairs, the public/bed/service cores,
   and one complete connector path. Add matching support surfaces immediately.
3. Build the level-1 public and emergency routes before adding furniture.
4. Add level-2 clean/dirty surgery circulation, then level-3 ICU bed routes,
   then the reusable level-4 room module.
5. Add solid fixture bases and explicit keep-clear zones. Small detail remains
   visual-only.
6. Hook the dedicated monitor material to the exact named face with UV 0..1.
7. Add fixture lenses and real runtime lights together. Tune one block at night
   before copying the cadence across the campus.
8. Add signs and decorative dressing last, after traversal and collision stay
   green.

## Future validation contract

No runtime validation is claimed by this planning sidecar. The eventual build
should prove at least:

- on-foot travel from the outer main sidewalk to registration, imaging
  reception, a public lift/stair, one patient room, and back outside;
- a blocked-door negative control beside a real opening;
- ambulance/resuscitation -> CT -> bed lift -> OR/PACU -> ICU travel without a
  public-route crossing;
- service receiving -> freight lift -> level-2 clean staging, plus a separate
  dirty return to decontamination;
- collision on reception desks, fixed seating, beds, CT outer body, operating
  tables, nurse stations, counters, and carts;
- no collision in scanner bores, doorway openings, bed-turn rectangles,
  connector paths, stair headroom, or public decision points;
- floor/threshold/bridge support at all four elevations, including both ends of
  every connector;
- daylight and night screenshots from lobby, imaging, OR, ICU, patient room,
  and service corridor, with the on-foot player inside and the car outside;
- actual stair traversal between all four floors, not just a clean build or GL
  smoke;
- neighboring parcel and outer-road separation around the road-free superblock,
  including clearance at every retained-road terminus.

## Current runtime integration gaps

These are handoff notes, not changes made by this sidecar.

- The hospital is currently appended with `SiteMaterialStyle::Construction`.
  The monitor face needs a dedicated hospital material field and exact-name
  routing; it should not inherit construction textures or glass behavior.
- Current hospital walk support covers the superblock lot, ground wing floors,
  entrance walks, emergency apron, helipad, and garage decks. It does not yet
  provide interior slabs for levels 2-4, hollow connector floors, stairs, lift
  thresholds, or a traversable garage skybridge.
- The connector cores and garage skybridge currently carry whole-box collision.
  Replace those with wall/rail collision and supported floor surfaces before
  testing circulation.
- No hospital interior fixture list is gathered into the live local-light path.
  Add named hospital lenses to that path and cull by nearby block/floor.

## Integration handoff

The first useful vertical slice is `NW` lobby -> public connector -> `NE`
imaging reception, plus `NC` ED -> CT and the `MC` bed core. That slice proves
all four route types, connector support, two kinds of controlled threshold,
furniture collision, the fitted monitor material, and day/night lighting before
the full 36 block-floor program is duplicated.
