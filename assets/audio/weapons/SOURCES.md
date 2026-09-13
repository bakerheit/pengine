# Weapon audio sources

`Glock17_Shoot_004.wav` is documented in [`README.md`](README.md) beside it; it
came out of the original Probable Cause checkout rather than from a pack.

## Molotov

Three user-supplied MP3s, cooked by `tools/prepare_molotov_audio.py` into
48 kHz **mono** 16-bit WAVs under `runtime/`. Mono because all three play
spatially — at the bottle's impact point, or at the fire's burning centroid —
and the mixer derives their pan from that position, so a stereo take would be
two sources pretending to be one.

- Supplied to the build on 2026-09-12.
- Per-file source and runtime hashes, cut points and gains: `runtime/manifest.json`
- Runtime hashes alone: `RUNTIME_SHA256SUMS`

| Runtime file | Source MP3 | Source SHA-256 |
| --- | --- | --- |
| `runtime/molotov_glass.wav` | `dragon-studio-glass-breaking-504033.mp3` | `b5af69b5…cbca55e2` |
| `runtime/molotov_whoosh.wav` | `djartmusic-short-fire-whoosh_1-317280.mp3` | `42de6e27…5099a1b4` |
| `runtime/fire_loop.wav` | `maxhammarback-fire-sound-efftect-21991.mp3` | `7e49bcb4…1647deb8` |

> **LICENCE AND ATTRIBUTION ARE NOT RECORDED, AND THAT IS A GAP, NOT AN
> OMISSION.** The three files arrived as bare MP3s with no licence text and no
> metadata. Their filenames follow the Pixabay download convention —
> `<uploader>-<title>-<id>.mp3` — which points at uploaders `Dragon-Studio`,
> `DjArtMusic` and `maxhammarback` and at sound ids 504033, 317280 and 21991,
> but a filename is an inference and a licence is what this file is for.
> Before these ship in a build that leaves this machine, confirm the pages and
> fill the rows in.
>
> - Creators: **unconfirmed — filenames suggest Dragon-Studio, DjArtMusic, maxhammarback**
> - Source pages: **unconfirmed — to be supplied**
> - Licence: **unconfirmed — to be supplied**

### What each cook does, and the one that mattered

Run:

```sh
python3 tools/prepare_molotov_audio.py \
  ~/Downloads/dragon-studio-glass-breaking-504033.mp3 \
  ~/Downloads/djartmusic-short-fire-whoosh_1-317280.mp3 \
  ~/Downloads/maxhammarback-fire-sound-efftect-21991.mp3 \
  assets/audio/weapons/runtime
```

**`molotov_glass.wav`** — 2.05 s, from source 0.09–2.14 s, peak 0.80.
The take opens with 90 ms of room before the break, and the shatter has to be
sample zero of a sound triggered on an impact. The tail is kept long on
purpose: the shards settling at 1.3 s and again at 1.9 s are what make the
first second of the fire sound like broken glass rather than a generic bang.
The source decodes to a peak of 1.48 — over full scale — so it is scaled down
rather than clipped.

**`molotov_whoosh.wav`** — 1.45 s, from source 0.98–2.43 s, peak 0.72.
**This is the one that had to be trimmed and it was not a small trim.** The
supplied file is 5.04 s, of which the first 0.565 s is digital silence and the
next 0.3 s sits under -40 dBFS, which is inaudible. Played raw, the ignition
you can actually hear would arrive nearly a second after the bottle broke, and
pre-roll never reads as an audio offset — it reads as the game being late. Cut
at the -30 dB crossing, the clip opens on an audible swell and peaks 0.45 s in,
which is the fuel taking hold and is matched by the fire's visual bloom. The
last 2.6 s of the source is room tone and digital silence and is dropped.

**`fire_loop.wav`** — 6.0 s seamless loop, from source 2.0–8.0 s, peak 0.62.
The source is fifteen seconds of continuous crackle: a bed, not a one-shot. Six
seconds of it is cut and the following second is equal-power crossfaded back
over its head, so playback wraps with no click and no level dip. Equal power
rather than linear because crackle is decorrelated noise and a linear pair sums
to -3 dB through the middle of the seam, which reads as a dropout every wrap.
Same construction as `light_rain_loop.wav`; see
[`../world/SOURCES.md`](../world/SOURCES.md).

No compression, no pitch change, no added effects anywhere in the three.

### How the runtime plays them

All three are **recorded only, with no synthesised fallback**, which is the
same call `SfxBank::footsteps` and `SfxBank::city_ambience` make: a generated
pane of glass is a burst of noise and a generated fire is a hiss, and the ear
knows. A missing file leaves the clip empty and the mixer treats an empty clip
as silence, so the molotov still works and simply does not sound.

- The glass and the whoosh are spatial one-shots at the impact point, fired in
  that order from `App::break_molotov`.
- The fire loop is **one** spatial looping voice for the whole `FireField`,
  opened when the first cell lights, moved to the burning centroid and gained
  by total burn every step, and closed when the last cell goes out. One voice
  rather than one per cell: sixty-four copies of the same crackle would spend
  twice the mixer's loop budget and phase against each other.

## The flames themselves

The fire the molotov leaves is a sprite atlas, not audio, but it arrived from
the same place and carries the same gap, so it is recorded here rather than
in a file of its own: `assets/textures/effects/fire_sheet.png` is cooked by
`tools/cook_fire_sprites.py` from a supplied `Fire Spritesheet.zip`.

- Supplied to the build on 2026-09-12.
- Source PNG SHA-256: `1f575985…bb91d9908`, 3072 x 2816, 12 x 11 cells of 256 px
- Cooked to 1536 x 1408 (128 px cells), 132 frames, SHA-256 `143514ee…ddad97fd`

> **LICENCE AND ATTRIBUTION ARE NOT RECORDED HERE EITHER.** The archive held
> one PNG, no licence text and no metadata. Fill this in before it ships.
>
> - Creator: **unconfirmed — to be supplied**
> - Source page: **unconfirmed — to be supplied**
> - Licence: **unconfirmed — to be supplied**
