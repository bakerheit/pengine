#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "audio/mixer.h"
#include "audio/synth.h"
#include "core/asset_root.h"
#include "core/rng.h"

namespace apricot {

// Rook's respray booth, in sound: the hiss of the gun while the paint goes on,
// and one stinger for each way a respray can end.
//
//   synth_respray_hiss()          one second of air and paint off the gun
//   synth_respray_done(false)     a plain respray: clack, one bright note
//   synth_respray_done(true)      the heat is gone: clack, a rising fifth
//   synth_respray_kept()          a cop saw you pull in: clack, one low note
//
// The three stingers open on the SAME clack on purpose. It is the gun going
// down on the bench, and it is the same gun whatever the police think, so the
// only thing that tells the outcomes apart is the notes after it: rising and
// major reads as "you got away with it", one low note that sags reads as "no
// you didn't", and neither needs the player to read the banner first.
//
// Header-only and device-free, like app/weapon_audio.h, so a headless suite
// measures the exact samples the booth plays. Every sample is a pure function
// of its index: the noise is splitmix64_mix() of (seed, index), never a stream
// and never anything clocked, so two builds of a clip are byte-identical.
//
// Each clip can be replaced by a recording at audio/ui/respray_*.wav. None is
// shipped; a missing file is the normal state and keeps the synthesised clip.

// The hiss is authored to a one-second spray, so it runs out with the spray
// instead of trailing under the stinger that follows it. If the booth's spray
// time changes, change this with it: the suite pins the frame count to this
// constant, not to the booth.
inline constexpr float kResprayHissSeconds = 1.0f;

// The notes, exported so the suite asserts against these rather than against
// a second copy of the numbers.
inline constexpr double kResprayPaintedHz = 783.99;      // G5
inline constexpr double kResprayClearedLowHz = 659.26;   // E5
inline constexpr double kResprayClearedHighHz = 987.77;  // B5, a fifth up
inline constexpr double kResprayClearedHighAtSeconds = 0.23;
inline constexpr double kResprayKeptHz = 196.0;          // G3

// Emitter gains, before the Ui category trim and master. Untuned: set by ear
// at the attended run, not by measurement.
inline constexpr float kResprayHissGain = 0.55f;
inline constexpr float kResprayStingerGain = 0.80f;

namespace respray_audio_detail {

// Double precision, like synth.cpp: phase accumulates across a whole clip, and
// core's kTwoPi is a float.
inline constexpr double kHalfTurn = 3.1415926535897932385;
inline constexpr double kTurn = 6.2831853071795864769;

inline constexpr uint64_t kHissSeed = 0x5B4A7C0Dull;
inline constexpr uint64_t kRushSeed = 0x2E1D5A11ull;
inline constexpr uint64_t kBreathSeed = 0x7AB0C3E5ull;
inline constexpr uint64_t kFlutterSeed = 0x6F1E77A3ull;
inline constexpr uint64_t kStrikeSeed = 0xC1AC4B3Dull;
inline constexpr uint64_t kClunkSeed = 0x3D09B2C7ull;

// Uniform in [-1, 1), from (seed, index) alone.
inline double noise(uint64_t seed, std::size_t index) {
    const uint64_t bits = splitmix64_mix(
        seed ^ (static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ull));
    return static_cast<double>(bits >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

// A slow random wander in [-1, 1): hashed control points `period` frames apart,
// cosine-interpolated so the level bends and never steps.
inline double wander(uint64_t seed, std::size_t index, std::size_t period) {
    const std::size_t k = index / period;
    const double u = static_cast<double>(index % period) / static_cast<double>(period);
    const double w = 0.5 - 0.5 * std::cos(kHalfTurn * u);
    return noise(seed, k) * (1.0 - w) + noise(seed, k + 1) * w;
}

// Chamberlin state-variable filter, as in synth.cpp: two poles and a resonance
// control, stable while the corner stays below a sixth of the sample rate.
struct Svf {
    double f = 0.0, q = 1.0, lp = 0.0, bp = 0.0, hp = 0.0;
    void set(double cutoff_hz, double resonance, double sr) {
        f = 2.0 * std::sin(kHalfTurn * std::min(cutoff_hz, sr / 6.0) / sr);
        q = 1.0 / std::max(resonance, 0.5);
    }
    void step(double x) {
        lp += f * bp;
        hp = x - lp - q * bp;
        bp += f * hp;
    }
};

inline double rms_of(const std::vector<double>& s) {
    if (s.empty()) return 0.0;
    double acc = 0.0;
    for (const double v : s) acc += v * v;
    return std::sqrt(acc / static_cast<double>(s.size()));
}

// Scale to unit RMS, so layers are mixed by stated weight rather than by
// whatever gain a filter happened to have at its corner.
inline void unit_rms(std::vector<double>& s) {
    const double r = rms_of(s);
    if (r <= 1e-12) return;
    for (double& v : s) v /= r;
}

// Remove DC, then scale the peak to `headroom`.
inline void finish(std::vector<double>& s, double headroom) {
    if (s.empty()) return;
    double mean = 0.0;
    for (const double v : s) mean += v;
    mean /= static_cast<double>(s.size());
    double top = 1e-12;
    for (double& v : s) {
        v -= mean;
        top = std::max(top, std::fabs(v));
    }
    const double scale = headroom / top;
    for (double& v : s) v *= scale;
}

// Linear ramps at both ends, landing on exactly zero, so no playback of the
// clip starts or stops on a step.
inline void fade_ends(std::vector<double>& s, std::size_t head, std::size_t tail) {
    const std::size_t n = s.size();
    if (n == 0) return;
    const std::size_t h = std::min(head, n / 2);
    const std::size_t t = std::min(tail, n / 2);
    for (std::size_t i = 0; i < h; ++i) {
        s[i] *= static_cast<double>(i) / static_cast<double>(h);
    }
    for (std::size_t i = 0; i < t; ++i) {
        s[n - 1 - i] *= static_cast<double>(i) / static_cast<double>(t);
    }
}

inline PcmClip to_clip(const std::vector<double>& s, uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    clip.samples.resize(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        clip.samples[i] = static_cast<float>(s[i]);
    }
    return clip;
}

// The gun set down on the steel bench: a bright, tight strike, a short hollow
// clunk and a low knock under both. About 140 ms, and unpitched on purpose so
// it never argues with the note that follows it.
inline std::vector<double> clack(double sr) {
    const std::size_t n = static_cast<std::size_t>(0.14 * sr);
    std::vector<double> strike(n, 0.0), clunk(n, 0.0), knock(n, 0.0);
    Svf hi;
    hi.set(2600.0, 2.5, sr);
    Svf mid;
    mid.set(850.0, 4.0, sr);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double attack = std::min(1.0, t / 0.0008);
        hi.step(noise(kStrikeSeed, i));
        mid.step(noise(kClunkSeed, i));
        strike[i] = attack * hi.bp * std::exp(-t * 80.0);
        clunk[i] = attack * mid.bp * std::exp(-t * 45.0);
        knock[i] = std::min(1.0, t / 0.002) * std::sin(kTurn * 150.0 * t) *
                   std::exp(-t * 40.0);
    }
    unit_rms(strike);
    unit_rms(clunk);
    unit_rms(knock);
    std::vector<double> out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = 0.70 * strike[i] + 0.50 * clunk[i] + 0.45 * knock[i];
    }
    double top = 1e-12;
    for (const double v : out) top = std::max(top, std::fabs(v));
    for (double& v : out) v /= top;
    return out;
}

// A struck note: fundamental, two harmonics, and a slightly sharp 2.76
// partial, which is what makes it ring like a bell rather than hum like an
// organ.
inline void add_bell(std::vector<double>& out, double sr, double at_s, double hz,
                     double level, double decay) {
    const std::size_t n0 = static_cast<std::size_t>(at_s * sr);
    const std::size_t attack = static_cast<std::size_t>(0.004 * sr);
    for (std::size_t i = n0; i < out.size(); ++i) {
        const std::size_t k = i - n0;
        const double dt = static_cast<double>(k) / sr;
        double env = std::exp(-decay * dt);
        if (k < attack) env *= static_cast<double>(k) / static_cast<double>(attack);
        const double ph = kTurn * hz * dt;
        out[i] += level * env *
                  (std::sin(ph) + 0.34 * std::sin(2.0 * ph) +
                   0.13 * std::sin(3.0 * ph) + 0.09 * std::sin(2.76 * ph));
    }
}

// A low, dull note that sags as it dies. No inharmonic partial: the bell's
// shimmer is what makes the other two sound like a reward, so this one is
// denied it.
inline void add_sag(std::vector<double>& out, double sr, double at_s, double hz,
                    double level, double decay, double sag) {
    const std::size_t n0 = static_cast<std::size_t>(at_s * sr);
    const std::size_t attack = static_cast<std::size_t>(0.015 * sr);
    double phase = 0.0;
    for (std::size_t i = n0; i < out.size(); ++i) {
        const std::size_t k = i - n0;
        const double dt = static_cast<double>(k) / sr;
        double env = std::exp(-decay * dt);
        if (k < attack) env *= static_cast<double>(k) / static_cast<double>(attack);
        out[i] += level * env *
                  (std::sin(phase) + 0.45 * std::sin(2.0 * phase) +
                   0.16 * std::sin(3.0 * phase));
        const double f = hz * (1.0 - sag * std::min(1.0, dt / 0.5));
        phase = std::fmod(phase + kTurn * f / sr, kTurn);
    }
}

enum class Stinger : uint8_t { Painted, Cleared, Kept };

inline PcmClip stinger(Stinger kind, uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0) return clip;
    const double sr = static_cast<double>(sample_rate);
    const double seconds =
        kind == Stinger::Cleared ? 1.15 : (kind == Stinger::Kept ? 0.95 : 0.60);
    // Rounded, not truncated: 1.15 s at 48 kHz is 55199.9999 in double.
    std::vector<double> out(static_cast<std::size_t>(std::llround(seconds * sr)), 0.0);

