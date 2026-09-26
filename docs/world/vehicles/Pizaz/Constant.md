# Pizaz Constant

![Pizaz Constant — selected concept](../images/pizaz-constant-concept.png)

*Selected concept — generated design reference.*

[Vehicles](../README.md) · [Pizaz](../Pizaz.md)

Calm, front-biased early-90s sedan.

**Class 3 — design and detail benchmark.** User-designated reference for bringing Class 1 and Class 2 vehicles up to the same standard of design, model detail, documentation and reference imagery. See [vehicle quality classes](../quality-classes.md).

## Current interior and game model

September 26, 2026 repair: a sealed floor, front firewall, rear bulkhead and inner
sills close the views into the road and wheel cavities. The cabin now includes
analog instruments, cassette radio, heater controls, vents, a glovebox, carpet,
cloth seat inserts, a rear bench, belts, pedals, an automatic shifter and detailed
inner doors. These are inferred 1991 interior details. The approved exterior
references remain the design source.

![Constant — open driver door and finished cabin](../images/pizaz-constant-interior-cockpit.png)

![Constant — dashboard and center controls](../images/pizaz-constant-interior-inside.png)

![Constant — driver fit](../images/pizaz-constant-interior-occupied.png)

![Constant — passenger door open](../images/pizaz-constant-interior-passenger.png)

![Constant — in-game road check](../images/pizaz-constant-interior-game.png)

Painted window frames and rubber seals meet edge-to-edge. Lower fenders end at
the bumper corners. Wheel rims use an open ring with separated recesses, spokes
and hub layers. These remove the overlapping surfaces found during the repair.

See the [before/after review and moving captures](../../../design/reviews/pizaz-constant-interior/README.md)
for checks and their limits.

## Earlier game image

![Pizaz Constant — game screenshot from the local model work](../images/pizaz-constant.png)

*Game screenshot from the local model work.*

Saved project imagery; model renders and game captures may predate the latest refinements.

Image origins are recorded in the [source manifest](../images/sources.json).

## Model information

| Detail | Value |
|---|---|
| Manufacturer | [Pizaz](../Pizaz.md) |
| Model | Constant |
| Quality class | **Class 3** — user-designated benchmark |
| Status | Playable in the vehicle catalog; interior and surface repair verified in the production renderer |
| Vehicle role | calm, front-biased early-90s sedan |
| In-game mass | 1,510 kg |
| Authored wheelbase | 2.760 m |
| Authored track | 1.560 m |
| Tire radius | 0.320 m |

Dimensions and mass describe the game model and handling setup. Prices, production figures and unrecorded history remain undefined.

## Class 3 benchmark

Use this vehicle's selected concept, directional references, model renders and documentation together when refining another vehicle. Match the care given to proportions, body surfaces, panel seams, trim, lamps, wheels, transparent glass, cabin and visible inner door surfaces. Preserve the vehicle being upgraded's own manufacturer identity, era and body style.

The Class 3 designation recognizes the design, detail and supporting documentation approved by the user. Runtime integration and verification retain their own status in the model information and review records.

## Reference photos

Generated design references: front, rear, one side and top, plus the selected three-quarter concept above. No separate opposite-side or rear three-quarter concept was found.

### Constant reference — Front

![Constant reference — Front](../images/pizaz-constant-reference-front.png)

### Constant reference — Rear

![Constant reference — Rear](../images/pizaz-constant-reference-rear.png)

### Constant reference — Side

![Constant reference — Side](../images/pizaz-constant-reference-side.png)

### Constant reference — Top

![Constant reference — Top](../images/pizaz-constant-reference-top.png)

## Model angles

Updated September 26, 2026 from the editable source with the repaired interior. These studio renders use source materials; the runtime captures above show the game atlas.

### Constant model — Front three-quarter

![Constant model — Front three-quarter](../images/pizaz-constant-model-three-quarter.png)

### Constant model — Rear three-quarter

![Constant model — Rear three-quarter](../images/pizaz-constant-model-rear-three-quarter.png)

### Constant model — Front

![Constant model — Front](../images/pizaz-constant-model-front.png)

### Constant model — Rear

![Constant model — Rear](../images/pizaz-constant-model-rear.png)

### Constant model — Side

![Constant model — Side](../images/pizaz-constant-model-side.png)

### Constant model — Top

![Constant model — Top](../images/pizaz-constant-model-top.png)

### Constant model — Doors open

![Constant model — Doors open](../images/pizaz-constant-model-doors-open.png)

## Performance

| Metric | Value |
|---|---|
| Top speed | 146.1 mph |
| 0–60 mph | 4.9 s |

Measured in the headless game-physics benchmark using **Classic GTA**, the default driving preset, on flat dry ground. Values describe the current gameplay tuning. See [conditions, source snapshot and full roster](../performance.md).

## Game files

- Model key: `pizaz_constant`.
- Body: `assets/models/vehicles/pizaz_constant/body.emesh`.
- Texture: `assets/textures/vehicles/pizaz_constant/body.png`.
- Selection: **F1 → Vehicle → Choose Car → Pizaz → Constant**.

Sources: [vehicle catalog](../../../../src/app/player_car_catalog.h), [handling setup](../../../../src/app/vehicle_model_tuning.h).
