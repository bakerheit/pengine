# Cutscene Studio

A standalone native editor and viewer using Apricot's real city loader, roads, building geometry, textures, sky, ocean, cooked meshes, skeletons, animations, and lighting shaders. It does not run the gameplay loop. The game now reads the same opening scene for New Game; saving changes here updates the next game launch.

## Open it

On macOS, open `build/bin/Cutscene Studio.app`. The command-line executable is `build/bin/apricot_cutscene_lab`.

The default scene is `assets/cutscenes/johnny-opening.cutscene`: Johnny and the manager inside Halloway Gas discussing the lotto and a delivery. This is a 69.41-second voiced blocking draft, with eleven camera shots and nineteen recorded lines from GPT-Realtime-2.1. Johnny uses cedar and Lou uses ash. Playback WAVs and a complete listening reel are in `assets/audio/dialogue/johnny_lotto/take_01/`; the original recordings are retained. Lou walks behind the counter, picks up a taped box and passes it to Johnny while giving the Ostend delivery instructions. Facial and finger animation remain unfinished. The three new takes and complete listening reel are in `assets/audio/dialogue/johnny_lotto/delivery_01/`.

## Controls

- **Space:** play or pause. Playback follows the authored camera shots.
- **Tab:** switch between the editor and the clean viewer. **Escape** returns to the editor.
- **Home:** rewind. **Left / Right:** step one frame at 30 fps.
- Drag the timeline slider to scrub. Click a shot on the timeline or shot list to jump to it.
- Hold **right mouse** over the scene to fly the camera. **WASD** move, **Q/E** lower or raise it, and **Shift** moves faster.
- **Save** or **Cmd/Ctrl+S** saves the current project. Unsaved changes show an asterisk; closing asks whether to save them.

## Authoring

**Shots:** add or remove a shot, choose its duration, and capture the current camera as its start or end. “Hold still” uses the start view for both. A shot smoothly interpolates its eye, look target and lens; cuts happen at shot boundaries. “Exact camera positions” exposes coordinates.

**Cast:** choose a character or prop from the game asset folder, then choose its texture and animation. Set its height, position and facing. “Keep actor here” makes its end transform match its start. Expand “Movement” for a timed start-to-end position and facing change. A separate walking clip blends in along moving action-key segments. **Action keys** store times, positions, facing, reach weight and world-space hand targets; use Preview to scrub to a key, or edit/add/delete it. These keys take precedence over simple start/end movement. “Keep actor here” clears keys and holds the current position. Hand targets use the existing two-bone arm solver, preserving limb lengths. Character animation loops independently; root translation is stripped so the authored position remains authoritative.

**Conversation gestures** in Cast adds timed Explain, Dismiss, Shrug, Me/volunteer, Nod, and Glance beats. Set the start, length and amount, then use Preview to inspect a beat. Gestures blend into the idle pose and affect hands, head and upper-body posture while feet stay planted. Walking and box contacts take priority. The opening includes twenty dialogue-timed beats, with more expressive motion for Johnny and smaller reactions for Lou.

**Dialogue:** edit speakers, subtitles, timing, and optional WAV paths. Existing voice auditions appear in the voice picker. Empty audio paths produce subtitles only. Playback seeks into an active WAV after scrubbing or resuming. Dialogue is a single audio track; the last active cue in the list wins if cues overlap. Files larger than 16 MB are not loaded for preview.

**Project:** edit the title, time of day and save path. Reload reads the saved project and asks before discarding unsaved edits. Saves contain asset references and scene directions, not duplicated game assets. Raw imported vehicle body meshes still require their separate wheels and other parts to be staged if needed.

The asset catalog is scanned when the editor starts. Restart after adding new files. The city geometry is compiled shared game data; rebuilding the tool picks up map changes. Terrain streams around the camera. Ambient gameplay traffic, police, missions and ambient pedestrians are not running in the editor.

## Build and capture

```sh
cmake -S . -B build
cmake --build build --target apricot_cutscene_lab cutscene_document_tests -j 4
ctest --test-dir build -R '^cutscene_document_tests$' --output-on-failure
build/bin/apricot_cutscene_lab --viewer --time 20 --frames 8 --screenshot build/cutscene.png
```

Use `--scene FILE` to open another cutscene. `--create-default --scene NEW_FILE` writes the current starter scene only when that file does not exist. `--play` begins playback immediately. `--export-frames DIR` renders the complete clean viewer at 30 fps to numbered PNG files; it does not export audio or encode a video. Large sequences consume substantial disk space. `--frames N` can limit a capture run.

The scene format is versioned text (v3 adds conversation gestures; v2 adds actor action keys and walking clips; v1 and v2 files still load), with bounded counts and validation on load/save. Incomplete or invalid input does not replace the open document. Saves are written through a temporary file. The earlier exterior draft is kept in `assets/cutscenes/archive/`.

## Checks performed

The focused headless suite covers shot boundaries, camera interpolation, shortest-path actor rotation, stationary/instant movement, quoted-text round trips, save/load, and preserving the last good document after malformed input or an invalid save.

The native viewer has been visually checked at the gas station, including actor textures and skinning, subtitle overlays, editor and clean-view modes. Play/pause, returning from clean view, and one-frame stepping were exercised through the running app. Final performance quality and speech-to-animation timing still require authored performances.

The delivery pass additionally checks full-grip wrist contact, the route around the checkout, finite poses, repeatable backward scrubbing, and v1/v2/v3 document compatibility. Run `cutscene_action_tests` with the private character pack present for these rig checks. Gesture checks also cover finite rotations, planted feet, visible hand travel, envelope endpoints and box-contact priority. The original recordings are retained and every authored dialogue window contains its complete WAV.



The parcel is now a single 42 x 30 x 10 cm cuboid (12 triangles) with an image-generated diffuse atlas, `models/props/delivery_box/cardboard_generated.png`. Lou lifts it, rotates it outward in front of his body, then passes it to Johnny. The action test now checks a torso clearance envelope through the entire turn, alongside wrist contact and the route around the counter.

Current casting: every Lou line uses Ash from `audio/dialogue/johnny_lotto/lou_ash_04/balanced/`. Direction is a lightly raspy working-class voice, relaxed and dryly amused, with clear everyday speech. No pitch, speed or pause edits are applied; only constant volume matching. Johnny keeps the original Cedar recordings. The current listening reel and timing manifest are in `lou_ash_04/`. Previous Ballad takes remain available.

The game and Studio share `game/cutscene_document.h`, `app/cutscene_pose.h` and `app/cutscene_assets.h`. Studio compatibility headers remain under `tools/`. See [In-game opening](game-opening.md) for playback controls and checkpoint behavior.
