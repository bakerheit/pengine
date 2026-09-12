#include <cstdio>
#include <vector>

#include "audio/city_audio.h"
#include "audio/player_car_assets.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

void the_recorded_city_bed_loops_at_background_level() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + shipped_footstep_take_count() + kTrafficHornClipCount +
            kCarSoundUseCount * // Eight vehicle roles, ambience, rain and mission sting.
                     static_cast<std::size_t>(kCarSoundVariantCount));

    CityAudio city;
    REQUIRE(city.start(mixer, bank));
    REQUIRE(city.loop_count() == 1u);

    std::vector<float> out(1024u * 2u, 0.0f);
    mixer.render(out.data(), 1024u);
    const double level = rms(out);
    REQUIRE_MSG(level > 0.002, "city ambience is silent", "live city loop");
    REQUIRE_MSG(level < 0.05, "city ambience overwhelms foreground audio",
                "background level");
    REQUIRE(mixer.active_voices() == 1u);
    REQUIRE(mixer.dropped_commands() == 0u);

    city.stop();
    mixer.render(out.data(), 1024u);
    REQUIRE(mixer.active_voices() == 0u);
    std::printf("      (city bed RMS %.4f at 22%% emitter gain)\n", level);
    pass("the recorded city bed owns one quiet persistent loop");
}

void a_missing_city_recording_is_normal_silence() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    CityAudio city;
    REQUIRE(!city.start(mixer, SfxBank{}));
    REQUIRE(!city.started());
    REQUIRE(city.loop_count() == 0u);
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("a missing city recording stays safely silent");
}

}  // namespace

int main() {
    std::printf("audio_city_runtime_tests\n");
    the_recorded_city_bed_loops_at_background_level();
    a_missing_city_recording_is_normal_silence();
    return done("audio_city_runtime_tests");
}
