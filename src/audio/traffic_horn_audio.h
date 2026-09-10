#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <optional>

#include "audio/mixer.h"
#include "core/fixed_step.h"
#include "traffic/crowd.h"

namespace apricot {

inline uint64_t traffic_horn_seed(uint64_t lane, uint32_t slot) {
    uint64_t h=lane ^ (uint64_t(slot)*0x9e3779b97f4a7c15ULL);
    h=(h^(h>>30))*0xbf58476d1ce4e5b9ULL;
    h=(h^(h>>27))*0x94d049bb133111ebULL;
    return h^(h>>31);
}

inline std::size_t traffic_horn_clip(const VehicleAgent& car) {
    const auto kind=traffic_vehicle_kind(car);
    if (kind==TrafficVehicleKind::BoxTruck || kind==TrafficVehicleKind::Firetruck ||
        kind==TrafficVehicleKind::Snowplow)
        return 2;
    return static_cast<std::size_t>(traffic_horn_seed(car.lane_key,car.slot)%2u);
}

// Audio reads the real frustration state without changing traffic decisions.
// In particular, waiting for a red light must not start a chorus of horns.
inline bool traffic_horn_blocked(const VehicleAgent& car, const LaneGraph& graph,
                                 int64_t step, const CrowdTuning& tuning) {
    if (!graph.valid(car.lane) || !(car.speed_mps<0.5f) ||
        !(car.delay_seconds>0.0f) || vehicle_engine_failed(car.mechanical) ||
        car.police_pursuit ||
        (car.police_unit && !police_officer_driving_allowed(car.officer))) return false;
    const auto junction=graph.lane(car.lane).junction_to;
    if (car.committed_junction!=junction && car.turn_from_lane==kInvalidLane &&
        junction<graph.junction_count()) {
        if (graph.junction_control(junction)==JunctionControl::Signal &&
            traffic_signal_phase(graph,junction,car.lane,step,tuning)!=TrafficSignalPhase::Green)
            return false;
        if (graph.approach_control(car.lane)==JunctionControl::Stop &&
            car.stop_junction==junction && car.stop_wait_steps>0 && !car.stop_completed)
            return false;
    }
    return true;
}

struct TrafficHornEvent {
    VisiblePoliceIdentity driver{};
    uint64_t step=0;
    std::size_t clip=0;
    glm::vec3 position{0};
};

// Fixed-step requests; the mixer owns playback. No decoding or PCM allocation
// occurs during a request, and a streamed/reordered car keeps its own horn.
class TrafficHornAudio {
public:
    static constexpr std::size_t kVoiceBudget=3;
    static constexpr float kAudibleRangeM=70.0f;

    void start(VoiceMixer& mixer, const SfxBank& bank) {
        stop(); mixer_=&mixer; bank_=&bank; active_=true;
    }
    void stop() { reset(); mixer_=nullptr; bank_=nullptr; active_=false; }
    void reset() {
        if (mixer_) for (auto& voice:voices_) mixer_->stop_oneshot(voice.handle);
        voices_={}; drivers_.clear(); last_step_.reset(); next_global_step_=0;
    }
    void set_active(bool active) {
        if (!active && active_) reset();
        active_=active;
    }
    uint64_t play_count() const { return play_count_; }
    unsigned played_clip_mask() const { return played_clip_mask_; }
    const TrafficHornEvent& last_event() const { return last_event_; }
    std::size_t loaded_clip_count() const {
        if (!bank_) return 0;
        return static_cast<std::size_t>(std::count_if(bank_->traffic_horns.begin(),
            bank_->traffic_horns.end(),[](const auto& clip){return !clip.empty();}));
    }
    std::size_t voice_count() const {
        return static_cast<std::size_t>(std::count_if(voices_.begin(),voices_.end(),
            [](const auto& voice){return voice.handle.valid();}));
    }

