# Player car audio sources

The six engine loops below are retained for provenance and possible future
experiments, but are no longer connected to normal gameplay. The live player
car mix is recorded-only and uses the Pixabay throttle, ignition, collision,
and tyre recordings documented below.

## Live throttle recording

`runtime/throttle_attack.wav`, `throttle_hold.wav`, and `throttle_release.wav`
come from **Car Throttle Static** by TanwerAman:

- Source: https://pixabay.com/sound-effects/car-throttle-static-337873/
- License: Pixabay Content License
- Retrieved: 2026-09-01
- Original MP3 SHA-256: `872e4588c8002778d5b7bada01685c7b050b610ff369647e9ab1d7e640188c14`

The 11.49-second recording contains one throttle rise, a stable held section,
and one release. Attack is source 0.55-3.35 s; the original hold is 3.65-8.65
s; release is 8.85-10.90 s. Those cuts preserve stereo, convert to 48 kHz /
16-bit PCM, and apply the same -7.8 dB gain correction.

The live held-throttle layer is `throttle_hold.wav`, the five-second stable
middle (source 3.65–8.65 s). At load time Apricot folds its final 80 ms into
the head, so long full-throttle runs repeat only the steady engine note rather
than reaching the source recording's release and quiet tail.

`engine_loop.wav` is Andrew Baker's Paudio export of the full supplied source,
made on 2026-09-02 with the Car Engine Loop recipe: mono, 48 kHz, 16-bit PCM,
-3 dBFS peak normalization, and an 80 ms loop crossfade. It is retained for
comparison only: the full source contains the rise and release, so it is not a
valid held-throttle loop. Exact output hashes are in `RUNTIME_SHA256SUMS`.

## Live engine warning

`runtime/engine_warning.wav` is converted from **Car Warning Sound 2** by
Pixabay user **Liecio**:

- Source: https://pixabay.com/sound-effects/film-special-effects-car-warning-sound-2-189737/
- Media: https://cdn.pixabay.com/download/audio/2024/02/05/audio_8443ea6605.mp3
- License: Pixabay Content License
- Retrieved: 2026-09-04, at the user's request
- Source MP3 SHA-256: `3150c1c4d28122d7e203c8e351e0d24a5e1ee697fe8959b1eff51a3595a2034a`

The source is a 4.049-second, 44.1 kHz stereo MP3. Apricot removes its 25 ms
leading silence, converts it to mono 48 kHz / 16-bit PCM, and adds only a 5 ms
entrance fade and 174 ms exit fade. The resulting warning is 4.024 seconds.
It replaces the synthesized dashboard ding for coolant, oil, and fuel-leak
warnings; the synth remains a fallback if the recorded asset is unavailable.
The exact converted WAV hash is in `RUNTIME_SHA256SUMS`.

## Recorded ignition and idle

`runtime/engine_start.wav` and `runtime/engine_idle.wav` are derived from
**Car Engine Start**, credited to **tbsounddesigns (Freesound)** on Pixabay:

- Source: https://pixabay.com/sound-effects/technology-car-engine-start-44357/
- Media: https://cdn.pixabay.com/audio/2022/03/10/audio_f5ade9481f.mp3
- Page license: Pixabay Content License
- Retrieved: 2026-09-03, at the user's request
- Source MP3 SHA-256: `1862ee51faa8cdbd87ba9bd91a5da77cb05d1db58a5943898cba9557571d1c93`

`tools/prepare_engine_idle.py` decodes mono 48 kHz PCM, removes DC, detects
onset and settling using 50 ms RMS/spectral windows, selects a stable
three-second bed, finds a correlated end, and folds a 120 ms cosine overlap.
There is no synthesized engine layer. Startup uses source 0.125–3.050 s;
idle uses 3.950–7.051 s and becomes a 2.981 s seamless loop. Both use the same
1.362012 gain; startup also has 8 ms/150 ms edge fades. The adjacent
`runtime/engine_idle_analysis.json` records the exact hashes and measurements.

Reproduce from the downloaded MP3:

```sh
python3 tools/prepare_engine_idle.py /path/to/car-engine-start-44357.mp3 build/engine-idle-analysis
```

That also writes a listening preview with startup followed by three wraps.
Only the two runtime WAVs and analysis report ship; the preview is build-only.

## Live car collision recording

`runtime/car_collision.wav` is converted from **Sound Effect - Car Crash** by
Pixabay user `u_mgq59j5ayf`:

- Source: https://pixabay.com/sound-effects/sound-effect-car-crash-394903/
- License: Pixabay Content License
- Retrieved: 2026-09-01
- Original MP3 SHA-256: `d028500d0c5c90b9c5b11f87a7d928db7141d751cdec6fa1cf72e3e7d1c881df`

