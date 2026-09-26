# Municipal Ambulance

[Vehicles](../README.md) · [Municipal](../Municipal.md)

Emergency van.

## Images

![Municipal Ambulance — model preview sheet](../images/municipal-ambulance.png)

*Model preview sheet.*

Saved project imagery; model renders and game captures may predate the latest refinements.

Image origins are recorded in the [source manifest](../images/sources.json).

## Model information

| Detail | Value |
|---|---|
| Manufacturer | [Municipal](../Municipal.md) |
| Model | Ambulance |
| Status | Selectable in the game catalog |
| Vehicle role | emergency van |
| In-game mass | 3,200 kg |
| Authored wheelbase | 3.360 m |
| Authored track | 1.960 m |
| Tire radius | 0.430 m |

Dimensions and mass describe the game model and handling setup. Prices, production figures and unrecorded history remain undefined.

Municipal is a fleet label for city service vehicles, not a manufacturer.

## Performance

| Metric | Value |
|---|---|
| Top speed | 163.9 mph |
| 0–60 mph | 4.9 s |

Measured in the headless game-physics benchmark using **Classic GTA**, the default driving preset, on flat dry ground. Values describe the current gameplay tuning. See [conditions, source snapshot and full roster](../performance.md).

## Game files

- Model key: `ambulance`.
- Body: `assets/models/vehicles/ambulance/body.emesh`.
- Texture: `assets/textures/vehicles/ambulance/body.png`.
- Selection: **F1 → Vehicle → Choose Car → Municipal → Ambulance**.

Sources: [vehicle catalog](../../../../src/app/player_car_catalog.h), [handling setup](../../../../src/app/vehicle_model_tuning.h).
