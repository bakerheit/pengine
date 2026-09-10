#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "core/fixed_step.h"
#include "traffic/crowd.h"

namespace apricot {

struct PoliceArrestEvent {
    VisiblePoliceIdentity officer{};
    uint64_t step = 0;
};

// One observation per completed fixed step. Each officer must personally
// maintain range and sight for the full hold; a changing nearest officer must
// neither erase another officer's progress nor inherit it.
class PoliceArrestTracker {
public:
    static constexpr uint32_t kHoldSteps =
        static_cast<uint32_t>(kPoliceArrestHoldSeconds * kSimHz);

    void reset() {
        holds_.clear();
        last_step_.reset();
        reported_ = false;
    }

    std::optional<PoliceArrestEvent> observe(
            uint64_t step, bool player_on_foot, int wanted_level,
            glm::vec3 player_position, const std::vector<VehicleAgent>& officers,
            const std::vector<VisiblePoliceIdentity>& visible) {
        if (wanted_level <= 0) {
            reset();
            return std::nullopt;
        }
        if (!player_on_foot) holds_.clear();
        if (last_step_) {
            if (step == *last_step_) return std::nullopt;
            if (step < *last_step_ || step - *last_step_ != 1u) holds_.clear();
        }
        last_step_ = step;
        if (reported_ || !player_on_foot) return std::nullopt;

        for (const auto& car : officers) {
            // An officer on the ground cannot make an arrest. Without this a
            // downed cop keeps accruing his own hold and cuffs the player from
            // where he fell.
            if (!car.police_unit || !car.police_pursuit ||
                police_officer_downed(car.officer) ||
                car.officer.phase != PoliceOfficerPhase::Pursuing) continue;
            const VisiblePoliceIdentity id{car.lane_key, car.slot};
            if (std::find(visible.begin(), visible.end(), id) == visible.end()) continue;
            const glm::vec3 delta = car.officer.pos - player_position;
            if (!(glm::dot(delta, delta) <= kPoliceArrestRangeM * kPoliceArrestRangeM))
                continue;
            auto& hold = holds_[id];
            // A duplicate identity in a supplied snapshot is still one tick.
            if (hold.ticks == 0 || hold.last_step != step) ++hold.ticks;
            hold.last_step = step;
        }
        for (auto it = holds_.begin(); it != holds_.end();) {
            if (it->second.last_step != step) it = holds_.erase(it);
            else ++it;
        }
        // Stable identity order also resolves simultaneous arrests consistently.
        for (const auto& [id, hold] : holds_) {
            if (hold.ticks < kHoldSteps) continue;
            reported_ = true;
            const PoliceArrestEvent event{id, step};
            holds_.clear();
            return event;
        }
        return std::nullopt;
    }

private:
    struct Hold {
        uint64_t last_step = 0;
        uint32_t ticks = 0;
    };
    std::map<VisiblePoliceIdentity, Hold> holds_;
    std::optional<uint64_t> last_step_;
    bool reported_ = false;
};

}  // namespace apricot
