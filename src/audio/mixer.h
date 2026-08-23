#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/glm.hpp>

#include "audio/synth.h"

namespace apricot {

// Gain routing and voice mixing. HEADER-ONLY AND DEVICE-FREE on purpose: every
// emitter's final volume is decided by this arithmetic, and it is exactly the
// arithmetic that goes wrong silently. Keeping it here means a headless test
// can assert that muting music does not duck the engine, and can render a
// whole block of stereo PCM to inspect, with no audio hardware in sight.
//
// The routing is deliberately shallow — one category trim times one master:
//
//     final = emitter_gain * category[c] * master
//
// Deeper graphs (per-emitter buses, sends) are where "why is this one sound
// quiet" becomes unanswerable. Add a category before you add a layer.

enum class Category : int {
    Engine = 0,
    Tyres,
    Impacts,
    World,
    Weather,
    Ui,
    Music,
    kCount,
};

inline constexpr std::size_t kCategoryCount =
    static_cast<std::size_t>(Category::kCount);

inline constexpr const char* category_name(Category c) {
    switch (c) {
        case Category::Engine:  return "engine";
        case Category::Tyres:   return "tyres";
        case Category::Impacts: return "impacts";
        case Category::World:   return "world";
        case Category::Weather: return "weather";
        case Category::Ui:      return "ui";
        case Category::Music:   return "music";
        case Category::kCount:  break;
    }
    return "?";
}

struct Mixer {
    float master = 1.0f;
    std::array<float, kCategoryCount> category{};

    // Categories default to unity. Written as a constructor rather than a
    // member initialiser because a value-initialised std::array is all zeroes,
    // and a mixer that defaults to silent is a bug report waiting to happen.
    Mixer() { category.fill(1.0f); }

    // Trims are clamped to [0, 1]: a trim above unity is how you get clipping
    // that only appears when several loud things happen at once.
    void set_master(float g) { master = std::clamp(g, 0.0f, 1.0f); }

    void set_category(Category c, float g) {
        const std::size_t i = static_cast<std::size_t>(c);
        if (i < kCategoryCount) category[i] = std::clamp(g, 0.0f, 1.0f);
    }

    float category_gain(Category c) const {
        const std::size_t i = static_cast<std::size_t>(c);
        return i < kCategoryCount ? category[i] : 0.0f;
    }

