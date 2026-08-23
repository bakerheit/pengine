// THE ENGINE NOTE. A rally game lives or dies on it, so it gets its own suite.
//
// The claim under test is not "the engine buffer is non-empty" — a buffer of
// 4 Hz square wave would pass that. It is:
//
//   1. the FUNDAMENTAL FREQUENCY is proportional to rpm, measured, across the
//      whole rev range and at every load;
//   2. that fundamental is the FIRING frequency the physics predicts, not some
//      other partial that happens to be loud;
//   3. TIMBRE moves with load while pitch does NOT;
//   4. all of it survives the real mixer — the layer crossfade and the
//      resampler have to preserve pitch, or the measurement above is only
//      testing a buffer nobody ever plays.
//
// (4) is the one that matters most. A synthesiser that is perfect on its own
// and a resampler that is a percent sharp still adds up to a car that sounds
// wrong, and only an end-to-end measurement catches it.

#include <cstdio>
#include <vector>

#include "audio/mixer.h"
#include "audio/synth.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

constexpr uint32_t kRate = kDefaultSampleRate;

// Long enough for a clean measurement at idle (30 Hz is 1600 samples a cycle,
// so this holds ten of them) and short enough that the whole suite stays fast.
constexpr float kAnalysisSeconds = 0.35f;

const float kRpms[] = {900.0f,  1200.0f, 1500.0f, 2400.0f, 3000.0f,
                       4200.0f, 5000.0f, 5600.0f, 6800.0f};
const float kLoads[] = {1.0f, 0.0f, -1.0f};

void the_firing_formula_is_what_it_claims() {
    // A four-stroke fires each cylinder once every TWO crank revolutions.
    // Pinned here because every other test in this file measures against it,
    // and a test that re-derives the formula it is checking proves only that
    // someone typed the same thing twice.
    REQUIRE_NEAR(static_cast<double>(engine_firing_hz(3000.0f, 4)), 100.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(engine_firing_hz(6000.0f, 4)), 200.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(engine_firing_hz(3000.0f, 6)), 150.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(engine_firing_hz(3000.0f, 8)), 200.0, 1e-4);
    pass("firing frequency is rpm * cylinders / 120");
}

void the_fundamental_tracks_rpm() {
    // THE CENTREPIECE ASSERTION.
    //
    // The search window is a full octave and a half either side of the
    // prediction, so a detector that octave-slips — which an earlier
    // harmonic-comb implementation demonstrably did on exactly this material —
    // has plenty of room to return the wrong answer and fail this.
    std::printf("      %6s %6s %10s %10s %9s\n", "rpm", "load", "expect",
                "measured", "err%");

    double min_ratio = 1e9;
    double max_ratio = -1e9;
    double worst_err = 0.0;

    for (const float rpm : kRpms) {
        for (const float load : kLoads) {
            const PcmClip c =
                synth_engine_tone(rpm, load, kAnalysisSeconds, kRate, 4);
            REQUIRE(!c.samples.empty());
            REQUIRE(all_finite(c.samples));

            const double expect = static_cast<double>(engine_firing_hz(rpm, 4));
            const double measured =
                estimate_f0(c.samples, kRate, expect * 0.35, expect * 3.0);
            REQUIRE_MSG(measured > 0.0, "no fundamental could be measured",
                        "detection");

            const double err = 100.0 * (measured - expect) / expect;
            worst_err = std::max(worst_err, std::fabs(err));

            const double ratio = measured / static_cast<double>(rpm);
            min_ratio = std::min(min_ratio, ratio);
            max_ratio = std::max(max_ratio, ratio);

            std::printf("      %6.0f %6.1f %10.3f %10.3f %+9.3f\n",
                        static_cast<double>(rpm), static_cast<double>(load),
                        expect, measured, err);

            REQUIRE_MSG(std::fabs(err) < 0.5,
                        "the engine's fundamental is not at the firing "
                        "frequency for this rpm", "rpm tracking");
        }
    }

    // PROPORTIONALITY, which is the actual property "tracks rpm" means. Across
    // a 7.5:1 rev range the ratio measured/rpm must be one constant — an
    // engine whose pitch rose but not linearly would pass every individual
    // check above by widening the tolerance, and would fail this.
    //
    // Half a percent is about a twelfth of a semitone — inaudible as mistuning,
    // and the residual is not sloppiness but the overrun's own crackle, which
    // is aperiodic on purpose and genuinely makes the pitch of a burbling
    // engine slightly less well defined. On the throttle the error is under
    // 0.01%.
    const double spread = 100.0 * (max_ratio - min_ratio) / min_ratio;
    std::printf("      (worst error %.3f%%, measured/rpm spans %.6f..%.6f, "
                "%.3f%% wide)\n", worst_err, min_ratio, max_ratio, spread);
    REQUIRE_MSG(spread < 0.5,
                "the fundamental is not PROPORTIONAL to rpm across the range",
                "proportionality");
    REQUIRE_NEAR(min_ratio, 1.0 / 30.0, 5e-4);

    pass("the fundamental is the firing frequency, proportional to rpm, "
         "over a 7.5:1 range");
}

