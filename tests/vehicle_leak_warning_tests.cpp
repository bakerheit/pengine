#include <limits>
#include "audio/vehicle_leak_warning.h"
#include "audio_analysis.h"
#include "core/fixed_step.h"
#include "test_assert.h"
using namespace apricot;
namespace {
std::vector<float> render(VoiceMixer& mixer,std::size_t frames=1024) {
    std::vector<float> pcm(frames*2u);mixer.render(pcm.data(),frames);return pcm;
}
void cadence_and_combined_leaks() {
    VoiceMixer mixer;mixer.prepare(kDefaultSampleRate);
    VehicleLeakWarning warning;
    REQUIRE(!warning.update(mixer,true,true,4,0,0.0));
    REQUIRE(warning.update(mixer,true,true,4,7,0.0));
    REQUIRE(apricot_test::rms(render(mixer))>.01);
    REQUIRE(mixer.active_voices()==1u);
    // Simultaneous oil/coolant/fuel leaks share a single immediate warning.
    for(int cycle=0;cycle<3;++cycle) {
        for(int tick=1;tick<480;++tick) {
            REQUIRE(!warning.update(mixer,true,true,4,7,kSimDt));
            render(mixer,134);
            REQUIRE(mixer.active_voices()<=1u);
        }
        REQUIRE(warning.update(mixer,true,true,4,7,kSimDt));
        REQUIRE(apricot_test::rms(render(mixer))>.01);
        REQUIRE(mixer.active_voices()==1u);
    }
    // Render-only frames cannot advance the dashboard timer.
    for(int i=0;i<1000;++i) REQUIRE(!warning.update(mixer,true,true,4,7,0.0));
    REQUIRE(!warning.update(mixer,true,true,4,7,-1.0));
    REQUIRE(!warning.update(mixer,true,true,4,7,std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(!warning.update(mixer,true,true,4,7,std::numeric_limits<double>::infinity()));
    // The same 480 simulated steps grouped into 60-Hz render frames agree.
    for(int i=1;i<240;++i) REQUIRE(!warning.update(mixer,true,true,4,7,2.0*kSimDt));
    REQUIRE(warning.update(mixer,true,true,4,7,2.0*kSimDt));
    REQUIRE(warning.update(mixer,true,true,4,7,20.0));
    REQUIRE(!warning.update(mixer,true,true,4,7,0.0));
    REQUIRE(!warning.update(mixer,true,true,4,7,0.0));
    REQUIRE(mixer.dropped_commands()==0u);
    apricot_test::pass("combined leak chime repeats every four simulated seconds without frame or catch-up spam");
}
void pause_repair_and_occupancy() {
    VoiceMixer mixer;mixer.prepare(kDefaultSampleRate);
    VehicleLeakWarning warning;
    REQUIRE(warning.update(mixer,true,true,4,1,0.0));
    REQUIRE(!warning.update(mixer,true,true,4,1,2.0));
    REQUIRE(!warning.update(mixer,false,true,4,1,500.0));
    REQUIRE(apricot_test::rms(render(mixer,16000))<1e-8);
    REQUIRE(mixer.active_voices()==0u);
    REQUIRE(!warning.update(mixer,true,true,4,1,1.9));
    REQUIRE(warning.update(mixer,true,true,4,1,.1));
    // New leak kinds during pause are heard once on resume, together.
    REQUIRE(!warning.update(mixer,false,true,4,7,100.0));
    REQUIRE(warning.update(mixer,true,true,4,7,0.0));
    REQUIRE(apricot_test::rms(render(mixer))>.01);
    REQUIRE(mixer.active_voices()==1u);
    REQUIRE(!warning.update(mixer,true,true,4,0,0.0));
    render(mixer,16000);
    REQUIRE(apricot_test::rms(render(mixer))<1e-8);
    REQUIRE(mixer.active_voices()==0u);
    REQUIRE(!warning.update(mixer,true,true,4,0,40.0));
    REQUIRE(warning.update(mixer,true,true,4,1,0.0));
    REQUIRE(!warning.update(mixer,true,false,4,1,0.0));
    render(mixer,16000);
    REQUIRE(mixer.active_voices()==0u);
    REQUIRE(warning.update(mixer,true,true,4,1,0.0));
    REQUIRE(warning.update(mixer,true,true,5,1,0.0));
    render(mixer);
    REQUIRE(mixer.active_voices()==1u);
    REQUIRE(!warning.update(mixer,false,true,5,0,0.0));
    REQUIRE(warning.update(mixer,true,true,5,1,0.0));
    warning.stop(mixer);render(mixer,16000);
    REQUIRE(mixer.active_voices()==0u);
    REQUIRE(mixer.dropped_commands()==0u);
    apricot_test::pass("pause freezes cadence and silences playback; repair, exit and vehicle changes rearm");
}
}
int main() {
    VehicleDamageState damage;
    REQUIRE(vehicle_leak_mask(damage)==0u);
    damage.zones[kDamageFrontCenter]=.50f;REQUIRE(vehicle_leak_mask(damage)==1u);
    damage.zones[kDamageFrontCenter]=.80f;REQUIRE(vehicle_leak_mask(damage)==3u);
    damage.zones[kDamageRearCenter]=.70f;REQUIRE(vehicle_leak_mask(damage)==7u);
    const auto& clip=vehicle_leak_ding();
    REQUIRE(clip.sample_rate==48000u && clip.channels==1u);
    REQUIRE(clip.duration_seconds()>4.01f && clip.duration_seconds()<4.04f);
    REQUIRE(apricot_test::all_finite(clip.samples));
    REQUIRE(apricot_test::peak(clip.samples)>.20f && apricot_test::peak(clip.samples)<.30f);
    REQUIRE(apricot_test::rms(clip.samples)>.04f);
    REQUIRE(std::fabs(clip.samples.back())<.001f);
    cadence_and_combined_leaks();
    pause_repair_and_occupancy();
    return apricot_test::done("vehicle_leak_warning_tests");
}
