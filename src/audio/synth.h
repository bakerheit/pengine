#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace apricot {

// PCM generation. No device, no file loading, no streaming — this half of the
// audio module is pure maths that turns parameters into samples, which is why
// it lives in the sim library and is testable with no sound hardware.
//
// apricot has no audio assets. Every sound is synthesised at load, so there is
// nothing to ship, nothing to license and nothing to go missing on a fresh
// clone.
//
// TWO KINDS OF FUNCTION LIVE HERE and the difference matters:
//
//   synth_*()   OFFLINE generators. Called once at init, allocate freely, take
//               as long as they like. They produce PcmClip.
//   *_mix()     PER-FRAME parameter maps. Pure, allocation-free, no state:
//               they turn "the car is at 4200 rpm on gravel" into which clip
//               plays at what gain/pitch/brightness. The audio thread never
//               calls these; the sim thread does, and hands the result to the
//               voice mixer. Keeping them pure is what lets a headless test
//               assert the whole gain curve with no device open.

inline constexpr uint32_t kDefaultSampleRate = 48000;

struct PcmClip {
    uint32_t sample_rate = kDefaultSampleRate;
    uint16_t channels = 1;

    // Interleaved when channels > 1. Normalised to roughly [-1, 1]; the mixer
    // applies gain, so generators should not pre-attenuate.
    std::vector<float> samples;

    std::size_t frame_count() const {
        return channels ? samples.size() / channels : 0;
    }
    float duration_seconds() const;
    bool empty() const { return samples.empty(); }
};

// ---------------------------------------------------------------------------
//  Primitives
// ---------------------------------------------------------------------------

// A windowed sine. Real and complete — it is the reference the other
// generators are checked against, and it is what the tests can assert exact
// sample values from.
//
// `fade_seconds` ramps the head and tail. Without it a clip starts and ends on
// a non-zero sample and every playback begins with an audible click, which is
// the single most common synthesised-audio bug.
PcmClip synth_tone(float hz, float seconds, float fade_seconds = 0.005f,
                   uint32_t sample_rate = kDefaultSampleRate);

// ---------------------------------------------------------------------------
//  Engine
// ---------------------------------------------------------------------------

// Firing frequency of a four-stroke, in Hz: every cylinder fires once every
// TWO crank revolutions, so f = (rpm / 60) * (cylinders / 2).
//
// Exported rather than buried in the .cpp because the RPM-tracking test must
// assert against this number, and a test that re-derives the formula it is
// checking proves only that someone typed the same thing twice.
constexpr float engine_firing_hz(float rpm, int cylinders = 4) {
    return rpm * static_cast<float>(cylinders) / 120.0f;
}

// A seamlessly loopable engine note.
//
//   `rpm`      crank speed. Sets the pitch of the whole harmonic stack.
//   `load`     +1 full throttle, 0 coasting, -1 trailing throttle / overrun.
//              Drives timbre and level, NOT pitch: the ear reads a bike-like
//              rasp opening up as "on the power" entirely from harmonic
//              content, and pitch that moves with the pedal reads as broken.
//   `seconds`  requested loop length. The ACTUAL length is snapped down to an
//              even whole number of crank revolutions (see below), so ask for
//              what you want and read frame_count() for what you got.
//
// SEAMLESS BY CONSTRUCTION, not by crossfade. Every partial in the stack is an
// integer multiple of half the crank frequency, so if the buffer holds an even
// number of crank revolutions every one of them lands back on phase zero at
// the loop seam. That also survives resampling: play the buffer at any rate
// and the wrap is still phase-continuous, which is the whole reason the
// runtime can slide pitch across a rev range without a tick every loop.
PcmClip synth_engine_tone(float rpm, float load, float seconds,
                          uint32_t sample_rate = kDefaultSampleRate,
                          int cylinders = 4);

// Crank-frequency form of the above, kept because it is the older and more
// primitive spelling: `hz` is crank revolutions per second, i.e. rpm / 60.
// Full load.
PcmClip synth_engine_loop(float hz, float seconds,
                          uint32_t sample_rate = kDefaultSampleRate);

// ---------------------------------------------------------------------------
//  Surfaces
// ---------------------------------------------------------------------------