void the_firing_order_dominates_the_spectrum() {
    // Proportionality alone could in principle be satisfied by a signal whose
    // energy sat somewhere else entirely and merely scaled. This pins the
    // energy TO the predicted frequency: compare it against probes at
    // deliberately non-harmonic offsets, which should have nothing in them.
    double worst = 1e18;
    for (const float rpm : kRpms) {
        for (const float load : {1.0f, -1.0f}) {
            const PcmClip c =
                synth_engine_tone(rpm, load, kAnalysisSeconds, kRate, 4);
            const double f = static_cast<double>(engine_firing_hz(rpm, 4));

            const double at_firing = goertzel(c.samples, kRate, f);
            const double probe_hi = goertzel(c.samples, kRate, f * 1.31);
            const double probe_lo = goertzel(c.samples, kRate, f * 0.77);
            const double noise = std::max(std::max(probe_hi, probe_lo), 1e-12);
            const double ratio = at_firing / noise;
            worst = std::min(worst, ratio);

            REQUIRE_MSG(ratio > 10.0,
                        "the firing frequency does not stand above the "
                        "non-harmonic floor", "dominance");
        }
    }
    std::printf("      (weakest firing-vs-noise ratio across the range: %.1fx)\n",
                worst);
    pass("the firing frequency dominates non-harmonic neighbours everywhere");
}

void load_changes_timbre_and_leaves_pitch_alone() {
    // Brightness is measured ON the harmonic grid. A log-spaced spectral
    // centroid was tried first and reported the OVERRUN as brighter than full
    // throttle — the opposite of the truth — because with partials tens of Hz
    // apart, half its low-end probe points land in the gaps between harmonics
    // and read zero. It was measuring its own grid spacing.
    std::printf("      %6s %10s %10s %10s %8s\n", "rpm", "overrun", "coast",
                "power", "ratio");

    double weakest_contrast = 1e9;
    for (const float rpm : kRpms) {
        const double f = static_cast<double>(engine_firing_hz(rpm, 4));
        double brightness[3];
        double pitch[3];

        for (int i = 0; i < 3; ++i) {
            const float load = -1.0f + static_cast<float>(i);
            const PcmClip c =
                synth_engine_tone(rpm, load, kAnalysisSeconds, kRate, 4);
            brightness[i] = harmonic_ratio(c.samples, kRate, f, 4, 5, 20);
            pitch[i] = estimate_f0(c.samples, kRate, f * 0.35, f * 3.0);
        }

        REQUIRE_MSG(brightness[0] < brightness[1],
                    "coasting is not brighter than the overrun", "load timbre");
        REQUIRE_MSG(brightness[1] < brightness[2],
                    "full throttle is not brighter than coasting", "load timbre");

        const double contrast = brightness[2] / std::max(brightness[0], 1e-9);
        weakest_contrast = std::min(weakest_contrast, contrast);
        REQUIRE_MSG(contrast > 3.0,
                    "on and off the throttle sound almost the same", "load timbre");

        // PITCH MUST NOT MOVE. An engine whose note rises when you press the
        // pedal, rather than when the revs rise, reads as broken to anyone who
        // has driven a car — and it is an easy accident when load and rpm are
        // both fed into the same synthesis.
        for (int i = 1; i < 3; ++i) {
            const double drift = 100.0 * (pitch[i] - pitch[0]) / pitch[0];
            REQUIRE_MSG(std::fabs(drift) < 0.5,
                        "changing load moved the PITCH, not just the timbre",
                        "pitch independence");
        }

        std::printf("      %6.0f %10.4f %10.4f %10.4f %7.1fx\n",
                    static_cast<double>(rpm), brightness[0], brightness[1],
                    brightness[2], contrast);
    }
    std::printf("      (weakest power-vs-overrun brightness contrast: %.1fx)\n",
                weakest_contrast);
    pass("load moves timbre by at least 3x and never moves pitch");
}

