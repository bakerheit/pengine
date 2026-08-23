// Gain routing, 3D placement and the voice mixer.
//
// Every one of these runs with NO AUDIO DEVICE. That is the whole reason the
// gain maths and the render loop live in apricot_sim: the arithmetic that
// decides how loud everything is, and the loop that turns voices into samples,
// are the two things that go wrong silently, and both are testable here in
// milliseconds on a machine with no sound card.

#include <cstdio>
#include <vector>

#include "audio/mixer.h"
#include "audio/synth.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

void category_times_master_is_the_whole_law() {
    Mixer m;

    // Defaults must be exactly unity, not approximately. An emitter asking for
    // gain 1 on a fresh mixer has to get back the bits it handed in, or every
    // "is the mix the same as before" comparison drifts.
    REQUIRE(m.master == 1.0f);
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
        REQUIRE(m.category_gain(static_cast<Category>(i)) == 1.0f);
    }
    REQUIRE(m.gain(Category::Engine, 1.0f) == 1.0f);

    m.set_master(0.5f);
    m.set_category(Category::Engine, 0.25f);
    REQUIRE_NEAR(static_cast<double>(m.gain(Category::Engine, 1.0f)), 0.125, 1e-9);
    REQUIRE_NEAR(static_cast<double>(m.gain(Category::Engine, 0.5f)), 0.0625, 1e-9);

    // A category trim must not leak into any other category.
    REQUIRE_NEAR(static_cast<double>(m.gain(Category::Music, 1.0f)), 0.5, 1e-9);

    pass("final gain is emitter x category x master");
}

void every_category_is_independent() {
    // The bug this exists to catch: muting one category ducking another,
    // usually through an off-by-one in the index or a shared accumulator.
    for (std::size_t muted = 0; muted < kCategoryCount; ++muted) {
        Mixer m;
        m.set_category(static_cast<Category>(muted), 0.0f);
        for (std::size_t other = 0; other < kCategoryCount; ++other) {
            const Category c = static_cast<Category>(other);
            const float g = m.gain(c, 1.0f);
            if (other == muted) {
                REQUIRE_MSG(g == 0.0f, "a muted category still sounds",
                            category_name(c));
            } else {
                REQUIRE_MSG(g == 1.0f, "muting one category ducked another",
                            category_name(c));
            }
        }
    }
    pass("muting one category leaves every other at unity");
}

void the_categories_the_game_needs_all_exist() {
    // Named, not just counted. A renamed or reordered enum that still has the
    // right COUNT would sail past a size assertion.
    const char* required[] = {"engine", "tyres", "world",
                              "weather", "ui",   "music"};
    for (const char* want : required) {
        bool found = false;
        for (std::size_t i = 0; i < kCategoryCount; ++i) {
            const char* have = category_name(static_cast<Category>(i));
            if (have[0] == want[0]) {
                std::size_t k = 0;
                while (want[k] && have[k] == want[k]) ++k;
                if (want[k] == '\0' && have[k] == '\0') found = true;
            }
        }
        REQUIRE_MSG(found, "a required mixer category is missing", want);
    }
    // Every category must have a real name; the fallback means an enumerator
    // was added without a case in category_name().
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
        const char* n = category_name(static_cast<Category>(i));
        REQUIRE_MSG(n[0] != '?', "a category has no name", "naming");
    }
    pass("engine, tyres, world, weather, ui and music all exist and are named");
}