The user-supplied 22.05 kHz mono MP3 was converted to 48 kHz stereo 16-bit PCM.
Its original level and 1.62-second duration are preserved, with only a 5 ms
entrance fade and 80 ms exit fade added to keep the one-shot edges clean.
The exact output hash is in `RUNTIME_SHA256SUMS`.

## Live handbrake and drift tyre recording

`runtime/tyre_screech_loop.wav` is converted from **Car brake** by Pixabay user
`MagiaZ`:

- Source: https://pixabay.com/sound-effects/film-special-effects-car-brake-325519/
- Media: https://cdn.pixabay.com/audio/2025/04/10/audio_aec5ce03a0.mp3
- License: Pixabay Content License
- Retrieved: 2026-09-04, at the user's request
- Original MP3 SHA-256: `08792590435455aeef80224785131d95f6b7e6e18f0a69130c8fdbf30349d4fc`

The source is a 5.042-second, 44.1 kHz mono MP3. Apricot converts it to mono
48 kHz / 16-bit PCM for the spatial player tyre loop. At load time the bank
folds a 160 ms tail over the head so a sustained drift can keep running without
repeating the short jolt from the previous 1.536-second clip. Runtime gain comes
from handbrake pull at speed or rear-tyre slip, so a slow parking input stays
quiet while a moving handbrake turn or drift squeals. The exact converted WAV
hash is in `RUNTIME_SHA256SUMS`.

`engine_0.wav` through `engine_5.wav` are the six files from **racing car
engine sound loops** by OpenGameArt user `domasx2`:

- Source page: https://opengameart.org/content/racing-car-engine-sound-loops
- License: CC0 1.0 Universal
- License text: https://creativecommons.org/publicdomain/zero/1.0/
- Retrieved: 2026-09-01
- Source note: the author says these were remade from a public-domain recording
  after a provenance concern was raised in the page comments.

The files are stored byte-for-byte as downloaded. At load time Apricot folds a
45 ms tail over each head and removes that overlap. It also retunes each WAV's
sample-rate metadata from its measured dominant periodicity to the RPM anchor
it replaces. The fold makes the seam continuous and the calibration keeps the
engine pitch proportional to RPM without changing the checked-in source files.

Measured dominant periodicities are `42.86`, `60.08`, `64.76`, `69.89`,
`73.01`, and `76.30 Hz`, respectively. `player_car_assets.cpp` stores their
four-cylinder-equivalent RPM values (`Hz * 30`) beside the path table. The
asset regression test measures the loaded result back against every target
anchor with a 2% error ceiling.

| Runtime file | Original URL | SHA-256 |
|---|---|---|
| `engine_0.wav` | https://opengameart.org/sites/default/files/loop_0.wav | `69d74b106509037ce547429e4c3f9ae906b0fffe88d0bb8b3b5a8b0cfc239048` |
| `engine_1.wav` | https://opengameart.org/sites/default/files/loop_1_0.wav | `0bc6bf7e1ad33d347ed805b4f2b55d1f79e38f892006f98acbe85a3177ff77a2` |
| `engine_2.wav` | https://opengameart.org/sites/default/files/loop_2_0.wav | `e26bef6f1804670aba02b6b9a8c5485cf05a6a1e902a5a50c098230ace49967f` |
| `engine_3.wav` | https://opengameart.org/sites/default/files/loop_3_0.wav | `015aee5cd2dec394b28ff0994360c780cfbe29c3cddfb16dba8df81b4a68eea2` |
| `engine_4.wav` | https://opengameart.org/sites/default/files/loop_4_0.wav | `fadf4420104d9273e6cc0a1c4583d863937af382382866251fc2b8dc005f9369` |
| `engine_5.wav` | https://opengameart.org/sites/default/files/loop_5_0.wav | `e0c65476764931770e46308dec744a196d8ecf996f8c7675f125805d28609b34` |

Credit is not required by CC0. This record stays with the files so a public
build can still prove where they came from and what was changed at runtime.

## F1 car sound auditions

Twenty files under `auditions/` are cropped from real-world recordings on
Freesound. Each individual source page was checked on 2026-09-01 and displayed
**Creative Commons 0** with this license text:
https://creativecommons.org/publicdomain/zero/1.0/

For those 20, Apricot downloaded Freesound's public HQ MP3 preview, selected the
useful recorded event, converted it to mono 48 kHz / 16-bit PCM, normalized it
to roughly -18 LUFS, and added only 20 ms / 80 ms edge fades. There is no
synthesis, pitch shifting, filtering, reverb, or layering in those files.

