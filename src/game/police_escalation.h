#pragma once

#include <algorithm>
#include <cmath>

namespace apricot {

// The wanted meter says how serious the crime is. This small state machine says
// how officers should approach it. Keeping that distinction here prevents a
// one-star traffic offence from silently becoming a full tactical chase.
enum class PolicePosture { None, TrafficStop, Chase, Tactical };

struct PoliceEscalationInput {
    int wanted_level = 0;
    bool driving = false;
    bool armed = false;
    bool responder_active = false;
    bool officer_on_foot_near = false;
    float speed_mps = 0.0f;
    float dt = 0.0f;
};

struct PoliceEscalationOutput {
    PolicePosture posture = PolicePosture::None;
    // This can raise the live wanted level for an immediately dangerous act,
    // or for refusing a lawful one-star stop. It never lowers it.
    int minimum_wanted_level = 0;
    bool pull_over_prompt = false;
    bool fled_stop = false;
    bool stop_resolved = false;
};

class PoliceEscalationTracker {
public:
    static constexpr float kFleeSpeedMps = 4.5f;
    static constexpr float kStoppedSpeedMps = 1.0f;
    static constexpr float kFleeSeconds = 4.0f;
    static constexpr float kComplianceSeconds = 2.0f;

    void reset() {
        flee_seconds_ = 0.0f;
        compliance_seconds_ = 0.0f;
        civilian_kills_ = 0;
    }

    void record_civilian_kill() { ++civilian_kills_; }
    unsigned civilian_kills() const { return civilian_kills_; }

    PoliceEscalationOutput step(const PoliceEscalationInput& input) {
        PoliceEscalationOutput out;
        if (input.wanted_level <= 0) {
            reset();
            return out;
        }

        const float dt = std::isfinite(input.dt)
            ? std::clamp(input.dt, 0.0f, 1.0f) : 0.0f;
        const bool dangerous = input.armed || civilian_kills_ >= 2;
        out.minimum_wanted_level = input.wanted_level;
        if (dangerous) {
            // A drawn gun and multiple civilian deaths are not a roadside
            // conversation. Three stars activates the tactical response,
            // including PIT authority for a fleeing vehicle.
            out.posture = PolicePosture::Tactical;
            out.minimum_wanted_level = std::max(3, input.wanted_level);
            flee_seconds_ = compliance_seconds_ = 0.0f;
            return out;
        }

        if (input.wanted_level >= 3) {
            out.posture = PolicePosture::Tactical;
            flee_seconds_ = compliance_seconds_ = 0.0f;
            return out;
        }
        if (input.wanted_level >= 2) {
            out.posture = PolicePosture::Chase;
            flee_seconds_ = compliance_seconds_ = 0.0f;
            return out;
        }

        // One star is a traffic stop only after a cruiser has actually joined
        // the incident. Before that, dispatch is still finding a unit.
        out.posture = PolicePosture::TrafficStop;
        out.pull_over_prompt = input.driving && input.responder_active;
        if (!out.pull_over_prompt) {
            flee_seconds_ = compliance_seconds_ = 0.0f;
            return out;
        }

        if (input.speed_mps > kFleeSpeedMps) {
            flee_seconds_ += dt;
            compliance_seconds_ = 0.0f;
            if (flee_seconds_ >= kFleeSeconds) {
                out.posture = PolicePosture::Chase;
                out.minimum_wanted_level = 2;
                out.pull_over_prompt = false;
                out.fled_stop = true;
            }
            return out;
        }

        flee_seconds_ = 0.0f;
        if (input.speed_mps <= kStoppedSpeedMps && input.officer_on_foot_near) {
            compliance_seconds_ += dt;
            if (compliance_seconds_ >= kComplianceSeconds) {
                out.stop_resolved = true;
                out.pull_over_prompt = false;
            }
        } else {
            compliance_seconds_ = 0.0f;
        }
        return out;
    }

private:
    float flee_seconds_ = 0.0f;
    float compliance_seconds_ = 0.0f;
    unsigned civilian_kills_ = 0;
};

}  // namespace apricot