void trims_clamp_at_both_ends() {
    Mixer m;

    m.set_master(5.0f);
    REQUIRE(m.master == 1.0f);
    m.set_master(-3.0f);
    REQUIRE(m.master == 0.0f);
    m.set_master(1.0f);

    m.set_category(Category::Tyres, 99.0f);
    REQUIRE(m.category_gain(Category::Tyres) == 1.0f);
    m.set_category(Category::Tyres, -0.4f);
    REQUIRE(m.category_gain(Category::Tyres) == 0.0f);

    // Exact boundary values must survive untouched, not be nudged by a
    // comparison written with the wrong strictness.
    m.set_master(0.0f);
    REQUIRE(m.master == 0.0f);
    m.set_master(1.0f);
    REQUIRE(m.master == 1.0f);
    m.set_category(Category::Ui, 0.0f);
    REQUIRE(m.category_gain(Category::Ui) == 0.0f);
    m.set_category(Category::Ui, 1.0f);
    REQUIRE(m.category_gain(Category::Ui) == 1.0f);

    // An out-of-range category must not read past the array.
    REQUIRE(m.category_gain(Category::kCount) == 0.0f);
    REQUIRE(m.gain(Category::kCount, 1.0f) == 0.0f);

    pass("trims clamp to [0,1] and the boundaries are exact");
}

void emitter_gain_may_exceed_unity_but_never_inverts() {
    Mixer m;
    // Distance attenuation and one-shot emphasis legitimately hand in gains
    // above 1, so this one is NOT clamped at the top.
    REQUIRE_NEAR(static_cast<double>(m.gain(Category::Impacts, 2.5f)), 2.5, 1e-9);

    // A negative emitter gain would invert the waveform rather than quieten it.
    REQUIRE(m.gain(Category::Impacts, -1.0f) == 0.0f);
    REQUIRE(m.gain(Category::Impacts, -0.0f) == 0.0f);
    pass("emitter gain passes above unity, and is floored at zero");
}

void soft_clip_is_transparent_then_bounded() {
    // Below the knee it must be the IDENTITY, bit for bit. A limiter that
    // colours a quiet mix is a limiter nobody can reason about.
    for (int i = -80; i <= 80; ++i) {
        const float x = static_cast<float>(i) * 0.01f;
        REQUIRE_MSG(soft_clip(x) == x, "soft clip altered a below-knee sample",
                    "transparency");
    }
    // Above it, bounded and monotonic, no matter how absurd the input.
    float previous = soft_clip(0.8f);
    for (int i = 81; i <= 4000; ++i) {
        const float x = static_cast<float>(i) * 0.01f;
        const float y = soft_clip(x);
        REQUIRE_MSG(y < 1.0f, "soft clip let a sample reach full scale", "bound");
        REQUIRE_MSG(y >= previous, "soft clip is not monotonic", "monotonic");
        REQUIRE_MSG(soft_clip(-x) == -y, "soft clip is not odd-symmetric",
                    "symmetry");
        previous = y;
    }
    pass("soft clip is exact below the knee and bounded above it");
}

void pan_is_constant_power_and_never_negative() {
    for (int i = -20; i <= 20; ++i) {
        const float p = static_cast<float>(i) * 0.05f;
        const StereoGain g = constant_power_pan(p);
        const double power = static_cast<double>(g.left) * g.left +
                             static_cast<double>(g.right) * g.right;
        REQUIRE_MSG(std::fabs(power - 1.0) < 1e-5,
                    "pan does not preserve power across the image", "power");
        // cos(pi/2) in float is about -4e-8. A negative channel gain is an
        // inverted channel, and it cancels correlated material elsewhere.
        REQUIRE_MSG(g.left >= 0.0f && g.right >= 0.0f,
                    "a pan gain went negative", "sign");
    }
    REQUIRE_NEAR(static_cast<double>(constant_power_pan(0.0f).left),
                 0.70710678, 1e-5);
    REQUIRE(constant_power_pan(1.0f).right > 0.999f);
    REQUIRE(constant_power_pan(-1.0f).left > 0.999f);
    // Out of range input must clamp, not wrap round into the other channel.
    REQUIRE(constant_power_pan(9.0f).right > 0.999f);
    REQUIRE(constant_power_pan(-9.0f).left > 0.999f);
    pass("pan is constant-power, clamped, and never inverts a channel");
}

