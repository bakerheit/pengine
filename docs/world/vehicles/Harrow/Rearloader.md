# Harrow Rearloader

![Harrow Rearloader — working concept](../../../design/references/harrow_rearloader/concept-working.png)

*Generated design concept used for this model.*

[Vehicles](../README.md) · [Harrow](../Harrow.md)

A 1991 municipal rear-loading refuse truck with a cream cab and forest-green collection body.

**Class 3 — reviewed 2026-09-26.** Compared with the [Hookline](Hookline.md) truck-detail benchmark: rounded connected panels, mapped inner surfaces, transparent glazing, detailed working equipment and complete reference/model galleries. This is an individual model review; the user-designated benchmarks remain unchanged.

## Model information

| Detail | Value |
|---|---|
| Manufacturer / model | Harrow / Rearloader |
| Era | 1991 |
| Role | Municipal refuse collection |
| State | Playable vehicle; references, editable source, cooked assets and runtime review complete |
| Quality | Class 3; individually reviewed against Hookline |
| Nominal length / body width | 7.60 / 2.40 m |
| Wheelbase / track | 3.80 / 2.00 m |
| Tire radius | 0.60 m |
| Axles | Two; single front tires and dual rear tires |
| Playable integration | HARROW → REARLOADER; fitted driver/door, single front and dual rear wheels, lamps, plates, paint and heavy-truck handling |
| Refuse collection / packer gameplay | Not implemented |

## Design and construction

Flat-front cab-over layout with rounded stamped cab corners, a near-vertical split windshield and rectangular lamps. The refuse body has broad pressed side panels, three major side ribs and restrained curved upper shoulders. An open rear hopper shows its worn inner trough and packer, with outboard hydraulic rams, hoses, pivot brackets, grab rails and worker platforms.

The cab has separate transparent glazing, a complete mapped interior and a solid driver door with textured inner card. Wheels and glass are separate runtime parts. [Full reference decisions and shape contract](../../../design/references/harrow_rearloader/README.md).

## Reference photos

Generated design references. All seven views were saved and reviewed before modeling. Small generated-view disagreements are resolved in the reference contract.

### Front three-quarter

![Front three-quarter](../../../design/references/harrow_rearloader/front-three-quarter.png)

### Rear three-quarter

![Rear three-quarter](../../../design/references/harrow_rearloader/rear-three-quarter.png)

### Front

![Front](../../../design/references/harrow_rearloader/front.png)

### Rear

![Rear](../../../design/references/harrow_rearloader/rear.png)

### Left

![Left](../../../design/references/harrow_rearloader/left.png)

### Right

![Right](../../../design/references/harrow_rearloader/right.png)

### Top

![Top](../../../design/references/harrow_rearloader/top.png)

## Performance

| Metric | Value |
|---|---|
| Top speed | **69.2 mph** |
| 0–60 mph | **21.4 s** |

Measured with the production game physics: **Classic GTA**, flat dry Rock ground, unladen 10,500 kg truck, automatic gearbox, full throttle from rest at 120 Hz. The final five-second mean settled after 50 seconds. These are game figures. [Raw result](../../../design/reviews/harrow-rearloader/performance.csv) · [Source snapshot](../../../design/reviews/harrow-rearloader/performance-snapshot.json) · [Fleet conditions](../performance.md).

## Model review

The following are actual Blender renders from the saved editable model. [Full comparison, counts, audits and runtime checks](../../../design/reviews/harrow-rearloader/README.md).

### Front three-quarter

![Authored front three-quarter](../../../design/reviews/harrow-rearloader/front-three-quarter.png)

### Rear three-quarter

![Authored rear three-quarter](../../../design/reviews/harrow-rearloader/rear-three-quarter.png)

### Front / rear

![Authored front](../../../design/reviews/harrow-rearloader/front.png)

![Authored rear](../../../design/reviews/harrow-rearloader/rear.png)

### Both sides

![Authored driver side](../../../design/reviews/harrow-rearloader/left.png)

![Authored passenger side](../../../design/reviews/harrow-rearloader/right.png)

### Top

![Authored top](../../../design/reviews/harrow-rearloader/top.png)

### Door and cabin

![Door partly open](../../../design/reviews/harrow-rearloader/door-partial.png)

![Door open](../../../design/reviews/harrow-rearloader/door-open.png)

![Door interior](../../../design/reviews/harrow-rearloader/door-inside.png)

![Cabin](../../../design/reviews/harrow-rearloader/cabin.png)

## Current playable model

Captured in the game and its production-renderer inspection tool. The 650-frame entry/exit regression passed, including blocked paths, cancellation and re-entry. Steering captures use opposite full locks at the physical suspension limits. [Recorded checks](../../../design/reviews/harrow-rearloader/runtime-checks.json).

![In game](../../../design/reviews/harrow-rearloader/game.png)

![Runtime front](../../../design/reviews/harrow-rearloader/runtime-front.png)

![Runtime rear](../../../design/reviews/harrow-rearloader/runtime-rear.png)

![Runtime side](../../../design/reviews/harrow-rearloader/runtime-side.png)

![Runtime open door and cab](../../../design/reviews/harrow-rearloader/runtime-door-open.png)

![Runtime inner surfaces](../../../design/reviews/harrow-rearloader/runtime-inside.png)

![Full lock, compressed suspension](../../../design/reviews/harrow-rearloader/runtime-steer-compressed.png)

![Opposite lock, extended suspension](../../../design/reviews/harrow-rearloader/runtime-steer-extended.png)

The cabin and hidden chassis details are inferred from the design. The truck is available in the vehicle menu; it is not an ambient refuse-service vehicle. Packer and hydraulic equipment are static; refuse collection and worker gameplay are not implemented.

## Asset files

- Model key: `harrow_rearloader`; select **HARROW → REARLOADER** in the vehicle menu.
- Source: `assets/models/vehicles/harrow_rearloader/source.blend` (private/ignored).
- Cooked body, open body, driver door, four pane categories and front/rear wheels: `assets/models/vehicles/harrow_rearloader/`.
- Atlas: `assets/textures/vehicles/harrow_rearloader/body.png`.
- Generator: [`make_harrow_rearloader_assets.py`](../../../../tools/make_harrow_rearloader_assets.py).
- Shape/anchor contract: [`harrow_rearloader_spec.py`](../../../../tools/harrow_rearloader_spec.py).
- Paint profile: `tools/paint_profiles/harrow_rearloader.json`; resprays affect the green body while preserving the cream cab.
