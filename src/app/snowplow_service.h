#pragma once

#include <algorithm>
#include <vector>

#include "game/snow_clearance.h"
#include "traffic/crowd.h"

namespace apricot {

// Observe actual traffic movement. First appearances, streaming gaps and
// collision shoves never draw a cleared line from an old truck position.
class SnowplowService {
public:
    void reset() { previous_.clear(); cleared_distance_m_ = 0.0f; }
    float cleared_distance_m() const { return cleared_distance_m_; }

    void step(const std::vector<VehicleAgent>& vehicles,
              SnowClearanceField& field, float depth_m) {
        std::vector<Sample> next;
        for (const auto& agent : vehicles) {
            if (!agent.snowplow_unit) continue;
            const glm::vec3 blade = agent.pos + agent.fwd * kSnowplowBladeForwardM;
            const auto old = std::find_if(previous_.begin(), previous_.end(),
                [&](const Sample& sample) {
                    return sample.key == agent.lane_key && sample.slot == agent.slot;
                });
            if (old != previous_.end() && depth_m > 0.008f &&
                agent.speed_mps > 0.1f && agent.collision_recovery_seconds <= 0.0f) {
                const float distance = glm::distance(blade, old->blade);
                // At 120 Hz this is generous even at road speed, but cannot
                // bridge a despawn, a respawn or a lane-pose discontinuity.
                if (distance > 0.0001f && distance < 0.75f &&
                    glm::dot(agent.fwd, old->forward) > 0.95f &&
                    field.clear_segment(old->blade, blade,
                        kSnowplowBladeWidthM * 0.5f, depth_m)) {
                    cleared_distance_m_ += distance;
                }
            }
            next.push_back({agent.lane_key, agent.slot, blade, agent.fwd});
        }
        previous_ = std::move(next);
    }

private:
    struct Sample {
        uint64_t key;
        uint32_t slot;
        glm::vec3 blade;
        glm::vec3 forward;
    };
    std::vector<Sample> previous_;
    float cleared_distance_m_ = 0.0f;
};

}  // namespace apricot
