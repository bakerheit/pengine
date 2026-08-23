# src/audio

**Zero audio files on disk.** Every sound the game makes is PCM generated at
init. Nothing to ship, nothing to license, nothing to go missing on a fresh
clone. A WAV can be dropped in over any clip as an optional override, and its
absence is a normal state, not a failure.

---

## The split, and why this module is the awkward one

`src/audio/` is the **only module that spans both link targets**, so it is the
one place the sim/host boundary runs through the middle of a feature rather
than around it.

| File | Target | May reference |
|---|---|---|
| `mixer.h` | `apricot_sim` | glm. Nothing else, ever. |
| `synth.h` / `synth.cpp` | `apricot_sim` | glm, `core/`. Nothing else, ever. |
| `device.h` / `device.cpp` | `apricot_host` | the playback backend |
| `miniaudio_impl.c` | `apricot_host` | it *is* the backend |

The line is drawn so that **all PCM generation and all gain arithmetic sit on
the sim side**. That is not tidiness. Those are the two things that go wrong
silently — a category that ducks another, a loop that ticks, an engine note
that does not track the revs — and putting them in `apricot_sim` is what lets
`tests/audio_*` measure real waveforms on a runner with no sound card.

### The guard's sharp edge

`tools/guard_sim_purity.sh` lists the sim-side files **by name** and greps them
for `glad`, `SDL`, `<GL/`, `miniaudio`, `imgui`. It is a plain text search, so
**it trips on those words in comments too.** Inside `mixer.h`, `synth.h` and
`synth.cpp`, write **"the playback backend"** or **"the host layer"**.

If you rename or split a sim-side file here, update `SIM_FILES` in the guard in
the same commit. A guard that silently stops checking a file is decoration.

---

## What lives where

### `mixer.h` — routing, 3D, and the render loop

```
final gain = emitter_gain x category[c] x master
```

One category trim times one master. Deliberately shallow: deeper graphs are
where "why is this one sound quiet" stops being answerable. **Add a category
before you add a layer.**

`VoiceMixer` renders every voice into one interleaved stereo block: 32 one-shot
voices, 32 loop voices, a linear-interpolating resampler, a per-voice one-pole
low-pass for continuous brightness, constant-power pan, inverse-distance
attenuation, and a soft-knee limiter on the master bus.

### `synth.h` / `synth.cpp` — the generators

Two kinds of function, and the difference matters:

- **`synth_*()`** — offline. Called once at init, allocate freely, take as long
  as they like. They produce `PcmClip`.
- **`*_mix()`** — per-frame, pure, allocation-free, stateless. They turn "the
  car is at 4200 rpm on gravel" into which clip plays at what gain, pitch and
  brightness. The audio thread never calls these; the sim thread does.

### `device.cpp` — the only file that touches hardware

Opens the backend, hands `VoiceMixer::render()` a buffer, stays out of the way.

---

## The rules you can actually break

### 1. The audio thread has different rules

Everything reached from `data_callback` runs on a thread the OS owns, on a
deadline of a few milliseconds, with no way to report a problem. **No malloc,
no free, no mutex, no logging, no file IO, no exception**, and nothing that
might do one of those on your behalf. A callback that misses its deadline does
not run slow, it drops out — and a dropout is louder than the sound it
interrupted.

The whole discipline is discharged in one place: the sim thread only ever
pushes POD commands into a lock-free SPSC ring, and `render()` drains it and
then touches nothing but its own fixed arrays. **Keep it that way.** The first
`std::vector` resize added under `render()` ends it.

### 2. Voices hold bare pointers into the bank

`SfxBank` must outlive the mixer and must not be reallocated while the device
is running. Build it once in `start()`, keep it for the process. Dropping a WAV
in over a clip is safe **before `start()` or after `stop()`** — `ma_device_uninit`
joins the audio thread, so after `stop()` returns nothing is reading it.

