#include "audio/synth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/rng.h"

namespace apricot {

namespace {

constexpr double kPi = 3.1415926535897932385;
constexpr double kTwoPi = 6.2831853071795864769;

// Zero-mean white noise from the engine's own deterministic stream. Not
// std::rand, not a Mersenne twister, not anything seeded off a clock: the whole
// bank must be byte-identical on every machine and every run, or "the gravel
// sounds different on Ben's laptop" becomes a real conversation.
struct Noise {
    Rng rng;
    explicit Noise(uint64_t seed) : rng{splitmix64_mix(seed)} {}
    double next() { return static_cast<double>(rng.next_float()) * 2.0 - 1.0; }
    double unit() { return static_cast<double>(rng.next_float()); }
    double range(double lo, double hi) { return lo + (hi - lo) * unit(); }
};

// One-pole low-pass. The cheapest useful filter there is, and the right one for
// a noise bed: 6 dB/octave is a gentle enough slope that sweeping the corner
// reads as "duller" rather than as "someone is turning a knob".
struct OnePole {
    double a = 1.0;
    double y = 0.0;
    void set(double cutoff_hz, double sr) {
        a = 1.0 - std::exp(-kTwoPi * cutoff_hz / sr);
        if (a > 1.0) a = 1.0;
        if (a < 0.0) a = 0.0;
    }
    double lp(double x) { y += a * (x - y); return y; }
    double hp(double x) { return x - lp(x); }
};

// Chamberlin state-variable filter. Two poles with an independent resonance
// control, which is what a scrub or a howl needs — the character is in the peak
// at the corner, and a one-pole has no peak to give.
//
// Stable while cutoff stays below about sr/6. Everything here does.
struct Svf {
    double f = 0.0, q = 1.0, lp = 0.0, bp = 0.0, hp = 0.0;
    void set(double cutoff_hz, double resonance, double sr) {
        f = 2.0 * std::sin(kPi * std::min(cutoff_hz, sr / 6.0) / sr);
        q = 1.0 / std::max(resonance, 0.5);
    }
    void step(double x) {
        lp += f * bp;
        hp = x - lp - q * bp;
        bp += f * hp;
    }
};

// Remove DC, then peak-normalise to `headroom`.
//
// Both halves matter. A buffer with a DC offset wastes headroom, thumps when it
// starts and, mixed with a copy of itself at a different pitch, produces a
// wandering rumble nobody can source. And normalising WITHOUT removing DC first
// scales the offset up along with the signal.
void finish(std::vector<float>& s, double headroom) {
    if (s.empty()) return;

    double sum = 0.0;
    for (const float v : s) sum += static_cast<double>(v);
    const double mean = sum / static_cast<double>(s.size());

    double peak = 1e-12;
    for (float& v : s) {
        const double d = static_cast<double>(v) - mean;
        v = static_cast<float>(d);
        peak = std::max(peak, std::fabs(d));
    }

    const double scale = headroom / peak;
    for (float& v : s) v = static_cast<float>(static_cast<double>(v) * scale);
}

// Turn `n + fade` generated frames into an `n`-frame seamless loop.
//
// The naive version of this — blending the tail toward the head IN PLACE — is
// wrong, and wrong in a way that still sort of works, which is worse. It makes
// the last sample resemble the sample `fade` frames IN, not the sample that
// should precede the first one. This version generates `fade` extra frames past
// the end and crossfades them over the head, so the frame after the last is
// genuinely the frame before the first, and the wrap is continuous in both
// value and slope.
//
// Only for aperiodic material. Anything built from partials on a whole-cycle
// grid is already seamless and must not be touched.
void fold_loop_tail(std::vector<float>& raw, std::size_t n, std::size_t fade) {
    if (raw.size() < n + fade || fade == 0) {
        raw.resize(n);
        return;
    }
    for (std::size_t i = 0; i < fade; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(fade);
        raw[i] = raw[i] * u + raw[n + i] * (1.0f - u);
    }
    raw.resize(n);
}

// Ramp the head and tail of a ONE-SHOT to zero. Loops must never see this.
void fade_ends(std::vector<float>& s, std::size_t head, std::size_t tail) {
    const std::size_t n = s.size();
    if (n == 0) return;
    const std::size_t h = std::min(head, n / 2);
    const std::size_t t = std::min(tail, n / 2);
    for (std::size_t i = 0; i < h; ++i) {
        s[i] *= static_cast<float>(i) / static_cast<float>(h);
    }
    for (std::size_t i = 0; i < t; ++i) {
        s[n - 1 - i] *= static_cast<float>(i) / static_cast<float>(t);
    }
}

}  // namespace

float PcmClip::duration_seconds() const {
    if (sample_rate == 0) return 0.0f;
    return static_cast<float>(frame_count()) / static_cast<float>(sample_rate);
}

PcmClip synth_tone(float hz, float seconds, float fade_seconds,
                   uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;

    if (sample_rate == 0 || !(seconds > 0.0f)) return clip;

    const double rate = static_cast<double>(sample_rate);
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<double>(seconds) * rate);
    if (frames == 0) return clip;

    // Phase advance per sample, in double. Accumulating a float phase over a
    // few seconds at 48 kHz drifts audibly flat by the end of the clip.
    const double phase_step = static_cast<double>(hz) * kTwoPi / rate;

    const std::size_t fade =
        std::min(frames / 2,
                 static_cast<std::size_t>(
                     std::max(0.0, static_cast<double>(fade_seconds) * rate)));

    clip.samples.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        float s = static_cast<float>(std::sin(phase_step * static_cast<double>(i)));

        // Linear head/tail ramp so the clip starts and ends at zero.
        if (fade > 0) {
            if (i < fade) {
                s *= static_cast<float>(i) / static_cast<float>(fade);
            } else if (i >= frames - fade) {
                const std::size_t tail = frames - 1 - i;
                s *= static_cast<float>(tail) / static_cast<float>(fade);
            }
        }
        clip.samples[i] = s;
    }
    return clip;
}