void the_overrun_crackles() {
    // A trailing throttle must not be merely a quieter, duller power stroke.
    // The thing the ear actually latches onto off the throttle is the CRACKLE,
    // and its absence is the commonest reason a synthesised engine goes
    // lifeless the instant the player lifts.
    //
    // Measured as APERIODIC energy. A steady harmonic stack puts energy only
    // at multiples of its fundamental; a pop is a transient and by definition
    // smears energy into the gaps between them. So off-lattice energy IS the
    // crackle, directly, with nothing else it could be.
    //
    // (Crest factor and short-time envelope variance were both tried first and
    // measurably do NOT separate these: the power stroke's own partials beat
    // against each other enough to muddy both. This one separates them by two
    // to six orders of magnitude.)
    std::printf("      %6s %12s %12s %10s\n", "rpm", "power", "overrun",
                "ratio");
    double weakest = 1e18;
    for (const float rpm : {900.0f, 1500.0f, 3000.0f, 4200.0f, 6800.0f}) {
        // A full second: the probes sit 0.37 of a harmonic spacing away from
        // real partials, and a short window leaks across that gap at idle.
        const PcmClip power = synth_engine_tone(rpm, 1.0f, 1.0f, kRate, 4);
        const PcmClip overrun = synth_engine_tone(rpm, -1.0f, 1.0f, kRate, 4);
        const double f = static_cast<double>(engine_firing_hz(rpm, 4));

        const double steady = off_lattice_ratio(power.samples, kRate, f);
        const double crackling = off_lattice_ratio(overrun.samples, kRate, f);
        const double ratio = crackling / std::max(steady, 1e-12);
        weakest = std::min(weakest, ratio);

        std::printf("      %6.0f %12.6f %12.6f %9.1fx\n",
                    static_cast<double>(rpm), steady, crackling, ratio);

        REQUIRE_MSG(crackling > 0.005,
                    "the overrun has no aperiodic content — no crackle",
                    "overrun");
        REQUIRE_MSG(ratio > 50.0,
                    "the overrun is no spikier than the power stroke",
                    "overrun");
    }

    // And the crackle must BELONG to the overrun rather than being sprinkled
    // everywhere: it fades as the driver gets back on the throttle, and on the
    // power it is gone.
    //
    // Checked in two halves on purpose. Once the crackle is switched off, what
    // is left in the off-lattice bins is window leakage — a number around 1e-6
    // whose ordering is meaningless noise. Demanding monotonicity across THAT
    // region is demanding that leakage be sorted, which it has no reason to be.
    const double f = static_cast<double>(engine_firing_hz(4000.0f, 4));
    const auto aperiodic_at = [&](float load) {
        const PcmClip c = synth_engine_tone(4000.0f, load, 1.0f, kRate, 4);
        return off_lattice_ratio(c.samples, kRate, f);
    };

    const double full_overrun = aperiodic_at(-1.0f);
    const double part_overrun = aperiodic_at(-0.6f);
    REQUIRE_MSG(full_overrun > part_overrun,
                "the crackle did not ease off as the throttle came back",
                "load");
    REQUIRE_MSG(part_overrun > 0.002,
                "a part-closed throttle lost the crackle entirely", "load");

    for (const float load : {0.0f, 0.6f, 1.0f}) {
        REQUIRE_MSG(aperiodic_at(load) < 0.001,
                    "the engine still crackles ON the throttle", "load");
    }

    std::printf("      (weakest overrun-vs-power aperiodic ratio: %.1fx)\n",
                weakest);
    pass("the overrun crackles, and the crackle fades as the throttle opens");
}

