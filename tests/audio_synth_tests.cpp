// Every synthesised clip, checked as a NUMBER rather than as a vibe.
//
// Nobody can hear a test, so "the buffer is non-empty" is worth nothing — a
// buffer of NaN is non-empty. These are the properties that stand in for
// listening to it:
//
//   finite + in range     it is playable at all
//   non-silent            it is signal, not a zeroed allocation
//   no DC offset          it is centred: no wasted headroom, no thump on start
//   loop seam continuous  it wraps without a tick, once a second, forever
//   one-shot ends at zero it starts and stops without a click
//
// The seam check is the one worth explaining. A loop point is only a click if
// it is discontinuous COMPARED TO the signal's own slope: a bright hiss
// legitimately moves a long way between samples, and an absolute threshold
// would either fail it or pass a genuine tick in a quiet bed. So the seam step
// is measured against the 99.9th percentile of the buffer's own interior steps.

#include <cstdio>
#include <cstdio>
#include <string>
#include <vector>

#include "audio/synth.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

constexpr uint32_t kRate = kDefaultSampleRate;

// Shared health check. Applied to every single clip in the bank, because the
// failure this catches — one generator quietly producing garbage while the
// others are fine — is invisible from any single aggregate number.
void check_playable(const char* name, const PcmClip& c, double min_rms = 0.02) {
    REQUIRE_MSG(!c.samples.empty(), "clip is empty", name);
    REQUIRE_MSG(c.sample_rate == kRate, "clip has the wrong sample rate", name);
    REQUIRE_MSG(c.channels >= 1, "clip has no channels", name);
    REQUIRE_MSG(c.frame_count() > 0, "clip has no frames", name);
    REQUIRE_MSG(all_finite(c.samples), "clip contains NaN or infinity", name);

    const float pk = peak(c.samples);
    REQUIRE_MSG(pk <= 1.0f, "clip exceeds full scale before any gain", name);
    REQUIRE_MSG(pk > 0.1f, "clip is far quieter than its headroom target", name);

    REQUIRE_MSG(rms(c.samples) > min_rms, "clip is silent or near-silent", name);

    // A DC offset wastes headroom, thumps at the start, and beats against a
    // pitched copy of itself into a rumble nobody can source.
    REQUIRE_MSG(std::fabs(dc_offset(c.samples)) < 1e-3,
                "clip has a DC offset", name);
}

void check_loop_seam(const char* name, const PcmClip& c) {
    const double seam = seam_step(c.samples);
    const double routine = adjacent_step_percentile(c.samples, 0.999);
    REQUIRE_MSG(routine > 0.0, "clip has no signal to compare the seam to", name);
    REQUIRE_MSG(seam <= routine,
                "the loop seam jumps further than the buffer's own 99.9th "
                "percentile step — that is an audible tick every loop", name);
}

void check_oneshot_ends(const char* name, const PcmClip& c) {
    // Starting or ending on a non-zero sample is the single most common
    // synthesised-audio bug, and it is a click on every single play.
    REQUIRE_MSG(std::fabs(c.samples.front()) < 1e-4,
                "one-shot does not start at zero", name);
    REQUIRE_MSG(std::fabs(c.samples.back()) < 1e-4,
                "one-shot does not end at zero", name);
}

void the_reference_tone_is_exact() {
    const PcmClip c = synth_tone(1000.0f, 0.25f, 0.005f, kRate);
    REQUIRE(c.frame_count() == 12000);
    REQUIRE_NEAR(static_cast<double>(c.duration_seconds()), 0.25, 1e-6);
    check_playable("synth_tone", c);
    check_oneshot_ends("synth_tone", c);

    const double f0 = estimate_f0(c.samples, kRate, 200.0, 4000.0);
    REQUIRE_MSG(std::fabs(f0 - 1000.0) < 5.0,
                "the reference tone is not at the frequency it was asked for",
                "reference pitch");

    // Degenerate inputs must produce an empty clip, not a crash and not a
    // buffer of NaN.
    REQUIRE(synth_tone(1000.0f, 0.0f, 0.0f, kRate).samples.empty());
    REQUIRE(synth_tone(1000.0f, -1.0f, 0.0f, kRate).samples.empty());
    REQUIRE(synth_tone(1000.0f, 0.25f, 0.0f, 0).samples.empty());

    std::printf("      (1 kHz reference measures %.2f Hz)\n", f0);
    pass("the reference tone is exact and rejects nonsense input");
}