`accelerate_0.wav` through `accelerate_4.wav` are Andrew Baker's supplied
`car-default-acceleration-1.wav` through `-5.wav`, copied byte-for-byte on
2026-09-01. They remain stereo 44.1 kHz / 24-bit PCM and remain available in
the F1 sound lab; normal driving now uses the three-part runtime recording.
Exact output hashes are in `AUDITIONS_SHA256SUMS`.

| Runtime file | Menu label | Freesound source | Creator | Source crop |
|---|---|---|---|---|
| `accelerate_0.wav` | Gear 1 | User-supplied `car-default-acceleration-1.wav` | User supplied | Copied byte-for-byte |
| `accelerate_1.wav` | Gear 2 | User-supplied `car-default-acceleration-2.wav` | User supplied | Copied byte-for-byte |
| `accelerate_2.wav` | Gear 3 | User-supplied `car-default-acceleration-3.wav` | User supplied | Copied byte-for-byte |
| `accelerate_3.wav` | Gear 4 | User-supplied `car-default-acceleration-4.wav` | User supplied | Copied byte-for-byte |
| `accelerate_4.wav` | Gear 5 / 6 | User-supplied `car-default-acceleration-5.wav` | User supplied | Copied byte-for-byte |
| `brake_0.wav` | Winter Tyres | https://freesound.org/people/Jupo_22/sounds/839068/ | Jupo_22 | 2.0-5.0 s |
| `brake_1.wav` | Heavy Brake | https://freesound.org/people/IlpoTervam%C3%A4ki/sounds/715806/ | IlpoTervamäki | 3.5-6.5 s |
| `brake_2.wav` | Brake Run | https://freesound.org/people/milkotz/sounds/613739/ | milkotz | 13.0-16.0 s |
| `brake_3.wav` | Handbrake | https://freesound.org/people/launemax/sounds/250008/ | launemax | 2.5-5.5 s |
| `brake_4.wav` | Gravel Stop | https://freesound.org/people/kyles/sounds/637161/ | kyles | full 1.126 s |
| `crash_0.wav` | Glass Crash | https://freesound.org/people/magnuswaker/sounds/592388/ | magnuswaker | 0.0-2.5 s |
| `crash_1.wav` | Fast Hit | https://freesound.org/people/qubodup/sounds/332056/ | qubodup | full 0.308 s |
| `crash_2.wav` | Body Hit | https://freesound.org/people/qubodup/sounds/332058/ | qubodup | full 0.9 s |
| `crash_3.wav` | Car Crash | https://freesound.org/people/squareal/sounds/237375/ | squareal | 0.0-3.0 s |
| `crash_4.wav` | Squeal + Crash | https://freesound.org/people/HoBoTrails/sounds/426021/ | HoBoTrails | 0.0-3.0 s |
| `tyres_0.wav` | Chrysler Squeal 1 | https://freesound.org/people/audible-edge/sounds/71737/ | audible-edge | 4.0-7.0 s |
| `tyres_1.wav` | Chrysler Squeal 2 | https://freesound.org/people/audible-edge/sounds/71738/ | audible-edge | 6.0-9.0 s |
| `tyres_2.wav` | Chrysler Squeal 3 | https://freesound.org/people/audible-edge/sounds/71739/ | audible-edge | 10.5-13.5 s |
| `tyres_3.wav` | Volvo Turn | https://freesound.org/people/audible-edge/sounds/76805/ | audible-edge | 1.5-4.5 s |
| `tyres_4.wav` | Maxima Burnout | https://freesound.org/people/audible-edge/sounds/71740/ | audible-edge | 3.0-6.0 s |
| `surface_0.wav` | Asphalt | https://freesound.org/people/felix.blume/sounds/685074/ | felix.blume | 88.0-91.0 s |
| `surface_1.wav` | Gravel | https://freesound.org/people/OBXJohn/sounds/251661/ | OBXJohn | 2.0-5.0 s |
| `surface_2.wav` | Dirt | https://freesound.org/people/kyles/sounds/637192/ | kyles | 26.0-29.0 s |
| `surface_3.wav` | Wet Road | https://freesound.org/people/Breviceps/sounds/462862/ | Breviceps | 1.5-4.5 s |
| `surface_4.wav` | Cobblestone | https://freesound.org/people/orlandorizo/sounds/591078/ | orlandorizo | 31.5-34.5 s |

## Burnout loop

`runtime/burnout_loop.wav`: audible-edge (Tom Haigh), “Nissan Maxima burnout (04-25-2009).wav”.
Original: https://freesound.org/people/audible-edge/sounds/71740/ (CC0).
Downloaded the public HQ MP3 preview at https://cdn.freesound.org/previews/71/71740_995351-hq.mp3 on 2026-09-04 because the Pixabay download failed.
Cut exactly 2.5–5.5 seconds, converted to mono 48 kHz 16-bit PCM. Runtime folds a 40 ms tail to soften the repeat seam.
