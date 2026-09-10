# Pinatty network and density pass

Nickel Road now starts at Halloway Street's east end instead of Fifth Street.
Its stable road id remains 182, so traffic identity and downstream systems do
not re-roll. The North Arm now swings east from Halloway Square and meets the
south endpoint of Pinatty Row at the shared 13.5 m datum. Rook Lane keeps its
separate endpoint and no longer receives North Arm traffic.

Three new skyscrapers complete the block face immediately north of Tenth
Street. Since Eleventh Street deliberately stays east of the Ferrone fork, all
three lobbies face south onto Tenth rather than opening toward an empty field.

| Tower | Pinatty grid centre | Floors | Use | Crown |
|---|---:|---:|---|---|
| Briar Sentinel | `(-414, -341)` | 29 | mixed | twin fins |
| Mercer North | `(-322, -341)` | 23 | residential | terrace |
| Bellweather Point | `(-230, -341)` | 32 | office | beacon mast |

Mercer Textile Exchange at `(-230, -155)` and Bellweather Pharmacy Offices at
`(-230, 93)` add two finished mid-rise background buildings in open central
blocks. They reuse the bounded Pinatty infill system, including shopfronts,
apartment windows, rear service details, roof equipment, and matching solid
collision geometry.

## Acceptance evidence

- The road graph contains both requested connections and a real production
  vehicle drives Nickel Road to Halloway Street and the North Arm to Pinatty Row
  without sinking.
- All 22 tower plots clear roads and active neighbors; all lobby approaches are
  walkable, including the three south-facing Tenth Street entrances.
- All 12 infill parcels remain on the 12 m Pinatty datum and clear road ribbons.
- The whole-city inventory contains 73 active lots with no overlaps.

The broader `traffic_junction_tests` suite still reports a 51.80 s healthy-lead
wait at Nickel's `(950, 200)` loop. Restoring the former Nickel Road west
endpoint produced the same deterministic failure, so this pass does not claim
to fix that separate inner-loop starvation issue.
