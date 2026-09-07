# Mission 1: We are square

Interacting with Devon on foot during the active delivery starts
`assets/cutscenes/mission1-delivery.cutscene`. It runs 18.03 seconds with five
shots, Johnny, Devon's existing civilian rig, the opening's flat parcel,
and four new recorded lines:

- Johnny: “Devon? Lou sent me. Got your package.”
- Devon: “did anyone follow you?”
- Johnny: “No. Why?”
- Devon: “Just checking. Tell Lou we're square.”

Johnny approaches carrying the parcel. Devon glances aside during his question,
then reaches for it after Johnny's reply. Both hold it before Johnny releases.
The final shot leaves Devon holding the delivery. Mission success, gameplay
controls, ambient/vehicle audio and autosave resume on natural completion or
skip. Progress stays active if the document, cast or referenced dialogue fails
to load. The parcel is a cinematic prop; ordinary gameplay does not render it
in Devon's hands after the scene. No new mission or reward amount is introduced.

Author with `python3 tools/stage_mission1_delivery.py`. This script owns only
this scene and its voice manifest/timing. It reads each available WAV's real
length and retimes the edit. Original voice takes are retained under
`assets/audio/dialogue/mission1_delivery/take_01/` and Devon’s replacement takes
under `devon_ash_02/`: Cedar for Johnny, Ash for
Devon, GPT-Realtime-2.1. All four transcripts match and have zero clipped samples.
The listening reel is `assets/audio/dialogue/mission1_delivery/mission1-delivery-reel.wav`.
Delivery quality still needs the user's listening review; waveform and transcript
checks do not establish acting quality. The rigs use skeletal acting, without lip sync.

```sh
build/bin/apricot_cutscene_lab --scene assets/cutscenes/mission1-delivery.cutscene --viewer
build/bin/apricot --delivery-preview --frames 640 --save-file /tmp/mission1-preview.json
build/bin/apricot --delivery-check --frames 900 --save-file /tmp/mission1-check.json
ctest --test-dir build -R '^(delivery_cutscene|delivery_mission|cutscene_document|cutscene_action|cutscene_playback)_tests$' --output-on-failure
```

The bounded runtime check drives actual SDL interaction, pause/resume and skip
inputs, verifies movement after skip, restarts the delivery and verifies natural
completion. Bounded runs never autosave. Pose checks sample both real rigs at
30 fps, check wrist reach, torso clearance, receiver grip before release, floor
placement and deterministic backward scrubbing. Studio captures cover the five
shots and handoff. The original opening also passes its existing pose suite and
a handoff visual check after the shared inward wrist orientation fix.

Devon was recast from Verse to Ash with simpler, conversational direction.
`voice_overrides.json` selects his two replacement takes; Johnny’s WAV files
are unchanged. The original scene is archived as
`assets/cutscenes/archive/mission1-delivery-before-devon-ash-v1.cutscene`.