    const std::vector<double> gun = clack(sr);
    for (std::size_t i = 0; i < std::min(gun.size(), out.size()); ++i) {
        out[i] += 0.80 * gun[i];
    }
    switch (kind) {
        case Stinger::Painted:
            add_bell(out, sr, 0.07, kResprayPaintedHz, 0.90, 8.0);
            break;
        case Stinger::Cleared:
            add_bell(out, sr, 0.08, kResprayClearedLowHz, 0.80, 7.0);
            add_bell(out, sr, kResprayClearedHighAtSeconds, kResprayClearedHighHz,
                     1.00, 4.2);
            break;
        case Stinger::Kept:
            add_sag(out, sr, 0.07, kResprayKeptHz, 1.00, 4.0, 0.055);
            break;
    }
    finish(out, 0.90);
    fade_ends(out, static_cast<std::size_t>(0.002 * sr),
              static_cast<std::size_t>(0.030 * sr));
    return to_clip(out, sample_rate);
}

}  // namespace respray_audio_detail

// One second of paint off a compressor-fed gun: a resonant jet around 4-5 kHz
// that opens a little as the gun runs, a high-passed rush of air around it,
// and a quiet low breath from the line. The trigger cracks open with a short
// pressure burst, the level wanders a few percent the way a real jet sputters,
// and the last 90 ms close off rather than stopping dead.
inline PcmClip synth_respray_hiss(uint32_t sample_rate = kDefaultSampleRate) {
    namespace d = respray_audio_detail;
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0) return clip;
    const double sr = static_cast<double>(sample_rate);
    const double seconds = static_cast<double>(kResprayHissSeconds);
    const std::size_t n = static_cast<std::size_t>(seconds * sr);

    std::vector<double> jet(n, 0.0), rush(n, 0.0), breath(n, 0.0);
    d::Svf jet_band;
    d::Svf breath_band;
    breath_band.set(520.0, 0.8, sr);
    const double rush_a = 1.0 - std::exp(-d::kTurn * 1800.0 / sr);
    double rush_lp = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(n);
        jet_band.set(4300.0 + 900.0 * u, 1.6, sr);
        jet_band.step(d::noise(d::kHissSeed, i));
        jet[i] = jet_band.bp;
        const double white = d::noise(d::kRushSeed, i);
        rush_lp += rush_a * (white - rush_lp);
        rush[i] = white - rush_lp;
        breath_band.step(d::noise(d::kBreathSeed, i));
        breath[i] = breath_band.bp;
    }
    d::unit_rms(jet);
    d::unit_rms(rush);
    d::unit_rms(breath);

    const std::size_t flutter_period =
        std::max<std::size_t>(1, static_cast<std::size_t>(0.035 * sr));
    constexpr double kOpen = 0.015;
    constexpr double kClose = 0.09;
    std::vector<double> out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double open = std::min(1.0, t / kOpen);
        const double burst = 1.0 + 0.45 * std::exp(-std::max(0.0, t - kOpen) / 0.06);
        const double close =
            t > seconds - kClose
                ? 0.5 + 0.5 * std::cos(d::kHalfTurn * (t - (seconds - kClose)) / kClose)
                : 1.0;
        const double flutter = 1.0 + 0.10 * d::wander(d::kFlutterSeed, i, flutter_period);
        out[i] = open * burst * close * flutter *
                 (0.90 * jet[i] + 0.45 * rush[i] + 0.30 * breath[i]);
    }
    d::finish(out, 0.80);
    d::fade_ends(out, static_cast<std::size_t>(0.001 * sr),
                 static_cast<std::size_t>(0.005 * sr));
    return d::to_clip(out, sample_rate);
}

