# Vellum Regional hospital garage and grounds plan

Planning sidecar for the last-row, two-block parking garage in
`src/city/hospital_campus.h`. This file does not change production geometry,
collision, roads, tests, CMake, or the parent hospital-campus brief.

## Parent-scope assumptions

- The hospital wings are four stories. The live parent source still defines a
  separate three-deck garage, P1 at grade and P2-P3 above it; this plan keeps
  that distinction.
- The campus portions of Rook Lane and Seventh Street are removed. Their
  surviving perimeter stubs are not garage access and no garage vehicle
  movement in this plan depends on either alignment.
- Bellweather Road, Sixth Street, and Vellum Row remain perimeter streets.
  Garage entry is from Bellweather Road (road id 22), exit is to Sixth Street
  (road id 36), and Vellum Row (road id 24) gets no curb cut.
- The garage remains in the current two-block cell. Do not move it to recover
  parking capacity; circulation and a safe hospital walk matter more than the
  last row of stalls.

## Current coordinate contract

All dimensions and placements below are hospital-site local. `+x` is grid
east, `+z` is grid south, and the whole site keeps the downtown yaw of -6
degrees. Convert through `kHospitalSite`; do not paste the diagnostic world
coordinates into production code.

| Item | Current value |
| --- | ---: |
| Site origin in world XZ | `(-38.0806, -331.8965)` |
| Garage centre, site local | `(46.0, 186.0)` |
| Garage centre in world XZ | `(-11.7749, -142.1072)` |
| Garage structural/deck envelope | `x[-27.0, 119.0]`, `z[167.5, 204.5]` |
| Garage paved-lot envelope | `x[-27.0, 119.0]`, `z[167.0, 205.0]` |
| Bellweather Road centreline | `x=-46.0` |
| Former campus Rook Lane alignment | `x=46.0` |
| Former campus Seventh Street alignment | `z=155.0` |
| Sixth Street centreline | `z=217.0` |
| Vellum Row centreline | `x=138.0` |

Current deck-envelope corners, useful only for visual QA, are:

- northwest `(-27.0, 167.5)` -> world `(-82.4412, -168.1364)`;
- northeast `(119.0, 167.5)` -> world `(62.7590, -152.8752)`;
- southeast `(119.0, 204.5)` -> world `(58.8915, -116.0779)`;
- southwest `(-27.0, 204.5)` -> world `(-86.3087, -131.3391)`.

With current road widths, the campus-side edge of Bellweather's sidewalk is
approximately `x=-36`, Sixth's is `z=207`, and Vellum Row's is `x=124`.
Re-measure these from baked ribbons after the parent road edit. The current
third-row hospital rear wall (source row index 2) is near `z=141.7`, leaving
about 25.8 m between the wing and the garage's north face once the Seventh
Street segment is removed. The live parent source also places a P2 skybridge
at `x=46`, `z=155`, spanning approximately `z[143,167]`.

```text
                                      north / -z
          four-story hospital wings and a real bridge/door opening
                             ||
                 P2 skybridge over protected ground walk
            lawn / rain garden in former Seventh Street corridor
                             ||
 Bellweather      +---------------------------------------------+   Vellum Row
 Road             | west stair | main lift | parking           |   (no access)
 entry ---------->| pay gate   | stacked ramp / circulation    |
                  |             dropoff pocket -----------> exit|
                  +----------------------------------------|----+
                                                   Sixth Street
                                      south / +z
```

## Garage massing and levels

- Keep the open-sided 146 m by 37 m deck envelope. Its long face should read as
  parking, with deep openings, visible floor bands, and two solid vertical
  cores rather than four stories of blank concrete.
- Use three drive surfaces at `+0.30`, `+4.00`, and `+7.70` m above the 12 m
  city plate. This is a 3.70 m floor-to-floor rise. A 0.28 m upper slab leaves
  about 3.42 m clear before lights and signs.
- Put a 1.05-1.10 m collision-bearing parapet at every exposed upper edge.
  The top parapet lands near `+8.8` m and stays clearly below the four-story
  hospital roofline while still giving the garage a finished crown.
- Keep open wall area above each parapet. Mesh screens are visual only unless
  their actual posts have matching collision; never turn an open facade into
  one solid invisible box.
- Align columns with parking noses and bay dividers. Do not put columns in the
  entry queue, exit queue, ramp opening, ramp turn aprons, dropoff lane, or the
  protected pedestrian spine.
- The current placeholder columns at `(46,171)` and `(80,201)` conflict with
  the proposed bridge core and dropoff pocket. Move those grid points before
  treating the column schedule as final; do not merely hide their meshes while
  leaving their colliders behind.

## Entry, exit, and ground circulation

### Bellweather entry

