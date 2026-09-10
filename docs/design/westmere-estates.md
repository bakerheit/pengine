# Westmere Estates

Westmere Estates is Pinatty's high-end residential enclave on the dry bluff
west of Pinatty Row and south of the Kessel Channel. It remains part of the
Meadows for district and police-response rules; its identity comes from its
street layout and authored grounds, not a new gameplay district.

## Layout

- Westmere Drive branches from the existing Marsh Road control point at
  `(-560, -340)`, passes the community club and becomes the quiet neighborhood
  spine. It avoids adding a Saltmarsh through-route.
- Laurel Court, Cedar Court, and Magnolia Court branch from the main drive.
  Each is a compact lollipop cul-de-sac with a planted center island and a
  twelve-sided curve that reads nearly round at street scale.
- Nine large two-storey estates face the three cul-de-sac bulbs. Each has a
  two-car garage, a supported private drive, a front walk, low walls, mature
  trees, entrance lamps, and a distinct facade/roof combination. Six have
  private rear pools.
- Each turnaround island has its own mature trees, low shrub ring, benches,
  and pedestrian-scale lighting, so none of the bulbs reads as dead grass.
- The Westmere Community Club sits near the entrance with a full swimming
  pool, lane markers, ladders and loungers, plus a tennis court, planted paths,
  benches, and a residents pavilion.
- A masonry gate, paired lamps, and guardhouse mark the neighborhood approach.

## Runtime contract

The roads are normal authored `RoadSpine`s and share an exact graph node with
Marsh Road. Estate driveways use the building-access baker to cut their real
curbs and sidewalks. Each long private drive is one indexed, continuously
sloped triangle ribbon with terrain-following side faces; the old chain of 46
horizontal one-metre slabs is forbidden. The same top triangles feed rendering
and collision. House floors, drives, paths, pool terraces, the common, and the
gate sample the real terrain rather than painting a flat super-lot over the
bluff. Random country scatter is suppressed inside the neighborhood because
its trees are placed by hand.

`luxury_neighborhood_tests` pins the branch and three cul-de-sacs, validates all authored
parts against the real terrain, checks the common and gate inventory, and
drives a production vehicle from each court through every curb cut to every
garage. The smooth-driveway regression also pins the shared ribbon sections,
the nine-percent maximum grade, absence of legacy slab parts, and a continuous
non-colliding edge face. The 2026-09-06 visual check inspected both the garage
approach and the driveway side profile in the real game; all nine drive runs
reached their garage apron with zero impacts.