### 3. Silence is a supported outcome, and it needs `set_silent`

A CI runner, a container, a machine with nothing plugged in: `start()` returns
`false` and everything else keeps working and goes nowhere. **No caller should
ever guard its audio code with `if (audio_ok)`.**

The non-obvious half is `VoiceMixer::set_silent()`. Without it, a machine with
no device has a sim thread cheerfully pushing an engine-note update every step
into a ring nothing drains; it fills in about half a second and
`dropped_commands()` then climbs forever. The counter that was supposed to be
the alarm becomes noise, and the first *real* overflow is invisible underneath
it.

### 4. Loop seams are exact by construction, not by crossfade

The engine tone is additive on a **half-crank grid**, and the buffer LENGTH is
snapped to a whole number of grid periods — not the frequency snapped to the
buffer. Rounding the length costs under 0.005% of pitch; rounding the frequency
to a 4 Hz bin would put idle 20% sharp. Every partial then completes whole
cycles, so the wrap is phase-continuous **at any playback rate**, which is what
lets the runtime slide pitch across the rev range without a tick every loop.

Aperiodic beds (noise, rain, wind, gravel) cannot do that, so they get a proper
head/tail fold: generate `n + fade` frames and crossfade the extra over the
head. The tempting in-place version — blending the tail toward the head — is
wrong in a way that still sort of works, which is worse: it makes the last
sample resemble the sample `fade` frames *in*, not the one that should precede
the first.

### 5. Load moves timbre. Load does not move pitch.

Pitch tracks rpm and nothing else. An engine whose note rises when you press
the pedal rather than when the revs rise reads as broken to anyone who has
driven a car, and it is an easy accident when load and rpm feed the same
synthesis. Pinned by `audio_engine_tone_tests`.

---

## Testing audio when nobody can hear it

**No test can listen.** "The buffer is non-empty" is worth nothing — a buffer of
NaN is non-empty. Every claim in `tests/audio_*` is a number, and
`tests/audio_analysis.h` holds the measurement tools.

Three of those tools exist because the obvious choice was **tried and measured
to be wrong**, which is worth knowing before you reach for it again:

- **`estimate_f0` is autocorrelation, not a harmonic comb.** A comb was tried
  first and octave-slipped in both directions on this material — 900 rpm read
  as 15 Hz, 5600 rpm as 46.7 Hz. An engine spectrum is dense enough that
  several candidate fundamentals genuinely explain it. Autocorrelation with a
  first-peak rule and parabolic interpolation locks every time.
- **`harmonic_ratio` samples ON the harmonic grid.** A log-spaced spectral
  centroid reported the overrun as *brighter* than full throttle — the opposite
  of the truth — because with partials tens of Hz apart, half its low-end probe
  points land in the gaps between harmonics and read zero. It was measuring its
  own grid spacing.
- **`off_lattice_ratio` detects transients.** Crest factor and short-time
  envelope variance both failed to separate the overrun crackle from the power
  stroke, whose own partials beat against each other enough to muddy both.
  A transient is aperiodic by definition and a harmonic stack cannot put energy
  between its own partials, so off-lattice energy is the crackle, directly. It
  separates them by 183x at worst.

Loop seams are judged against the buffer's **own 99.9th percentile** interior
step, not an absolute threshold: a bright hiss legitimately moves a long way
between samples, and a fixed threshold would either fail it or wave through a
genuine tick in a quiet bed.

---

## Not implemented yet

- **Device reopen after loss.** Unplugging an interface is detected (the
  backend's stopped-notification sets a flag, and `stop()` reports it), but the
  session stays silent from there. Reopening properly needs a per-frame poll
  from the app layer, which nothing calls yet. See the TODO in `device.cpp`.
- **Nothing calls this module.** `src/app/` does not yet construct an
  `AudioDevice`; wiring it to the vehicle and weather state belongs to whoever
  owns that integration.