    // The one function that decides how loud anything is.
    // `emitter_gain` is NOT clamped to 1: distance attenuation and one-shot
    // emphasis legitimately hand in values above unity. It is clamped at zero,
    // because a negative gain inverts the waveform's phase rather than making
    // it quiet, and that reads as a weirdly hollow mix rather than as an error.
    float gain(Category c, float emitter_gain = 1.0f) const {
        return std::max(emitter_gain, 0.0f) * category_gain(c) * master;
    }
};

// ---------------------------------------------------------------------------
//  Output limiting
// ---------------------------------------------------------------------------

// Soft knee limiter for the master bus. Below `knee` it is the identity — the
// normal mix is passed through bit-exact, so this cannot be blamed for the
// tone of a quiet scene. Above it, the excess is bent through x/(1+x), which
// is bounded, monotonic and C1-continuous at the knee, so |out| < 1 ALWAYS
// with no discontinuity for a transient to catch on.
//
// A hard clamp would also keep the value in range. It would also turn every
// loud moment into an octave of odd harmonics, and "the audio goes crunchy
// when three things happen at once" is a bug people describe as "the audio is
// bad" and never file.
inline float soft_clip(float x, float knee = 0.8f) {
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float head = 1.0f - knee;
    const float u = (a - knee) / head;
    const float y = knee + head * (u / (1.0f + u));
    return x < 0.0f ? -y : y;
}

// ---------------------------------------------------------------------------
//  3D placement
// ---------------------------------------------------------------------------

// Where the ears are. `forward` and `right` must be unit and perpendicular;
// they come straight off the camera basis, so they already are.
struct Listener {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
};

// Inverse-distance falloff, the shape every audio middleware defaults to.
//
//   ref_distance   inside this radius the source is at full gain. Not zero:
//                  true inverse-square goes to infinity at the origin, and a
//                  sound the camera passes through would blow the mix apart.
//   max_distance   past this it is silent. A hard cutoff rather than an
//                  asymptote so distant sources can be skipped entirely.
//   rolloff        how fast it falls between the two. 1 is physical-ish.
struct Attenuation {
    float ref_distance = 4.0f;
    float max_distance = 160.0f;
    float rolloff = 1.0f;
};

inline float distance_attenuation(float distance, const Attenuation& a) {
    if (!(distance > 0.0f)) return 1.0f;
    if (distance >= a.max_distance) return 0.0f;
    const float ref = std::max(a.ref_distance, 1e-4f);
    if (distance <= ref) return 1.0f;
    const float g = ref / (ref + std::max(a.rolloff, 0.0f) * (distance - ref));

    // Fade the last fifth of the range to zero. Without it a source crossing
    // max_distance drops from a small but audible gain to nothing in one
    // block, and a car looping past the cutoff radius ticks on every pass.
    const float fade_start = a.max_distance * 0.8f;
    if (distance > fade_start) {
        const float span = a.max_distance - fade_start;
        if (span > 1e-4f) {
            return g * (1.0f - (distance - fade_start) / span);
        }
    }
    return g;
}

struct StereoGain {
    float left = 1.0f;
    float right = 1.0f;
};

// Constant-power pan. `pan` is -1 hard left, 0 centre, +1 hard right.
//
// Constant POWER, not constant amplitude: left+right summing to a constant
// makes a sound audibly dip as it crosses the centre, because perceived
// loudness follows energy. sin/cos keeps left^2 + right^2 == 1 the whole way
// across, and a car sweeping past the camera holds its level.
inline StereoGain constant_power_pan(float pan) {
    const float p = std::clamp(pan, -1.0f, 1.0f);
    const float theta = (p + 1.0f) * 0.7853981634f;  // 0 .. pi/2
    return StereoGain{std::cos(theta), std::sin(theta)};
}

// Distance attenuation and stereo placement for a source, from the listener's
// own basis. Returns the pair of channel gains to multiply the voice by.
inline StereoGain spatial_gains(const Listener& listener,
                                const glm::vec3& source,
                                const Attenuation& att) {
    const glm::vec3 delta = source - listener.position;
    const float d2 = glm::dot(delta, delta);
    const float d = std::sqrt(d2);

    const float att_gain = distance_attenuation(d, att);
    if (att_gain <= 0.0f) return StereoGain{0.0f, 0.0f};

    float pan = 0.0f;
    if (d > 1e-4f) {
        pan = glm::dot(delta, listener.right) / d;

        // Collapse the pan toward centre as the source approaches the head.
        // Without this, a source passing through the listener slams from hard
        // left to hard right over a couple of centimetres — which is what the
        // maths says and is nothing like what a real close sound does.
        const float ref = std::max(att.ref_distance, 1e-4f);
        pan *= std::min(1.0f, d / ref);
    }

    StereoGain g = constant_power_pan(pan);
    g.left *= att_gain;
    g.right *= att_gain;
    return g;
}

// ---------------------------------------------------------------------------
//  Voices
// ---------------------------------------------------------------------------

// Everything about one playing sound that the sim side gets to decide.
// Trivially copyable on purpose: it crosses to the audio thread by value
// through a lock-free queue, so it may never own anything.
struct VoiceParams {
    Category category = Category::Ui;
    float gain = 1.0f;
    float pitch = 1.0f;

    // Used only when `spatial` is false. Ignored otherwise — a positioned
    // source's pan comes from its position, and letting both apply is how you
    // get a sound that is somehow on the left AND behind you.
    float pan = 0.0f;

    // One-pole low-pass corner in Hz, 0 to bypass. This is the knob that makes
    // brightness track a continuous input.
    float lp_cutoff_hz = 0.0f;

    bool looping = false;
    bool spatial = false;

