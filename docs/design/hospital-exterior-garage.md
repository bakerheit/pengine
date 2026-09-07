# Vellum Regional Hospital garage exterior polish

Implementation sidecar for the 146 x 38 m, three-deck parking garage and the
reclaimed Seventh Street corridor. All coordinates are local to
`kHospitalSite`; `+x` is grid east and `+z` is grid south.

The module owns no roads, parent slabs, world material routing, map geometry,
or runtime lights. Its single public entry point is:

```cpp
inline std::vector<StartPart> bake_hospital_exterior_garage();
```

## Authored layer

The sidecar returns exactly 150 purposeful `StartPart` records:

| Detail family | Pieces | Function |
| --- | ---: | --- |
| Open P2 facade screen | 10 | Broken rails and mullions preserve air and sight lines around the bridge and exit. |
| P2/P3 parapets | 10 | Collision-backed edge protection on all exposed deck sides. |
| Lift and stair core shells | 32 | Full-height side walls and floor-by-floor walls with real door gaps. |
| Public stair flights | 44 | Two alternating 22-step flights at a 168 mm rise. |
| Payment, gate, and clearance hardware | 15 | Driver-side island, kiosk, raised arms, posts, and 2.75 m clearance bars. |
| Level markers | 9 | Three boards use one, two, or three geometry bars instead of a repeated texture. |
| Ceiling fixtures | 12 | Six housings and six named lenses for parent-owned runtime lights. |
| Drainage | 2 | Flush east slot and south exit trench drains. |
| Rain gardens | 10 | Two low beds with visible curbs, soil, and shrub masses. |
| Bridge landing | 6 | Supported P2 landing, guards, and a 4 m clear portal. |

The detail stays deliberately chunky enough for Apricot's PSX language. The
extra craft comes from believable layers and function, not invisible polygons
or a generic high-frequency material.

## Circulation and collision

- The Bellweather entry lane remains clear south of the payment island. The
  island ends at `z=196.0`; its nearest clearance post sits outside the lane.
- The Sixth Street exit remains clear through `x[91.9,96.1]`. Gate arms are
  modeled raised and remain non-solid.
- Clearance posts and bars are solid because they are visible physical
  obstructions. The bar undersides are 2.75 m above the garage surface.
- Facade screen rails, marker faces, light meshes, plants, and flush drains are
  non-solid. Core walls, stair treads, landing guards, parapets, bollards, and
  curb volumes carry matching collision.
- The bridge portal leaves a real 4.0 m opening centered at `x=46`. No sidecar
  wall crosses the protected ground walk or P2 bridge path.
- The two rain gardens sit at `x=9` and `x=91`, clear of the bridge axis,
  driveway sight triangles, and the narrow Vellum Row edge.

## Parent integration

1. Include `city/hospital_exterior_garage.h`, call
   `bake_hospital_exterior_garage()`, and append the returned records after the
   base hospital bake.
2. Route the exact receiver name `hospital garage west entry control face` to
   `textures/world/hospital/garage/polish/west-entry-control-face-generated.png`.
   Fit the complete image once with UV scale `(1,1)`; do not tile or atlas it.
3. Route every visible light emitter named exactly
   `hospital garage ceiling light lens` to real night lights. The white lens
   mesh is not a substitute for illumination.
4. Remove or relocate the parent's placeholder columns at `(46,171)` and
   `(80,201)`, plus any old parking stripes under the two new cores. The first
   column blocks the bridge/lift-core path; the second conflicts with the
   planned dropoff circulation.
5. Cut the parent P2/P3 slabs around the west stair well before claiming the
   authored steps traversable. The current full-width parent slabs overlap the
   stair volume even though this sidecar adds no slab or ramp blocker.
6. Rebuild the parent's current monolithic skybridge as floor, side walls,
   glazing, and roof before claiming the new landing and portal are usable.
7. Validate the Bellweather approach, the Sixth exit turn, all three stair
   doors, the bridge route, and the 2.75 m bars with the actual player vehicle
   and on-foot controller.

The generated control texture is original project art. It does not copy GTA V
assets, branding, locations, or trade dress.