// What the tyres are rolling on. Plain data crossing no boundary: the terrain
// module classifies ground however it likes and hands audio one of these, so
// neither module has to include the other's headers.
enum class Surface : int {
    Tarmac = 0,
    Gravel,
    Dirt,
    Snow,
    kCount,
};

inline constexpr std::size_t kSurfaceCount =
    static_cast<std::size_t>(Surface::kCount);

constexpr const char* surface_name(Surface s) {
    switch (s) {
        case Surface::Tarmac: return "tarmac";
        case Surface::Gravel: return "gravel";
        case Surface::Dirt:   return "dirt";
        case Surface::Snow:   return "snow";
        case Surface::kCount: break;
    }
    return "?";
}

// Broadband scrub for a sliding tyre. Bright, resonant, and deliberately
// generated at ONE brightness — the runtime tracks slip with a low-pass on the
// voice rather than with a bank of variants, because slip is continuous and
// crossfading noise beds phases them against each other.
PcmClip synth_tyre_scrub(float seconds,
                         uint32_t sample_rate = kDefaultSampleRate);

// The rolling bed under the car: the texture of `surface` at speed. Tarmac is
// a thin hiss, gravel is a loose rattle, dirt sits between them, snow is a
// muffled squeak with almost no top end.
PcmClip synth_surface_roll(Surface surface, float seconds,
                           uint32_t sample_rate = kDefaultSampleRate);

// ---------------------------------------------------------------------------
//  Impacts and weather
// ---------------------------------------------------------------------------

// A suspension bottoming out on landing: a damped low thump under a body
// rattle. `weight` in [0,1] picks how much car came down — 0 is a kerb, 1 is
// all four wheels off a crest. Heavier means lower, longer and grittier.
PcmClip synth_suspension_thump(float weight,
                               uint32_t sample_rate = kDefaultSampleRate);

// Rain on the roof: dense high-passed noise with a scatter of discrete drop
// transients so it does not read as flat hiss.
PcmClip synth_rain_bed(float seconds,
                       uint32_t sample_rate = kDefaultSampleRate);

// Wind: low-passed noise with a slow band-passed howl riding on top.
PcmClip synth_wind_bed(float seconds,
                       uint32_t sample_rate = kDefaultSampleRate);

// ---------------------------------------------------------------------------
//  Stingers
// ---------------------------------------------------------------------------

// Checkpoint: two rising notes a just major sixth apart, bell-ish, ~0.5 s.
// Rising major intervals are the cheapest, most reliable way to sound POSITIVE
// without any sample content at all.
PcmClip synth_checkpoint_stinger(uint32_t sample_rate = kDefaultSampleRate);

// Lap record: the same idea earned out — a four-note major arpeggio up to the
// octave with a shimmer tail. Longer and brighter than the checkpoint so the
// two are never confused at speed.
PcmClip synth_lap_record_stinger(uint32_t sample_rate = kDefaultSampleRate);

// ---------------------------------------------------------------------------
//  The bank
// ---------------------------------------------------------------------------

// RPM anchor points for the engine layer bank.
//
// A single loop pitched across a whole rev range sounds like a tape player,
// because resampling drags the formants with the pitch. Real engine audio
// stacks a handful of loops recorded at fixed revs and crossfades between the
// two that bracket the current RPM; each layer is only ever stretched a little
// way from where it was made, so the resonances stay put.
//
// Seven anchors, spaced tighter at the BOTTOM. An even split left a full
// octave between idle and the next layer, which is both the widest stretch in
// the set and the place a rally driver spends most of the stage — exactly
// backwards. Fourteen short loops is under four seconds of PCM; it is not
// worth being clever to save that.
inline constexpr std::size_t kEngineLayerCount = 7;

// Every sound in the game, generated once. Held by value and never mutated
// after synth_bank() returns — the audio thread holds bare pointers into these
// clips, so a bank that is reassigned, resized or destroyed while the device is
// running is a use-after-free on the audio thread. Build it once at init, keep
// it alive for the process, and do not get clever.
struct SfxBank {
    uint32_t sample_rate = kDefaultSampleRate;

    std::array<float, kEngineLayerCount> engine_rpm{};
    std::array<PcmClip, kEngineLayerCount> engine_power{};   // on the throttle
    std::array<PcmClip, kEngineLayerCount> engine_overrun{}; // trailing / off