void every_clip_in_the_bank_is_playable() {
    const SfxBank bank = synth_bank(kRate);
    char name[64];

    for (std::size_t i = 0; i < kEngineLayerCount; ++i) {
        std::snprintf(name, sizeof(name), "engine_power[%zu]", i);
        check_playable(name, bank.engine_power[i]);
        check_loop_seam(name, bank.engine_power[i]);

        std::snprintf(name, sizeof(name), "engine_overrun[%zu]", i);
        check_playable(name, bank.engine_overrun[i]);
        check_loop_seam(name, bank.engine_overrun[i]);

        REQUIRE_MSG(bank.engine_rpm[i] > 0.0f, "an engine anchor has no rpm",
                    name);
        if (i > 0) {
            REQUIRE_MSG(bank.engine_rpm[i] > bank.engine_rpm[i - 1],
                        "engine anchors are not in ascending rpm order", name);
        }
    }

    check_playable("tyre_scrub", bank.tyre_scrub);
    check_loop_seam("tyre_scrub", bank.tyre_scrub);

    for (std::size_t i = 0; i < kSurfaceCount; ++i) {
        const char* sn = surface_name(static_cast<Surface>(i));
        check_playable(sn, bank.surface_roll[i]);
        check_loop_seam(sn, bank.surface_roll[i]);
    }

    for (std::size_t i = 0; i < SfxBank::kThumpCount; ++i) {
        std::snprintf(name, sizeof(name), "suspension_thump[%zu]", i);
        check_playable(name, bank.suspension_thump[i]);
        check_oneshot_ends(name, bank.suspension_thump[i]);
    }

    check_playable("rain", bank.rain);
    check_loop_seam("rain", bank.rain);
    check_playable("wind", bank.wind);
    check_loop_seam("wind", bank.wind);

    check_playable("checkpoint", bank.checkpoint);
    check_oneshot_ends("checkpoint", bank.checkpoint);
    check_playable("lap_record", bank.lap_record);
    check_oneshot_ends("lap_record", bank.lap_record);

    pass("every clip in the bank is finite, in range, non-silent and DC-free");
}

void the_bank_is_deterministic() {
    // The noise beds come from the engine's own hashed RNG, so two banks built
    // in the same process must be sample-identical. If they are not, something
    // reached for a clock or a global generator, and the engine's whole
    // replay story is already broken.
    const SfxBank a = synth_bank(kRate);
    const SfxBank b = synth_bank(kRate);
    REQUIRE(a.tyre_scrub.samples == b.tyre_scrub.samples);
    REQUIRE(a.rain.samples == b.rain.samples);
    REQUIRE(a.wind.samples == b.wind.samples);
    for (std::size_t i = 0; i < kSurfaceCount; ++i) {
        REQUIRE(a.surface_roll[i].samples == b.surface_roll[i].samples);
    }
    for (std::size_t i = 0; i < kEngineLayerCount; ++i) {
        REQUIRE(a.engine_power[i].samples == b.engine_power[i].samples);
        REQUIRE(a.engine_overrun[i].samples == b.engine_overrun[i].samples);
    }
    pass("the bank is bit-identical between builds in the same process");
}

void surfaces_sound_different_from_each_other() {
    // Four generators that all reduce to the same filtered hiss would pass
    // every health check above. Brightness ordering is the cheapest number
    // that says they are actually different materials.
    const SfxBank bank = synth_bank(kRate);
    double centroid[kSurfaceCount];
    for (std::size_t i = 0; i < kSurfaceCount; ++i) {
        centroid[i] = spectral_centroid(bank.surface_roll[i].samples, kRate);
        std::printf("      (%-6s centroid %7.1f Hz)\n",
                    surface_name(static_cast<Surface>(i)), centroid[i]);
    }
    const std::size_t tarmac = static_cast<std::size_t>(Surface::Tarmac);
    const std::size_t gravel = static_cast<std::size_t>(Surface::Gravel);
    const std::size_t dirt = static_cast<std::size_t>(Surface::Dirt);
    const std::size_t snow = static_cast<std::size_t>(Surface::Snow);

    REQUIRE_MSG(centroid[tarmac] > centroid[gravel],
                "tarmac should be brighter than gravel", "surfaces");
    REQUIRE_MSG(centroid[gravel] > centroid[dirt],
                "gravel should be brighter than dirt", "surfaces");
    REQUIRE_MSG(centroid[dirt] > centroid[snow],
                "dirt should be brighter than snow", "surfaces");
    REQUIRE_MSG(centroid[tarmac] > 2.0 * centroid[snow],
                "tarmac and snow are barely distinguishable", "surfaces");
    pass("the four surfaces are ordered tarmac > gravel > dirt > snow");
}