- Make one 4.2 m inbound driveway from Bellweather, centred near `z=198.0`.
  The access crosses the existing sidewalk at a single, square crossing. The
  sidewalk stays visibly continuous across the driveway with a flush ramp;
  it must not become a flat collider over sloped access triangles.
- Keep the lane clear from the site-side sidewalk edge at `x=-36` to the gate
  line near `x=3`. That gives roughly 39 m of on-site stacking, mostly beneath
  P2, and is enough for five
  ordinary cars without queueing across Bellweather's sidewalk or carriageway.
- Use a low splitter/island on the driver's side of the lane for the pay
  station and barrier pedestal. Begin the island only after the sidewalk
  crossing so a person following Bellweather is not pinched against equipment.
- Restrict this access explicitly to road id 22. Nearest-road selection is not
  safe here because the removed Rook/Seventh segments and the Vellum arterial
  are all close enough to be plausible wrong answers.

### Sixth Street exit

- Put one 4.2 m outbound lane near `x=94`, crossing Sixth's sidewalk at 90
  degrees. The gate line sits inside the structure around `z=200.5`, with at
  least 18 m of queue storage behind it.
- Shape the curb return for a right turn toward grid west. Do not add a second
  curb cut to Vellum Row or let the apron spread into the Vellum/Sixth
  intersection sight area.
- Merge P1 parking and dropoff traffic before the gate, not on the sidewalk.
  Mark a yield line at that merge and keep a 6 m by 6 m clear sight box around
  it.
- Restrict this access explicitly to road id 36. Preserve Sixth's road id and
  lane geometry.

### Internal flow

- Use a clockwise, one-way P1 loop from the Bellweather gate to the ramp,
  parking aisles, dropoff pocket, and Sixth exit. A 4.0 m clear one-way aisle
  is enough; keep 6.5 m clear where cars reverse out of 90-degree stalls.
- Upper decks may use two-way parking aisles, but arrows should send climbing
  traffic directly to the next ramp end instead of through every bay row.
- Use 2.7 m by 5.4 m ordinary bays. Put accessible bays beside the main core,
  with a 1.5 m access aisle and a flush route into the lobby. Stripe only after
  ramp openings, columns, cores, turn sweeps, and walkways are carved out.
- Do not restore a through route on the former Rook Lane or Seventh Street
  alignments. The only northward continuation from the garage is pedestrian.

## Ramp system

- Reserve a longitudinal ramp shaft at approximately `x[-8,44]`,
  `z[184,193]`. The clear two-way drive surface is 8.0 m wide; the extra
  width is for 0.30 m edge barriers, not driving.
- Each 52 m ramp rises 3.70 m. Use 6 m parabolic easing at both ends and a
  constant middle grade; the peak grade is about 8.04 percent. Render and
  collide against the same subdivided ramp quads.
- Alternate the slope by story: P1-P2 rises east-to-west and P2-P3 rises
  west-to-east. A driver entering from Bellweather can reach the east P1 ramp
  end without a U-turn, and the P2-P3 ramp starts where that driver arrives.
- Keep the west turn apron `x[-27,-4]` and the east turn apron `x[40,64]`
  clear across the ramp band. Confirm at least 8 m inner / 10 m outer swept
  turning radii with the actual player vehicle.
- Cut each upper slab around its active ramp surface. A full horizontal slab
  under or over a ramp can make wheel/support probes select the wrong floor.
  Ramp barriers follow the slope in short segments and stay outside the 8 m
  clear surface.
- Give every ramp endpoint a real overhead-clearance check. Keep lights,
  beams, signs, and gate hardware at least 2.5 m above the highest point in the
  vehicle envelope.

## Pedestrian links and patient dropoff

### Main hospital link

- Put the main stair/elevator core on the current bridge axis, nominally
  `x[40,52]`, `z[167.5,183]`. Its P1 south door opens toward the protected
  dropoff walk; its P2 north door opens directly into the skybridge.
- Keep the current skybridge centreline at `x=46` and make it a usable P2 link,
  not a monolithic solid box: author a supported floor, side walls/glazing,
  roof, and real openings at both ends. Extend or land its north end into a
  real receiving volume at the third-row hospital seam.
- Run a 2.8 m clear, step-free ground walk north under the bridge axis for P1
  users. The parent hospital geometry must provide a real rear-wall opening
  and supported threshold at its end, or route it visibly to an exterior
  entrance. Do not terminate either link at a solid wall.
- Keep the former street corridor as pedestrian ground, rain garden, and lawn.
  No bollard line should imply that cars can continue north through it.

### Secondary egress

- Put a second public stair near `x[-15,-8]`, `z[170,179]`. Connect it to a
  2.4 m west walk that reaches Bellweather's sidewalk north of the driveway.
- Use real treads, landings, and support surfaces if characters are expected to
  climb it. A stair-shaped render over one ramp box is not traversable evidence.
