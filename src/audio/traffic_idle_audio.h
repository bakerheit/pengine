#pragma once

#include <array>
#include <algorithm>
#include "audio/mixer.h"
#include "audio/vehicle_sound_profile.h"

namespace apricot {

// One shared recording, at most twelve audible emitters. Selection is O(N*12)
// with no allocation, decoding or PCM copies during a frame. Lane+slot keep
// voices attached to cars as the traffic list reorders/streams.
class TrafficIdleAudio {
public:
    static constexpr std::size_t kVoiceBudget = 12;
    void start(VoiceMixer& mixer, const SfxBank& bank) {
        stop(); mixer_ = &mixer; clip_ = &bank.engine_idle;
    }
    void stop() {
        if (mixer_) for (auto& v : voices_) mixer_->close_loop(v.handle);
        voices_ = {}; mixer_ = nullptr; clip_ = nullptr;
    }
    void begin_frame(glm::vec3 listener) {
        listener_ = listener; count_ = 0;
    }
    // Optional catalog mesh path adds model timbre without changing selection.
    void submit(uint64_t lane, uint32_t slot, glm::vec3 position, float speed,
                std::string_view mesh_path = {}) {
        if (!clip_ || clip_->empty()) return;
        const auto delta = position - listener_;
        const float distance2 = glm::dot(delta, delta);
        if (!(distance2 < 50.0f * 50.0f)) return;
        Candidate candidate{lane, slot, position, speed, distance2,
                            vehicle_sound_profile(mesh_path)};
        // Slight preference for existing voices prevents boundary churn.
        for (const auto& v : voices_) if (v.handle.valid() && same(v.car, candidate)) {
            candidate.priority *= .85f; break;
        }
        if (count_ < kVoiceBudget) candidates_[count_++] = candidate;
        else {
            auto worst = std::max_element(candidates_.begin(), candidates_.end(),
                [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
            if (candidate.priority < worst->priority) *worst = candidate;
        }
    }
    void end_frame(bool active) {
        if (!mixer_ || !clip_ || clip_->empty()) return;
        if (!active) count_ = 0;
        for (auto& v : voices_) {
            bool keep = false;
            for (std::size_t i = 0; i < count_; ++i) keep |= same(v.car, candidates_[i]);
            if (!keep) { mixer_->close_loop(v.handle); v.handle = {}; }
        }
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& car = candidates_[i];
            auto voice = std::find_if(voices_.begin(), voices_.end(), [&](const Voice& v) {
                return v.handle.valid() && same(v.car, car);
            });
            if (voice == voices_.end()) voice = std::find_if(voices_.begin(), voices_.end(),
                [](const Voice& v) { return !v.handle.valid(); });
            if (voice == voices_.end()) continue;
            VoiceParams p;
            p.category = Category::Engine;
            p.spatial = true;
            p.position = car.position;
            p.attenuation = {3.0f, 50.0f, 1.25f};
            const float motion = std::clamp(std::abs(car.speed) / 10.0f, 0.0f, 1.0f);
            p.gain = kVehicleSoundGain * .6f * (.23f - .15f * motion);
            p.pitch = traffic_idle_pitch(car.lane, car.slot, car.speed, car.profile);
            voice->car = car;
            if (voice->handle.valid()) mixer_->set_loop(voice->handle, p);
            else voice->handle = mixer_->open_loop(clip_, p);
        }
    }
    std::size_t loop_count() const {
        return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(),
            [](const Voice& v) { return v.handle.valid(); }));
    }
private:
    struct Candidate {
        uint64_t lane = 0;
        uint32_t slot = 0;
        glm::vec3 position{0};
        float speed = 0, priority = 0;
        VehicleSoundProfile profile{"default", 1.0f};
    };
    static bool same(const Candidate& a, const Candidate& b) {
        return a.lane == b.lane && a.slot == b.slot;
    }
    struct Voice { Candidate car; VoiceHandle handle; };
    VoiceMixer* mixer_ = nullptr;
    const PcmClip* clip_ = nullptr;
    glm::vec3 listener_{0};
    std::array<Candidate, kVoiceBudget> candidates_{};
    std::array<Voice, kVoiceBudget> voices_{};
    std::size_t count_ = 0;
};

} // namespace apricot