// `wanted_cleared` false: a respray with no heat to lose. True: the respray
// took the stars with it.
inline PcmClip synth_respray_done(bool wanted_cleared,
                                  uint32_t sample_rate = kDefaultSampleRate) {
    namespace d = respray_audio_detail;
    return d::stinger(wanted_cleared ? d::Stinger::Cleared : d::Stinger::Painted,
                      sample_rate);
}

// The car is painted, but a cop saw it pull in and the stars stay.
inline PcmClip synth_respray_kept(uint32_t sample_rate = kDefaultSampleRate) {
    namespace d = respray_audio_detail;
    return d::stinger(d::Stinger::Kept, sample_rate);
}

// Relative to the assets root. Joined at load time, never at namespace scope:
// see core/asset_root.h.
inline constexpr const char* kResprayHissWav = "audio/ui/respray_hiss.wav";
inline constexpr const char* kResprayPaintedWav = "audio/ui/respray_painted.wav";
inline constexpr const char* kResprayClearedWav = "audio/ui/respray_cleared.wav";
inline constexpr const char* kResprayKeptWav = "audio/ui/respray_kept.wav";

struct ResprayClipPaths {
    std::string hiss;
    std::string painted;
    std::string cleared;
    std::string kept;
};

inline ResprayClipPaths respray_clip_paths() {
    return {asset_path(kResprayHissWav), asset_path(kResprayPaintedWav),
            asset_path(kResprayClearedWav), asset_path(kResprayKeptWav)};
}

