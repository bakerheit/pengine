# World audio sources

## City ambience

`city_ambience.wav` is adapted from **City ambience** by Pixabay user `KM007_`:

- Source: https://pixabay.com/sound-effects/city-city-ambience-9272/
- License: Pixabay Content License
- Retrieved: 2026-09-01
- Original MP3 SHA-256: `f9f91767b223e1fe513e0723b0802e6d8a79518daf958d8da01827a2cbc8cae4`

The user-supplied 44.1 kHz stereo MP3 was converted to 48 kHz stereo 16-bit
PCM. To make a stable 33.86-second loop, the output begins at source second 3,
runs through second 33.85875, then crossfades the source's final three seconds
into its first three seconds. No synthesis, pitch shift, compression or added
effects are used.

## Menu and in-game rain

`light_rain_loop.wav` is adapted from **LIGHT RAIN** by Pixabay user **Liecio**.

- Source: https://pixabay.com/sound-effects/nature-light-rain-109591/
- Working direct download: https://cdn.pixabay.com/download/audio/2022/04/16/audio_520eb6a5cc.mp3?filename=liecio-light-rain-109591.mp3
- License: Pixabay Content License, https://pixabay.com/service/license-summary/
- Retrieved: 2026-09-05
- Original MP3 SHA-256: `cd93fb3b707033aa844866b67abbc65c774fb4216105480dbc6114a0a5a968f8`
- Runtime WAV SHA-256: `b0cc598e01863a20482e9a6291a964d66930783630ba91684bf7cab7b72f8a88`

Converted the full 104.359-second 44.1 kHz stereo MP3 to 48 kHz stereo 16-bit
PCM. A one-second equal-power tail-to-head crossfade produces a 103.359-second
loop; playback begins at source second 1 and wraps back there after the seam.
No gain boost, compression, pitch change, or added sounds.

The menu and gameplay share this PCM through `SfxBank::rain`. Menu gain is
0.35 beneath the title score; gameplay gain follows rain intensity and fades
to silence in clear weather. The title rain file under `audio/intro/` is an
older generated stem and is no longer loaded by the game.

Reproduce with FFmpeg:

```sh
ffmpeg -i liecio-light-rain-109591.mp3 -filter_complex \
  '[0:a]aresample=48000,asplit=3[body][tail][head];[body]atrim=start=1:end=103.358276644,asetpts=PTS-STARTPTS[b];[tail]atrim=start=103.358276644,asetpts=PTS-STARTPTS[t];[head]atrim=end=1,asetpts=PTS-STARTPTS[h];[t][h]acrossfade=d=1:c1=qsin:c2=qsin[seam];[b][seam]concat=n=2:v=0:a=1[out]' \
  -map '[out]' -c:a pcm_s16le light_rain_loop.wav
```
