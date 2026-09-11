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
    enum class Kind : uint8_t {
        None, RedLight, StopSign, Speeding, PoliceCollision, ArmedThreat
    } kind = Kind::None;
    float observed_speed_mps = 0.0f;
    float speed_limit_mps = 0.0f;
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
    static constexpr float stop_sign_heat = 1.0f;
    static constexpr float speeding_heat = 1.0f;
    static constexpr float armed_threat_heat = 3.0f;
    static constexpr float speeding_tolerance_mps = 2.7f; // about 6 mph
    static constexpr int64_t speeding_hold_steps = 180;   // 1.5 s
    static constexpr int64_t stop_dwell_steps = 60;       // 0.5 s
    static constexpr float cruiser_hit_heat = 2.0f;
    // The closing-speed gate and the who-was-the-mover rule are
    // police_ram_verdict() in city/police_ai.h — one rule, pinned once. A
    // deliberate parking-speed bump counts; resting contact and solver
    // vibration do not. More serious rams use the same attribution.
    static constexpr int64_t contact_release_steps = 120;
    static constexpr int64_t collision_cooldown_steps = 600;

    void reset() {
        have_previous_ = false;
        crossed_lanes_.clear();
        stop_approaches_.clear();
        speed_lane_key_ = 0;
        speeding_steps_ = 0;
        speeding_reported_ = false;
        armed_reported_ = false;
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
            reset_driving_state();
            return {};
        }
        const int64_t elapsed = sample.step - before.step;
        if (elapsed <= 0 || elapsed > kMaxStepsPerFrame) {
            reset_driving_state();
            return {};
        }
        const float dt = static_cast<float>(static_cast<double>(elapsed) * kSimDt);
        const glm::vec2 displacement = xz(sample.position - before.position);
        const float speed = std::max(glm::length(xz(before.velocity)),
                                     glm::length(xz(sample.velocity)));
        const float plausible = speed * dt + 0.35f;
        if (glm::dot(displacement, displacement) > plausible * plausible) {
            reset_driving_state();
            return {};
        }
        PoliceOffenseReport speeding_report;
        const glm::vec2 current_forward = glm::normalize(xz(sample.forward));
        const auto current_projection = graph.nearest_lane_along(
            xz(sample.position), current_forward, 6.0f);
        if (current_projection.valid()) {
            const Lane& current_lane = graph.lane(current_projection.lane);
            const float current_speed = glm::length(xz(sample.velocity));
            if (speed_lane_key_ != current_lane.key) {
                speed_lane_key_ = current_lane.key;
                speeding_steps_ = 0;
                speeding_reported_ = false;
            }
            if (current_speed > current_lane.speed_limit_mps + speeding_tolerance_mps) {
                speeding_steps_ = std::min<int64_t>(
                    speeding_hold_steps, speeding_steps_ + elapsed);
                if (!speeding_reported_ && speeding_steps_ >= speeding_hold_steps &&
                    police_offense_has_witness(sample.position, witnesses, police)) {
                    speeding_reported_ = true;
                    speeding_report = {true, WantedSystem::Crime::TrafficViolation,
                        speeding_heat, current_projection.lane, {},
                        PoliceOffenseReport::Kind::Speeding, current_speed,
                        current_lane.speed_limit_mps};
                }
            } else if (current_speed <= current_lane.speed_limit_mps + 1.0f) {
                speeding_steps_ = 0;
                speeding_reported_ = false;
            }
        } else {
            speed_lane_key_ = 0;
            speeding_steps_ = 0;
            speeding_reported_ = false;
        }
        const glm::vec2 previous_forward = glm::normalize(xz(before.forward));
        const auto projection = graph.nearest_lane_along(
            xz(before.position), previous_forward, 6.0f);
        if (!projection.valid()) return speeding_report;
        const LaneRef incoming = projection.lane;
        const auto& lane = graph.lane(incoming);
        const JunctionControl control = graph.approach_control(incoming);
        if (control != JunctionControl::Signal && control != JunctionControl::Stop)
            return speeding_report;
        const auto line = graph.pose(incoming,
            police_signal_line_station(graph, incoming, traffic));
        const glm::vec2 tangent = glm::normalize(xz(line.tangent));
        const glm::vec2 right{-tangent.y, tangent.x};
        const glm::vec2 front_before = xz(before.position) +
            previous_forward * std::max(0.0f, before.half_length_m);
        const glm::vec2 front_after = xz(sample.position) +
            current_forward * std::max(0.0f, sample.half_length_m);
        const float from = glm::dot(front_before - xz(line.position), tangent);
        const float to = glm::dot(front_after - xz(line.position), tangent);
        // Reversing a few centimetres over the line must not farm new reports.
        if (from < -6.0f) crossed_lanes_.erase(lane.key);
        if (control == JunctionControl::Stop) {
            StopApproach& stop = stop_approaches_[lane.key];
            const float current_speed = glm::length(xz(sample.velocity));
            if (from >= -15.0f && from <= 0.75f && current_speed <= 0.35f)
                stop.stopped_steps = std::min<int64_t>(
                    stop_dwell_steps, stop.stopped_steps + elapsed);
            else if (from < -15.0f)
                stop.stopped_steps = 0;
        }
        if (glm::dot(previous_forward, tangent) < 0.65f ||
            glm::dot(current_forward, tangent) < 0.35f ||
            glm::dot(displacement, tangent) <= 0.0f ||
            glm::dot(xz(sample.velocity), tangent) < 0.15f)
            return speeding_report;
        if (from >= 0.0f || to < 0.0f || crossed_lanes_.count(lane.key) != 0)
            return speeding_report;
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
            return speeding_report;
        // Latch ALL crossings, including unseen/green/yellow ones. A witness
        // arriving later cannot retroactively turn that passage into a crime.
        crossed_lanes_.insert(lane.key);
        const int64_t crossing_step = before.step + static_cast<int64_t>(
            std::floor(crossing_fraction * static_cast<float>(elapsed)));
        bool violation = false;
        PoliceOffenseReport::Kind kind = PoliceOffenseReport::Kind::None;
        float heat = 0.0f;
        if (control == JunctionControl::Signal &&
            traffic_signal_phase(graph, lane.junction_to, incoming,
                crossing_step, traffic) == TrafficSignalPhase::Red) {
            violation = true;
            kind = PoliceOffenseReport::Kind::RedLight;
            heat = red_light_heat;
        } else if (control == JunctionControl::Stop) {
            const auto stop = stop_approaches_.find(lane.key);
            violation = stop == stop_approaches_.end() ||
                stop->second.stopped_steps < stop_dwell_steps;
            kind = PoliceOffenseReport::Kind::StopSign;
            heat = stop_sign_heat;
            stop_approaches_.erase(lane.key);
        }
        if (!violation ||
            !police_offense_has_witness(sample.position, witnesses, police))
            return speeding_report;
        return {true, WantedSystem::Crime::TrafficViolation, heat, incoming, {},
                kind};
    }

    PoliceOffenseReport observe_armed(
        glm::vec3 offender, bool armed,
        const std::vector<PoliceOffenseWitness>& witnesses,
        const PoliceTuning& police) {
        if (!armed) {
            armed_reported_ = false;
            return {};
        }
        if (armed_reported_ ||
            !police_offense_has_witness(offender, witnesses, police)) return {};
        armed_reported_ = true;
        return {true, WantedSystem::Crime::ArmedThreat, armed_threat_heat,
                kInvalidLane, {}, PoliceOffenseReport::Kind::ArmedThreat};
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
        if (!police_ram_verdict(sample.player_velocity, sample.police_velocity,
                                normal).player_rammed) return {};
        contact.reported = true;
        contact.last_report_step = sample.step;
        return {true, WantedSystem::Crime::PoliceVehicleCollision,
                cruiser_hit_heat, kInvalidLane, sample.cruiser,
                PoliceOffenseReport::Kind::PoliceCollision};
    }

private:
    struct Contact {
        int64_t last_contact_step = 0;
        int64_t last_report_step = 0;
        bool reported = false;
    };
    struct StopApproach { int64_t stopped_steps = 0; };
    void reset_driving_state() {
        crossed_lanes_.clear();
        stop_approaches_.clear();
        speed_lane_key_ = 0;
        speeding_steps_ = 0;
        speeding_reported_ = false;
    }
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
    std::map<uint64_t, StopApproach> stop_approaches_;
    uint64_t speed_lane_key_ = 0;
    int64_t speeding_steps_ = 0;
    bool speeding_reported_ = false;
    bool armed_reported_ = false;
    std::map<VisiblePoliceIdentity, Contact> contacts_;
};

}  // namespace apricot
