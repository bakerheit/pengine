#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "core/fixed_step.h"
#include "game/wanted_system.h"
#include "traffic/crowd.h"

namespace apricot {

struct PoliceOffenseWitness {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    bool los_clear = false;
};

// Sample the actual player vehicle once per fixed step, after movement. A
// vehicle switch, replay seek or teleport must set discontinuous (or reset the
// tracker). Velocity also rejects implausible jumps if the host misses a reset.
struct PoliceDrivingSample {
    uint64_t vehicle_identity = 0;
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 velocity{0.0f};
    float half_length_m = 2.2f;
    int64_t step = 0;
    bool driving = true;
    bool discontinuous = false;
};

// The contact normal points from the cruiser toward the player. Velocities
// are world-space velocities AT THE CONTACT, before either collision impulse.
struct PoliceCollisionSample {
    VisiblePoliceIdentity cruiser{};
    glm::vec3 player_velocity{0.0f};
    glm::vec3 police_velocity{0.0f};
    glm::vec3 normal{0.0f};
    int64_t step = 0;
};

struct PoliceOffenseReport {
    bool reported = false;
    WantedSystem::Crime crime = WantedSystem::Crime::Other;
    float heat = 0.0f;
    LaneRef incoming = kInvalidLane;
    VisiblePoliceIdentity cruiser{};
    explicit operator bool() const { return reported; }
};

inline bool police_offense_has_witness(
    glm::vec3 offender, const std::vector<PoliceOffenseWitness>& witnesses,
    const PoliceTuning& tuning) {
    for (const auto& witness : witnesses) {
        // This is a new crime. Existing wanted level must NOT gate witnessing.
        if (police_can_witness({witness.position.x, witness.position.z},
                {witness.forward.x, witness.forward.z},
                {offender.x, offender.z}, true, witness.los_clear, tuning))
            return true;
    }
    return false;
}

// The visible/physical line is where a nominal traffic car's nose waits. Its
// centre waits farther back, at the shared AI gate. Using the player's nose
// lets short cars and long vans obey the same line.
inline float police_signal_line_station(const LaneGraph& graph, LaneRef lane,
                                         const CrowdTuning& tuning) {
    if (!graph.valid(lane)) return 0.0f;
    const auto& approach = graph.lane(lane);
    return std::min(approach.length_m,
        std::max(0.0f, approach.length_m - traffic_junction_clearance(
            graph, approach.junction_to, tuning)) + tuning.traffic_half_length_m);
}

class PoliceOffenseTracker {
public:
    static constexpr float red_light_heat = 1.0f;
    static constexpr float cruiser_hit_heat = 2.0f;
    // A deliberate parking-speed bump counts; resting contact and solver
    // vibration do not. More serious rams retain the same attribution rule.
    static constexpr float collision_min_closing_mps = 1.0f;
    static constexpr int64_t contact_release_steps = 120;
    static constexpr int64_t collision_cooldown_steps = 600;

    void reset() {
        have_previous_ = false;
        crossed_lanes_.clear();
        contacts_.clear();
    }