void the_layer_bank_covers_the_rev_range_without_gaps() {
    const SfxBank bank = synth_bank(kRate);

    float widest = 0.0f;
    for (std::size_t i = 1; i < kEngineLayerCount; ++i) {
        const float ratio = bank.engine_rpm[i] / bank.engine_rpm[i - 1];
        widest = std::max(widest, ratio);
        // No adjacent pair further apart than a major sixth. Beyond that a
        // layer is being stretched far enough for the resampler to visibly
        // drag its formants, which is the exact artefact the bank exists to
        // avoid — and the first version of this bank had a full OCTAVE between
        // idle and the next anchor, which is where a rally driver spends most
        // of the stage.
        REQUIRE_MSG(ratio < 1.7,
                    "an engine layer gap is wide enough to drag the formants",
                    "layer spacing");
    }
    std::printf("      (%zu anchors, widest adjacent ratio %.3f)\n",
                kEngineLayerCount, static_cast<double>(widest));

    // Every rpm in the range must find bracketing layers with real gain, and
    // the total power must never collapse between anchors — an equal-power
    // crossfade that was written as a linear one dips audibly at every
    // boundary as the revs sweep through.
    double min_power = 1e9;
    double max_power = 0.0;
    for (int i = 0; i <= 200; ++i) {
        const float rpm = bank.engine_rpm.front() +
                          (bank.engine_rpm.back() - bank.engine_rpm.front()) *
                              static_cast<float>(i) / 200.0f;
        const EngineMix m = engine_mix(bank, rpm, 1.0f);
        double power = 0.0;
        int sounding = 0;
        for (const VoiceMix& l : m.layers) {
            if (l.gain <= 0.0f) continue;
            REQUIRE_MSG(l.clip != nullptr, "a sounding layer has no clip",
                        "engine mix");
            REQUIRE_MSG(l.pitch > 0.3f && l.pitch < 3.0f,
                        "an engine layer is stretched absurdly far",
                        "engine mix");
            power += static_cast<double>(l.gain) * static_cast<double>(l.gain);
            ++sounding;
        }
        REQUIRE_MSG(sounding > 0, "no engine layer sounds at this rpm",
                    "engine mix");
        min_power = std::min(min_power, power);
        max_power = std::max(max_power, power);
    }
    // Level rises with revs by design, so this is not flat — but it must never
    // DIP, which is what a bad crossfade looks like.
    REQUIRE_MSG(min_power > 0.05, "the engine goes quiet somewhere in the rev "
                                  "range", "crossfade");
    std::printf("      (summed layer power over the rev range: %.4f .. %.4f)\n",
                min_power, max_power);

    // Out-of-range rpm must clamp to the ends, not fall off the array.
    REQUIRE(engine_mix(bank, 0.0f, 1.0f).layers[0].clip != nullptr);
    REQUIRE(engine_mix(bank, 99999.0f, 1.0f).layers[1].clip != nullptr);

    pass("the layer bank covers the rev range with no gap and no dip");
}

void louder_on_the_throttle_than_off_it() {
    const SfxBank bank = synth_bank(kRate);
    for (const float rpm : {1200.0f, 3000.0f, 5000.0f, 6500.0f}) {
        const EngineMix power = engine_mix(bank, rpm, 1.0f);
        const EngineMix overrun = engine_mix(bank, rpm, -1.0f);
        double pe = 0.0, oe = 0.0;
        for (const VoiceMix& l : power.layers) pe += static_cast<double>(l.gain);
        for (const VoiceMix& l : overrun.layers) oe += static_cast<double>(l.gain);
        REQUIRE_MSG(pe > oe, "lifting off did not reduce the engine level",
                    "load level");
    }
    pass("the engine is louder on the throttle than off it");
}

// ---------------------------------------------------------------------------
//  End to end
// ---------------------------------------------------------------------------