// ---------------------------------------------------------------------------
//  Engine
// ---------------------------------------------------------------------------

namespace {

// Magnitude of a resonant peak at `centre` with quality `q`, evaluated at `f`.
// Standard second-order resonance response — unity at the centre, symmetric in
// log frequency, falling away either side.
double resonance(double f, double centre, double q) {
    if (f <= 0.0) return 0.0;
    const double r = f / centre - centre / f;
    return 1.0 / std::sqrt(1.0 + q * q * r * r);
}

// The FORMANT stack, and this is the whole difference between an engine and a
// buzzer.
//
// A raw harmonic series pitched up and down sounds like a synthesiser, because
// every partial rises together and the timbre never changes. A real engine's
// intake plenum, exhaust and body cavity resonate at FIXED frequencies: as the
// revs climb, the harmonics sweep UP THROUGH those resonances and each one
// lights up in turn. That sweep is what the ear hears as an engine pulling.
//
// So this is a function of absolute frequency and knows nothing about rpm.
// That is the point.
double formant_gain(double f) {
    return 0.30
         + 1.00 * resonance(f, 480.0, 1.7)     // body / plenum
         + 0.72 * resonance(f, 1150.0, 2.3)    // the mid rasp
         + 0.42 * resonance(f, 2450.0, 3.1);   // the edge on the top end
}

}  // namespace