void distance_attenuation_falls_off_and_reaches_silence() {
    Attenuation a;
    a.ref_distance = 4.0f;
    a.max_distance = 160.0f;

    // Inside the reference radius: full gain, and no blow-up at zero.
    REQUIRE(distance_attenuation(0.0f, a) == 1.0f);
    REQUIRE(distance_attenuation(2.0f, a) == 1.0f);
    REQUIRE(distance_attenuation(4.0f, a) == 1.0f);

    // Monotonically decreasing from there out.
    float previous = 1.0f;
    for (float d = 4.0f; d <= 200.0f; d += 0.5f) {
        const float g = distance_attenuation(d, a);
        REQUIRE_MSG(g <= previous + 1e-6f, "attenuation is not monotonic",
                    "monotonic");
        REQUIRE_MSG(g >= 0.0f, "attenuation went negative", "sign");
        previous = g;
    }

    // Silent at and past the cutoff, and approaching silence smoothly rather
    // than stepping — a source looping past the cutoff radius must not tick.
    REQUIRE(distance_attenuation(160.0f, a) == 0.0f);
    REQUIRE(distance_attenuation(400.0f, a) == 0.0f);
    REQUIRE(distance_attenuation(159.5f, a) < 0.01f);

    pass("distance attenuation is monotonic, bounded and fades to silence");
}

void spatial_gains_put_sources_on_the_right_side() {
    Listener l;
    l.position = glm::vec3(0.0f);
    l.right = glm::vec3(1.0f, 0.0f, 0.0f);
    l.forward = glm::vec3(0.0f, 0.0f, -1.0f);
    Attenuation a;

    const StereoGain right = spatial_gains(l, glm::vec3(30.0f, 0.0f, 0.0f), a);
    REQUIRE_MSG(right.right > right.left, "a source on the right is not on the "
                                          "right", "sides");

    const StereoGain left = spatial_gains(l, glm::vec3(-30.0f, 0.0f, 0.0f), a);
    REQUIRE_MSG(left.left > left.right, "a source on the left is not on the "
                                        "left", "sides");

    // Mirror symmetry: the same distance either side must be the same level.
    REQUIRE_NEAR(static_cast<double>(right.right), static_cast<double>(left.left),
                 1e-6);

    // Straight ahead is centred.
    const StereoGain ahead = spatial_gains(l, glm::vec3(0.0f, 0.0f, -30.0f), a);
    REQUIRE_NEAR(static_cast<double>(ahead.left), static_cast<double>(ahead.right),
                 1e-6);

    // AT the listener the pan must collapse to centre. Straight maths says a
    // source one millimetre to the right is hard right, and a sound passing
    // through the camera would slam across the image.
    const StereoGain on_top = spatial_gains(l, glm::vec3(0.001f, 0.0f, 0.0f), a);
    REQUIRE_MSG(std::fabs(on_top.left - on_top.right) < 0.01f,
                "pan did not collapse to centre at the listener", "near field");

    // Out of range is silent on both sides.
    const StereoGain gone = spatial_gains(l, glm::vec3(5000.0f, 0.0f, 0.0f), a);
    REQUIRE(gone.left == 0.0f && gone.right == 0.0f);

    pass("3D gains place sources correctly and collapse at the head");
}

// ---------------------------------------------------------------------------
//  The render loop
// ---------------------------------------------------------------------------

PcmClip loud_square(uint32_t sample_rate) {
    // Deliberately worse than anything the synthesiser produces: full-scale,
    // no headroom, crest factor 1. If the limiter can hold this it can hold
    // a rally stage.
    PcmClip c;
    c.sample_rate = sample_rate;
    c.channels = 1;
    c.samples.resize(600);
    for (std::size_t i = 0; i < c.samples.size(); ++i) {
        c.samples[i] = (i % 60 < 30) ? 1.0f : -1.0f;
    }
    return c;
}

void a_silent_mixer_renders_exact_zeroes() {
    VoiceMixer mx;
    mx.prepare(48000);
    std::vector<float> out(512 * 2, 12345.0f);
    mx.render(out.data(), 512);
    for (const float v : out) {
        REQUIRE_MSG(v == 0.0f, "render did not clear the output buffer",
                    "clear");
    }
    // render() must REPLACE, not accumulate: the callback is handed whatever
    // was in the driver's buffer last time round.
    pass("a mixer with no voices writes exact silence");
}