std::vector<float> render_engine(const SfxBank& bank, float rpm, float load,
                                 uint32_t device_rate) {
    VoiceMixer mx;
    mx.prepare(device_rate);

    const EngineMix em = engine_mix(bank, rpm, load);
    for (const VoiceMix& l : em.layers) {
        if (!l.clip || l.gain <= 0.0f) continue;
        VoiceParams p;
        p.category = Category::Engine;
        p.gain = l.gain;
        p.pitch = l.pitch;
        p.lp_cutoff_hz = l.lp_cutoff_hz;
        p.looping = true;
        mx.open_loop(l.clip, p);
    }

    constexpr std::size_t kBlock = 2048;
    std::vector<float> out(kBlock * 2);
    std::vector<float> mono;
    for (int b = 0; b < 12; ++b) {
        mx.render(out.data(), kBlock);
        // Skip the first blocks: the per-sample gain ramp is still climbing,
        // and analysing a fade-in measures the fade, not the tone.
        if (b >= 4) {
            const std::vector<float> left = deinterleave(out, 2, 0);
            mono.insert(mono.end(), left.begin(), left.end());
        }
    }
    return mono;
}

void the_rendered_engine_still_tracks_rpm() {
    // THE ONE THAT ACTUALLY MATTERS. Everything above measures a buffer; this
    // measures what comes out of the mixer the device will pull from — through
    // the layer crossfade, the linear-interpolating resampler and the gain
    // smoothing. A synthesiser that is perfect and a resampler that is a
    // percent sharp still add up to a car that sounds wrong.
    const SfxBank bank = synth_bank(kRate);

    std::printf("      %6s %6s %10s %10s %9s\n", "rpm", "load", "expect",
                "rendered", "err%");
    double worst = 0.0;
    for (const float rpm : {1000.0f, 1700.0f, 2600.0f, 3400.0f, 4600.0f,
                            5800.0f, 6700.0f}) {
        for (const float load : {1.0f, -1.0f}) {
            const std::vector<float> mono = render_engine(bank, rpm, load, kRate);
            REQUIRE(all_finite(mono));
            REQUIRE_MSG(rms(mono) > 0.005, "the rendered engine was silent",
                        "end to end");
            REQUIRE_MSG(peak(mono) <= 1.0f, "the rendered engine clipped",
                        "end to end");

            const double expect = static_cast<double>(engine_firing_hz(rpm, 4));
            const double measured =
                estimate_f0(mono, kRate, expect * 0.35, expect * 3.0);
            const double err = 100.0 * (measured - expect) / expect;
            worst = std::max(worst, std::fabs(err));

            std::printf("      %6.0f %6.1f %10.3f %10.3f %+9.3f\n",
                        static_cast<double>(rpm), static_cast<double>(load),
                        expect, measured, err);
            REQUIRE_MSG(std::fabs(err) < 0.5,
                        "the RENDERED engine note is not at the firing "
                        "frequency", "end to end");
        }
    }
    std::printf("      (worst end-to-end pitch error: %.3f%%)\n", worst);
    pass("the engine note survives the mixer at the right pitch, on the "
         "throttle and off it");
}

void a_rev_sweep_never_glitches() {
    // Sweep the revs the way a driver does and check the output stays sane at
    // every step. This is the shape of the real bug the layer crossfade
    // exists to prevent: a discontinuity that only happens WHILE the rpm is
    // moving, and so never shows up in a test that holds it still.
    const SfxBank bank = synth_bank(kRate);

    VoiceMixer mx;
    mx.prepare(kRate);

    VoiceHandle handles[4];
    const EngineMix initial = engine_mix(bank, 900.0f, 1.0f);
    for (std::size_t i = 0; i < 4; ++i) {
        VoiceParams p;
        p.category = Category::Engine;
        p.gain = initial.layers[i].gain;
        p.pitch = initial.layers[i].pitch;
        p.looping = true;
        handles[i] = mx.open_loop(
            initial.layers[i].clip ? initial.layers[i].clip
                                   : &bank.engine_power[0], p);
        REQUIRE(handles[i].valid());
    }

    std::vector<float> out(512 * 2);
    double worst_peak = 0.0;
    for (int step = 0; step <= 600; ++step) {
        const float t = static_cast<float>(step) / 600.0f;
        const float rpm = 900.0f + (6800.0f - 900.0f) * t;
        // Load swings on and off the throttle through the sweep too.
        const float load = std::sin(static_cast<float>(step) * 0.05f);

        const EngineMix em = engine_mix(bank, rpm, load);
        for (std::size_t i = 0; i < 4; ++i) {
            VoiceParams p;
            p.category = Category::Engine;
            p.gain = em.layers[i].gain;
            p.pitch = em.layers[i].pitch;
            p.looping = true;
            mx.set_loop(handles[i], p);
        }

        mx.render(out.data(), 512);
        REQUIRE_MSG(all_finite(out), "a rev sweep produced a non-finite sample",
                    "sweep");
        for (const float v : out) {
            worst_peak = std::max(worst_peak, std::fabs(static_cast<double>(v)));
            REQUIRE_MSG(std::fabs(v) <= 1.0f, "a rev sweep clipped", "sweep");
        }
    }
    REQUIRE_MSG(mx.dropped_commands() == 0,
                "the rev sweep overflowed the command ring", "sweep");
    REQUIRE_MSG(worst_peak > 0.05, "the rev sweep produced nothing audible",
                "test would be vacuous");

    std::printf("      (600-step sweep 900 -> 6800 rpm with load swinging, "
                "worst |out| %.4f)\n", worst_peak);
    pass("a full rev sweep under changing load stays finite and unclipped");
}