PcmClip synth_engine_tone(float rpm, float load, float seconds,
                          uint32_t sample_rate, int cylinders) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;

    if (sample_rate == 0 || !(seconds > 0.0f) || !(rpm > 0.0f) || cylinders < 1) {
        return clip;
    }

    const double sr = static_cast<double>(sample_rate);

    // The partial grid is HALF the crank frequency, not the crank frequency.
    // Four-strokes carry real energy at half-crank orders (one cylinder firing
    // slightly stronger than its neighbour repeats every two revolutions, not
    // every one), and that lumpiness is a lot of why an engine sounds mechanical
    // rather than electronic. Building the grid at f/2 makes those orders
    // expressible AND keeps them exactly on the loop-seam lattice.
    const double f_half = static_cast<double>(rpm) / 120.0;

    // Snap the BUFFER LENGTH to a whole number of grid periods, rather than
    // snapping the frequency to the buffer. Rounding the length costs at most
    // half a sample of period — a pitch error under 0.005% — while rounding the
    // frequency to a 4 Hz bin would put idle 20% sharp. The buffer is the free
    // variable here; the pitch is not.
    const long long cycles =
        std::max(1LL, static_cast<long long>(std::llround(
                          static_cast<double>(seconds) * f_half)));
    const long long frames_ll =
        std::llround(static_cast<double>(cycles) * sr / f_half);
    if (frames_ll < 16) return clip;
    const std::size_t frames = static_cast<std::size_t>(frames_ll);

    // The exact frequency the buffer can hold. Every partial is an integer
    // multiple of this, so every one of them completes whole cycles and the
    // loop seam is phase-continuous to the last bit — no crossfade, no taper,
    // and it stays true at any playback rate.
    const std::size_t cyc = static_cast<std::size_t>(cycles);

    // One period of sine, sampled at the buffer's own resolution. Partial m is
    // then just this table stepped by (m * cycles) mod frames, which is EXACT
    // rather than an accumulated phase that drifts. It is also about five times
    // faster than calling std::sin per partial per sample, which matters when
    // the bank is twelve of these.
    std::vector<double> table(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        table[i] = std::sin(kTwoPi * static_cast<double>(i) /
                            static_cast<double>(frames));
    }

    // load: +1 power, 0 coast, -1 overrun -> t in [0, 1].
    const double t = std::clamp((static_cast<double>(load) + 1.0) * 0.5, 0.0, 1.0);

    // Which partial index is the firing order. A four-stroke fires each
    // cylinder once per two crank revolutions, so on a half-crank grid the
    // firing order lands at m == cylinders. This is the partial the ear calls
    // "the note", and it must be the strongest thing in the buffer.
    const std::size_t m_fire = static_cast<std::size_t>(cylinders);

    // Top-end rolloff. Wide open on the throttle, shut down on the overrun —
    // a trailing engine loses its rasp before it loses its volume, and getting
    // that backwards is why synthesised engines sound like vacuum cleaners.
    const double fc_top = 700.0 + 6300.0 * t;

    // How fast the firing series decays with harmonic number, which is the
    // REAL load control and the one a corner filter cannot fake.
    //
    // A brake-fired cylinder is a sharper pressure pulse than a coasting one,
    // and a sharper pulse is a slower harmonic rolloff — that is the whole
    // physics of it. 1/n^1.9 off the throttle is nearly a triangle wave, soft
    // and hollow; 1/n^0.8 on it is brighter than a sawtooth and bites. Sweeping
    // the exponent moves far more perceived energy than sweeping a corner
    // frequency does, because it lifts EVERY harmonic rather than un-shelving
    // the handful above the corner that were already inaudible.
    const double series_exp = 1.90 - 1.10 * t;

    // Deterministic per-partial phase. Starting every partial at zero stacks
    // them into one enormous spike at sample 0, which the peak-normalise then
    // divides the entire rest of the buffer by — the clip comes out quiet with
    // a click on the front. Scattering the phases drops the crest factor by
    // roughly a factor of three and costs nothing.
    Noise phase_rng(0x516E17E5ull ^ static_cast<uint64_t>(std::llround(rpm)) ^
                    (static_cast<uint64_t>(std::llround(t * 64.0)) << 32));

    constexpr std::size_t kMaxPartials = 256;
    std::vector<double> acc(frames, 0.0);

    for (std::size_t m = 1; m <= kMaxPartials; ++m) {
        const double f = static_cast<double>(m) * f_half;
        if (f > 10000.0 || f > sr * 0.45) break;

        double amp;
        if (m % m_fire == 0) {
            // The firing order and its harmonics: a 1/n^k series, which is what
            // a train of pressure pulses actually looks like. k is the load.
            amp = 1.0 / std::pow(static_cast<double>(m / m_fire), series_exp);
        } else if (m % 2 == 0) {
            // Whole crank orders that are not firing orders — rotational
            // imbalance. Present, secondary.
            amp = 0.20 / (static_cast<double>(m) * 0.5);
        } else {
            // Half orders — cylinder-to-cylinder variation. Quiet, and the
            // reason the loop does not sound like a sample of a tone generator.
            amp = 0.075 / (static_cast<double>(m) * 0.5 + 0.5);
        }

        // Off the throttle the non-firing content collapses first: the overrun
        // is a smoother, hollower sound than the power stroke.
        if (m % m_fire != 0) amp *= 0.55 + 0.95 * t;

        amp *= formant_gain(f);
        amp *= 1.0 / std::sqrt(1.0 + std::pow(f / fc_top, 4.0));

        if (amp < 1e-4) continue;

        const std::size_t step = (m * cyc) % frames;
        std::size_t idx = static_cast<std::size_t>(
            phase_rng.unit() * static_cast<double>(frames)) % frames;
        for (std::size_t i = 0; i < frames; ++i) {
            acc[i] += amp * table[idx];
            idx += step;
            if (idx >= frames) idx -= frames;
        }
    }

    // OVERRUN CRACKLE. A rally car off the throttle pops and burbles, and its
    // absence is the single most common reason a synthesised engine sounds
    // lifeless the moment the player lifts. Placed well inside the buffer with
    // fully-decayed tails so the seam stays untouched.
    if (t < 0.4) {
        const double crackle = (0.4 - t) / 0.4;
        Noise pop_rng(0xC7ACC1Eull ^ static_cast<uint64_t>(std::llround(rpm)));

        // Level the pops AGAINST the harmonic content rather than against full
        // scale. Measured, before this: the crackle was plainly audible in the
        // envelope at 1500 rpm and had vanished into the harmonic stack by
        // 3500, because a fixed absolute level competes with a tone whose own
        // level varies enormously across the rev range. Referencing the tone's
        // own RMS makes the burble equally present everywhere.
        //
        // The bed is normalised to a known peak FIRST so that reference is
        // stable, and so the saturation stage below has a fixed operating
        // point instead of one that drifts with the harmonic stack's crest.
        double bed_peak = 1e-12;
        for (const double v : acc) bed_peak = std::max(bed_peak, std::fabs(v));
        const double bed_scale = 0.55 / bed_peak;
        for (double& v : acc) v *= bed_scale;

        double harmonic_sq = 0.0;
        for (const double v : acc) harmonic_sq += v * v;
        const double harmonic_rms =
            std::sqrt(harmonic_sq / static_cast<double>(frames));

        // A RATE, not a count. The buffer length changes with rpm (it is
        // snapped to whole cycles), so a fixed number of pops per buffer means
        // a fixed number per LOOP — which is a different burble speed at every
        // anchor. Per second is the thing the ear is actually judging.
        constexpr double kPopsPerSecond = 11.0;
        const double duration = static_cast<double>(frames) / sr;
        const int pops =
            std::max(1, static_cast<int>(std::lround(kPopsPerSecond * duration)));

        for (int p = 0; p < pops; ++p) {
            // Kept clear of both ends so every tail has fully decayed before
            // the seam. The loop stays seamless by construction and the pops
            // must not be the thing that breaks it.
            const double at = pop_rng.range(0.04, 0.72);
            const double decay = pop_rng.range(120.0, 240.0);
            const double ring = pop_rng.range(220.0, 520.0);
            const double level =
                crackle * harmonic_rms * pop_rng.range(1.6, 3.4);

            // BAND-LIMITED, and this was a real bug before it was a comment.
            // A pop built from raw white noise is broadband, so a spectral
            // measurement of the overrun came out BRIGHTER than full throttle —
            // the exact opposite of the intent, and audible as a hiss on the
            // trailing throttle rather than a burble. Exhaust pops are a slug
            // of unburnt fuel going off in a big steel pipe; there is nothing
            // above a couple of kHz in one.
            OnePole pop_lp;
            pop_lp.set(900.0, sr);

            const std::size_t n0 =
                static_cast<std::size_t>(at * static_cast<double>(frames));
            for (std::size_t i = n0; i < frames; ++i) {
                const double dt =
                    static_cast<double>(i - n0) / sr;
                const double env = std::exp(-decay * dt);
                if (env < 1e-4) break;
                acc[i] += level * env *
                          (1.8 * pop_lp.lp(pop_rng.next()) +
                           0.4 * std::sin(kTwoPi * ring * dt));
            }
        }

        // SATURATE, rather than letting the pops set the peak.
        //
        // Without this the loudest pop decides the normalising divisor and the
        // engine note underneath it gets quieter as the crackle gets louder —
        // measured, the overrun's RMS fell from 0.35 to 0.19 for exactly this
        // reason. Which is backwards: a real exhaust pop does not make the
        // engine quieter, it clips against the pipe. tanh at this drive leaves
        // the bed almost untouched (it peaks at 0.55) and compresses only the
        // transients on top, which is both the honest model and the result
        // that keeps the note where it was.
        for (double& v : acc) v = std::tanh(v);
    }

    clip.samples.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        clip.samples[i] = static_cast<float>(acc[i]);
    }
    // No DC removal branch needed — finish() does it, and every partial here is
    // zero-mean anyway, so it costs one pass and buys the crackle's asymmetry.
    finish(clip.samples, 0.92);
    return clip;
}

PcmClip synth_engine_loop(float hz, float seconds, uint32_t sample_rate) {
    return synth_engine_tone(hz * 60.0f, 1.0f, seconds, sample_rate, 4);
}

