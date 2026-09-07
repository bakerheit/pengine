# Saved games

The title menu offers **Continue** when a valid local save exists, plus a separate **New Game**. Pause offers **Save Game** and **Load Game**. Failed loads leave the current game intact and show an error; a successful load resumes play. New Game keeps the previous slot until a new checkpoint is successfully saved.

The single slot lives in SDL's per-user `Bakerheit/Probable Cause` preferences folder (`checkpoint.save`), outside game assets. `--save-file PATH` selects an isolated slot for QA. Saving writes a unique temporary in the same directory, flushes it, and atomically replaces the slot. Unsupported versions, mismatched world identity, damaged checksums, truncated/oversized data, invalid models and non-finite/out-of-range values are rejected before gameplay changes.

A checkpoint restores Johnny's location, facing, camera look and on-foot/car mode; the current car's model, position, orientation, health, dents, fuel/oil reserves and engine failure; driving style; session seed, simulation clock and mission stage. The authored O'Haven and Florangia world identity remains fixed. Cars resume stationary with fresh suspension state. Saving waits for vehicle transitions or the opening scene to finish, and requires leaving aircraft/boats first.

This is a mission/player checkpoint, not a complete simulation snapshot. Extra cars parked during the session are removed on load. Traffic, pedestrians, particles, weapons, bank interactions, aircraft/boats and unrelated world interactions are not serialized. Use the feature for mission progress and the current player/car, rather than replaying every world event.

`save_game_tests` covers file and memory roundtrips, persistent damage/fluids, overwrite, missing/corrupt/unsupported data, invalid values and preservation of an existing slot after a rejected write. `ui_flow_tests` covers Continue/New Game separation and keeping title/pause open while load success is pending.
