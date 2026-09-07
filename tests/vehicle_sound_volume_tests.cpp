#include "audio/vehicle_audio.h"
#include "audio_analysis.h"
#include "test_assert.h"
using namespace apricot;
namespace {
PcmClip tone() {
    PcmClip clip;clip.channels=1;clip.sample_rate=kDefaultSampleRate;
    clip.samples.resize(kDefaultSampleRate);
    for(std::size_t i=0;i<clip.samples.size();++i)
        clip.samples[i]=static_cast<float>(.10*std::sin(6.283185307179586*220.0*
            static_cast<double>(i)/kDefaultSampleRate));
    return clip;
}
std::vector<float> render(VoiceMixer& mixer) {
    std::vector<float> pcm(8192);mixer.render(pcm.data(),4096);return pcm;
}
}
int main() {
    SfxBank bank;bank.player_car_collision=tone();
    VoiceMixer actual,previous;actual.prepare(kDefaultSampleRate);previous.prepare(kDefaultSampleRate);
    VehicleAudio car;REQUIRE(car.start(actual,bank));
    VoiceParams old;old.category=Category::Impacts;
    old.gain=recorded_collision_gain(15.0f)*.75f;old.spatial=true;
    old.position={0,0,0};old.attenuation.ref_distance=7;old.attenuation.max_distance=100;
    old.attenuation.rolloff=.65f;
    previous.play_oneshot(&bank.player_car_collision,old);
    car.play_car_collision(15.0f,{0,0,0});
    const auto now=render(actual),before=render(previous);
    REQUIRE(apricot_test::all_finite(now));
    REQUIRE(apricot_test::peak(now)<1.0);
    REQUIRE_NEAR(apricot_test::rms(now)/apricot_test::rms(before),1.25,1e-5);
    car.play_car_collision(12.0f,{0,0,0});render(actual);
    REQUIRE(actual.active_voices()==1u);
    car.stop();render(actual);
    REQUIRE(actual.active_voices()==0u);
    // Every audition uses the same production gain policy. Tyres remain at
    // their original level; engine/brake/surface retain the prior 25% cut.
    for(std::size_t i=0;i<kCarSoundUseCount;++i) {
        const auto use=static_cast<CarSoundUse>(i);
        bank.car_sound_audition[i][0]=tone();
        const auto mix=car_sound_audition_mix(bank,use,0);
        const float expected=use==CarSoundUse::Tyres ? 1.0f :
            (use==CarSoundUse::Crash ? .9375f : .75f);
        REQUIRE_NEAR(mix.gain,expected,1e-6);
    }
    REQUIRE(actual.dropped_commands()==0u);
    apricot_test::pass("rendered crash PCM is 25% louder without stacking; tyre and other car levels stay unchanged");
    return apricot_test::done("vehicle_sound_volume_tests");
}