// ---------------------------------------------------------------------------
//  Surfaces
// ---------------------------------------------------------------------------

PcmClip synth_tyre_scrub(float seconds, uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0 || !(seconds > 0.0f)) return clip;

    const double sr = static_cast<double>(sample_rate);
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<double>(seconds) * sr);
    const std::size_t fade = static_cast<std::size_t>(0.05 * sr);
    if (frames < fade * 4) return clip;

    Noise noise(0x5C2BBB1Eull);
    OnePole tilt;
    tilt.set(320.0, sr);

    // Two resonant bands, not one. A single band reads as filtered hiss; a pair
    // beating against each other reads as rubber under load, because that is
    // what a tyre does — the contact patch has more than one thing ringing in
    // it. Kept broadband on purpose: the RUNTIME tracks slip with a low-pass on
    // the voice, so this clip has to have the top end available to take away.
    Svf squeal_a, squeal_b;
    squeal_a.set(2100.0, 4.5, sr);
    squeal_b.set(3350.0, 3.2, sr);

    std::vector<float> raw(frames + fade);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const double time = static_cast<double>(i) / sr;
        const double w = noise.next();

        squeal_a.step(w);
        squeal_b.step(w);

        // The wobble is what stops it sounding like a held note. Both LFO rates
        // are whole cycles across the requested length, so they survive the
        // seam fold without a beat appearing at the loop point.
        const double wob = 1.0 + 0.28 * std::sin(kTwoPi * 7.0 * time)
                                + 0.16 * std::sin(kTwoPi * 11.0 * time);

        const double s = 0.55 * tilt.hp(w)
                       + 0.85 * squeal_a.bp * wob
                       + 0.50 * squeal_b.bp;
        raw[i] = static_cast<float>(s);
    }

    fold_loop_tail(raw, frames, fade);
    clip.samples = std::move(raw);
    finish(clip.samples, 0.9);
    return clip;
}

PcmClip synth_surface_roll(Surface surface, float seconds,
                           uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0 || !(seconds > 0.0f)) return clip;

    const double sr = static_cast<double>(sample_rate);
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<double>(seconds) * sr);
    const std::size_t fade = static_cast<std::size_t>(0.06 * sr);
    if (frames < fade * 4) return clip;

    // Per surface: the body corner, how much loose grit rattles on top, how
    // often a stone actually strikes, and how bright that strike is.
    struct Profile {
        double body_hz;
        double grit;
        double strikes_per_sec;
        double strike_hz;
        double strike_decay;
    };
    Profile p{};
    switch (surface) {
        case Surface::Tarmac:
            // Coarse chip seal: a thin, even hiss and essentially no debris.
            p = Profile{2600.0, 0.10, 6.0, 3200.0, 900.0};
            break;
        case Surface::Gravel:
            // The loud one. Loose stone under the arches at a rate you can
            // almost count, which is the entire identity of a rally stage.
            p = Profile{900.0, 0.95, 90.0, 2100.0, 520.0};
            break;
        case Surface::Dirt:
            // Packed earth: gravel's body without gravel's stones.
            p = Profile{620.0, 0.45, 26.0, 1400.0, 420.0};
            break;
        case Surface::Snow:
            // Almost nothing above 400 Hz, plus the compression squeak.
            p = Profile{330.0, 0.20, 14.0, 900.0, 300.0};
            break;
        case Surface::kCount:
            return clip;
    }

    Noise noise(0x50FACE00ull ^ static_cast<uint64_t>(surface));
    OnePole body;
    body.set(p.body_hz, sr);
    OnePole rumble;
    rumble.set(110.0, sr);

    std::vector<float> raw(frames + fade, 0.0f);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const double w = noise.next();
        const double s = 1.35 * body.lp(w) + 0.9 * rumble.lp(w);
        raw[i] = static_cast<float>(s);
    }

    // Discrete strikes on top of the bed. A pure filtered-noise bed is what
    // every naive surface loop sounds like, and it is why they all sound like
    // the same tape hiss at different cutoffs. Individual stones give it grain.
    if (p.grit > 0.01) {
        Noise hit(0x9B17CE55ull ^ static_cast<uint64_t>(surface));
        const double span = static_cast<double>(raw.size()) / sr;
        const int count = static_cast<int>(p.strikes_per_sec * span);
        for (int k = 0; k < count; ++k) {
            const double at = hit.range(0.0, span);
            const std::size_t n0 = static_cast<std::size_t>(at * sr);
            if (n0 >= raw.size()) continue;
            const double level = p.grit * hit.range(0.25, 1.0);
            const double freq = p.strike_hz * hit.range(0.7, 1.4);
            for (std::size_t i = n0; i < raw.size(); ++i) {
                const double dt = static_cast<double>(i - n0) / sr;
                const double env = std::exp(-p.strike_decay * dt);
                if (env < 1e-4) break;
                raw[i] += static_cast<float>(level * env *
                                             std::sin(kTwoPi * freq * dt));
            }
        }
    }

    fold_loop_tail(raw, frames, fade);
    clip.samples = std::move(raw);
    finish(clip.samples, 0.9);
    return clip;
}

// ---------------------------------------------------------------------------
//  Impacts and weather
// ---------------------------------------------------------------------------