    bool update(uint64_t step, const std::vector<VehicleAgent>& cars,
                const LaneGraph& graph, const CrowdTuning& tuning, glm::vec3 listener) {
        if (!active_ || !mixer_ || !bank_) return false;
        if (last_step_) {
            if (step==*last_step_) return false;
            if (step<*last_step_ || step-*last_step_>1u) reset();
        }
        last_step_=step;
        for (auto& voice:voices_) {
            if (!voice.handle.valid()) continue;
            const auto car=std::find_if(cars.begin(),cars.end(),[&](const auto& v){
                return id(v)==voice.driver;
            });
            if (step>=voice.end_step || car==cars.end() ||
                glm::distance(car->pos,listener)>=kAudibleRangeM) {
                mixer_->stop_oneshot(voice.handle); voice={};
            } else {
                voice.params.position=car->pos+glm::vec3{0,0.8f,0};
                mixer_->set_oneshot(voice.handle,voice.params);
            }
        }
        const VehicleAgent* chosen=nullptr;
        float best_distance2=kAudibleRangeM*kAudibleRangeM;
        for (const auto& car:cars) {
            const auto delta=car.pos-listener;
            const float distance2=glm::dot(delta,delta);
            if (!(distance2<kAudibleRangeM*kAudibleRangeM)) continue;
            auto& driver=drivers_[id(car)];
            driver.last_seen=step;
            if (!traffic_horn_blocked(car,graph,static_cast<int64_t>(step),tuning)) {
                driver.blocked_since.reset(); continue;
            }
            if (!driver.blocked_since) driver.blocked_since=step;
            const uint64_t seed=traffic_horn_seed(car.lane_key,car.slot);
            const float stagger=float((seed>>8)&1023u)/1023.0f*0.65f;
            const auto wait=seconds_to_steps(std::max(0.5f,car.profile.honk_after)+stagger);
            if (step-*driver.blocked_since<wait || step<driver.next_honk ||
                car.delay_seconds<car.profile.honk_after ||
                bank_->traffic_horns[traffic_horn_clip(car)].empty()) continue;
            if (!chosen || distance2<best_distance2 ||
                (distance2==best_distance2 && id(car)<id(*chosen))) {
                chosen=&car; best_distance2=distance2;
            }
        }
        for (auto it=drivers_.begin();it!=drivers_.end();) {
            if (it->second.last_seen!=step) it=drivers_.erase(it);
            else ++it;
        }
        if (!chosen || step<next_global_step_) return false;
        auto voice=std::find_if(voices_.begin(),voices_.end(),
            [](const auto& v){return !v.handle.valid();});
        if (voice==voices_.end()) return false;
        const uint64_t seed=traffic_horn_seed(chosen->lane_key,chosen->slot);
        const std::size_t clip=traffic_horn_clip(*chosen);
        VoiceParams params;
        params.category=Category::World;
        params.spatial=true;
        params.position=chosen->pos+glm::vec3{0,0.8f,0};
        params.attenuation={4.0f,kAudibleRangeM,1.1f};
        params.gain=0.8f;
        params.pitch=0.94f+float((seed>>20)&1023u)/1023.0f*0.12f;
        voice->handle=mixer_->play_oneshot(&bank_->traffic_horns[clip],params);
        if (!voice->handle.valid()) return false;
        voice->params=params; voice->driver=id(*chosen);
        voice->end_step=step+seconds_to_steps(bank_->traffic_horns[clip].duration_seconds()/params.pitch);
        auto& driver=drivers_[voice->driver];
        const float repeat=std::max(4.5f,chosen->profile.patience_seconds+2.0f)+
            float((seed>>32)&1023u)/1023.0f*1.5f;
        driver.next_honk=step+seconds_to_steps(repeat);
        next_global_step_=step+seconds_to_steps(0.65f);
        // The reported event keeps the public (lane, slot) shape; the
        // generation is an internal bookkeeping detail of this module.
        last_event_={{voice->driver.lane_key,voice->driver.slot},
                     step,clip,params.position};
        ++play_count_; played_clip_mask_|=1u<<clip;
        return true;
    }

private:
    // Keyed on the DEPARTURE, not the slot. A slot re-departs on its schedule,
    // so a fresh car can occupy a pair with no absent frame; keeping a driver
    // entry across that would hand a brand-new car the previous one's queue
    // impatience and honk cooldown, and would slide an in-flight horn across
    // the map onto a car that never sounded it.
    struct DriverId {
        uint64_t lane_key=0; uint32_t slot=0; int64_t generation=0;
        bool operator<(const DriverId& o) const {
            return std::tie(lane_key,slot,generation) <
                   std::tie(o.lane_key,o.slot,o.generation);
        }
        bool operator==(const DriverId& o) const {
            return lane_key==o.lane_key && slot==o.slot &&
                   generation==o.generation;
        }
    };
    static DriverId id(const VehicleAgent& car) {
        return {car.lane_key,car.slot,car.generation};
    }
    static uint64_t seconds_to_steps(float seconds) {
        return static_cast<uint64_t>(std::ceil(double(seconds)*kSimHz));
    }
    struct Driver {
        std::optional<uint64_t> blocked_since;
        uint64_t next_honk=0, last_seen=0;
    };
    struct Voice {
        OneShotHandle handle;
        DriverId driver{};
        uint64_t end_step=0;
        VoiceParams params{};
    };
    VoiceMixer* mixer_=nullptr;
    const SfxBank* bank_=nullptr;
    bool active_=false;
    std::optional<uint64_t> last_step_;
    uint64_t next_global_step_=0, play_count_=0;
    unsigned played_clip_mask_=0;
    TrafficHornEvent last_event_{};
    std::map<DriverId,Driver> drivers_;
    std::array<Voice,kVoiceBudget> voices_{};
};

} // namespace apricot
