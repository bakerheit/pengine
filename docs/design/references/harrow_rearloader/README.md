# Harrow Rearloader — reference contract

Original 1991 municipal rear-loading refuse truck. The working concept is a design anchor, not a user-approved selection. Generated with the built-in image generation tool on 2026-09-26. See the [completed Class 3 model review](../../reviews/harrow-rearloader/README.md) for the authored and playable result.

## Reference-first gate

The working concept and all seven separate angle images were generated, saved here and visually inspected **before modeling began**. The first concept incorrectly had a tandem rear axle; it was rejected. The saved concept corrects that to two axles with dual tires on the one rear axle.

## Controlling views and resolved differences

- `concept-working.png`: cream flat-front Harrow cab, forest-green refuse body, black chassis, steel wheels and overall identity.
- `left.png`: wheelbase, overhangs, cab/roof height, door seam, three large side ribs, single rear wheel opening.
- `right.png`: opposite-side inspection; keep three ribs and single rear axle. Minor tank and marker differences are allowable asymmetric equipment.
- `front.png`: split windscreen, softened rectangular window corners, narrow center mullion, rectangular lamps beside broad grille, amber roof beacons.
- `rear.png`: vertically stacked upper and lower rear lamp housings, open hopper, twin outboard hydraulic rams and worker platforms. The horizontal upper lamps in the rear three-quarter generation do not override this view.
- `top.png`: gently rounded body shoulders, cab width, roof ribs, hopper width. It exaggerates rear overhang slightly; side view controls length.
- `front-three-quarter.png` and `rear-three-quarter.png`: volumes, cab corner rounding and hopper construction. Do not copy isolated AI plumbing intersections; use coherent connected hoses and pivots.

These are generated design references, not historical photos or scanned geometry. The cabin interior and concealed chassis construction are inferred.

## Shape contract

Dimensions are design estimates normalized from the left reference's approximately 735 px wheelbase. Assigned 3.80 m wheelbase gives approximately 193 px/m; the selected 7.60 m overall length and 3.55 m body roof fit its silhouette. The reference's tire diameter reads around 1.20–1.30 m; the model uses 1.20 m.

| Feature | Contract |
|---|---|
| Runtime axes | +X driver-left; +Y up; +Z forward; metres |
| Overall nominal bounds | 7.60 m length; 2.40 m body width; 3.55 m body roof, mirrors and fittings additional |
| Front/rear axle Z | +2.27 / −1.53 m |
| Track / tire radius | 2.00 m / 0.60 m |
| Wheels | Four runtime nodes: two single front tires and two dual rear assemblies; six physical tires |
| Cab rear / front screen base | Z +1.48 / +3.53 m |
| Cab roof / belt | Y 3.12 / 2.04 m |
| Refuse body | Z +1.40 to −2.10 m; rear hopper to −3.70 m |
| Driver door | Z +1.65 to +3.22 m; arch cutout; solid mapped inner card; glass moves with door |
| Driver hinge | Runtime (+1.155, +2.04, +3.22) m |
| Seat / steering | Runtime (+0.58, +1.58, +2.16) / (+0.58, +2.09, +2.82) m |
| Step/exit reference | Approach Z +3.10 m; tread center Z +3.17 m, moved forward for steering clearance; runtime entry/exit regression passed |
| Finish | Rounded connected cab surfaces; matte tires; green pressed panels; worn hopper interior |
| Runtime budget | 65,000 opaque-body triangles maximum; separate wheel/pane budgets reported |
| Atlas | 256×256 RGBA, semantic regions, all visible reverse faces mapped |

The machine-readable contract is [`tools/harrow_rearloader_spec.py`](../../../../tools/harrow_rearloader_spec.py). Mechanical packer geometry is initially visual only; no refuse pickup or compactor gameplay is claimed.

## Prompt record

Built-in image generation, `product-mockup` concept: original realistic 1991 HARROW cab-over rear-loading municipal refuse truck, cream cab, forest-green ribbed body, two axles, single front tires and one rear axle with dual tires, split windshield, rectangular lamps, black bumper, stalk mirrors, transparent glass, rear hopper/packer, exposed hydraulic cylinders, worker steps, grab bars and warning lamps; neutral studio, no real brands or people.

Corrective edit: remove the extra tandem rear axle, retain exactly two near-side wheel circles, keep styling unchanged. Each final angle used that corrected image as an identity reference with the same two-axle constraint and requested one of: straight front, straight rear, left profile, right profile, overhead top, front-left three-quarter, rear-left three-quarter. Flat views request no camera tilt and minimal perspective. Source generation files remain in Codex's generated-image store; all final images are copied here.