void tyre_scrub_brightness_tracks_slip() {
    const SfxBank bank = synth_bank(kRate);

    float previous_gain = -1.0f;
    float previous_cut = -1.0f;
    for (int i = 0; i <= 10; ++i) {
        const float slip = static_cast<float>(i) * 0.1f;
        const VoiceMix v = tyre_scrub_mix(bank, slip, 25.0f);
        REQUIRE(v.clip != nullptr);
        REQUIRE_MSG(v.gain >= previous_gain, "scrub gain is not monotonic in "
                                             "slip", "slip");
        REQUIRE_MSG(v.lp_cutoff_hz > previous_cut,
                    "scrub brightness is not monotonic in slip", "slip");
        previous_gain = v.gain;
        previous_cut = v.lp_cutoff_hz;
    }
    // Fully sliding must be MUCH brighter than barely slipping, not marginally.
    REQUIRE(tyre_scrub_mix(bank, 1.0f, 25.0f).lp_cutoff_hz >
            4.0f * tyre_scrub_mix(bank, 0.0f, 25.0f).lp_cutoff_hz);

    // A stationary car computes enormous lateral slip at full lock. It must
    // not scream in a car park.
    REQUIRE_MSG(tyre_scrub_mix(bank, 1.0f, 0.0f).gain == 0.0f,
                "the tyres squeal while stationary", "speed gate");
    REQUIRE(tyre_scrub_mix(bank, 1.0f, 0.5f).gain <
            tyre_scrub_mix(bank, 1.0f, 25.0f).gain);
    REQUIRE_MSG(tyre_scrub_mix(bank, 0.0f, 25.0f).gain == 0.0f,
                "a gripping tyre scrubs", "no slip");

    pass("scrub gain and brightness track slip, and are gated by speed");
}

void surface_roll_tracks_speed_and_surface() {
    const SfxBank bank = synth_bank(kRate);
    // 35 m/s (126 km/h) is the design flat-out speed everything scales to, so
    // the sweep stops there: past it the mapping SATURATES on purpose, and
    // demanding strict monotonicity beyond saturation would be testing the
    // test rather than the code. Saturation is checked separately below.
    constexpr float kFlatOut = 35.0f;
    for (std::size_t s = 0; s < kSurfaceCount; ++s) {
        const Surface surface = static_cast<Surface>(s);
        float previous_gain = -1.0f;
        float previous_cut = -1.0f;
        for (int i = 0; i <= 10; ++i) {
            const float speed = kFlatOut * static_cast<float>(i) / 10.0f;
            const VoiceMix v = surface_roll_mix(bank, speed, surface);
            REQUIRE_MSG(v.clip != nullptr, "no roll clip for a surface",
                        surface_name(surface));
            REQUIRE_MSG(v.gain >= previous_gain, "roll gain is not monotonic "
                                                 "in speed", surface_name(surface));
            REQUIRE_MSG(v.lp_cutoff_hz > previous_cut,
                        "roll brightness is not monotonic in speed",
                        surface_name(surface));
            REQUIRE_MSG(v.pitch > 0.0f, "roll pitch is not positive",
                        surface_name(surface));
            previous_gain = v.gain;
            previous_cut = v.lp_cutoff_hz;
        }
        REQUIRE_MSG(surface_roll_mix(bank, 0.0f, surface).gain == 0.0f,
                    "a stationary car still rolls", surface_name(surface));

        // Past flat out it holds rather than running away — a downhill stage
        // must not drive the cutoff past Nyquist or the gain past full scale.
        const VoiceMix at_limit = surface_roll_mix(bank, kFlatOut, surface);
        const VoiceMix beyond = surface_roll_mix(bank, 500.0f, surface);
        REQUIRE_MSG(beyond.gain == at_limit.gain &&
                        beyond.lp_cutoff_hz == at_limit.lp_cutoff_hz,
                    "roll parameters kept climbing past flat out",
                    surface_name(surface));
        REQUIRE_MSG(beyond.gain <= 1.0f, "roll gain exceeded full scale",
                    surface_name(surface));
    }
    // Gravel is the loud one; snow is the quiet one. That is the identity of a
    // rally stage and it should be visible in the numbers.
    REQUIRE(surface_roll_mix(bank, 30.0f, Surface::Gravel).gain >
            surface_roll_mix(bank, 30.0f, Surface::Snow).gain);

    // An out-of-range surface must be silent, not read past the array.
    REQUIRE(surface_roll_mix(bank, 30.0f, Surface::kCount).clip == nullptr);

    pass("roll gain, brightness and pitch track speed for every surface");
}

