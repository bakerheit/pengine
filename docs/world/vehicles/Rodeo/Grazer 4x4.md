# Rodeo Grazer 4x4

[Vehicles](../README.md) · [Rodeo](../Rodeo.md)

Compact four-wheel-drive pickup.

## Images

![Rodeo Grazer 4x4 — game screenshot](../images/rodeo-grazer.png)

*Game screenshot.*

Saved project imagery; model renders and game captures may predate the latest refinements.

Image origins are recorded in the [source manifest](../images/sources.json).

## Model information

| Detail | Value |
|---|---|
| Manufacturer | [Rodeo](../Rodeo.md) |
| Model | Grazer 4x4 |
| Status | Selectable in the game catalog |
| Vehicle role | compact four-wheel-drive pickup |
| In-game mass | 1,580 kg |
| Authored wheelbase | 2.930 m |
| Authored track | 1.640 m |
| Tire radius | 0.405 m |

Dimensions and mass describe the game model and handling setup. Prices, production figures and unrecorded history remain undefined.

## Asset record

[Detailed model notes and prior validation](../../../assets/rodeo-grazer-emblem.md).

## Plow variant

Come the first real snow, a lot of Grazers stop being grocery-store trucks.
The **Grazer 4x4 Plow** (`rodeo_grazer_plow`) is the contractor's version: a
7'6" red straight blade on a black A-frame and headgear with its own pair of
plow lamps, and an amber service bar on the cab roof. Small outfits hang them
off the Grazer because it is short enough to turn in a gas-station forecourt
and cheap enough to park all summer. In Pinatty the Grazer and the
[Harrow Workman](../Harrow/Workman.md) plow trucks clear the forecourts and
restaurant lots while the municipal plows keep the streets. See
[Snowplows](../../../snowplows.md) for how the blade and lot crews work.

## Additional model views

Saved renders and captures from earlier model work; these are not generated concept references or a complete all-angle set.

### Front three-quarter

![Front three-quarter](../images/rodeo-grazer-model-front.png)

### Side

![Side](../images/rodeo-grazer-model-side.png)

### Rear three-quarter

![Rear three-quarter](../images/rodeo-grazer-model-rear.png)

## Performance

| Metric | Value |
|---|---|
| Top speed | 166.6 mph |
| 0–60 mph | 4.2 s |

Measured in the headless game-physics benchmark using **Classic GTA**, the default driving preset, on flat dry ground. Values describe the current gameplay tuning. See [conditions, source snapshot and full roster](../performance.md).

## Game files

- Model key: `rodeo_grazer`.
- Body: `assets/models/vehicles/rodeo_grazer/body.emesh`.
- Texture: `assets/textures/vehicles/rodeo_grazer/body.png`.
- Selection: **F1 → Vehicle → Choose Car → Rodeo → Grazer 4x4**.

Sources: [vehicle catalog](../../../../src/app/player_car_catalog.h), [handling setup](../../../../src/app/vehicle_model_tuning.h).