- Keep both stair doors and a 2 m landing clear on every level. Do not stripe a
  parking nose into the landing.

### Non-emergency dropoff

- Reserve a 3.6 m one-way pull-through pocket inside P1 along the south edge,
  approximately `x[58,88]`, `z[197.5,202.5]`. It is for patient pickup/dropoff,
  not ambulances; the emergency receiving route remains separate.
- Provide about 24 m of usable curb for three cars, plus tapered ends. The
  passenger curb is on the north side and feeds the main core without crossing
  the moving lane.
- Dropoff traffic uses the same controlled Bellweather entry and Sixth exit.
  A free/grace-period behavior may be added later, but this plan does not
  create a parking economy or payment gameplay.
- Add one raised or strongly painted yield point where dropoff traffic rejoins
  the exit stream. Its support geometry must agree with what is drawn.

## Grounds and planting

- **North campus green:** use the space from the third-row rear wall to the
  garage north face as a pedestrian court. Keep the main walk and west egress
  open, then use low rain-garden beds in the leftover former-road surface.
  Suggested tree centres are `(8,151)`, `(76,151)`, and `(104,159)`, subject
  to the parent's final doors, bridge landing, and underground-service fiction.
- **West buffer:** only about 9 m remains between Bellweather's sidewalk and
  the widened garage edge. Use flush paving and low planting north of the
  driveway; do not squeeze a canopy tree or freestanding sign into this strip.
  Nothing taller than 0.75 m belongs beside the entry throat or in its sight
  triangle.
- **East drainage strip:** only about 5 m remains between the widened garage
  and Vellum Row's sidewalk. A narrow slot drain or low planted run near
  `x[120,123]`, `z[171,198]` is enough. Do not place trees, benches, or a
  driveway in it, and keep its south end low so the Sixth/Vellum corner remains
  readable.
- **South edge:** space is tight between the garage and Sixth's sidewalk. Use
  flush paving and low groundcover only. No tree, bench, pay kiosk, or deep
  planter goes between `z=204.5` and the sidewalk crossing.
- Give every tree trunk a small matching solid collider; canopies and soft beds
  stay non-solid. Large planters are solid only when their visible volume and
  collider match. Keep trunks and planter corners at least 1.0 m from a walk
  edge and 1.5 m from a vehicle lane edge.
- Flatten/support only the actual garage, paths, and planting cells. Exclude
  procedural scatter from the whole garage/grounds envelope so trees and rocks
  cannot respawn in a ramp, sight line, or walk.

## Lighting

- Add real runtime lights. Emissive or bright texture color alone does not
  light a deck or a pedestrian face.
- Put shielded ceiling fixtures over circulation at roughly 12 m spacing on
  P1-P3, with extra fixtures at both ends of every ramp and at each core door.
  Keep fixture meshes and light origins above the 2.5 m clearance envelope.
- Put 4.2 m pedestrian poles in planting beds along the north walk, not in the
  walk itself. Light the Bellweather sidewalk crossing, dropoff curb, and Sixth
  sidewalk crossing without aiming glare at either street.
- Give the pay station a small downward canopy light and the exit merge a
  separate fixture. The generated control-face texture is albedo; any screen
  glow is presentation only and must not replace the light source.
- Night QA must be done with the player on foot and the car left far enough
  away that headlights cannot make the plan look better than it is.

## Wayfinding and visual language

Use exact, short copy and render it with geometry or the runtime font path so
it stays readable. Do not bake these words into a generic repeating texture.

- Bellweather entry sign: `VELLUM REGIONAL` / `PARKING` / `ENTRY`.
- Dropoff fascia: `PATIENT DROPOFF` / `10 MIN`.
- Main core, garage-facing: `HOSPITAL` / `ELEVATORS`.
- Exit header: `EXIT` / `SIXTH STREET`.
- Level bands: `P1`, `P2`, `P3`; use one stable color per level and
  repeat it on the core, columns nearest the core, and stair landings.

Keep the existing hospital teal as the primary garage accent, warm off-white
for pedestrian information, amber for caution, and red only for emergency/no
parking markings. Parking and emergency signs must not share the same dominant
red treatment.

## Props, barriers, and collision-safe placement

