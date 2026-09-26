# 1991 Harrow Hookline references

Selected from [candidate 4](concept-selected.png): a compact cab-over city
recovery truck. These seven images are the modeling reference set. The same
mustard-yellow cab and service bed, cream roof, amber lightbar, rear dual
wheels, storage cabinets, winch, raised boom, hook, and striped rear bar should
appear in every view.

| Feature | Controlling views |
|---|---|
| Overall identity and service equipment | [front three-quarter](front-three-quarter.png), [rear three-quarter](rear-three-quarter.png) |
| Wheelbase, single cab, cabinets and boom pivot | [left](left.png), [right](right.png) |
| Cab width, grille, lamps and lightbar | [front](front.png) |
| Rear axle, dual tires, tail lamps and striped tow bar | [rear](rear.png) |
| Cabinet plan, central winch and boom alignment | [top](top.png) |

Resolve image differences by keeping a **two-seat single cab** with one door
per side; the rear-facing cab window is not an extra passenger row. The boom
pivots behind that cab, above the service bed, and rises toward the rear. The
top view shows its horizontal projection, not a second flat boom pose. Keep
one winch and one centered hook. Let side views determine the chassis and
boom dimensions; rear and top views locate the dual tires and equipment.
Dimensions remain estimates until the modeling shape contract is written.

The runtime model uses transparent cab glass and mapped door cards, seats,
dash, headliner and cabinet surfaces. The boom and hook are visual equipment;
they do not yet tow other vehicles. This reference set is concept art, not a
recovered engineering drawing. Rebuild the playable model with
`python3 tools/make_1991_candidates_assets.py harrow_hookline`.

[Complete three-vehicle reference index](../1991-selected-vehicles.md).
