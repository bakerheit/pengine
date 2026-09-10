# Recorded traffic horns

These game sound effects are recordings by **Universfield**, downloaded from
Pixabay on 2026-09-09. They remain under the
[Pixabay Content License](https://pixabay.com/service/license-summary/), separate
from the project's code license. Use them as integrated game effects; the
license does not permit distributing or selling the sounds on a standalone basis.

| Runtime file | Recording | Native sample rate | Runtime duration |
| --- | --- | --- | --- |
| `runtime/horn_short.wav` | [Car Horn 02](https://pixabay.com/sound-effects/city-car-horn-02-153260/) | 44,100 Hz | 1.229 s |
| `runtime/horn_double.wav` | [Double Car Honk](https://pixabay.com/sound-effects/film-special-effects-double-car-honk-352443/) | 48,000 Hz | 0.546 s |
| `runtime/horn_truck.wav` | [Truck Horn](https://pixabay.com/sound-effects/city-truck-horn-153263/) | 44,100 Hz | 0.814 s |

The downloads' identities and creator credits were checked on each source page.
The original media URLs, source SHA-256 hashes, source probes, exact cut times,
applied gains and runtime hashes are recorded in `runtime/manifest.json`.
`RUNTIME_SHA256SUMS` checks the shipped WAVs.

## Preparation

`tools/prepare_traffic_horns.py` decodes the original MP3s at their native rates,
folds stereo to mono for spatial playback, trims idle lead/tail, matches RMS to
0.21 with a 0.80 peak ceiling, and applies 5 ms attack / 25 ms release fades.
The output is PCM16. The double horn's two beeps remain in the same recording.
Nothing is synthesized, looped, stretched or pitch-shifted in the asset files.
The mixer applies a small stable pitch variation per vehicle during playback.

From the repository root, with the three MP3s named as in the preparation script:

```sh
python3 tools/prepare_traffic_horns.py build/traffic-horn-qa/source assets/audio/vehicles/traffic/runtime
```

Keep downloaded originals in scratch storage. Missing runtime recordings stay
silent; there is no generated horn fallback.
