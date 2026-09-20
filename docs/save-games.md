# Saved games

The title menu offers **Continue** when a valid local save exists, plus a separate **New Game**. Pause offers **Save Game** and **Load Game**. Failed loads leave the current game intact and show an error; a successful load resumes play. New Game keeps the previous slot until a new checkpoint is successfully saved.

The single slot lives in SDL's per-user `Bakerheit/Probable Cause` preferences folder (`checkpoint.save`), outside game assets. `--save-file PATH` selects an isolated slot for QA. Saving writes a unique temporary in the same directory, flushes it, and atomically replaces the slot. Unsupported versions, mismatched world identity, damaged checksums, truncated/oversized data, invalid models and non-finite/out-of-range values are rejected before gameplay changes.

A checkpoint restores Johnny's location, facing, camera look and on-foot/car mode; the current car's model, position, orientation, health, dents, fuel/oil reserves and engine failure; driving style; session seed, simulation clock and mission stage. The authored O'Haven and Florangia world identity remains fixed. Cars resume stationary with fresh suspension state. Saving waits for vehicle transitions or the opening scene to finish, and requires leaving aircraft/boats first.

## Car paint (save version 4)

Version 4 adds the car's paint, as three `GameSave` fields written as the body's last row, `paint_base has_paint r g b`:

- `car_paint_base` is an index into the model's stock paint list — `kCar5Paints` and `kCar8Paints`, today function-local in `src/app/traffic_visual.cpp`, where index 0 is the stock body texture.
- `car_has_paint` says whether a respray sits on top of that base.
- `car_paint` is the picked sRGB colour, a `PaintColor` from `game/vehicle_paint.h`.

**Not wired yet.** Only the format has landed. `src/app/app_save.cpp` neither fills these fields when it saves nor repaints the car when it loads, and there is no respray to pick a colour with, so every checkpoint still writes `0 0 0 0 0` and comes back in stock paint.

**Versions only append rows, and old saves load as stock paint.** Version 2 added the trailer row, 3 the plate, 4 the paint; the decoder reads a row only from the version that introduced it. A v1–v3 save loads as base 0 with no respray, which is what every car wore before a respray existed. The cost is on the tests. `save_game_tests`, `license_plate_tests` and `tractor_trailer_tests` build old saves by cutting rows off a new one, so every version bump rewrites those cuts, and a cut that lands on the wrong row does not always fail. `license_plate_tests` splices corrupt plate rows onto a body with its plate row cut off. Had that splice kept cutting one row after version 4, it would have cut the paint row and left the real plate row in place. Each corrupt row would then be refused as trailing tokens rather than for its values, and those negatives would pass while checking nothing. So each cut now asserts the exact row it removed and shows the older header refusing the uncut body, and each splice first proves the good row loads.

**The base index is range-checked, not checked against the car.** The save accepts any base from 0 to 255 on any model. The paint lists belong to the app, and the save takes no new include to reach them. The cost: a save can name a base the model does not have, and whoever applies it on load has to handle that, because the save cannot.

**Every drivable car can carry paint, emergency vehicles included.** Nothing in the save depends on the model, and `save_game_tests` round-trips a respray on every player car id.

**An unpainted car stores colour `0 0 0`.** `validate_game_save` refuses `has_paint` 0 with any colour, on encode and on decode, so one car state has exactly one encoding. Black with `has_paint` 1 is a colour a player can pick, and it round-trips as painted. The cost: code that clears a respray must zero the colour too. If it doesn't, `store_game_save` refuses the next save with "Invalid saved paint.", which `App::save_game()` puts in its save notice, and the previous slot is kept.

## What is not saved

This is a mission/player checkpoint, not a complete simulation snapshot. Extra cars parked during the session are removed on load. Traffic, pedestrians, particles, weapons, bank interactions, aircraft/boats and unrelated world interactions are not serialized. Use the feature for mission progress and the current player/car, rather than replaying every world event.

`save_game_tests` covers file and memory roundtrips, persistent damage/fluids, overwrite, missing/corrupt/unsupported data, invalid values, preservation of an existing slot after a rejected write, and the v4 paint row: its range and consistency checks, and the stock-paint default for v1–v3. `license_plate_tests` and `tractor_trailer_tests` hold the v1–v3 migrations. `ui_flow_tests` covers Continue/New Game separation and keeping title/pause open while load success is pending.
