#pragma once

#include <algorithm>
#include <vector>

#include "game/plow_blade.h"
#include "game/snow_clearance.h"
#include "traffic/crowd.h"

namespace apricot {

// Observe actual traffic movement. First appearances, streaming gaps and
// collision shoves never draw a cleared line from an old truck position. Each
// truck lays its strips through a PlowSweep (game/plow_blade.h), so a turn at
// a junction costs a chord every metre and a half instead of a strip a step.
class SnowplowService {
public:
    void reset() { trucks_.clear(); cleared_distance_m_ = 0.0f; }
    float cleared_distance_m() const { return cleared_distance_m_; }

    void step(const std::vector<VehicleAgent>& vehicles,
              SnowClearanceField& field, float depth_m) {
        std::vector<Truck> next;
        for (const auto& agent : vehicles) {
            if (!agent.snowplow_unit) continue;
            const glm::vec3 blade = agent.pos + agent.fwd * kSnowplowBladeForwardM;
            const auto old = std::find_if(trucks_.begin(), trucks_.end(),
                [&](const Truck& truck) {
                    return truck.key == agent.lane_key && truck.slot == agent.slot;
                });
            Truck truck{agent.lane_key, agent.slot, {}, agent.fwd};
            if (old != trucks_.end()) {
                truck.sweep = old->sweep;
                // Only contiguous forward travel scrapes: a lane-pose flip or
                // a collision shove ends the strip being laid.
                const bool contiguous = glm::dot(agent.fwd, old->forward) > 0.95f;
                const bool scraping = contiguous && depth_m > 0.008f &&
                    agent.speed_mps > 0.1f && agent.collision_recovery_seconds <= 0.0f;
                const float before = truck.sweep.cleared_distance_m();
                truck.sweep.step(blade, agent.speed_mps, scraping,
                                 kSnowplowBladeWidthM * 0.5f, field, depth_m);
                cleared_distance_m_ += truck.sweep.cleared_distance_m() - before;
            } else {
                // A first appearance anchors the blade and clears nothing.
                truck.sweep.step(blade, agent.speed_mps, depth_m > 0.008f,
                                 kSnowplowBladeWidthM * 0.5f, field, depth_m);
            }
            next.push_back(std::move(truck));
        }
        trucks_ = std::move(next);
    }

private:
    struct Truck {
        uint64_t key;
        uint32_t slot;
        PlowSweep sweep;
        glm::vec3 forward;
    };
    std::vector<Truck> trucks_;
    float cleared_distance_m_ = 0.0f;
};

}  // namespace apricot
