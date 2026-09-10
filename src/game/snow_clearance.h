#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "game/snowpack.h"

namespace apricot {

struct SnowClearanceStrip {
    // Endpoints are the bare road surface, never the top of the snowpack.
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    float half_width = 0.0f;
    double residual_depth = 0.0;
};

// Deterministic bounded footprints of actual blade travel. The owner updates
// between simulation steps and publishes the same field to physics and render.
class SnowClearanceField {
public:
    static constexpr std::size_t kMaxStrips = 128;
    static constexpr float kHeightTolerance = 0.75f;
    static constexpr float kMaxSegmentLength = 24.0f;
    static constexpr float kMaxStripLength = 96.0f;
    static constexpr double kRefillRateScale = 0.25;
    static constexpr float kResidualDepth = 0.008f;

    const std::vector<SnowClearanceStrip>& strips() const { return strips_; }
    void clear() { strips_.clear(); }

    // The main pack is authoritative, including pinned depth and changing
    // weather. A cleared road refills slowly but never exceeds that live level.
    void advance(float snowfall, float heat, float dt, float main_depth) {
        if (!std::isfinite(dt) || dt <= 0.0f || !std::isfinite(main_depth)) return;
        SnowpackState main_pack;
        main_pack.set_depth_m(main_depth);
        const double target = main_pack.depth_m();
        const SnowpackTuning tuning;
        const double snow = snowpack_detail::unit(snowfall);
        const double warmth = snowpack_detail::unit(heat);
        const double weather_rate = snow * tuning.accumulation_rate_m_per_s -
            (1.0 - snow) * tuning.passive_melt_rate_m_per_s -
            warmth * tuning.heat_melt_rate_m_per_s;
        // Slow accumulation only; thaw still removes snow at the normal rate.
        const double rate = weather_rate > 0.0
            ? weather_rate * kRefillRateScale : weather_rate;
        for (auto& strip : strips_) {
            strip.residual_depth = std::clamp(
                strip.residual_depth + rate * static_cast<double>(dt), 0.0, target);
        }
        // Once a strip catches today's main pack it is ordinary snow again.
        // Melting the main pack to zero also forgets history before a new storm.
        strips_.erase(std::remove_if(strips_.begin(), strips_.end(),
            [target](const auto& strip) { return strip.residual_depth >= target; }),
            strips_.end());
    }

    bool clear_segment(glm::vec3 a, glm::vec3 b, float half_width,
                       float global_depth) {
        if (!finite(a) || !finite(b) || !std::isfinite(half_width) ||
            half_width <= 0.0f || half_width > 8.0f ||
            !std::isfinite(global_depth) || global_depth <= kResidualDepth) return false;
        const glm::vec3 delta = b - a;
        const float length = glm::length(delta);
        // A route wrap or teleport never paints a clear line across the city.
        if (length > kMaxSegmentLength) return false;
        SnowClearanceStrip next{a, b, half_width, kResidualDepth};
        // Interleaved fleet samples can extend their own strips. Merge only
        // effectively straight, fresh travel; turns retain their actual shape.
        for (auto it = strips_.rbegin(); it != strips_.rend(); ++it) {
            if (glm::length(it->b - a) > 0.08f ||
                std::fabs(it->half_width - half_width) > 0.001f ||
                it->residual_depth > kResidualDepth + 0.001 ||
                glm::length(b - it->a) > kMaxStripLength) continue;
            const glm::vec3 old = it->b - it->a;
            const float old_length = glm::length(old);
            if (length > 0.001f && old_length > 0.001f &&
                glm::dot(old / old_length, delta / length) < 0.9998f) continue;
            it->b = b;
            return true;
        }
        if (strips_.size() == kMaxStrips) strips_.erase(strips_.begin());
        strips_.push_back(next);
        return true;
    }

    float depth_at(float x, float base_y, float z, float global_depth) const {
        if (!std::isfinite(global_depth) || global_depth <= 0.0f) return 0.0f;
        float depth = global_depth;
        if (!finite({x, base_y, z})) return depth;
        for (const auto& strip : strips_) {
            const glm::vec2 ab{strip.b.x - strip.a.x, strip.b.z - strip.a.z};
            const glm::vec2 ap{x - strip.a.x, z - strip.a.z};
            const float length_sq = glm::dot(ab, ab);
            const float t = length_sq > 1e-8f ?
                glm::clamp(glm::dot(ap, ab) / length_sq, 0.0f, 1.0f) : 0.0f;
            const glm::vec2 offset = ap - ab * t;
            if (glm::dot(offset, offset) > strip.half_width * strip.half_width) continue;
            if (std::fabs(base_y - glm::mix(strip.a.y, strip.b.y, t)) >
                kHeightTolerance) continue;
            depth = std::min(depth, static_cast<float>(strip.residual_depth));
        }
        return depth;
    }

private:
    static bool finite(glm::vec3 p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    std::vector<SnowClearanceStrip> strips_;
};

}  // namespace apricot
