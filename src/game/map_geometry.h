#pragma once

#include <array>
#include <algorithm>
#include <glm/glm.hpp>

namespace apricot {

// Convex polygons only. A clipped triangle/quad needs at most eight vertices.
struct MapPolygon {
    std::array<glm::vec2, 8> points{};
    int count = 0;
};

struct MapSlice {
    MapPolygon land;
    std::array<glm::vec2, 2> crossings{};
    int crossing_count = 0;
};

// Interpolate the actual level crossing instead of snapping shorelines to a
// sample's square. Shared edges produce identical endpoints in both cells.
inline MapSlice slice_map_triangle(const std::array<glm::vec2, 3>& points,
                                  const std::array<float, 3>& heights,
                                  float level) {
    MapSlice result;
    for (int i = 0; i < 3; ++i) {
        const auto a = static_cast<std::size_t>(i);
        const auto b = static_cast<std::size_t>((i + 1) % 3);
        const bool in_a = heights[a] > level;
        const bool in_b = heights[b] > level;
        if (in_a) result.land.points[static_cast<std::size_t>(result.land.count++)] = points[a];
        if (in_a != in_b) {
            const float t = (level - heights[a]) / (heights[b] - heights[a]);
            const glm::vec2 p = glm::mix(points[a], points[b], t);
            result.land.points[static_cast<std::size_t>(result.land.count++)] = p;
            result.crossings[static_cast<std::size_t>(result.crossing_count++)] = p;
        }
    }
    return result;
}

inline MapPolygon clip_map_polygon(MapPolygon polygon, glm::vec2 lo,
                                   glm::vec2 hi) {
    for (int edge = 0; edge < 4 && polygon.count > 0; ++edge) {
        MapPolygon output;
        const int axis = edge / 2;
        const float bound = edge % 2 == 0 ? lo[axis] : hi[axis];
        const auto inside = [=](glm::vec2 p) {
            return edge % 2 == 0 ? p[axis] >= bound : p[axis] <= bound;
        };
        for (int i = 0; i < polygon.count; ++i) {
            const glm::vec2 a = polygon.points[static_cast<std::size_t>(i)];
            const glm::vec2 b = polygon.points[static_cast<std::size_t>((i + 1) % polygon.count)];
            const bool in_a = inside(a), in_b = inside(b);
            if (in_a) output.points[static_cast<std::size_t>(output.count++)] = a;
            if (in_a != in_b) {
                const float t = (bound - a[axis]) / (b[axis] - a[axis]);
                output.points[static_cast<std::size_t>(output.count++)] = glm::mix(a, b, t);
            }
        }
        polygon = output;
    }
    return polygon;
}

}  // namespace apricot