PcmClip synth_suspension_thump(float weight, uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0) return clip;

    const double w = std::clamp(static_cast<double>(weight), 0.0, 1.0);
    const double sr = static_cast<double>(sample_rate);

    // Heavier means LOWER and LONGER, which is the one thing every impact sound
    // has to get right. A heavy landing that is just a loud light landing reads
    // as a volume change, not as mass.
    const double dur = 0.34 + 0.42 * w;
    const double thump_hz = 78.0 - 34.0 * w;
    const double decay = 26.0 - 11.0 * w;

    const std::size_t frames = static_cast<std::size_t>(dur * sr);
    if (frames < 64) return clip;

    Noise noise(0x7B0FF0EDull ^ static_cast<uint64_t>(std::llround(w * 255.0)));
    Svf rattle;
    rattle.set(420.0 - 120.0 * w, 2.4, sr);

    std::vector<float> out(frames, 0.0f);
    for (std::size_t i = 0; i < frames; ++i) {
        const double dt = static_cast<double>(i) / sr;
        const double n = noise.next();

        // The spring compressing: a damped low sine, plus its octave for a bit
        // of bite so it reads on a laptop speaker as well as on a subwoofer.
        double s = std::sin(kTwoPi * thump_hz * dt) * std::exp(-decay * dt)
                 + 0.35 * std::sin(kTwoPi * thump_hz * 2.0 * dt) *
                       std::exp(-decay * 1.8 * dt);

        // Tyre contact: a few milliseconds of noise. Short enough to be a
        // transient, not a hiss.
        if (dt < 0.012) s += 0.55 * n * (1.0 - dt / 0.012);

        // Chassis and load rattling in the bodyshell after the hit.
        rattle.step(n);
        s += (0.30 + 0.35 * w) * rattle.bp * std::exp(-14.0 * dt);

        // The rebound. Only the heavy end of the range gets one: a kerb strike
        // does not bounce, a crest landing does.
        if (w > 0.35) {
            const double rb = dt - 0.115;
            if (rb > 0.0) {
                s += 0.42 * (w - 0.35) / 0.65 *
                     std::sin(kTwoPi * thump_hz * 1.15 * rb) *
                     std::exp(-decay * 1.5 * rb);
            }
        }
        out[i] = static_cast<float>(s);
    }

    fade_ends(out, static_cast<std::size_t>(0.0012 * sr),
              static_cast<std::size_t>(0.020 * sr));
    clip.samples = std::move(out);
    finish(clip.samples, 0.95);
    // finish() rescales, which can lift the very first and last samples off
    // zero again by a hair; re-ramp so the one-shot is click-free as SHIPPED,
    // not as intermediate.
    fade_ends(clip.samples, static_cast<std::size_t>(0.0012 * sr),
              static_cast<std::size_t>(0.020 * sr));
    return clip;
}

PcmClip synth_rain_bed(float seconds, uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0 || !(seconds > 0.0f)) return clip;

    const double sr = static_cast<double>(sample_rate);
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<double>(seconds) * sr);
    const std::size_t fade = static_cast<std::size_t>(0.08 * sr);
    if (frames < fade * 4) return clip;

    Noise noise(0x4A17FA11ull);
    OnePole hiss;
    hiss.set(1400.0, sr);
    OnePole body;
    body.set(600.0, sr);

    std::vector<float> raw(frames + fade, 0.0f);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const double time = static_cast<double>(i) / sr;
        const double n = noise.next();
        // Hiss on top, body underneath, and a slow swell so a long stint in the
        // rain does not turn into a static tone the ear tunes out entirely.
        const double swell = 1.0 + 0.13 * std::sin(kTwoPi * 0.5 * time)
                                 + 0.07 * std::sin(kTwoPi * 1.25 * time);
        raw[i] = static_cast<float>((1.10 * hiss.hp(n) + 0.55 * body.lp(n)) * swell);
    }

    // Individual drops striking the roof and screen. Without these it is a
    // shower, not weather.
    Noise drop(0xD20B1E75ull);
    const double span = static_cast<double>(raw.size()) / sr;
    const int drops = static_cast<int>(140.0 * span);
    for (int k = 0; k < drops; ++k) {
        const std::size_t n0 =
            static_cast<std::size_t>(drop.range(0.0, span) * sr);
        if (n0 >= raw.size()) continue;
        const double level = drop.range(0.10, 0.42);
        const double freq = drop.range(2600.0, 6200.0);
        for (std::size_t i = n0; i < raw.size(); ++i) {
            const double dt = static_cast<double>(i - n0) / sr;
            const double env = std::exp(-1400.0 * dt);
            if (env < 1e-4) break;
            raw[i] += static_cast<float>(level * env *
                                         std::sin(kTwoPi * freq * dt));
        }
    }

    fold_loop_tail(raw, frames, fade);
    clip.samples = std::move(raw);
    finish(clip.samples, 0.88);
    return clip;
}

PcmClip synth_wind_bed(float seconds, uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0 || !(seconds > 0.0f)) return clip;

    const double sr = static_cast<double>(sample_rate);
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<double>(seconds) * sr);
    const std::size_t fade = static_cast<std::size_t>(0.10 * sr);
    if (frames < fade * 4) return clip;

    Noise noise(0x81057ull);
    OnePole low;
    low.set(240.0, sr);
    Svf howl;
    howl.set(520.0, 3.6, sr);

    std::vector<float> raw(frames + fade, 0.0f);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const double time = static_cast<double>(i) / sr;
        const double n = noise.next();
        howl.step(n);

        // Gusts. Three slow LFOs at unrelated-but-whole-cycle rates so the
        // pattern takes long enough to repeat that the ear never catches it.
        const double gust = 0.62
                          + 0.24 * std::sin(kTwoPi * 0.25 * time)
                          + 0.14 * std::sin(kTwoPi * 0.75 * time + 1.1)
                          + 0.09 * std::sin(kTwoPi * 1.5 * time + 2.3);

        raw[i] = static_cast<float>((1.5 * low.lp(n) + 0.55 * howl.bp) * gust);
    }

    fold_loop_tail(raw, frames, fade);
    clip.samples = std::move(raw);
    finish(clip.samples, 0.88);
    return clip;
}

// ---------------------------------------------------------------------------
//  Stingers
// ---------------------------------------------------------------------------

