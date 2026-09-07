#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include "city/roads.h"

namespace apricot {

inline float road_centreline_distance(glm::vec2 point, const city::Road& road) {
    float best = std::numeric_limits<float>::max();
    for (int i = 0; i + 1 < road.count; ++i) {
        const glm::vec2 a{road.path[i].x, road.path[i].z};
        const glm::vec2 b{road.path[i + 1].x, road.path[i + 1].z};
        const glm::vec2 segment = b - a;
        const float length2 = glm::dot(segment, segment);
        const float t = length2 > 1e-6f
            ? glm::clamp(glm::dot(point - a, segment) / length2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, glm::length(point - (a + segment * t)));
    }
    return best;
}

inline float road_name_width(const city::Road& road) {
    return road.width_m > 0.0f ? road.width_m : city::road_width_m(road.cls);
}

class CurrentRoadName {
public:
    const char* update(glm::vec2 point) {
        const city::Road* best = nullptr;
        float best_distance = std::numeric_limits<float>::max();
        for (const auto& road : city::kRoads) {
            const float distance = road_centreline_distance(point, road);
            const float capture = road_name_width(road) * .5f + 1.5f;
            if (distance <= capture && distance < best_distance) {
                best = &road;
                best_distance = distance;
            }
        }
        if (current_) {
            const float current_distance = road_centreline_distance(point, *current_);
            const float release = road_name_width(*current_) * .5f + 3.0f;
            // Retain the current street around crossings until the new one is
            // clearly nearer. This prevents one-frame label swaps at junctions.
            if (current_distance <= release &&
                (!best || best == current_ || best_distance + 2.0f >= current_distance))
                return current_->name;
        }
        current_ = best;
        return current_ ? current_->name : nullptr;
    }

    void reset() { current_ = nullptr; }

private:
    const city::Road* current_ = nullptr;
};

} // namespace apricot