    PcmClip tyre_scrub;
    std::array<PcmClip, kSurfaceCount> surface_roll{};

    // Light / medium / heavy landings. Discrete because an impact is discrete:
    // there is no crossfade to do, the runtime just picks one and pitches it.
    static constexpr std::size_t kThumpCount = 3;
    std::array<PcmClip, kThumpCount> suspension_thump{};

    PcmClip rain;
    PcmClip wind;

    PcmClip checkpoint;
    PcmClip lap_record;
};

// Generate the whole bank. Deterministic: same seed, same samples, on every
// platform and every run — the noise beds come from core/rng.h, never from
// std::rand or anything clocked.
SfxBank synth_bank(uint32_t sample_rate = kDefaultSampleRate,
                   uint64_t seed = 0xA0D10C0DEull);

// ---------------------------------------------------------------------------
//  Per-frame parameter maps
// ---------------------------------------------------------------------------

// What one voice should be doing this frame. `clip == nullptr` or `gain == 0`
// both mean silent; the mixer treats them identically, so callers never need a
// "should I play this" branch.
struct VoiceMix {
    const PcmClip* clip = nullptr;
    float gain = 0.0f;
    float pitch = 1.0f;
    // One-pole low-pass corner applied by the voice. 0 means bypass. This is
    // how brightness tracks a continuous input (slip, speed) without a bank of
    // pre-filtered variants that would phase against each other.
    float lp_cutoff_hz = 0.0f;
};

// The engine is FOUR voices, not one: the two RPM anchors bracketing the
// current revs, each in both its power and its overrun character. Load
// crossfades power against overrun; RPM crossfades anchor against anchor. Four
// voices is nothing, and it is the only way the transition off the throttle
// mid-rev-range does not click or jump timbre.
struct EngineMix {
    std::array<VoiceMix, 4> layers{};
};

// `rpm` is clamped to the bank's anchor range. `load` is +1 full throttle,
// 0 coasting, -1 overrun; values outside [-1, 1] are clamped.
EngineMix engine_mix(const SfxBank& bank, float rpm, float load);

// Scrub level and brightness from lateral slip. `lateral_slip` is the slip
// ratio (0 gripping, ~1 fully sliding); `speed_mps` gates it, because a
// stationary car with the wheel cranked over is not squealing.
VoiceMix tyre_scrub_mix(const SfxBank& bank, float lateral_slip,
                        float speed_mps);

// The roll bed for the surface under the car at this speed.
VoiceMix surface_roll_mix(const SfxBank& bank, float speed_mps,
                          Surface surface);

// Which thump, how loud, how low. `impact_mps` is the closing speed at
// touchdown; below a few m/s this returns a silent mix rather than a quiet
// thump, because a landing you can only just hear is worse than no landing.
VoiceMix suspension_thump_mix(const SfxBank& bank, float impact_mps);

struct WeatherMix {
    VoiceMix rain;
    VoiceMix wind;
};

// `rain_intensity` and `wind_intensity` are both [0,1]. They are independent:
// a dry gale and a still downpour are both real weather.
WeatherMix weather_mix(const SfxBank& bank, float rain_intensity,
                       float wind_intensity);

// ---------------------------------------------------------------------------
//  Optional file clips
// ---------------------------------------------------------------------------

// Replace a synthesised clip with a WAV off disk, if there is one.
//
// This is the ONLY file path in the audio module and it is strictly optional:
// a missing, unreadable or unparseable file leaves `clip` exactly as it was
// and returns false. It warns ONCE per path for the life of the process — a
// per-frame or per-play warning for a file that was always going to be absent
// is how a log stops being read.
//
// Understands uncompressed RIFF/WAVE only: PCM 8/16/24/32-bit and IEEE float
// 32-bit, mono or multi-channel, any sample rate (the mixer resamples). No
// compressed formats, on purpose — a decoder is a dependency, and the whole
// point of this module is that it does not need one.
bool override_clip_from_wav(PcmClip& clip, const std::string& path);

// The raw loader behind it. Leaves `out` untouched and returns false on any
// problem. Exposed for tests; prefer override_clip_from_wav() in engine code.
bool load_wav_clip(const std::string& path, PcmClip& out);

}  // namespace apricot