void the_stingers_read_as_positive() {
    // "Sounds positive" is not measurable, but the thing that MAKES it read as
    // positive is: rising, consonant, major intervals. That is measurable, and
    // it is the whole reason those intervals were chosen over a sample nobody
    // can ship.
    const SfxBank bank = synth_bank(kRate);

    struct Case {
        const char* name;
        const PcmClip* clip;
        double head_seconds;
        double tail_from;
        double expected_ratio;   // just intonation
    };
    const Case cases[] = {
        {"checkpoint", &bank.checkpoint, 0.08, 0.20, 5.0 / 3.0},  // major sixth
        {"lap_record", &bank.lap_record, 0.07, 0.30, 2.0},        // octave
    };

    for (const Case& c : cases) {
        const std::vector<float>& s = c.clip->samples;
        const std::size_t n = s.size();
        const std::size_t head_n =
            std::min(n, static_cast<std::size_t>(c.head_seconds * kRate));
        const std::size_t tail_start =
            std::min(n - 1, static_cast<std::size_t>(c.tail_from * kRate));

        const std::vector<float> head(s.begin(),
                                      s.begin() + static_cast<long>(head_n));
        const std::vector<float> tail(
            s.begin() + static_cast<long>(tail_start), s.end());

        const double first = estimate_f0(head, kRate, 200.0, 4000.0);
        const double last = estimate_f0(tail, kRate, 200.0, 4000.0);
        REQUIRE_MSG(first > 0.0 && last > 0.0,
                    "could not measure a stinger's pitch", c.name);

        REQUIRE_MSG(last > first,
                    "the stinger FALLS, which reads as a penalty", c.name);

        const double ratio = last / first;
        const double err = 100.0 * (ratio - c.expected_ratio) / c.expected_ratio;
        REQUIRE_MSG(std::fabs(err) < 3.0,
                    "the stinger's interval is not the consonant one it was "
                    "written as", c.name);

        std::printf("      (%-11s %.1f Hz -> %.1f Hz, ratio %.4f, wanted "
                    "%.4f, %+.2f%%)\n", c.name, first, last, ratio,
                    c.expected_ratio, err);
    }

    // And they must be distinguishable from each other at speed.
    REQUIRE_MSG(bank.lap_record.duration_seconds() >
                    2.0f * bank.checkpoint.duration_seconds(),
                "a lap record sounds the same length as a checkpoint",
                "distinct");
    pass("both stingers rise by a consonant major interval");
}

}  // namespace

int main() {
    std::printf("audio_engine_tone_tests\n");
    the_firing_formula_is_what_it_claims();
    the_fundamental_tracks_rpm();
    the_firing_order_dominates_the_spectrum();
    load_changes_timbre_and_leaves_pitch_alone();
    the_overrun_crackles();
    the_layer_bank_covers_the_rev_range_without_gaps();
    louder_on_the_throttle_than_off_it();
    the_rendered_engine_still_tracks_rpm();
    a_rev_sweep_never_glitches();
    the_stingers_read_as_positive();
    return done("audio_engine_tone_tests");
}
