#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/intro_audio.h"
#include "audio/player_car_assets.h"
#include "audio/rain_audio.h"
#include "audio_analysis.h"
#include "core/asset_root.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

int main() {
    SfxBank bank;
    SfxOverridePaths paths;
    paths.rain = player_car_audio_overrides().rain;
    REQUIRE(override_bank_from_wavs(bank, paths) == 1u);
    REQUIRE(bank.rain.channels == 2u);
    REQUIRE(bank.rain.sample_rate == 48000u);
    REQUIRE(bank.rain.duration_seconds() > 103.0f);
    REQUIRE(bank.rain.duration_seconds() < 104.0f);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        // Rain is broadband noise: adjacent samples need not match. The wrap
        // must be within normal local sample variation, not an arbitrary zero.
        double delta_energy = 0.0;
        for (std::size_t frame = 1; frame < 48000; ++frame) {
            const double delta = bank.rain.samples[frame * 2 + channel] -
                                 bank.rain.samples[(frame - 1) * 2 + channel];
            delta_energy += delta * delta;
        }
        const double normal_delta = std::sqrt(delta_energy / 47999.0);
        REQUIRE(std::abs(bank.rain.samples[channel] -
                         bank.rain.samples[bank.rain.samples.size() - 2 + channel]) <
                normal_delta * 3.0);
    }
    const auto original_frames = bank.rain.frame_count();
    paths.rain = "/missing/apricot-rain.wav";
    REQUIRE(override_bank_from_wavs(bank, paths) == 0u);
    REQUIRE(bank.rain.frame_count() == original_frames);

    std::vector<float> pcm(1600);
    const auto settled_level = [&](float intensity) {
        VoiceMixer mixer;
        mixer.prepare(kDefaultSampleRate);
        RainAudio rain;
        rain.update(mixer, bank, intensity, 2.0f);
        mixer.render(pcm.data(), 800);
        mixer.render(pcm.data(), 800);
        const double level = rms(pcm);
        REQUIRE(mixer.active_voices() == (intensity > 0.0f ? 1u : 0u));
        rain.stop(mixer);
        mixer.render(pcm.data(), 800);
        REQUIRE(mixer.active_voices() == 0u);
        return level;
    };
    REQUIRE(settled_level(0.0f) == 0.0);
    const double light = settled_level(0.2f);
    const double heavy = settled_level(1.0f);
    REQUIRE(light > 0.001);
    REQUIRE(heavy > light * 1.5);
    REQUIRE(heavy < 0.1);

    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    mixer.set_category(Category::Music, 0.0f);
    IntroAudio intro;
    REQUIRE(intro.load(asset_path("audio/intro/probable-cause-title-score-v1.wav"),
                       bank.rain));
    intro.update(mixer, true, 2.0f);
    mixer.render(pcm.data(), 800);
    mixer.render(pcm.data(), 800);
    REQUIRE(rms(pcm) > 0.001); // Rain remains audible with the score muted.
    REQUIRE(mixer.active_voices() == 2u);

    RainAudio weather;
    for (int frame = 0; frame < 120; ++frame) {
        intro.update(mixer, false, 1.0f / 60.0f);
        weather.update(mixer, bank, 1.0f, 1.0f / 60.0f);
        mixer.render(pcm.data(), 800);
    }
    REQUIRE(mixer.active_voices() == 1u);
    // Render past the full recording's wrap, including the prepared seam.
    for (int block = 0; block < 6300; ++block) {
        mixer.render(pcm.data(), 800);
        REQUIRE(rms(pcm) > 0.001);
        for (float sample : pcm) {
            REQUIRE(std::isfinite(sample));
            REQUIRE(std::abs(sample) < 0.5f);
        }
    }
    weather.update(mixer, bank, 0.0f, 1.0f / 60.0f);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 1u); // Clear weather fades, never cuts.
    for (int frame = 0; frame < 120; ++frame) {
        weather.update(mixer, bank, 0.0f, 1.0f / 60.0f);
        mixer.render(pcm.data(), 800);
    }
    REQUIRE(mixer.active_voices() == 0u);
    REQUIRE(rms(pcm) == 0.0);
    weather.update(mixer, bank, 0.5f, 2.0f);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 1u);
    weather.stop(mixer);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 0u);
    SfxBank empty;
    weather.update(mixer, empty, 1.0f, 2.0f);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 0u);
    REQUIRE(mixer.dropped_commands() == 0u);
    std::printf("rain RMS: light %.5f, heavy %.5f\n", light, heavy);
    pass("recorded menu/game rain loops, scales with weather, fades and stops safely");
    return done("audio_rain_runtime_tests");
}