    glm::vec3 position{0.0f};
    Attenuation attenuation{};
};

// A handle to a LOOPING voice the sim owns and must close. One-shots do not
// get one: they are fire-and-forget, and a handle to something that has
// already finished is a handle whose only use is a bug.
struct VoiceHandle {
    uint16_t slot = 0;
    uint16_t generation = 0;  // 0 is never issued, so a default handle is invalid

    bool valid() const { return generation != 0; }
};

// ---------------------------------------------------------------------------
//  The mixer
// ---------------------------------------------------------------------------

// Renders every active voice into one interleaved stereo block.
//
// THREADING, and this is the part that matters:
//
//   * Every public setter is called from the SIM thread and does nothing but
//     push a POD command into a single-producer/single-consumer ring.
//   * render() is called from the AUDIO thread. It drains the ring, then
//     touches nothing but its own arrays.
//   * There is no mutex anywhere in this file, no allocation after
//     prepare(), and no logging on the render path. The audio thread has a
//     hard deadline measured in single-digit milliseconds; a lock held by a
//     sim thread that just got descheduled is an audible dropout.
//
// The one thing this CANNOT protect you from: voices hold bare `const PcmClip*`
// into a bank the sim owns. That bank must outlive the mixer and must not be
// reallocated while it is running. Build it once, keep it forever.
class VoiceMixer {
public:
    // 32 one-shots and 32 loops. The split is fixed rather than pooled so a
    // storm of impacts can never starve the engine loops, which are the sounds
    // whose absence is instantly obvious.
    static constexpr std::size_t kOneShotVoices = 32;
    static constexpr std::size_t kLoopVoices = 32;
    static constexpr std::size_t kMaxVoices = kOneShotVoices + kLoopVoices;

    // Deep enough that a whole frame's worth of parameter updates fits several
    // times over. Overflow is counted, not silently swallowed — see
    // dropped_commands().
    static constexpr std::size_t kCommandCapacity = 512;

    VoiceMixer() = default;

    // Call from the sim thread BEFORE the device starts pulling. Sets the rate
    // render() will resample every clip to.
    void prepare(uint32_t sample_rate) {
        sample_rate_ = sample_rate ? sample_rate : kDefaultSampleRate;
    }

    uint32_t sample_rate() const { return sample_rate_; }

    // Declare that NOTHING will ever call render(). Commands are then dropped
    // at the door instead of queued.
    //
    // This exists because of a specific, quiet failure: on a machine with no
    // sound card the sim goes on cheerfully pushing an engine-note update every
    // step, the ring fills after half a second, and from then on
    // dropped_commands() climbs forever. The counter that was supposed to be
    // the alarm becomes noise, and the first real overflow is invisible under
    // it. A silent mixer is a normal state, so it gets a normal path.
    void set_silent(bool silent) { silent_ = silent; }
    bool silent() const { return silent_; }

    // ---- sim-thread API ---------------------------------------------------

    void set_master(float g) {
        gains_.set_master(g);
        Command c{};
        c.kind = Command::Kind::SetMaster;
        c.value = gains_.master;
        push_(c);
    }

    void set_category(Category cat, float g) {
        gains_.set_category(cat, g);
        Command c{};
        c.kind = Command::Kind::SetCategory;
        c.category = cat;
        c.value = gains_.category_gain(cat);
        push_(c);
    }

    // The sim-side view of the trims. Reading this never races: it is the
    // sim thread's own copy, and the audio thread has its own.
    const Mixer& gains() const { return gains_; }

    void set_listener(const Listener& l) {
        Command c{};
        c.kind = Command::Kind::SetListener;
        c.listener = l;
        push_(c);
    }

    // Fire a one-shot. No handle, no way to stop it, no failure to check: if
    // all 32 one-shot voices are busy the oldest is cut, which is what you
    // want from a rally car landing on gravel in the rain.
    void play_oneshot(const PcmClip* clip, const VoiceParams& params) {
        if (!clip || clip->empty()) return;
        Command c{};
        c.kind = Command::Kind::Start;
        c.slot = static_cast<uint16_t>(oneshot_cursor_);
        c.clip = clip;
        c.params = params;
        c.params.looping = false;
        push_(c);
        oneshot_cursor_ = (oneshot_cursor_ + 1) % kOneShotVoices;
    }