void loud_voices_sum_without_clipping() {
    VoiceMixer mx;
    mx.prepare(48000);
    const PcmClip clip = loud_square(48000);

    // Fill every loop slot with a full-scale square, all at unity gain and all
    // in different categories, then let them drift out of phase.
    std::size_t opened = 0;
    for (std::size_t i = 0; i < VoiceMixer::kLoopVoices; ++i) {
        VoiceParams p;
        p.category = static_cast<Category>(i % kCategoryCount);
        p.gain = 1.0f;
        p.looping = true;
        p.pitch = 1.0f + 0.011f * static_cast<float>(i);  // decorrelate them
        p.pan = static_cast<float>(i % 5) * 0.5f - 1.0f;
        if (mx.open_loop(&clip, p).valid()) ++opened;
    }
    REQUIRE(opened == VoiceMixer::kLoopVoices);

    std::vector<float> out(1024 * 2);
    double worst = 0.0;
    for (int block = 0; block < 120; ++block) {
        mx.render(out.data(), 1024);
        REQUIRE_MSG(all_finite(out), "the render produced a non-finite sample",
                    "finite");
        for (const float v : out) {
            worst = std::max(worst, std::fabs(static_cast<double>(v)));
            REQUIRE_MSG(std::fabs(v) <= 1.0f, "the mixer clipped", "no clipping");
        }
    }
    REQUIRE_MSG(worst > 0.5, "the loud-voice test did not actually get loud",
                "test would be vacuous");
    REQUIRE(mx.active_voices() == VoiceMixer::kLoopVoices);
    REQUIRE_MSG(mx.dropped_commands() == 0, "commands were dropped", "ring");

    std::printf("      (%zu full-scale square voices, worst |out| = %.5f)\n",
                opened, worst);
    pass("32 full-scale voices sum without ever leaving [-1, 1]");
}

void muting_a_category_silences_only_that_category() {
    // The same property as the arithmetic test, but through the REAL render
    // path — the routing has to survive per-sample gain smoothing and the
    // command ring, not just the one multiply.
    VoiceMixer mx;
    mx.prepare(48000);
    const PcmClip clip = loud_square(48000);

    VoiceParams engine;
    engine.category = Category::Engine;
    engine.gain = 0.5f;
    engine.looping = true;
    mx.open_loop(&clip, engine);

    VoiceParams music = engine;
    music.category = Category::Music;
    music.pitch = 1.37f;
    mx.open_loop(&clip, music);

    std::vector<float> out(2048 * 2);
    const auto settle = [&] {
        for (int i = 0; i < 20; ++i) mx.render(out.data(), 2048);
        return rms(out);
    };

    const double both = settle();
    REQUIRE(both > 0.01);

    mx.set_category(Category::Music, 0.0f);
    const double engine_only = settle();
    REQUIRE_MSG(engine_only < both,
                "muting music did not reduce the mix", "music mute");
    REQUIRE_MSG(engine_only > 0.01,
                "muting music silenced the engine too", "no cross-talk");

    mx.set_category(Category::Engine, 0.0f);
    const double nothing = settle();
    REQUIRE_MSG(nothing < 1e-6, "a fully muted mixer still made noise",
                "full mute");

    // And master alone must take everything down.
    mx.set_category(Category::Engine, 1.0f);
    mx.set_category(Category::Music, 1.0f);
    REQUIRE(settle() > 0.01);
    mx.set_master(0.0f);
    REQUIRE_MSG(settle() < 1e-6, "master mute did not silence the mix",
                "master");

    std::printf("      (both %.5f -> music muted %.5f -> all muted %.9f)\n",
                both, engine_only, nothing);
    pass("category and master mutes work through the real render path");
}