namespace {

// One struck note: a fundamental with two harmonics and one deliberately
// inharmonic partial for a bell edge, under a fast-attack exponential decay.
void add_note(std::vector<float>& out, double sr, double at_s, double hz,
              double level, double decay) {
    const std::size_t n0 = static_cast<std::size_t>(at_s * sr);
    const std::size_t attack = static_cast<std::size_t>(0.004 * sr);
    for (std::size_t i = n0; i < out.size(); ++i) {
        const double dt = static_cast<double>(i - n0) / sr;
        double env = std::exp(-decay * dt);
        if (env < 1e-4) break;
        if (i - n0 < attack) {
            env *= static_cast<double>(i - n0) / static_cast<double>(attack);
        }
        const double s = std::sin(kTwoPi * hz * dt)
                       + 0.34 * std::sin(kTwoPi * hz * 2.0 * dt)
                       + 0.13 * std::sin(kTwoPi * hz * 3.0 * dt)
                       + 0.09 * std::sin(kTwoPi * hz * 2.76 * dt);
        out[i] += static_cast<float>(level * env * s);
    }
}

}  // namespace

PcmClip synth_checkpoint_stinger(uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0) return clip;

    const double sr = static_cast<double>(sample_rate);
    const std::size_t frames = static_cast<std::size_t>(0.55 * sr);
    std::vector<float> out(frames, 0.0f);

    // A5, then a just major sixth above it. RISING and MAJOR, both deliberate:
    // interval direction and quality are the entire vocabulary available for
    // "good thing happened" when there is no sample on disk to lean on, and a
    // falling or minor interval reads as a penalty no matter how bright it is.
    constexpr double kBase = 880.0;
    add_note(out, sr, 0.000, kBase, 0.80, 9.0);
    add_note(out, sr, 0.105, kBase * 5.0 / 3.0, 1.00, 6.5);

    fade_ends(out, static_cast<std::size_t>(0.002 * sr),
              static_cast<std::size_t>(0.030 * sr));
    clip.samples = std::move(out);
    finish(clip.samples, 0.92);
    fade_ends(clip.samples, static_cast<std::size_t>(0.002 * sr),
              static_cast<std::size_t>(0.030 * sr));
    return clip;
}

PcmClip synth_lap_record_stinger(uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;
    if (sample_rate == 0) return clip;

    const double sr = static_cast<double>(sample_rate);
    const std::size_t frames = static_cast<std::size_t>(1.35 * sr);
    std::vector<float> out(frames, 0.0f);

    // The checkpoint's idea, earned out: a full major arpeggio climbing to the
    // octave. Longer, brighter and unmistakably a bigger deal than a
    // checkpoint, which matters because the player hears one of these a dozen
    // times a stage and the other once a week.
    constexpr double kBase = 880.0;
    add_note(out, sr, 0.000, kBase, 0.70, 10.0);
    add_note(out, sr, 0.085, kBase * 5.0 / 4.0, 0.78, 9.5);
    add_note(out, sr, 0.170, kBase * 3.0 / 2.0, 0.86, 9.0);
    add_note(out, sr, 0.255, kBase * 2.0, 1.00, 2.6);

    // A shimmer two octaves up, swelling in behind the top note and ringing
    // out under it.
    for (std::size_t i = 0; i < frames; ++i) {
        const double dt = static_cast<double>(i) / sr;
        if (dt < 0.255) continue;
        const double u = dt - 0.255;
        const double env = std::min(1.0, u / 0.10) * std::exp(-2.2 * u);
        out[i] += static_cast<float>(
            0.16 * env * (std::sin(kTwoPi * kBase * 4.0 * dt) +
                          0.6 * std::sin(kTwoPi * kBase * 6.0 * dt)));
    }

    fade_ends(out, static_cast<std::size_t>(0.002 * sr),
              static_cast<std::size_t>(0.040 * sr));
    clip.samples = std::move(out);
    finish(clip.samples, 0.92);
    fade_ends(clip.samples, static_cast<std::size_t>(0.002 * sr),
              static_cast<std::size_t>(0.040 * sr));
    return clip;
}

// ---------------------------------------------------------------------------
//  The bank
// ---------------------------------------------------------------------------

SfxBank synth_bank(uint32_t sample_rate) {
    SfxBank bank;
    bank.sample_rate = sample_rate;

    // Anchors, tighter at the bottom than the top. No adjacent pair is more
    // than a sixth apart, so no layer is ever stretched far from where it was
    // made and the formants survive the resampler — which is the whole reason
    // there is a bank rather than one loop.
    const std::array<float, kEngineLayerCount> anchors{
        900.0f, 1400.0f, 2100.0f, 2900.0f, 4100.0f, 5400.0f, 6800.0f};
    bank.engine_rpm = anchors;

    for (std::size_t i = 0; i < kEngineLayerCount; ++i) {
        bank.engine_power[i] =
            synth_engine_tone(anchors[i], 1.0f, 0.25f, sample_rate, 4);
        bank.engine_overrun[i] =
            synth_engine_tone(anchors[i], -1.0f, 0.25f, sample_rate, 4);
    }

    bank.tyre_scrub = synth_tyre_scrub(2.0f, sample_rate);
    for (std::size_t i = 0; i < kSurfaceCount; ++i) {
        bank.surface_roll[i] =
            synth_surface_roll(static_cast<Surface>(i), 2.0f, sample_rate);
    }

    for (std::size_t i = 0; i < SfxBank::kThumpCount; ++i) {
        const float w = static_cast<float>(i) /
                        static_cast<float>(SfxBank::kThumpCount - 1);
        bank.suspension_thump[i] = synth_suspension_thump(w, sample_rate);
    }

    bank.rain = synth_rain_bed(3.0f, sample_rate);
    bank.wind = synth_wind_bed(4.0f, sample_rate);

    bank.checkpoint = synth_checkpoint_stinger(sample_rate);
    bank.lap_record = synth_lap_record_stinger(sample_rate);

    return bank;
}

// ---------------------------------------------------------------------------
//  Per-frame parameter maps
// ---------------------------------------------------------------------------

