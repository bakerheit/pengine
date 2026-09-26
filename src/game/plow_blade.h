#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "game/snow_clearance.h"

namespace apricot {

// A truck plow's cutting edge in the truck's own chassis frame: x right, y up,
// -z forward, origin at the chassis centre of mass. The numbers come from the
// attachment mesh fitted to the real cooked body (app/plow_kit_mesh.h), and
// plow_kit_tests holds them to that fit, so the strip a blade clears is the
// blade the player sees.
struct PlowBladeMount {
    // Cutting-edge centre ahead of the chassis origin, metres (positive).
    float edge_forward_m = 0.0f;
    // Cutting-edge centre below the chassis origin at rest, metres (positive).
    float edge_drop_m = 0.0f;
    // Half the blade's width across the truck, metres.
    float half_width_m = 0.0f;
    // How far the edge rises when the blade is fully raised, metres.
    float lift_m = 0.0f;

    glm::vec3 edge_local(float raised) const {
        const float r = std::clamp(raised, 0.0f, 1.0f);
        return {0.0f, -edge_drop_m + lift_m * r, -edge_forward_m};
    }
};

// What a lot crew's planner and driver need to know about a plow truck, in
// chassis metres.
struct LotPlowTruckSpec {
    float half_width_m = 1.0f;
    float rear_m = 2.6f;         // chassis origin back to the tailgate
    float wheelbase_m = 2.9f;
    float min_turn_radius_m = 6.0f;
    float ride_height_m = 0.7f;  // chassis origin above the road at rest
    PlowBladeMount blade;
};

// Blade raise and lower, stepped on the sim cadence. A hydraulic straight
// blade takes most of a second to travel, and it only scrapes the road once
// it is fully down.
struct PlowBladeState {
    bool lowered = true;
    float raised = 0.0f;  // 0 down on the road, 1 fully up

    void step(float dt) {
        if (!(dt > 0.0f) || !std::isfinite(dt)) return;
        constexpr float kTravelSeconds = 0.85f;
        const float delta = dt / kTravelSeconds;
        raised = std::clamp(raised + (lowered ? -delta : delta), 0.0f, 1.0f);
    }
    bool scraping() const { return lowered && raised <= 0.02f; }
};

// Turns a moving blade into clearance strips.
//
// The field holds 128 strips for the whole city, and it merges a new segment
// into the previous strip only when the two are straight to about a degree.
// Submitting a segment every step is fine on a straight road; in a turn it
// is a new strip every 30 cm, so a single U-turn in a lot, or a traffic plow
// taking a junction, costs dozens of strips and evicts somebody's cleared road.
//
// So a sweep lays one LIVE strip and extends it every step for as long as the
// path it has actually scraped stays within kSagittaM of the strip's centre
// line; the moment it would not, the strip is left where it ends and a new one
// starts there. A straight pass is one strip. A turn on a 6 m radius is a
// chord every metre and a half, whose sag inside a 1.2 m half-width nobody
// can see. And because the live strip always ends at the blade, the cleared
// ground never lags the truck.
//
// A raised blade, thin snow or a teleport-sized jump ends the live strip, so
// nothing is drawn across ground the blade did not touch.
class PlowSweep {
public:
    static constexpr float kSagittaM = 0.04f;
    static constexpr float kMaxChordM = 48.0f;
    static constexpr float kMaxJumpM = 0.75f;   // one step at far past road speed
    static constexpr float kSampleM = 0.25f;    // path memory spacing

    void reset() {
        anchored_ = false;
        live_ = 0u;
        path_.clear();
        cleared_distance_m_ = 0.0f;
    }
    float cleared_distance_m() const { return cleared_distance_m_; }
    bool anchored() const { return anchored_; }

    // edge: world position of the cutting-edge centre on the road surface.
    // speed_mps: the truck's speed, either sign (unused beyond motion).
    void step(glm::vec3 edge, float speed_mps, bool scraping,
              float half_width_m, SnowClearanceField& field, float depth_m) {
        (void)speed_mps;
        const bool finite_edge = std::isfinite(edge.x) && std::isfinite(edge.y) &&
                                 std::isfinite(edge.z);
        if (!scraping || !finite_edge || depth_m <= SnowClearanceField::kResidualDepth) {
            anchored_ = false;
            live_ = 0u;
            path_.clear();
            return;
        }
        if (!anchored_ || glm::length(edge - head_) > kMaxJumpM) {
            // First contact, or a respawn, teleport or shove: start here,
            // clear nothing between.
            anchor_ = head_ = edge;
            path_.assign(1, edge);
            live_ = 0u;
            anchored_ = true;
            return;
        }
        const float moved = glm::length(glm::vec2{edge.x - head_.x, edge.z - head_.z});
        if (moved < 1e-5f) return;
        if (!fits(edge)) {
            // Leave the live strip ending at the last edge and start anew.
            anchor_ = head_;
            path_.assign(1, head_);
            live_ = 0u;
        }
        if (live_ == 0u || !field.extend_strip(live_, edge)) {
            live_ = field.clear_segment_id(anchor_, edge, half_width_m, depth_m, false);
            if (live_ == 0u) {
                anchor_ = head_ = edge;
                path_.assign(1, edge);
                return;
            }
        }
        cleared_distance_m_ += moved;
        head_ = edge;
        if (glm::length(edge - path_.back()) >= kSampleM) path_.push_back(edge);
    }

private:
    // Would a strip from the anchor to this edge still cover the path the
    // blade actually took since the anchor?
    bool fits(glm::vec3 edge) const {
        const glm::vec2 a{anchor_.x, anchor_.z};
        const glm::vec2 chord = glm::vec2{edge.x, edge.z} - a;
        const float length = glm::length(chord);
        if (length > kMaxChordM) return false;
        if (length < 1e-4f) return true;
        const glm::vec2 along = chord / length;
        for (const auto& p : path_) {
            const glm::vec2 d = glm::vec2{p.x, p.z} - a;
            const float t = glm::dot(d, along);
            if (t < -kSagittaM) return false;  // the path doubled back
            if (std::fabs(along.x * d.y - along.y * d.x) > kSagittaM) return false;
        }
        const glm::vec2 h = glm::vec2{head_.x, head_.z} - a;
        return std::fabs(along.x * h.y - along.y * h.x) <= kSagittaM;
    }

    glm::vec3 anchor_{0.0f};
    glm::vec3 head_{0.0f};
    std::vector<glm::vec3> path_;
    uint32_t live_ = 0u;
    bool anchored_ = false;
    float cleared_distance_m_ = 0.0f;
};

}  // namespace apricot
