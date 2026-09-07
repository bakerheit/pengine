# Tacomaco

Tacomaco is a second, enterable copy of the Cloggers fast-food building. It
sits in the Vellum Row block at grid east `136`, south `155`, two blocks east
of its original parcel, between Sixth and Seventh Street. The site uses the
same `64 m × 38 m` parcel, north-facing frontage, parking row, drive-through
return, kitchen partition, fixtures,
floor supports, and two live push-door leaves as Cloggers.

The shared parking row has five `3.5 m` bays: three west of the door crossing
and two east. Its stripes and wheel stops are building-anchored, so parcel
expansion leaves the pedestrian crossing clear instead of dragging the bays
toward the sidewalk.

The authored shell is intentionally shared through `kTacomacoPlan` and
`kFastFoodPlan`; site transforms, building-access registration, interior lights,
and brand materials are independent. `Tacomaco` gets its own map footprint,
restaurant marker, dev teleport, measured sidewalk/driveway connection,
redesigned logo, 15-combo menu with approximate 1991 prices, and an orange-and-
green exterior palette. The interior and generated menu boards keep their
existing materials.

The next menu-art pass follows the shared physical/raster contract in
`docs/design/restaurant-menu-board-spec.md`: two 4:1 interior boards and one
2:3 drive-through board. Tacomaco's 15 combos must be divided across the two
interior receivers instead of being squeezed onto the portrait order board.

Validation targets:

- `tacomaco_tests`: copied shell identity, measured sidewalk connection, and
  actual on-foot movement through the doorway.
- `building_access_tests`: all neighborhood plots remain connected after the
  new parcel is registered.
- Runtime: approach the new block, inspect the exterior logo and drive-through
  board, then enter and inspect the interior menu and lighting.
