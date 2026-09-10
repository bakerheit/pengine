# Pinatty Regional Hospital overhaul: public realm

## Scope

`src/city/hospital_overhaul_public_realm.h` implements:

```cpp
inline std::vector<StartPart> bake_hospital_overhaul_public_realm();
```

The integrated sidecar contains 393 `StartPart` records in `kHospitalSite`-local
coordinates. It is matched to the ring-and-spine shell in
`hospital-overhaul-master.md`; it does not recreate shell walls, floors,
garage pieces, the north visitor lot, road ribbons, or access logic. It reuses
the current `StartFinish` palette and fitted hospital art. No texture asset was
created or copied for this pass.

## North civic frontage

The public facade follows the replacement north bar at local `z = -8`:

- public/lobby zone: `x[-15,45]`, warm masonry and teal identity fins;
- diagnostic zone: `x[45,144]`, steel spandrels and a measured window rhythm;
- emergency zone: `x[144,204]`, red departmental blades and roofline accent.

Four floor bands, independent projecting sunshades, vertical mullions, base
courses, and grouped roof louvers break the 219 m frontage into readable
departments. The roof screens top out at `16.61 m`; they screen plant and do
not read as a fifth occupied floor.

The main lobby axis is fixed at local `x = 0`, matching the existing protected
pedestrian spine and crosswalk from the north visitor lot. The mobility layer's
single porte cochere reaches from the north wall to the weather edge; this
public-realm layer adds two full-height identity fins outside the clear
approach. The glazed vestibule has two door lines, each with a `3.2 m` open
centre gap, and a
supported floor skin. The facade lights sit below the first sunshade so the
fixtures do not clip the horizontal shading system.

## Fitted art receivers

Each existing generated composition has one purpose-built receiver and occurs
exactly once in this sidecar:

| Exact part name | Site-local placement | Receiver size |
| --- | ---: | ---: |
| `hospital main entry mural face` | `(20.0, -8.36)` | `9.0 x 6.0 m`, north face |
| `hospital northwest healing art glass face` | `(-15.29, 17.0)` | `6.0 x 4.0 m`, west return |
| `hospital grounds healing mosaic face` | `(122.0, 34.20)` | `3.6 x 2.4 m`, north-court wall |

The receivers remain thin, non-solid planes with matching steel backings.
Parent material routing can keep using normalized `(1,1)` fitted UVs. None of
the images is tiled, mirrored, cropped, or placed on a generic facade bay.

## Healing courts

Both courts retain continuous perimeter loops, but their interior paths are
not copies of each other.

### North healing court

The `x[45,144]`, `z[34,60]` court uses a broad perimeter loop and a four-leg
meander through alternating low beds. It includes three benches, two varied
trees, a wheelchair-height horticultural therapy table, a shallow water rill,
and the fitted botanical mosaic. The solid planting and furniture stay in
side bays outside the meander and loop. Three separated slot drains serve the
south low edge without becoming trip colliders.

### South rehabilitation court

The `x[45,144]`, `z[78,100]` court uses a tighter perimeter loop and a distinct
four-leg zig-zag. Its southwest therapy bay contains full-length parallel
bars; six flush cadence pads and a separate rest rail add a second therapy
sequence. Two benches, two trees, four varied beds, and three south-edge slot
drains finish the space without obstructing the accessible through-route.

The court-facing facades continue the exterior mullion, sunshade, and identity
fin language. These thin fixtures hug the shell faces or sit above head height;
they are not structural court walls.

## Perimeter planting and staff respite

Four shallow foundation beds soften the north diagnostic frontage but stop at
`x = 128`, well before the ED zone. Three narrow west-edge beds remain inside
the clinical parcel and leave the perimeter walk clear. No planting, bench,
tree, or path-light collider is placed east of `x = 144` south of the north
bar, preserving Juniper-side ambulance and southeast service sight lines.

The staff respite patio occupies the quiet southwest gap at local
`(13,151)`. It stays west of the `x = 46` garage walking axis and clear of the
southeast service court. A small open shade structure, two benches, two tree
beds, four path lights, and a flush drain make it usable without duplicating
the garage or skybridge systems.

## Collision and lighting contract

- Canopy frames, planter curbs, tree trunks, bench members, therapy rails,
  roof screens, and shade columns are solid only where their visible geometry
  exists.
- Foliage, fitted art faces, paving skins, drains, water, light lenses, facade
  trim, and high sunshades are non-solid.
- Paving pieces depend on the parent hospital ground/support geometry; this
  sidecar does not add broad invisible support boxes.
- Twelve non-solid emitters are named exactly
  `hospital facade wall light lens`.
- Eighteen non-solid emitters are named exactly
  `hospital grounds path light lens`.
- The parent must keep both names connected to real runtime lights; emissive
  tint alone is not night illumination.

## Parent integration and QA

Append `bake_hospital_overhaul_public_realm()` after the replacement shell and
before collision/GPU upload. Do not append the superseded
`bake_hospital_exterior_facade()` or `bake_hospital_exterior_grounds()` streams,
or their old receivers and fixtures will duplicate this overhaul.

The sidecar was checked directly with Clang in C++17 mode. A compiled contract
probe reported:

```text
parts=393
hospital main entry mural face=1
hospital northwest healing art glass face=1
hospital grounds healing mosaic face=1
hospital facade wall light lens=12
hospital grounds path light lens=18
invalid=0
east_operational_solids=0
```

Parent integration completed the focused hospital geometry/map tests and the
day/night runtime pass. The north-lot crossing-to-vestibule alignment,
unobstructed court loops, three one-shot art mappings, ED/service sight lines,
and real dusk coverage from both named light sets were checked in the
integrated campus.