void one_shots_finish_and_free_their_voices() {
    VoiceMixer mx;
    mx.prepare(48000);
    PcmClip clip = loud_square(48000);   // 600 frames, 12.5 ms

    VoiceParams p;
    p.category = Category::Ui;
    p.gain = 0.5f;
    mx.play_oneshot(&clip, p);

    std::vector<float> out(256 * 2);
    mx.render(out.data(), 256);
    REQUIRE_MSG(mx.active_voices() == 1, "a one-shot did not start", "start");

    // 600 frames of clip; well inside 8 blocks of 256.
    for (int i = 0; i < 8; ++i) mx.render(out.data(), 256);
    REQUIRE_MSG(mx.active_voices() == 0, "a one-shot never finished", "end");

    // Fire more than the pool holds. This must not crash, leak or drop below
    // a full pool — the oldest is simply stolen.
    for (std::size_t i = 0; i < VoiceMixer::kOneShotVoices * 3; ++i) {
        mx.play_oneshot(&clip, p);
    }
    mx.render(out.data(), 16);
    REQUIRE(mx.active_voices() <= VoiceMixer::kOneShotVoices);
    REQUIRE_MSG(mx.active_voices() > 0, "every one-shot was lost", "steal");

    pass("one-shots end on their own and over-firing steals rather than fails");
}

void loop_handles_are_owned_and_reusable() {
    VoiceMixer mx;
    mx.prepare(48000);
    const PcmClip clip = loud_square(48000);

    VoiceParams p;
    p.category = Category::Engine;
    p.gain = 1.0f;
    p.looping = true;

    REQUIRE(!VoiceHandle{}.valid());

    std::vector<VoiceHandle> handles;
    for (std::size_t i = 0; i < VoiceMixer::kLoopVoices; ++i) {
        const VoiceHandle h = mx.open_loop(&clip, p);
        REQUIRE_MSG(h.valid(), "a loop slot was refused while free", "open");
        handles.push_back(h);
    }
    // Full. The next must FAIL VISIBLY rather than silently return a handle
    // that does nothing.
    REQUIRE_MSG(!mx.open_loop(&clip, p).valid(),
                "the mixer handed out a 33rd loop voice", "exhaustion");

    const VoiceHandle recycled = handles.front();
    mx.close_loop(recycled);
    const VoiceHandle fresh = mx.open_loop(&clip, p);
    REQUIRE_MSG(fresh.valid(), "a closed slot was not reusable", "reuse");
    REQUIRE_MSG(fresh.slot == recycled.slot, "expected the same slot back",
                "reuse");
    REQUIRE_MSG(fresh.generation != recycled.generation,
                "a reused slot kept its generation, so a stale handle would "
                "still steer the new voice", "generation");

    // The stale handle must now be inert. If it were not, closing a voice and
    // opening another would let old game code retarget a sound it no longer
    // owns — which sounds like a random sample playing at the wrong pitch.
    std::vector<float> out(64 * 2);
    mx.render(out.data(), 64);
    const std::size_t before = mx.active_voices();
    mx.close_loop(recycled);
    mx.render(out.data(), 64);
    REQUIRE_MSG(mx.active_voices() == before,
                "a stale handle closed a live voice", "stale handle");

    // A never-issued handle is inert too.
    mx.close_loop(VoiceHandle{0, 999});
    mx.set_loop(VoiceHandle{0, 999}, p);
    mx.render(out.data(), 64);
    REQUIRE(mx.active_voices() == before);

    pass("loop handles are owned, recycled safely, and stale ones are inert");
}

void a_silent_mixer_does_not_pretend_to_drop_commands() {
    // On a machine with no sound card nothing ever drains the ring. Without
    // the silent path the counter climbs forever and the one signal that would
    // have flagged a REAL overflow becomes noise.
    VoiceMixer mx;
    mx.prepare(48000);
    mx.set_silent(true);
    const PcmClip clip = loud_square(48000);
    VoiceParams p;
    p.category = Category::Engine;
    for (int i = 0; i < 10000; ++i) {
        mx.play_oneshot(&clip, p);
        mx.set_master(0.5f);
    }
    REQUIRE_MSG(mx.dropped_commands() == 0,
                "a silent mixer reported dropped commands", "silent");
    REQUIRE(mx.silent());

    // And it must still be safe to render from — a device that opens later
    // finds a coherent mixer, not a corrupted one.
    std::vector<float> out(128 * 2, 1.0f);
    mx.render(out.data(), 128);
    for (const float v : out) REQUIRE(v == 0.0f);

    pass("a silent mixer discards commands quietly and stays renderable");
}

