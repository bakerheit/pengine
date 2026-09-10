# Pinatty Regional Hospital exterior facade polish

## Scope

`src/city/hospital_exterior_facade.h` is an exterior-only sidecar for the
four-story hospital campus. It returns `StartPart` records in
`kHospitalSite` local coordinates. The parent should append those parts after
`bake_hospital_campus()` so the existing hospital shell remains authoritative.

The pass keeps Apricot's low-poly/PSX language while raising the authored
layering and believable function. It contains 179 purposeful pieces:

- three projecting front and rear spandrel layers on every wing;
- two front and two rear vertical mullions per wing;
- paired north-corner reveals and one high south sunshade per wing;
- eight visible facade light lenses on public approaches;
- one teal seam fin on each exposed connector face;
- two grouped, screened roof plant yards with air handlers and exhausts;
- a main-lobby stair lantern with a real eight-piece maintenance rail;
- two entrance fins, two canopy ribs, and one fitted healing-art panel.

## Placement and circulation

All facade pieces hug the existing wing or connector faces. Decorative strips,
mullions, shades, glazing, and high-level trim are non-solid. The main entrance
fins are outside the existing 7 m walk, at local `x = +/-5.25 m`, so they can
carry honest visible collision without narrowing the door route. Roof screens,
handlers, exhausts, the lantern cap, and the guard rail are solid because they
are visible physical obstacles on supported roof slabs.

The roof plant sits on the middle-east and south-east wings. Nothing is added
to the north-east helipad roof or its approach sector. Nothing projects into a
perimeter road, ambulance apron, loading path, court, or connector opening.

## Generated feature receiver

The generated texture belongs to exactly one named plane:

- piece: `hospital northwest healing art glass face`;
- location: west return of the north-west main-lobby wing;
- centre: site-local `(-27.08, -8.0)`;
- receiver size: `0.06 m` wide x `4.00 m` high x `6.00 m` deep;
- intended visible face: local west-facing side, exactly `6.00 x 4.00 m`;
- texture: `assets/textures/world/hospital/facade/polish/northwest-healing-art-glass-panel-generated.png`;
- mapping: full 1536 x 1024 RGB image once over normalized UVs, clamp edges,
  white tint, no repeat, crop, mirror, or atlas packing.

The separate `hospital northwest healing art panel backing` provides a dark
metal reveal. The image is deliberately a complete, non-seamless composition,
not a generic glass material.

## Parent integration notes

Append `bake_hospital_exterior_facade()` to the hospital site's part vector.
Route the exact receiver name above to the generated image and mark it as a
fitted one-shot texture. All other pieces can use their normal `StartFinish`
materials.

The canopy ribs, stair lantern, and roof rail are fixtures only. The parent
must add real runtime lights at every part named exactly
`hospital facade wall light lens`; this sidecar has no baked glow and does not
treat bright tint as lighting.

Validation still needed after integration: compile the parent target, inspect
all four perimeter approaches and the courts, walk the main entrance, check
roof/helipad clearance, and verify the receiver uses one uncropped UV image.