void suspension_thump_weight_tracks_impact() {
    const SfxBank bank = synth_bank(kRate);

    // A landing you can only just hear on every kerb is worse than no landing.
    REQUIRE_MSG(suspension_thump_mix(bank, 0.0f).clip == nullptr,
                "a stationary car thumped", "floor");
    REQUIRE_MSG(suspension_thump_mix(bank, 1.0f).clip == nullptr,
                "a kerb strike triggered a landing", "floor");

    float previous_gain = -1.0f;
    float previous_pitch = 99.0f;
    double previous_f0 = 1e9;
    for (const float v : {1.5f, 3.0f, 5.0f, 7.0f, 9.5f, 20.0f}) {
        const VoiceMix m = suspension_thump_mix(bank, v);
        REQUIRE_MSG(m.clip != nullptr, "a real landing was silent", "impact");
        REQUIRE_MSG(m.gain >= previous_gain, "thump gain is not monotonic in "
                                             "impact speed", "impact");
        REQUIRE_MSG(m.pitch <= previous_pitch,
                    "a heavier landing did not get LOWER", "impact");

        // Both mechanisms must point the same way: a heavier hit picks a lower
        // clip AND pitches it further down. Mass is the one thing an impact
        // has to communicate in the first 20 ms.
        const double f0 =
            estimate_f0(m.clip->samples, kRate, 25.0, 300.0) *
            static_cast<double>(m.pitch);
        REQUIRE_MSG(f0 <= previous_f0 + 1.0,
                    "a heavier landing rang at a HIGHER pitch", "impact");
        previous_gain = m.gain;
        previous_pitch = m.pitch;
        previous_f0 = f0;
    }

    // Heavier landings are also longer.
    REQUIRE(bank.suspension_thump[2].duration_seconds() >
            bank.suspension_thump[0].duration_seconds());

    pass("thump gain rises and pitch falls with impact speed");
}

void weather_layers_are_independent() {
    const SfxBank bank = synth_bank(kRate);

    const WeatherMix dry = weather_mix(bank, 0.0f, 0.0f);
    REQUIRE_MSG(dry.rain.clip == nullptr && dry.wind.clip == nullptr,
                "clear weather still made noise", "clear");

    // A dry gale and a still downpour are both real weather; the layers must
    // not be wired to a single "weather intensity".
    const WeatherMix gale = weather_mix(bank, 0.0f, 1.0f);
    REQUIRE(gale.rain.clip == nullptr);
    REQUIRE(gale.wind.clip != nullptr && gale.wind.gain > 0.3f);

    const WeatherMix downpour = weather_mix(bank, 1.0f, 0.0f);
    REQUIRE(downpour.wind.clip == nullptr);
    REQUIRE(downpour.rain.clip != nullptr && downpour.rain.gain > 0.3f);

    float previous_rain = -1.0f;
    float previous_wind = -1.0f;
    for (int i = 1; i <= 10; ++i) {
        const float t = static_cast<float>(i) * 0.1f;
        const WeatherMix w = weather_mix(bank, t, t);
        REQUIRE(w.rain.gain > previous_rain);
        REQUIRE(w.wind.gain > previous_wind);
        REQUIRE(w.rain.lp_cutoff_hz > 0.0f && w.wind.lp_cutoff_hz > 0.0f);
        previous_rain = w.rain.gain;
        previous_wind = w.wind.gain;
    }

    // Out-of-range intensity must clamp rather than run away.
    const WeatherMix storm = weather_mix(bank, 9.0f, 9.0f);
    REQUIRE(storm.rain.gain <= 1.0f && storm.wind.gain <= 1.0f);
    REQUIRE(weather_mix(bank, -5.0f, -5.0f).rain.clip == nullptr);

    pass("rain and wind scale independently and clamp");
}

// ---------------------------------------------------------------------------
//  Optional file clips
// ---------------------------------------------------------------------------

void write_test_wav(const std::string& path, uint32_t rate, uint16_t channels,
                    const std::vector<int16_t>& samples) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    const uint32_t data_bytes =
        static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t byte_rate = rate * channels * 2u;

    const auto u32 = [&](uint32_t v) {
        unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                              static_cast<unsigned char>((v >> 8) & 0xFFu),
                              static_cast<unsigned char>((v >> 16) & 0xFFu),
                              static_cast<unsigned char>((v >> 24) & 0xFFu)};
        std::fwrite(b, 1, 4, f);
    };
    const auto u16 = [&](uint16_t v) {
        unsigned char b[2] = {static_cast<unsigned char>(v & 0xFFu),
                              static_cast<unsigned char>((v >> 8) & 0xFFu)};
        std::fwrite(b, 1, 2, f);
    };

    std::fwrite("RIFF", 1, 4, f);
    u32(36u + data_bytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16u);
    u16(1u);                                  // PCM
    u16(channels);
    u32(rate);
    u32(byte_rate);
    u16(static_cast<uint16_t>(channels * 2u));
    u16(16u);
    std::fwrite("data", 1, 4, f);
    u32(data_bytes);
    std::fwrite(samples.data(), 1, data_bytes, f);
    std::fclose(f);
}