void the_command_ring_reports_overflow_instead_of_hiding_it() {
    VoiceMixer mx;
    mx.prepare(48000);
    const PcmClip clip = loud_square(48000);
    VoiceParams p;
    p.category = Category::Engine;

    // Push far more than the ring holds without ever rendering.
    for (std::size_t i = 0; i < VoiceMixer::kCommandCapacity * 4; ++i) {
        mx.play_oneshot(&clip, p);
    }
    REQUIRE_MSG(mx.dropped_commands() > 0,
                "the ring swallowed an overflow without counting it",
                "observability");

    // The commands that DID fit must still be intact — an overflow must not
    // corrupt the queue.
    std::vector<float> out(64 * 2);
    mx.render(out.data(), 64);
    REQUIRE(mx.active_voices() > 0);
    REQUIRE(all_finite(out));

    pass("ring overflow is counted, not hidden, and does not corrupt the queue");
}

void resampling_holds_pitch_and_handles_odd_device_rates() {
    // A 48 kHz clip on a 44.1 kHz device must play at the SAME pitch, not
    // 8.8% sharp. Getting this backwards is the classic sample-rate bug and it
    // is inaudible to whoever wrote it, because their device runs at 48.
    const float tone_hz = 440.0f;
    const PcmClip clip = synth_tone(tone_hz, 1.0f, 0.0f, 48000);

    for (const uint32_t device_rate : {44100u, 48000u, 96000u}) {
        VoiceMixer mx;
        mx.prepare(device_rate);
        VoiceParams p;
        p.category = Category::Ui;
        p.gain = 1.0f;
        p.looping = true;
        mx.open_loop(&clip, p);

        std::vector<float> out(4096 * 2);
        std::vector<float> mono;
        for (int b = 0; b < 8; ++b) {
            mx.render(out.data(), 4096);
            if (b >= 2) {
                const std::vector<float> l = deinterleave(out, 2, 0);
                mono.insert(mono.end(), l.begin(), l.end());
            }
        }
        const double measured =
            estimate_f0(mono, static_cast<double>(device_rate), 200.0, 900.0);
        const double err = 100.0 * (measured - tone_hz) / tone_hz;
        std::printf("      (device %5u Hz: 440 Hz clip renders at %7.2f Hz, "
                    "%+.3f%%)\n", device_rate, measured, err);
        REQUIRE_MSG(std::fabs(err) < 0.5,
                    "a clip changed pitch when the device rate changed",
                    "rate conversion");
    }
    pass("clips keep their pitch on 44.1, 48 and 96 kHz devices");
}

}  // namespace

int main() {
    std::printf("audio_mixer_tests\n");
    category_times_master_is_the_whole_law();
    every_category_is_independent();
    the_categories_the_game_needs_all_exist();
    trims_clamp_at_both_ends();
    emitter_gain_may_exceed_unity_but_never_inverts();
    soft_clip_is_transparent_then_bounded();
    pan_is_constant_power_and_never_negative();
    distance_attenuation_falls_off_and_reaches_silence();
    spatial_gains_put_sources_on_the_right_side();
    a_silent_mixer_renders_exact_zeroes();
    loud_voices_sum_without_clipping();
    muting_a_category_silences_only_that_category();
    one_shots_finish_and_free_their_voices();
    loop_handles_are_owned_and_reusable();
    a_silent_mixer_does_not_pretend_to_drop_commands();
    the_command_ring_reports_overflow_instead_of_hiding_it();
    resampling_holds_pitch_and_handles_odd_device_rates();
    return done("audio_mixer_tests");
}