    // Open a looping voice. Returns an invalid handle when all loop slots are
    // taken — check it. A silently dropped engine loop is the difference
    // between "the audio is broken" and a one-line log.
    VoiceHandle open_loop(const PcmClip* clip, const VoiceParams& params) {
        if (!clip || clip->empty()) return VoiceHandle{};
        for (std::size_t i = 0; i < kLoopVoices; ++i) {
            if (loop_taken_[i]) continue;
            loop_taken_[i] = true;
            if (++loop_generation_[i] == 0) loop_generation_[i] = 1;

            Command c{};
            c.kind = Command::Kind::Start;
            c.slot = static_cast<uint16_t>(kOneShotVoices + i);
            c.clip = clip;
            c.params = params;
            c.params.looping = true;
            push_(c);

            return VoiceHandle{static_cast<uint16_t>(i), loop_generation_[i]};
        }
        return VoiceHandle{};
    }

    // Retarget a live loop. Every field is smoothed on the audio thread, so
    // calling this every sim step with wildly different values is fine and is
    // exactly how the engine note is driven.
    void set_loop(VoiceHandle h, const VoiceParams& params) {
        if (!loop_handle_live_(h)) return;
        Command c{};
        c.kind = Command::Kind::Update;
        c.slot = static_cast<uint16_t>(kOneShotVoices + h.slot);
        c.params = params;
        c.params.looping = true;
        push_(c);
    }

    // Close a loop. The slot is reusable immediately: the ring is FIFO, so the
    // stop is guaranteed to be processed before any start that reuses it.
    void close_loop(VoiceHandle h) {
        if (!loop_handle_live_(h)) return;
        loop_taken_[h.slot] = false;
        Command c{};
        c.kind = Command::Kind::Stop;
        c.slot = static_cast<uint16_t>(kOneShotVoices + h.slot);
        push_(c);
    }

    // Cut everything. Used on shutdown and on a device reopen.
    void stop_all() {
        for (std::size_t i = 0; i < kLoopVoices; ++i) loop_taken_[i] = false;
        Command c{};
        c.kind = Command::Kind::StopAll;
        push_(c);
    }

    // Commands lost to a full ring since startup. Should be zero. If it is
    // not, the sim is pushing faster than the device is pulling and something
    // upstream is looping when it should be latching.
    uint32_t dropped_commands() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    // ---- audio-thread API -------------------------------------------------

    // Render `frames` stereo frames into `out` (2 * frames floats), REPLACING
    // whatever was there. Allocation-free, lock-free, log-free.
    void render(float* out, std::size_t frames) {
        if (!out || frames == 0) return;
        drain_();

        const std::size_t n = frames * 2;
        for (std::size_t i = 0; i < n; ++i) out[i] = 0.0f;

        const float rate = static_cast<float>(sample_rate_);
        // ~8 ms smoothing. Long enough to kill zipper noise on a per-step
        // parameter change, short enough that a checkpoint stinger still
        // sounds like it starts when it starts.
        const float smooth = 1.0f - std::exp(-1.0f / (0.008f * rate));

        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            Voice& voice = voices_[v];
            if (!voice.active || !voice.clip) continue;
            render_voice_(voice, out, frames, rate, smooth);
        }

        for (std::size_t i = 0; i < n; ++i) out[i] = soft_clip(out[i]);
    }

    // How many voices are currently sounding. Audio-thread state; read it from
    // a test or a debug overlay, not to make a decision from the sim.
    std::size_t active_voices() const {
        std::size_t n = 0;
        for (const Voice& v : voices_) {
            if (v.active) ++n;
        }
        return n;
    }

private:
    // ---- the command ring -------------------------------------------------

