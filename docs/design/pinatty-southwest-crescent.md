# Southwest Pinatty crescent

Three two-way streets form a rounded pocket south of First Street, between
the western grid and Route 1. A curved perimeter and an offset cross divide
the pocket into four unequal blocks, leaving room for future lots.

| Road | Stable ID | Route |
|---|---:|---|
| Sable Crescent | 207 | First Street's west end, around the south edge, to Mercer Avenue's south end |
| Shuttle Street | 208 | Briar Street's south end through the centre to Sable Crescent |
| Loom Way | 209 | West side of Sable through Shuttle to the east side of Sable |

The middle junction is at world `(-332, 300)`. The southern tip is
`(-330, 369)`. The whole road graph stays inside Pinatty's current boundary;
it adds no freeway or West Ramp junction. Existing streets retain their IDs
and geometry. The ordinary Street class supplies asphalt, lane markings,
curbs and 3 m sidewalks. The shared road table feeds terrain grading, road
collision, traffic lanes, street names and both maps.

The crescent rises gently from First's 11 m road bed to a 13 m southern
profile. Shuttle joins Briar at 12.5 m and reaches 13 m at Loom. Grading is
derived from these same spines, including the standard terrain LOD margin.
The road pass moved no building parcels. The later [Loom cultural quarter](loom-cultural-quarter.md) adds a museum and gazebo garden. Its two-story expansion removes First Street between Briar and Mercer: ID 30 remains east of Mercer, and ID 210 preserves the short west section between Sable and Briar. The museum now occupies the reclaimed street within a 60 × 88 m parcel.

## Validation

- Built `apricot`, `apricot_map_lab`, `city_roads_tests` and
  `authored_city_layout_tests` in the main checkout.
- `city_roads_tests` passes: all seven new/shared junctions, district bounds,
  no accidental freeway links, terrain support/grade checks and six production
  vehicle runs covering the three new streets in both directions. The worst
  deviation on those runs was 2.17 m from lane centre; no vehicle sank. An
  additional lane-planned route exercises the interior turn. Existing
  inter-district and Halloway ramp drives also pass.
- `authored_city_layout_tests` passes for 76 active lots, including road and
  sidewalk corridor clearance against every lot and flat support under the
  existing Pinatty parcels.
- The actual game and map lab were rendered and inspected. Both 300-frame
  overhead and street-approach runs completed with clean GL queues. The street
  view shows connected curbs, crosswalks, live traffic and pedestrians at
  Sable/Loom. Startup streaming spikes
  remain visible in the runtime log; this is not a performance benchmark or
  manual traffic-congestion test.

Reproduce the overhead view:

```sh
./build/bin/apricot --start-at -332 300 --start-heading 95 --road-start --overhead --frames 300 --clear --save-file /tmp/apricot-pinatty-southwest-qa.json --screenshot build/pinatty-southwest-overhead.bmp
./build/bin/apricot_map_lab --zoom 16 --x -332 --z 285 --layer 1 --width 1280 --height 800 --screenshot build/pinatty-southwest-map.png
```

Evidence is retained in `build/pinatty-southwest-roads.log`,
`build/pinatty-southwest-lots.log`, `build/pinatty-southwest-runtime.log`,
`build/pinatty-southwest-overhead.png`, `build/pinatty-southwest-street.png`,
`build/pinatty-southwest-street.log` and `build/pinatty-southwest-map.png`.
