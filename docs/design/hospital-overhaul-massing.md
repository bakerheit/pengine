# Hospital overhaul massing handoff

`bake_hospital_overhaul_massing()` is the replacement four-story clinical
shell defined by `hospital-overhaul-master.md`. All geometry is local to
`kHospitalSite`; this layer creates no roads, props, fitted texture receivers,
or surrounding-site work.

## Built form

- North public/diagnostic bar: `x[-15,204]`, `z[-8,34]`.
- West inpatient bar: `x[-15,45]`, `z[34,138]`.
- East surgery/ED bar: `x[144,204]`, `z[34,138]`.
- South support bar: `x[45,144]`, `z[100,138]`.
- Central clinical spine: `x[45,144]`, `z[60,78]`.
- North court stays open at `x[45,144]`, `z[34,60]`.
- South court stays open at `x[45,144]`, `z[78,100]`.

The five footprints each receive four solid floor/support slabs and a separate
roof slab. Collision-bearing walls are baked from `BuildingWall` records.
Department-specific grouped glazing, projected floor bands, court-facing
glazing, parapets, two grouped plant screens, and distinct warm/concrete/teal
material zones keep the complex from reading as repeated block boxes.

## Real openings

The wall bake leaves open, leaf-free collision gaps for:

- the 12 m main lobby portal on the north-lot walking axis at `x=0`;
- north diagnostic and ED walk-in doors;
- the Bellweather secondary public entrance;
- the Juniper trauma and service-receiving doors;
- the south loading door;
- paired doors on all four sides of each healing-court loop;
- the garage ground walk and upper bridge landing.

The garage connection crosses the seam at `x=45`, so matching openings are cut
through both the southwest inpatient wall and south support wall. Together they
clear the existing bridge width around the `x=46` axis instead of landing on a
solid corner.

## Roof landmarks

The clinical spine carries a steel-framed glass clerestory above the occupied
roof. A raised `42 x 30 m` helipad, support plinth, H marking, and access
vestibule sit on the east surgery/ED bar at local `(174,52)`. Both are
non-occupied roof structures; the hospital remains four stories.

## Key authored counts

- 5 connected clinical bars/volumes.
- 4 occupied floors.
- 20 solid occupied floor/support slabs.
- 5 solid roof slabs.
- 2 fully open daylight courts.
- 26 named circulation portal cuts, including the two-part ground-walk and
  two-part skybridge cuts at the garage seam.
- 12 collision-bearing roof parapet runs.
- 1 central clerestory and 1 east-bar helipad assembly.
- 0 new texture assets or texture receiver planes.

Parent integration should replace the legacy nine-wing clinical stream with
this bake, then combine it with the separately owned logistics, mobility,
public-realm, garage, and north-parking layers. Runtime/world collision, map,
lighting, and visual QA remain parent-owned.