    PoliceOffenseReport observe_driving(
        const LaneGraph& graph, const CrowdTuning& traffic,
        const PoliceDrivingSample& sample,
        const std::vector<PoliceOffenseWitness>& witnesses,
        const PoliceTuning& police) {
        const PoliceDrivingSample before = previous_;
        const bool had_previous = have_previous_;
        previous_ = sample;
        have_previous_ = sample.driving && valid_sample(sample);
        if (!have_previous_ || !had_previous || sample.discontinuous ||
            before.vehicle_identity != sample.vehicle_identity) {
            crossed_lanes_.clear();
            return {};
        }
        const int64_t elapsed = sample.step - before.step;
        if (elapsed <= 0 || elapsed > kMaxStepsPerFrame) {
            crossed_lanes_.clear();
            return {};
        }
        const float dt = static_cast<float>(static_cast<double>(elapsed) * kSimDt);
        const glm::vec2 displacement = xz(sample.position - before.position);
        const float speed = std::max(glm::length(xz(before.velocity)),
                                     glm::length(xz(sample.velocity)));
        const float plausible = speed * dt + 0.35f;
        if (glm::dot(displacement, displacement) > plausible * plausible) {
            crossed_lanes_.clear();
            return {};
        }
        const glm::vec2 previous_forward = glm::normalize(xz(before.forward));
        const auto projection = graph.nearest_lane_along(
            xz(before.position), previous_forward, 6.0f);
        if (!projection.valid()) return {};
        const LaneRef incoming = projection.lane;
        const auto& lane = graph.lane(incoming);
        if (graph.approach_control(incoming) != JunctionControl::Signal)
            return {};
        const auto line = graph.pose(incoming,
            police_signal_line_station(graph, incoming, traffic));
        const glm::vec2 tangent = glm::normalize(xz(line.tangent));
        const glm::vec2 right{-tangent.y, tangent.x};
        const glm::vec2 current_forward = glm::normalize(xz(sample.forward));
        if (glm::dot(previous_forward, tangent) < 0.65f ||
            glm::dot(current_forward, tangent) < 0.35f ||
            glm::dot(displacement, tangent) <= 0.0f ||
            glm::dot(xz(sample.velocity), tangent) < 0.15f)
            return {};
        const glm::vec2 front_before = xz(before.position) +
            previous_forward * std::max(0.0f, before.half_length_m);
        const glm::vec2 front_after = xz(sample.position) +
            current_forward * std::max(0.0f, sample.half_length_m);
        const float from = glm::dot(front_before - xz(line.position), tangent);
        const float to = glm::dot(front_after - xz(line.position), tangent);
        // Reversing a few centimetres over the line must not farm new reports.
        if (from < -6.0f) crossed_lanes_.erase(lane.key);
        if (from >= 0.0f || to < 0.0f || crossed_lanes_.count(lane.key) != 0)
            return {};
        const float crossing_fraction = -from / (to - from);
        const glm::vec2 crossing = glm::mix(front_before, front_after,
                                           crossing_fraction);
        const float lane_width = lane.approach_width_m /
            static_cast<float>(std::max(1, static_cast<int>(lane.lanes_at_end) *
                (lane.one_way ? 1 : 2)));
        if (std::fabs(glm::dot(crossing - xz(line.position), right)) >
                lane_width * 0.5f + 0.15f ||
            std::fabs(projection.lateral_m) > lane_width * 0.5f + 0.35f ||
            std::fabs(before.position.y - line.position.y) > 3.0f)
            return {};
        // Latch ALL crossings, including unseen/green/yellow ones. A witness
        // arriving later cannot retroactively turn that passage into a crime.
        crossed_lanes_.insert(lane.key);
        const int64_t crossing_step = before.step + static_cast<int64_t>(
            std::floor(crossing_fraction * static_cast<float>(elapsed)));
        if (traffic_signal_phase(graph, lane.junction_to, incoming,
                crossing_step, traffic) != TrafficSignalPhase::Red ||
            !police_offense_has_witness(sample.position, witnesses, police))
            return {};
        return {true, WantedSystem::Crime::TrafficViolation,
                red_light_heat, incoming, {}};
    }

    // Feed EVERY police contact, including contacts below the speed threshold.
    // The first contact owns attribution for its whole episode; a cop's impact
    // cannot become a player crime as the solver rebounds the two bodies.
    PoliceOffenseReport observe_police_contact(const PoliceCollisionSample& sample) {
        auto found = contacts_.find(sample.cruiser);
        const bool new_episode = found == contacts_.end() ||
            sample.step < found->second.last_contact_step ||
            sample.step - found->second.last_contact_step > contact_release_steps;
        if (found == contacts_.end())
            found = contacts_.emplace(sample.cruiser, Contact{}).first;
        Contact& contact = found->second;
        contact.last_contact_step = sample.step;
        if (!new_episode || (contact.reported && sample.step >= contact.last_report_step &&
                sample.step - contact.last_report_step < collision_cooldown_steps))
            return {};
        const float n2 = glm::dot(sample.normal, sample.normal);
        if (!finite(sample.player_velocity) || !finite(sample.police_velocity) ||
            !finite(sample.normal) || n2 < 1e-8f) return {};
        const glm::vec3 normal = sample.normal / std::sqrt(n2);
        const float player_into = -glm::dot(sample.player_velocity, normal);
        const float police_into = glm::dot(sample.police_velocity, normal);
        if (player_into + police_into < collision_min_closing_mps ||
            player_into <= police_into) return {};
        contact.reported = true;
        contact.last_report_step = sample.step;
        return {true, WantedSystem::Crime::PoliceVehicleCollision,
                cruiser_hit_heat, kInvalidLane, sample.cruiser};
    }

private:
    struct Contact {
        int64_t last_contact_step = 0;
        int64_t last_report_step = 0;
        bool reported = false;
    };
    static glm::vec2 xz(glm::vec3 v) { return {v.x, v.z}; }
    static bool finite(glm::vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    static bool valid_sample(const PoliceDrivingSample& sample) {
        return finite(sample.position) && finite(sample.forward) &&
            finite(sample.velocity) && std::isfinite(sample.half_length_m) &&
            glm::dot(xz(sample.forward), xz(sample.forward)) > 1e-8f;
    }
    PoliceDrivingSample previous_{};
    bool have_previous_ = false;
    std::set<uint64_t> crossed_lanes_;
    std::map<VisiblePoliceIdentity, Contact> contacts_;
};

}  // namespace apricot
