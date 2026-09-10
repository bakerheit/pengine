# Pistol audio

`Glock17_Shoot_004.wav` is copied byte-for-byte from the original local
Probable Cause checkout, `assets/audio/Glock17_Shoot_004.wav`.
That game selects this take in `src/audio/audio_engine.cpp` and plays it
as a non-positional one-shot. Five takes (001 through 005) exist there;
004 is the production selection. The clip is 3.3 seconds of stereo
32-bit float PCM at 44,100 Hz. Apricot loads it through `override_clip_from_wav`
and retains a generated fallback if the recording is unavailable.

No Glock reload recording was found in the original asset tree. Apricot's
short generated magazine-release sound remains in use.
