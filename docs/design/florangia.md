# Florangia — scaled terrain and biome pass

Florangia is the second state in Apricot's world. It sits southeast of O'Haven
across open water. This pass establishes the larger landmass, its first state
highway, and Florangia Regional Airport. This is the terrain-foundation
record; subsequent city work is covered by the [Miandi plan](miandi.md) and
the [current world guide](../lore/world.md). It does not define a Florangia
mission campaign.

## Shape, scale, and placement

World coordinates use +X east and +Z south. Florangia is scaled 1.45x in both
horizontal axes around its fixed northwest anchor at (2350, 3500), so its land
area is roughly 1.45 squared (2.10x) the first pass without narrowing the water
gap to O'Haven. Its dry land spans roughly X 2088..8264 m and Z 3320..9304 m,
with its atlas label at (4850, 5200). A long western panhandle joins a broad
shoulder, then bends into a narrow southeast peninsula and rounded tip.
Smooth-unioned capsules and ellipses keep the outline organic and Florida-like
without tracing the real state. The expanded world is 20,480 m square.

The shoulder around (4800, 4400) reserves a dry 650 m-radius airport field.
The authored 1200 x 600 m airport lot is level at 6.5 m, then feathers back to
natural terrain over 160 m. Its public access point is (4800, 4700).

At 32 m terrain sampling, Florangia measures 10.55 km² of dry land. The state
remains low, with a measured 8.56 m natural maximum and more than 99% of its
land under a ten-degree slope before authored structures.

The state is deliberately low: the first pass tops out below 12 m and keeps
more than 80% of sampled land under a ten-degree slope. O'Haven stays at its
original coordinates and Florangia's mask is exactly zero throughout the old
6144 m world box, preserving existing terrain and replay goldens.

## Coast

Most of the shore uses a broad, shallow falloff that naturally classifies as
sand. One contiguous Atlantic-side sector uses a steep geometric falloff and
naturally classifies as rock through the shared surface rules. The production
height field's zero contour measures 19,900 m at 20 m sampling:

- sand: 81.05%
- rock: 17.61%
- transition: 1.34%

This is measured coastline length, not a count of hand-tagged points.

## Vegetation

Trees spawned on Florangia select a dedicated four-variant palm family. The
first models use tapered, lightly bent trunks and double-sided radial fronds.
The existing four temperate variants remain reserved for O'Haven, and biome
selection reuses the existing random roll so old scatter does not reshuffle.

## Florangia Highway and airport access

Florangia Highway is a roughly 5 km, four-lane major route from the western
panhandle at (2800, 3500), across the broad central shoulder, then south along
the peninsula to (5900, 6650). It is authored as an Arterial rather than a
grade-separated Freeway: that keeps two lanes per direction while allowing
the state's first airport junction to be a real signalised crossing instead
of pretending an at-grade road is a motorway ramp.

The highway skirts west of runway 08-26 and bends around the airport's south
edge before passing through the stable public airport access node at
(4800, 4700). Florangia Airport Spur runs 120 m north from that node to
(4800, 4580), the airport's landside handoff. The airport owns circulation
north of the handoff; the state road owns the junction and approach. Both
roads grade their full ribbon and LOD margin to 8 m away from the airport,
feathering to the airport site's exact 6.5 m elevation at the access node, so
ribbon, terrain, collision, and AI lanes share one surface without a terminal
threshold step.

Stable authored IDs are 220 for Florangia Highway and 221 for Florangia
Airport Spur. Runtime integrations should use the constants in
`src/city/florangia_roads.h`, never nearest-road selection or table indices.

## Florangia Regional Airport

The airport occupies a level 1200 x 600 m parcel centred on the broad shoulder
at (4800, 4400). Runway 08-26 is a 1000 x 48 m east-west strip, keeping both
approaches over the state's widest dry ground. Two taxiways connect it to a
passenger apron; the landside complex includes a three-volume terminal,
control tower, maintenance hangar and apron, marked public parking, dropoff
road, and the direct highway access throat.

The site bakes through the same `StartSite`/`BuildingPiece` path used by the
Pinatty airport, so visible solid terminal, tower, hangar, light, and monument
pieces register matching collision. Thin runway, taxiway, apron, parking, and
road pieces use the airport paving material over the shared 6.5 m terrain
plate. Procedural palms are excluded from the airport parcel and its margin.
The atlas, minimap, and development teleport all use the same authored site.

## Validation

`state_terrain_tests` pins state names, placement, water separation, scaled
outline and area, low relief, the 650 m dry airport clearance, the measured
80/20 coast split, palm-only Florangia trees, and the O'Haven control family.
`florangia_highway_tests` pins the highway's dry-land route,
truck-safe grade, signalised airport T, two-way graph connection, right-side
lane placement, lane/collision agreement, and full vehicle-envelope sweeps
through every available airport movement. It also holds the full highway
ribbon outside runway 08-26's protection area. `florangia_airport_tests` pins
the complete facility inventory, runway markings, clear public approach, lot
bounds, and shared terrain support. Runtime validation should include the full
atlas plus a ground approach to the terminal; headless geometry cannot prove
the airport reads from the road or that the expanded silhouette reads on the
atlas.