namespace {

// Equal-power crossfade weights for a blend position in [0, 1]. Linear weights
// would dip in the middle: two decorrelated sources at 0.5 each sum to 0.707 of
// the power, and a rev sweep through every anchor boundary would sound like the
// engine breathing.
void equal_power(double u, double& a, double& b) {
    const double c = std::clamp(u, 0.0, 1.0) * kPi * 0.5;
    a = std::cos(c);
    b = std::sin(c);
}

}  // namespace

EngineMix engine_mix(const SfxBank& bank, float rpm, float load) {
    EngineMix mix;

    const float lo = bank.engine_rpm.front();
    const float hi = bank.engine_rpm.back();
    if (!(hi > lo)) return mix;

    const float r = std::clamp(rpm, lo, hi);

    std::size_t a = 0;
    while (a + 2 < kEngineLayerCount && bank.engine_rpm[a + 1] < r) ++a;
    const std::size_t b = a + 1;

    const float span = bank.engine_rpm[b] - bank.engine_rpm[a];
    const double u = span > 0.0f
                         ? static_cast<double>((r - bank.engine_rpm[a]) / span)
                         : 0.0;
    double wa = 0.0, wb = 0.0;
    equal_power(u, wa, wb);

    // Load crossfades the power character against the overrun character, also
    // equal-power: lifting off mid-corner must not punch a hole in the level.
    const double t = std::clamp((static_cast<double>(load) + 1.0) * 0.5, 0.0, 1.0);
    double w_over = 0.0, w_power = 0.0;
    equal_power(t, w_over, w_power);

    // Level. Loud on the throttle, present but backed off coasting, and it
    // still grows with revs on the overrun, because a car braking from
    // seven thousand is not quiet.
    const double rev01 = static_cast<double>((r - lo) / (hi - lo));
    const double level = std::min(0.60, 0.20 + 0.26 * t + 0.20 * rev01);

    const auto layer = [&](std::size_t idx, const PcmClip& clip,
                           std::size_t anchor, double gain) {
        mix.layers[idx].clip = clip.empty() ? nullptr : &clip;
        mix.layers[idx].gain = static_cast<float>(level * gain);
        mix.layers[idx].pitch = r / bank.engine_rpm[anchor];
        mix.layers[idx].lp_cutoff_hz = 0.0f;
    };

    layer(0, bank.engine_power[a], a, wa * w_power);
    layer(1, bank.engine_power[b], b, wb * w_power);
    layer(2, bank.engine_overrun[a], a, wa * w_over);
    layer(3, bank.engine_overrun[b], b, wb * w_over);
    return mix;
}

VoiceMix tyre_scrub_mix(const SfxBank& bank, float lateral_slip,
                        float speed_mps) {
    VoiceMix v;
    if (bank.tyre_scrub.empty()) return v;

    const float slip = std::clamp(lateral_slip, 0.0f, 1.0f);

    // Below walking pace there is no scrub, whatever the slip ratio says. A
    // stationary car with the wheel wound onto full lock computes enormous
    // lateral slip, and a tyre screaming in a car park is a bug people report
    // as "the audio is haunted".
    const float gate = std::clamp(speed_mps / 4.0f, 0.0f, 1.0f);

    v.clip = &bank.tyre_scrub;
    v.gain = 0.85f * slip * slip * gate;
    v.pitch = 0.85f + 0.35f * slip;

    // THE REQUIREMENT, and it is one line: brightness tracks slip. A tyre just
    // starting to let go is a dull rumble; one fully alight is a scream. Same
    // clip, different corner.
    v.lp_cutoff_hz = 900.0f + 7200.0f * slip;
    return v;
}

VoiceMix surface_roll_mix(const SfxBank& bank, float speed_mps,
                          Surface surface) {
    VoiceMix v;
    const std::size_t i = static_cast<std::size_t>(surface);
    if (i >= kSurfaceCount || bank.surface_roll[i].empty()) return v;

    // 35 m/s is 126 km/h — flat out on a stage. Everything scales to that.
    const float s01 = std::clamp(speed_mps / 35.0f, 0.0f, 1.0f);

    struct Trim { float level; float base_hz; float span_hz; };
    Trim trim{};
    switch (surface) {
        case Surface::Tarmac: trim = Trim{0.42f, 1500.0f, 6000.0f}; break;
        case Surface::Gravel: trim = Trim{0.80f,  700.0f, 4400.0f}; break;
        case Surface::Dirt:   trim = Trim{0.62f,  520.0f, 3000.0f}; break;
        case Surface::Snow:   trim = Trim{0.38f,  300.0f, 1500.0f}; break;
        case Surface::kCount: return v;
    }

    v.clip = &bank.surface_roll[i];
    // Square root, not linear: rolling noise level tracks perceived speed much
    // more closely than it tracks speed, and a linear ramp leaves the car
    // silent for the whole first half of the straight.
    v.gain = trim.level * std::sqrt(s01);
    v.pitch = 0.70f + 0.60f * s01;
    v.lp_cutoff_hz = trim.base_hz + trim.span_hz * s01;
    return v;
}

VoiceMix suspension_thump_mix(const SfxBank& bank, float impact_mps) {
    VoiceMix v;

    // Below this it did not land, it just drove over something. Returning
    // silence rather than a very quiet thump is deliberate: a thump you can
    // only just hear on every kerb is worse than no thump at all.
    constexpr float kFloor = 1.2f;
    constexpr float kCeiling = 9.5f;
    if (!(impact_mps > kFloor)) return v;

    const float w =
        std::clamp((impact_mps - kFloor) / (kCeiling - kFloor), 0.0f, 1.0f);

    std::size_t idx = static_cast<std::size_t>(
        w * static_cast<float>(SfxBank::kThumpCount));
    if (idx >= SfxBank::kThumpCount) idx = SfxBank::kThumpCount - 1;
    if (bank.suspension_thump[idx].empty()) return v;

    v.clip = &bank.suspension_thump[idx];
    v.gain = 0.40f + 0.60f * w;
    // Pitched DOWN as the hit gets heavier, on top of the already-lower clip.
    // Two mechanisms saying the same thing, because mass is the one property
    // an impact sound has to communicate in the first 20 ms.
    v.pitch = 1.12f - 0.26f * w;
    return v;
}

