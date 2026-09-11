# Impatient traffic horns

Press **H** while seated in a road vehicle to honk. Each press plays that
vehicle's recorded horn; keyboard repeat is ignored and a playing horn must
finish before another starts. Cars use the short or double horn, while trucks
use the heavy horn. It works with the engine off. Pause, vehicle exit, cutscene,
load and teleport stop the horn. Police siren/lights now use **J** or controller
**L3**, so honking does not toggle the siren. The in-game prompt shows both keys.

**J** does double duty by vehicle: in a Municipal cruiser it toggles the
siren and lightbar, and in the Vesper Mistral it raises or lowers the folding
canvas top ([`src/app/mistral_soft_top.h`](../src/app/mistral_soft_top.h)). No
car is both a cruiser and a convertible, so the two never contend for the key.
Like the siren, the top is presentation — it moves no dimension the physics
reads — so neither costs a replay-format change.

`build/bin/apricot --frames 1100 --convertible-check` presses J twice on the
real key path and captures the canvas latched up, mid-fold and stowed to
`build/convertible-check.*.bmp`. It fails if any of the three poses never
happens. The headless `mistral_soft_top_tests` covers the toggle and proves both
bows stay rigid and welded at their joint on a fitted, rotated body.

Nearby traffic now honks when a real obstruction holds it below 0.5 m/s long
enough. The controller reads `VehicleAgent::delay_seconds` after the live Crowd
step. It does not change routes, traffic right of way or the simulation RNG.

The first honk follows the driver's existing `honk_after` setting: 1.0 seconds
for aggressive, 1.8 for impatient, 3.0 for normal and 5.5 for cautious drivers,
plus a stable 0–0.65 second stagger. Continuing obstructions can produce another
honk after roughly 4.5–10.5 seconds, depending on personality and identity.
Moving again resets the continuous wait.

Red and yellow lights and incomplete mandatory stop-sign dwell suppress horns.
The wait starts fresh once the control permits travel. Police pursuing a player,
cruisers with their officer outside, and cars with failed engines stay quiet.

Each vehicle keeps the same recording and pitch across stream ordering changes.
Ordinary vehicles use one of two car recordings; box trucks, firetrucks and
snowplows use the truck recording. All three have source and license details in
`assets/audio/vehicles/traffic/SOURCES.md`.

Playback uses the World/SFX bus with stereo positioning, a 4 m reference range
and a 70 m cutoff. At most three horn voices can overlap, with at least 0.65 s
between starts across nearby traffic. Voices follow their cars and stop on
retirement or leaving range. Pause, map/menu, cutscene, load and teleport paths
clear the active horns and waiting state.

## Reproduce

```sh
cmake -S . -B build
cmake --build build --target apricot traffic_horn_audio_tests audio_synth_tests audio_vehicle_runtime_tests audio_city_runtime_tests vehicle_sound_volume_tests traffic_runtime_tests traffic_determinism_tests -j 8
ctest --test-dir build --output-on-failure -R '^(traffic_horn_audio_tests|audio_synth_tests|audio_vehicle_runtime_tests|audio_city_runtime_tests|vehicle_sound_volume_tests|traffic_runtime_tests|traffic_determinism_tests)$'
mkdir -p build/traffic-horn-qa
build/bin/traffic_horn_audio_tests build/traffic-horn-qa/horn-preview.wav
build/bin/apricot --traffic-horn-check --frames 6000 --seed 905 --save-file build/traffic-horn-qa/checkpoint.save --screenshot build/traffic-horn-qa/gameplay --log build/traffic-horn-qa/gameplay.log
```

The focused suite measures personality timing, repeated waits, legal-control
suppression, bounded voices, lifecycle cleanup, real Crowd frustration, and the
recordings' rendered stereo panning, distance falloff and SFX mute. Its optional
preview contains the three recordings played through the actual horn controller
and mixer, in short/double/truck order.

The gameplay check only stages the player car in front of an existing driver.
It first sends real H key events (including OS repeat) and verifies exactly one
horn. It also exercises J separately; pass `--player-car municipal_cruiser_91c`
to verify the siren toggle in a police car.
It selects an open stretch with enough room before the junction's actual stop
envelope, so a junction hold does not masquerade as a player obstruction.
It requires a working audio device and all three recordings, waits for the live
horn event, captures that state, clears the lane, and requires the driver to
resume above 2 m/s without another horn during the following four seconds.

## Verified on 2026-09-09

The player H binding passed `audio_vehicle_runtime_tests`,
`traffic_horn_audio_tests` and `police_siren_tests`. A live run in
`municipal_cruiser_91c` accepted one H press, ignored its OS repeats, and toggled
the siren independently with J. The traffic block/honk/release check also
passed, with no dropped audio commands or GL errors. Evidence is in
`build/player-horn-qa/tests.log` and `build/player-horn-qa/gameplay.log`.

- All three WAV probes and runtime SHA-256 checks passed: mono PCM16, finite,
  no clipping, RMS 0.210, native rates preserved.
- `apricot` built. All seven suites in the command above passed. The separate
  traffic collision failure seen earlier in the shared checkout was resolved
  by the concurrent traffic work before the final run.
- The seed-905 gameplay run selected driver `515396075520/3`. It played the
  short horn at step 1880 while stopped at 0.01 m/s, with a listener 9.25 m away.
  After the player cleared the lane, it accelerated to 16.70 m/s over four
  seconds with no additional horn. The 48 kHz audio device remained open,
  no mixer commands were dropped, and the GL error queue stayed clean.
- `build/traffic-horn-qa/gameplay-final.log` and the matching `.honk.png` /
  `.released.png` capture the successful run. Earlier staging on a large
  junction approach stayed blocked after release; the final check selects
  enough free road before the junction envelope. This is an audio and
  obstruction check, not a general junction-congestion or performance pass.
- `build/traffic-horn-qa/horn-preview.wav` contains the three controller/mixer
  outputs for listening. PCM levels and stereo behavior were measured;
  subjective listening quality is left for review with that preview.

## Horns at the player

Added 2026-09-11 (PENG-51). A driver the player cuts off or blocks at close
range honks at once, once: the player-hazard kernel's `honk` (binding gap
under 6 m) is latched on the car as a debounced one-shot on the sim clock
(`VehicleAgent::honk_player_fire`, 4 s interval), and
`traffic_horn_at_player()` plays it through the same voice budget and
0.65 s global spacing, outranking a frustration horn and ignoring signal
phase — a driver cut off at a red honks; a driver waiting at a red still
does not. Pursuing cruisers and dead engines never honk. A player horn sets
that driver's next-honk cooldown to 2 s. Pinned by
`traffic_horn_audio_tests` and, on the sim side, `civilian_maneuver_tests`.
