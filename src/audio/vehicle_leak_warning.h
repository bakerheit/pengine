#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "audio/synth.h"
#include "audio/mixer.h"
#include "core/asset_root.h"
#include "audio/vehicle_sound_profile.h"
#include "physics/vehicle_damage.h"

namespace apricot {
inline unsigned vehicle_leak_mask(const VehicleDamageState& damage) {
    unsigned mask=0;
    for(const auto& leak:vehicle_fluid_leaks(damage))
        if(leak.severity>0.f) mask|=1u<<static_cast<unsigned>(leak.kind);
    return mask;
}
inline const PcmClip& vehicle_leak_ding() {
    static const PcmClip clip=[] {
        PcmClip p;p.sample_rate=kDefaultSampleRate;p.channels=1;
        p.samples.resize(kDefaultSampleRate*3u/4u);
        for(std::size_t i=0;i<p.samples.size();++i) {
            const double t=double(i)/kDefaultSampleRate;
            double value=0;
            for(int note=0;note<2;++note) {
                const double u=t-double(note)*.28;
                if(u<0)continue;
                const double envelope=std::min(1.,u/.005)*std::exp(-u*12.)*
                    std::min(1.,(.75-t)/.025);
                const double phase=6.283185307179586*(note?660.:880.)*u;
                value+=envelope*(.65*std::sin(phase)+.10*std::sin(phase*2.01));
            }
            p.samples[i]=static_cast<float>(value);
        }
        // Recorded warning is the shipped path. Keep the deterministic ding
        // above as a safe fallback for asset-less development builds.
        override_clip_from_wav(p,asset_path(
            "audio/vehicles/player/runtime/engine_warning.wav"));
        return p;
    }();
    return clip;
}
struct VehicleLeakWarning {
    static constexpr double kRepeatSeconds = 4.0;
    uint64_t vehicle_key=0;
    unsigned previous_mask=0;
    double elapsed_seconds=0.0;
    OneShotHandle voice{};
    // Feed completed simulation time, never elapsed wall time. Pauses retain
    // the cadence and unseen leak onsets; repair/exit rearm the dashboard.
    bool update(VoiceMixer& mixer,bool active,bool occupied,uint64_t key,unsigned mask,
                double sim_dt_seconds) {
        if(!occupied || key!=vehicle_key || mask==0u) {
            previous_mask=0;vehicle_key=key;elapsed_seconds=0.0;
            mixer.stop_oneshot(voice);voice={};
            if(!occupied || mask==0u)return false;
        }
        if(!active) {mixer.stop_oneshot(voice);voice={};return false;}
        const bool onset=(mask & ~previous_mask)!=0;
        previous_mask=mask;
        if(onset) elapsed_seconds=0.0;
        else {
            if(std::isfinite(sim_dt_seconds) && sim_dt_seconds>0.0)
                elapsed_seconds+=sim_dt_seconds;
            if(elapsed_seconds+1e-9<kRepeatSeconds)return false;
            // Coalesce overdue reminders into one chime, with no catch-up
            // burst on the next render frames after a large time step.
            elapsed_seconds=std::fmod(elapsed_seconds+1e-9,kRepeatSeconds);
        }
        mixer.stop_oneshot(voice);
        VoiceParams p;p.category=Category::Engine;p.gain=.60f*kVehicleSoundGain;
        p.spatial=false;
        voice=mixer.play_oneshot(&vehicle_leak_ding(),p);
        return voice.valid();
    }
    void stop(VoiceMixer& mixer) {mixer.stop_oneshot(voice);*this={};}
};
} // namespace apricot
