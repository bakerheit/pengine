# 1991 Harrow Hookline references

[Manufacturer and model page](../../../world/vehicles/Harrow/Hookline.md)

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

## Reference photo gallery

These are the generated design references used to shape the playable vehicle.

### Selected concept

![1991 Harrow Hookline — selected concept](concept-selected.png)

### Front three-quarter

![1991 Harrow Hookline — front three-quarter](front-three-quarter.png)

### Rear three-quarter

![1991 Harrow Hookline — rear three-quarter](rear-three-quarter.png)

### Front

![1991 Harrow Hookline — front](front.png)

### Rear

![1991 Harrow Hookline — rear](rear.png)

### Left side

![1991 Harrow Hookline — left side](left.png)

### Right side

![1991 Harrow Hookline — right side](right.png)

### Top

![1991 Harrow Hookline — top](top.png)
