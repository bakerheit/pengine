#include <algorithm>
#include <cmath>
#include <vector>
#include "audio/intro_audio.h"
#include "core/asset_root.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    PcmClip rain;
    REQUIRE(load_wav_clip(asset_path("audio/world/light_rain_loop.wav"), rain));
    IntroAudio audio;
    REQUIRE(audio.load(asset_path("audio/intro/probable-cause-title-score-v1.wav"),
                       rain));
    std::vector<float> pcm(1600);
    double energy = 0;
    for (int frame = 0; frame < 120; ++frame) {
        audio.update(mixer, true, 1.0f / 60);
        mixer.render(pcm.data(), 800);
        for (float sample : pcm) {
            REQUIRE(std::isfinite(sample));
            REQUIRE(std::fabs(sample) < 0.5f);
            energy += sample * sample;
        }
    }
    REQUIRE(energy > 1);
    REQUIRE(mixer.active_voices() == 2);
    for (int frame = 0; frame < 120; ++frame) {
        audio.update(mixer, false, 1.0f / 60);
        mixer.render(pcm.data(), 800);
    }
    REQUIRE(mixer.active_voices() == 0);
    REQUIRE(std::all_of(pcm.begin(), pcm.end(), [](float v) { return v == 0; }));
    audio.update(mixer, true, 1);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 2);
    audio.stop(mixer);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 0);
    REQUIRE(mixer.dropped_commands() == 0);
    IntroAudio missing;
    missing.update(mixer, true, 1);
    mixer.render(pcm.data(), 800);
    REQUIRE(mixer.active_voices() == 0);
    apricot_test::pass("title score and recorded rain play, fade out, return and stop safely without clipping");
    return apricot_test::done("audio_intro_runtime_tests");
}