// Held for the life of the mixer: voices keep bare pointers into these, the
// same rule as SfxBank.
struct ResprayClips {
    PcmClip hiss;
    PcmClip painted;
    PcmClip cleared;
    PcmClip kept;
    std::size_t overrides_loaded = 0;

    const PcmClip& done(bool wanted_cleared) const {
        return wanted_cleared ? cleared : painted;
    }
};

// Synthesise all four, then swap in any recording that exists. The existence
// probe is deliberate: no recording is shipped, and override_clip_from_wav()
// would log a warning for every absent file on every launch. A file that is
// present but unreadable still reaches the loader, and still warns.
inline ResprayClips build_respray_clips(const ResprayClipPaths& overrides,
                                        uint32_t sample_rate = kDefaultSampleRate) {
    ResprayClips clips;
    clips.hiss = synth_respray_hiss(sample_rate);
    clips.painted = synth_respray_done(false, sample_rate);
    clips.cleared = synth_respray_done(true, sample_rate);
    clips.kept = synth_respray_kept(sample_rate);
    const auto apply = [&clips](PcmClip& clip, const std::string& path) {
        if (path.empty() || !std::ifstream(path, std::ios::binary).good()) return;
        if (override_clip_from_wav(clip, path)) ++clips.overrides_loaded;
    };
    apply(clips.hiss, overrides.hiss);
    apply(clips.painted, overrides.painted);
    apply(clips.cleared, overrides.cleared);
    apply(clips.kept, overrides.kept);
    return clips;
}

// Every booth sound is feedback about the game, not a sound in the world: it
// rides Category::Ui (so the SFX volume trims it) and is never positioned.
inline VoiceParams respray_voice(float gain) {
    VoiceParams p;
    p.category = Category::Ui;
    p.gain = gain;
    p.spatial = false;
    return p;
}

// The booth's voices. Owns only the hiss handle: the stingers are
// fire-and-forget, and a handle to one would have no use but a bug.
struct RespraySound {
    OneShotHandle hiss{};

    bool start_hiss(VoiceMixer& mixer, const ResprayClips& clips) {
        stop_hiss(mixer);
        hiss = mixer.play_oneshot(&clips.hiss, respray_voice(kResprayHissGain));
        return hiss.valid();
    }

    // FADES, never cuts. stop_oneshot() silences a voice between two samples,
    // and a cancelled spray lands at an arbitrary point in loud noise, so it
    // would click. Dropping the emitter gain to zero rides the mixer's
    // per-sample gain ramp down instead; the silent remainder of the clip
    // (under a second) runs out on its own. A stale handle is ignored.
    void stop_hiss(VoiceMixer& mixer) {
        mixer.set_oneshot(hiss, respray_voice(0.0f));
        hiss = {};
    }

    bool play_done(VoiceMixer& mixer, const ResprayClips& clips, bool wanted_cleared) {
        stop_hiss(mixer);
        return mixer.play_oneshot(&clips.done(wanted_cleared),
                                  respray_voice(kResprayStingerGain)).valid();
    }

    bool play_kept(VoiceMixer& mixer, const ResprayClips& clips) {
        stop_hiss(mixer);
        return mixer.play_oneshot(&clips.kept, respray_voice(kResprayStingerGain)).valid();
    }
};

}  // namespace apricot
