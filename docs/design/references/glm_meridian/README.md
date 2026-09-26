# 1991 GLM Meridian references

Selected from [candidate 1](concept-selected.png): a seven-seat, cab-forward
family minivan. These seven views are the directional reference set for the
playable model. Keep the pale seafoam body, charcoal lower bumpers and rockers,
dark window pillars, rectangular headlights with amber outer indicators,
upright rear hatch, and silver steel wheel covers with round holes.

| Feature | Controlling views |
|---|---|
| Overall identity and color boundaries | [front three-quarter](front-three-quarter.png), [rear three-quarter](rear-three-quarter.png) |
| Wheelbase, roofline, windows, doors and sliding track | [left](left.png), [right](right.png) |
| Headlights, grille and front bumper | [front](front.png) |
| Hatch, rear glass, taillights and rear bumper | [rear](rear.png) |
| Solid painted roof, hood and windshield placement | [top](top.png) |

Resolve image differences as **one hinged front door and one sliding rear door
per side**, plus a fixed rear-quarter window. The two handles near the B-pillar
belong to those two doors; do not turn them into extra doors. Put a single
sliding track under the rear side windows on each side. Use the side views for
axle positions and window divisions, and the top view for a solid roof without
a sunroof. The three-quarter views guide surface shape and material placement.
Dimensions remain estimates until the modeling shape contract is written.

The runtime model uses transparent glass and mapped cabin, seat, dashboard,
door-interior and headliner surfaces. These images remain concept references,
not engineering drawings. Rebuild the playable model with
`python3 tools/make_1991_candidates_assets.py glm_meridian`.

[Complete three-vehicle reference index](../1991-selected-vehicles.md).