    struct Command {
        enum class Kind : uint8_t {
            None = 0, SetMaster, SetCategory, SetListener,
            Start, Update, Stop, StopAll,
        };
        Kind kind = Kind::None;
        Category category = Category::Ui;
        uint16_t slot = 0;
        float value = 0.0f;
        const PcmClip* clip = nullptr;
        VoiceParams params{};
        Listener listener{};
    };
    static_assert(std::is_trivially_copyable<Command>::value,
                  "commands cross a thread boundary by value; they may not own");

    void push_(const Command& c) {
        if (silent_) return;
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % kCommandCapacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            // Full. Dropping is the only option that does not block the sim
            // thread, so drop LOUDLY: the counter is the whole reason this is
            // debuggable rather than "audio sometimes ignores me".
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        ring_[head] = c;
        head_.store(next, std::memory_order_release);
    }

    void drain_() {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        while (tail != head) {
            apply_(ring_[tail]);
            tail = (tail + 1) % kCommandCapacity;
        }
        tail_.store(tail, std::memory_order_release);
    }

    void apply_(const Command& c) {
        switch (c.kind) {
            case Command::Kind::SetMaster:
                audio_gains_.master = c.value;
                break;
            case Command::Kind::SetCategory:
                audio_gains_.set_category(c.category, c.value);
                break;
            case Command::Kind::SetListener:
                listener_ = c.listener;
                break;
            case Command::Kind::Start: {
                if (c.slot >= kMaxVoices) break;
                Voice& v = voices_[c.slot];
                v.clip = c.clip;
                v.params = c.params;
                v.cursor = 0.0;
                v.lp_l = 0.0f;
                v.lp_r = 0.0f;
                v.cutoff_hz = c.params.lp_cutoff_hz;
                v.pitch = std::max(c.params.pitch, 1e-3f);
                // Channel gains start at zero and ramp: a voice that starts at
                // its full target gain begins with a step discontinuity, which
                // is a click on every single one-shot.
                v.gain_l = 0.0f;
                v.gain_r = 0.0f;
                v.active = true;
                break;
            }
            case Command::Kind::Update: {
                if (c.slot >= kMaxVoices) break;
                Voice& v = voices_[c.slot];
                if (!v.active) break;
                v.params = c.params;
                break;
            }
            case Command::Kind::Stop:
                if (c.slot < kMaxVoices) voices_[c.slot].active = false;
                break;
            case Command::Kind::StopAll:
                for (Voice& v : voices_) v.active = false;
                break;
            case Command::Kind::None:
                break;
        }
    }

    // ---- the render path --------------------------------------------------

    struct Voice {
        const PcmClip* clip = nullptr;
        VoiceParams params{};
        double cursor = 0.0;   // fractional frame position into the clip
        float gain_l = 0.0f;   // smoothed, folds emitter x category x master x pan
        float gain_r = 0.0f;
        float pitch = 1.0f;    // smoothed
        float cutoff_hz = 0.0f;
        float lp_l = 0.0f;     // one-pole state
        float lp_r = 0.0f;
        bool active = false;
    };