| Prop / zone | Nominal local placement | Collision contract |
| --- | --- | --- |
| Entry pay-station island | `x[-2,5]`, `z[194.3,195.6]` | Low curb/island is solid and matches its mesh; begins east of the sidewalk crossing. |
| Entry pay station | centre about `(0.0,195.0)`, control face normal toward `-x` | Body/plinth solid; keep 0.60 m from the live lane. Map the generated face once to its 0.60 m by 0.90 m upper control face. |
| Entry gate pedestal | about `(3.0,195.0)` | Pedestal and protective bollards solid; arm breakaway/non-solid until a real dynamic barrier exists. |
| Entry gate arm | gate line near `x=3`, spanning the lane in `z` | Never use one solid AABB that includes the raised-arm sweep. Closed-arm collision must be dynamic or omitted. |
| Exit gate pedestal | east side of lane near `(96.5,200.5)` | Solid plinth outside the 4.2 m lane; preserve the core-to-dropoff walk. |
| Exit gate arm | across the `x=94` lane near `z=200.5` | Same breakaway/dynamic rule as entry; no permanent invisible bar. |
| Main stair/elevator core | `x[40,52]`, `z[167.5,183]` | Walls solid with real door openings; every landing/floor gets support. P2 aligns to the skybridge; elevator can remain presentation-only. |
| West stair | `x[-15,-8]`, `z[170,179]` | Treads and landings solid/supporting; guardrails outside the clear stair width. |
| Entry sign | wall-mounted near the west facade around `(-26.8,181)` | Sign face non-solid; no freestanding post in the narrow sidewalk setback or entry sight triangle. |
| North light poles | planting beds beside, never inside, the 2.8 m walk | Pole collider matches visible shaft/base; lamp head non-solid. |
| Parking stops | only at bays that face a wall/core | Low matching solids; omit where they create trip hazards across accessible paths. |

Global clearance rules:

- Preserve 0.75 m from any static prop collider to a straight vehicle sweep,
  1.0 m at turns, and the full 8/10 m inner/outer ramp-turn radii.
- Preserve 2.0 m clear at stair doors, 2.8 m on the main hospital walk, 2.4 m
  on the west walk, and 1.5 m beside accessible bays.
- Paint, arrows, fascia textures, sign faces, soft planting, and light cones are
  non-solid. Decks, ramps, parapets, cores, columns, curbs, plinths, bollards,
  trunks, and substantial planters use geometry-matched collision.
- Do not add a garage-sized collision box. It would close the ground level,
  block the ramp shaft, and erase the reason for an open parking structure.

## Generated fitted texture receiver

Use
`assets/textures/world/hospital/garage/entry-pay-station-control-face.png`
exactly once on the west-facing 0.60 m by 0.90 m upper face of the named
`hospital garage entry pay station control face` section. The texture is a
non-seamless 2:3 panel with a clear teal perimeter, screen, ticket slot,
contactless pad, help button, and speaker grille. Do not tile it, crop it into
a material swatch, or apply it to the whole kiosk body. Use ordinary painted
steel on the side, rear, and lower pedestal faces.

## Parent integration order

1. Use the parent's final split-stub road table and rebake the actual perimeter
   ribbons; do not reconnect the removed campus runs.
2. Re-measure the three sidewalk edges above; keep road ids 22, 24, and 36
   stable.
3. Bake the three deck surfaces, slab holes, exact ramp quads, barriers, cores,
   and stairs from one garage description.
4. Add the Bellweather and Sixth access geometry after road ribbons, replacing
   only overlapped old sidewalk/curb pieces.
5. Register support and collision from those same baked surfaces, then add
   small prop colliders individually.
6. Rebuild the skybridge as a traversable P2 link, then add its real hospital
   opening plus the protected ground walk/door.
7. Route the fitted pay-station face once, then add signs and real runtime
   lights.
8. Add map footprint/detail without inventing GPS routes; the minimap remains
   bearing-only.

## Integration acceptance checks

These are recommendations for the parent implementation; this sidecar does
not add or run them.

- Assert three drive surfaces, two ramp surfaces, two pedestrian cores, one
  traversable skybridge, and the unchanged two-block footprint.
- Probe every deck and sampled ramp point. The selected support must be the
  visible surface, not a slab above/below it, and every ramp triangle normal
  must face up.
- Drive the real player vehicle from Bellweather through the gate, up and down
  every ramp in both directions, through the dropoff, and out to Sixth. Require
  zero impacts and no height jump at ramp transitions.
- Sweep the largest relevant player vehicle through the entry queue, exit
  merge, dropoff tapers, and both ramp turn aprons. No column, bollard, kiosk,
  signpost, planter, or tree collider may intersect the swept volume.
- Walk from Bellweather's sidewalk to the west stair, from dropoff/access bays
  through the main core, and from the core through the former Seventh corridor
  and real hospital rear opening. Add blocked-wall/closed-door negative
  controls so a test cannot pass by walking through geometry.
- Assert no garage access or lane-graph connection uses the removed Rook Lane
  or Seventh Street campus segments and no curb cut touches Vellum Row.
- Inspect daytime and nighttime screenshots from Bellweather, Sixth, the north
  hospital court, P1 dropoff, an upper deck, and both ramp endpoints. A clean
  GL queue or bounded smoke run is stability evidence, not circulation or
  lighting evidence.