WeatherMix weather_mix(const SfxBank& bank, float rain_intensity,
                       float wind_intensity) {
    WeatherMix mix;

    const float r = std::clamp(rain_intensity, 0.0f, 1.0f);
    const float w = std::clamp(wind_intensity, 0.0f, 1.0f);

    if (!bank.rain.empty() && r > 0.0f) {
        mix.rain.clip = &bank.rain;
        mix.rain.gain = 0.75f * std::sqrt(r);
        mix.rain.pitch = 1.0f;
        mix.rain.lp_cutoff_hz = 1200.0f + 6500.0f * r;
    }
    if (!bank.wind.empty() && w > 0.0f) {
        mix.wind.clip = &bank.wind;
        mix.wind.gain = 0.70f * std::sqrt(w);
        // Wind pitches up as it strengthens; rain does not. Heavier rain is
        // more drops, not faster ones.
        mix.wind.pitch = 0.90f + 0.28f * w;
        mix.wind.lp_cutoff_hz = 420.0f + 2800.0f * w;
    }
    return mix;
}

// ---------------------------------------------------------------------------
//  Optional file clips
// ---------------------------------------------------------------------------

namespace {

uint32_t le32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t le16(const unsigned char* p) {
    return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) |
                                 (static_cast<uint32_t>(p[1]) << 8));
}

bool tag_is(const unsigned char* p, const char* t) {
    return p[0] == static_cast<unsigned char>(t[0]) &&
           p[1] == static_cast<unsigned char>(t[1]) &&
           p[2] == static_cast<unsigned char>(t[2]) &&
           p[3] == static_cast<unsigned char>(t[3]);
}

// Warn once per path, for the life of the process. A missing optional clip is
// an EXPECTED state, not an error, and logging it on every frame or every play
// is how a log stops being something anyone reads.
bool first_warning_for(const std::string& path) {
    static std::set<std::string> warned;
    return warned.insert(path).second;
}

}  // namespace

bool load_wav_clip(const std::string& path, PcmClip& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 44) return false;
    if (!tag_is(bytes.data(), "RIFF") || !tag_is(bytes.data() + 8, "WAVE")) {
        return false;
    }

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const unsigned char* data = nullptr;
    std::size_t data_bytes = 0;

    // Walk the chunk list rather than assuming the canonical 44-byte header.
    // Real files carry LIST/fact/cue chunks in front of the data, and a reader
    // that assumes the layout silently plays metadata as audio.
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char* id = bytes.data() + pos;
        const uint32_t size = le32(bytes.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (body + size > bytes.size()) break;

        if (tag_is(id, "fmt ") && size >= 16) {
            format = le16(bytes.data() + body);
            channels = le16(bytes.data() + body + 2);
            rate = le32(bytes.data() + body + 4);
            bits = le16(bytes.data() + body + 14);
            // WAVE_FORMAT_EXTENSIBLE hides the real format in the subformat
            // GUID, whose first two bytes are the plain format tag.
            if (format == 0xFFFE && size >= 40) {
                format = le16(bytes.data() + body + 24);
            }
        } else if (tag_is(id, "data")) {
            data = bytes.data() + body;
            data_bytes = size;
        }
        pos = body + size + (size & 1u);  // chunks are word-aligned
    }

    if (!data || data_bytes == 0 || channels == 0 || rate == 0) return false;
    if (format != 1 && format != 3) return false;
    if (format == 3 && bits != 32) return false;

    const std::size_t bytes_per_sample = bits / 8u;
    if (bytes_per_sample == 0) return false;
    const std::size_t count = data_bytes / bytes_per_sample;
    if (count == 0) return false;

    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char* p = data + i * bytes_per_sample;
        switch (bits) {
            case 8:
                samples[i] = (static_cast<float>(p[0]) - 128.0f) / 128.0f;
                break;
            case 16: {
                const int16_t v = static_cast<int16_t>(le16(p));
                samples[i] = static_cast<float>(v) / 32768.0f;
                break;
            }
            case 24: {
                int32_t v = static_cast<int32_t>(
                    (static_cast<uint32_t>(p[0]) << 8) |
                    (static_cast<uint32_t>(p[1]) << 16) |
                    (static_cast<uint32_t>(p[2]) << 24));
                samples[i] = static_cast<float>(v >> 8) / 8388608.0f;
                break;
            }
            case 32:
                if (format == 3) {
                    float v = 0.0f;
                    const uint32_t raw = le32(p);
                    std::memcpy(&v, &raw, sizeof(v));
                    samples[i] = v;
                } else {
                    const int32_t v = static_cast<int32_t>(le32(p));
                    samples[i] = static_cast<float>(v) / 2147483648.0f;
                }
                break;
            default:
                return false;
        }
    }

    // Only commit to `out` once the whole file has parsed. A half-replaced clip
    // is worse than a missing one: the caller has no way to tell.
    out.sample_rate = rate;
    out.channels = channels;
    out.samples = std::move(samples);
    return true;
}

bool override_clip_from_wav(PcmClip& clip, const std::string& path) {
    PcmClip loaded;
    if (load_wav_clip(path, loaded)) {
        clip = std::move(loaded);
        AP_INFO("audio: %s overrides the synthesised clip (%zu frames)",
                path.c_str(), clip.frame_count());
        return true;
    }
    if (first_warning_for(path)) {
        AP_WARN("audio: optional clip %s missing or unreadable; "
                "keeping the synthesised one", path.c_str());
    }
    return false;
}

}  // namespace apricot