    void render_voice_(Voice& v, float* out, std::size_t frames, float rate,
                       float smooth) {
        const PcmClip& clip = *v.clip;
        const std::size_t clip_frames = clip.frame_count();
        if (clip_frames == 0) {
            v.active = false;
            return;
        }
        const uint16_t channels = clip.channels ? clip.channels : uint16_t{1};

        // --- target channel gains, once per block ---
        const float routed = audio_gains_.gain(v.params.category, v.params.gain);
        StereoGain sg = v.params.spatial
                            ? spatial_gains(listener_, v.params.position,
                                            v.params.attenuation)
                            : constant_power_pan(v.params.pan);
        const float target_l = sg.left * routed;
        const float target_r = sg.right * routed;

        // --- resample ratio ---
        // A clip made at 48 kHz played on a 44.1 kHz device must play SLOWER
        // per output sample, not just at a different pitch: the ratio is the
        // clip's rate over the device's, times the musical pitch on top.
        const float clip_rate = static_cast<float>(
            clip.sample_rate ? clip.sample_rate : kDefaultSampleRate);
        const float target_pitch = std::max(v.params.pitch, 1e-3f);
        const double rate_ratio = static_cast<double>(clip_rate / rate);

        // --- filter coefficient, once per block ---
        // Cutoff is smoothed per BLOCK rather than per sample: a block is a few
        // milliseconds, the ear cannot hear a filter sweep quantised that fine,
        // and an exp() per sample is not free.
        v.cutoff_hz += (v.params.lp_cutoff_hz - v.cutoff_hz) * 0.25f;
        const bool filtered = v.cutoff_hz > 1.0f && v.cutoff_hz < rate * 0.45f;
        const float lp_a =
            filtered ? 1.0f - std::exp(-6.2831853f * v.cutoff_hz / rate) : 1.0f;

        for (std::size_t i = 0; i < frames; ++i) {
            // Wrap or finish. Done BEFORE the read so a loop never samples one
            // frame past the end.
            if (v.cursor >= static_cast<double>(clip_frames)) {
                if (v.params.looping) {
                    v.cursor -= static_cast<double>(clip_frames) *
                                std::floor(v.cursor /
                                           static_cast<double>(clip_frames));
                } else {
                    v.active = false;
                    return;
                }
            }

            const std::size_t i0 = static_cast<std::size_t>(v.cursor);
            const float frac = static_cast<float>(v.cursor - static_cast<double>(i0));
            // The next frame, wrapping for loops so the interpolation across
            // the seam is continuous too. A one-shot holds its last sample,
            // which it is about to stop on anyway.
            std::size_t i1 = i0 + 1;
            if (i1 >= clip_frames) i1 = v.params.looping ? 0 : i0;

            float src_l;
            float src_r;
            if (channels == 1) {
                const float s = lerp_(clip.samples[i0], clip.samples[i1], frac);
                src_l = s;
                src_r = s;
            } else {
                const std::size_t b0 = i0 * channels;
                const std::size_t b1 = i1 * channels;
                src_l = lerp_(clip.samples[b0], clip.samples[b1], frac);
                src_r = lerp_(clip.samples[b0 + 1], clip.samples[b1 + 1], frac);
            }

            if (filtered) {
                v.lp_l += lp_a * (src_l - v.lp_l);
                v.lp_r += lp_a * (src_r - v.lp_r);
                src_l = v.lp_l;
                src_r = v.lp_r;
            }

            // Per-sample smoothing of the two channel gains. Everything —
            // emitter gain, category trim, master, distance, pan — is already
            // folded into these two numbers, so this one ramp makes ALL of it
            // click-free, including a category being muted mid-note.
            v.gain_l += (target_l - v.gain_l) * smooth;
            v.gain_r += (target_r - v.gain_r) * smooth;
            v.pitch += (target_pitch - v.pitch) * smooth;

            out[i * 2] += src_l * v.gain_l;
            out[i * 2 + 1] += src_r * v.gain_r;

            v.cursor += rate_ratio * static_cast<double>(v.pitch);
        }
    }

    static float lerp_(float a, float b, float t) { return a + (b - a) * t; }

    bool loop_handle_live_(VoiceHandle h) const {
        return h.valid() && h.slot < kLoopVoices && loop_taken_[h.slot] &&
               loop_generation_[h.slot] == h.generation;
    }

    // ---- sim-thread state (never touched by render) -----------------------
    Mixer gains_;
    std::array<bool, kLoopVoices> loop_taken_{};
    std::array<uint16_t, kLoopVoices> loop_generation_{};
    std::size_t oneshot_cursor_ = 0;
    bool silent_ = false;

    // ---- shared: the ring, and nothing else -------------------------------
    std::array<Command, kCommandCapacity> ring_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<uint32_t> dropped_{0};

    // ---- audio-thread state (never touched by the sim) --------------------
    std::array<Voice, kMaxVoices> voices_{};
    Mixer audio_gains_;
    Listener listener_{};
    uint32_t sample_rate_ = kDefaultSampleRate;
};

static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "the command ring must not take a lock on the audio thread");

}  // namespace apricot