void a_missing_file_never_breaks_anything() {
    // THE CONTRACT: an optional clip that is not there is a NORMAL state. It
    // must leave the synthesised clip exactly as it was and carry on.
    SfxBank bank = synth_bank(kRate);
    const PcmClip before = bank.checkpoint;

    REQUIRE_MSG(!override_clip_from_wav(bank.checkpoint,
                                        "definitely/not/a/real/path.wav"),
                "a missing file reported success", "missing");
    REQUIRE_MSG(bank.checkpoint.samples == before.samples,
                "a missing file damaged the synthesised clip", "missing");
    REQUIRE(bank.checkpoint.sample_rate == before.sample_rate);
    REQUIRE(bank.checkpoint.channels == before.channels);

    // Repeated attempts must stay harmless — this gets called per load, and
    // the warn-once must not turn into a warn-never-but-corrupt.
    for (int i = 0; i < 5; ++i) {
        REQUIRE(!override_clip_from_wav(bank.checkpoint, "still/not/here.wav"));
    }
    REQUIRE(bank.checkpoint.samples == before.samples);

    PcmClip untouched;
    REQUIRE(!load_wav_clip("nope.wav", untouched));
    REQUIRE(untouched.samples.empty());

    pass("a missing optional clip is a no-op, not a failure");
}

void a_real_wav_replaces_the_synthesised_clip() {
    const std::string path = "apricot_audio_test_clip.wav";
    std::vector<int16_t> pcm(4000);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        // 300 Hz at 24 kHz, so the round trip can be checked by PITCH and not
        // just by byte count.
        pcm[i] = static_cast<int16_t>(
            20000.0 * std::sin(6.283185307 * 300.0 *
                               static_cast<double>(i) / 24000.0));
    }
    write_test_wav(path, 24000u, 1u, pcm);

    PcmClip clip;
    REQUIRE_MSG(load_wav_clip(path, clip), "a valid WAV failed to load", "wav");
    REQUIRE(clip.sample_rate == 24000u);
    REQUIRE(clip.channels == 1u);
    REQUIRE(clip.frame_count() == pcm.size());
    REQUIRE(all_finite(clip.samples));
    REQUIRE(peak(clip.samples) <= 1.0f);
    REQUIRE_NEAR(static_cast<double>(peak(clip.samples)), 20000.0 / 32768.0, 0.01);

    const double f0 = estimate_f0(clip.samples, 24000.0, 100.0, 2000.0);
    REQUIRE_MSG(std::fabs(f0 - 300.0) < 5.0,
                "the WAV round trip changed the pitch", "wav");

    // And the drop-in path replaces a synthesised clip with it.
    SfxBank bank = synth_bank(kRate);
    REQUIRE(override_clip_from_wav(bank.checkpoint, path));
    REQUIRE(bank.checkpoint.sample_rate == 24000u);
    REQUIRE(bank.checkpoint.frame_count() == pcm.size());

    // A truncated / corrupt file must be rejected, not half-loaded.
    const std::string bad = "apricot_audio_test_bad.wav";
    std::FILE* f = std::fopen(bad.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite("RIFFxxxxWAVEjunk", 1, 16, f);
    std::fclose(f);
    PcmClip rejected;
    REQUIRE_MSG(!load_wav_clip(bad, rejected), "a corrupt WAV was accepted",
                "wav");
    REQUIRE(rejected.samples.empty());

    std::remove(path.c_str());
    std::remove(bad.c_str());

    std::printf("      (300 Hz / 24 kHz WAV round-tripped at %.2f Hz)\n", f0);
    pass("a real WAV loads, keeps its pitch, and replaces the synth clip");
}

}  // namespace

int main() {
    std::printf("audio_synth_tests\n");
    the_reference_tone_is_exact();
    every_clip_in_the_bank_is_playable();
    the_bank_is_deterministic();
    surfaces_sound_different_from_each_other();
    tyre_scrub_brightness_tracks_slip();
    surface_roll_tracks_speed_and_surface();
    suspension_thump_weight_tracks_impact();
    weather_layers_are_independent();
    a_missing_file_never_breaks_anything();
    a_real_wav_replaces_the_synthesised_clip();
    return done("audio_synth_tests");
}
