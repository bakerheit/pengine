# In-game opening

Choose **New Game** to play the 69.41-second Halloway Gas scene. The game loads `assets/cutscenes/johnny-opening.cutscene` and its existing character, texture, animation and voice assets. Camera shots, the twenty conversation gestures, Lou's walk and the parcel handoff are the same authored pass used by Cutscene Studio.

**P / controller Start** pauses or resumes the scene. **Escape / controller B** skips it. Gameplay simulation and normal character rendering stay paused during the scene. Dialogue stops on pause and resumes at the current cue offset; skip clears it. Subtitles remain visible while paused. Missing required scene assets return to the title with an error instead of marking the opening complete.

Finishing or skipping returns control to Johnny at his final position inside the store. His Legacy Car 5 is nose-in in the parking space directly outside the entrance. A floating **Enter your car** cue and the minimap point to it; entering that specific car clears the cue and activates the Ostend delivery destination. The checkpoint written after the opening preserves this get-in-car step, while old v1 saves remain compatible. Continue loads without replaying the opening. Devon waits inside Ostend Bait & Tackle. Approaching him on foot shows the package handoff prompt; completing it clears the destination marker and saves the completed mission state. A walking carry animation and reward are not part of this integration.

New Game resets the player, current car, bank, doors and ambient session objects. The existing save is retained until the new opening finishes or the player explicitly saves. There is one save slot; see [Saved games](save-games.md).

On macOS, `build/bin/Probable Cause.app` is refreshed when the game is rebuilt. This development app uses the checkout's asset tree.

## Verification

`cutscene_document_tests`, `cutscene_action_tests`, `cutscene_playback_tests`, `delivery_mission_tests`, `save_game_tests` and `ui_flow_tests` cover format compatibility, real-rig gestures and box contact, pause/completion/skip, the Devon handoff, corrupt-file rejection, roundtrips and menu state. The complete opening was run through the game for 2,200 bounded frames with a clean GL queue and a screenshot after control returned.

Native UI checks used an isolated temporary save: New Game, pause and skip, autosave, manual Save, teleport to the motel, Load back to the gas station, corrupt-file rejection, and Continue without replay. No production save was used for those checks.

```sh
cmake --build build --target apricot apricot_cutscene_lab -j4
ctest --test-dir build -R '^(cutscene_(document|action|playback)|ui_flow|save_game)_tests$' --output-on-failure
build/bin/apricot --opening-preview --frames 2200 --screenshot build/opening-handoff.bmp
```

`--opening-preview` starts the real opening directly for QA. Bounded runs suppress opening autosave. `--save-file FILE` selects an isolated slot for native save/load testing; its parent directory must already exist. Ordinary `--frames` runs still bypass title/cutscene to preserve the existing gameplay smoke path.
