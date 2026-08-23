# src/audio — the one split module

Every other module in this engine belongs entirely to `apricot_sim` or entirely
to `apricot_host`. This one is split down the middle, and the split is the
whole point.

| File | Target | Why |
|---|---|---|
| `mixer.h` | `apricot_sim` | gain routing — header-only, device-free |
| `synth.h` / `synth.cpp` | `apricot_sim` | PCM generation — pure maths |
| `device.h` / `device.cpp` | `apricot_host` | the playback device |
| `miniaudio_impl.c` | `apricot_host` | the backend's single implementation TU |

Gain maths is exactly the arithmetic that goes wrong silently, and waveform
generation is exactly the code you want to assert exact sample values from.
Both belong where a headless test can reach them with no sound hardware in
sight. Only the last two files need a device, so only the last two live in the
host library.

## Three things that will bite you here

**The purity guard tracks this module by an explicit file list.**
`tools/guard_sim_purity.sh` scans whole directories for the other sim modules,
but it cannot do that here — half of this directory is host code. So it carries
`SIM_FILES` naming `mixer.h`, `synth.h` and `synth.cpp` individually, and it
**fails if one of them is missing**. If you rename or split a sim-side file
here, update `SIM_FILES` in the same commit. Otherwise the guard quietly stops
checking it, which is how a guard rots into decoration.

That also means the usual rule applies to the sim-side files and not the host
ones: `mixer.h`, `synth.h` and `synth.cpp` may not name the backend library,
**not even in a comment**. The guard is a plain text search. Say "the host
layer".

**`miniaudio_impl.c` exists so ~90k lines of third-party C compile exactly
once**, and so the whole file can be built with warnings off (see the
`COMPILE_OPTIONS "-w"` in `CMakeLists.txt`) without relaxing anything on our
own code. It defines `MA_NO_ENCODING`, `MA_NO_DECODING` and `MA_NO_GENERATION`,
because apricot synthesises every sound it plays — file IO here would be dead
weight and dead surface. **This TU must never end up in `apricot_sim`.**

**The playback callback runs on an OS-owned audio thread.** Anything it reads
must be safe to read there: no allocation, no locks, no logging. That
constraint is why the mixer is a flat struct of floats rather than anything
with a container in it.

## Status

- `mixer.h` — real and complete.
- `synth_tone()` — real and complete. It is the reference the other generators
  are checked against; the head/tail fade is not optional, a clip without one
  clicks on every playback.
- `synth_engine_loop()` — **placeholder.** Returns a bare `synth_tone()`, which
  sounds like a test signal and nothing like an engine. Ticket: engine audio.
- `AudioDevice::start()` — **not implemented.** It logs and returns `false`
  rather than half-opening a device. Callers must treat `false` as "no audio
  this session" and carry on; audio is never load-bearing for the app starting.
  The `ma_version_string()` call in there is not decoration — it forces a real
  symbol reference so the link against `miniaudio_impl.c` is proven by the build
  rather than assumed by whoever writes the real implementation. Ticket: audio
  device.
- Nothing in `src/app/` wires audio up yet.

Design rules and their costs: [`docs/architecture.md`](../../docs/architecture.md).
