#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <glm/glm.hpp>
#include "game/ui_flow.h"

namespace apricot {

// Presentation only: no routing, road snapping, or changes to replay state.
struct MinimapView {
    glm::vec2 origin{};
    glm::vec2 forward{0, -1};
    glm::vec2 centre{};
    float radius = 142.0f;
    float range_m = 160.0f;

    static MinimapView make(glm::vec3 position, glm::vec3 heading,
                            float speed_mph, glm::vec2 canvas) {
        MinimapView view;
        view.origin = {position.x, position.z};
        const glm::vec2 flat{heading.x, heading.z};
        if (glm::dot(flat, flat) > 1e-6f) view.forward = glm::normalize(flat);
        view.centre = {view.radius + 32, canvas.y - view.radius - 78};
        // Keep nearby POIs legible while stopped or creeping, then widen the
        // radar back to the old 440 m view as speed reaches 100 MPH.
        view.range_m = 160.0f + glm::clamp(speed_mph, 0.0f, 100.0f) * 2.8f;
        return view;
    }

    glm::vec2 direction(glm::vec2 delta) const {
        const glm::vec2 right{-forward.y, forward.x};
        return {glm::dot(delta, right), -glm::dot(delta, forward)};
    }
    glm::vec2 project(glm::vec2 world) const {
        return centre + direction(world - origin) * (radius / range_m);
    }
    glm::vec2 blip(glm::vec2 world) const {
        glm::vec2 offset = project(world) - centre;
        const float length = glm::length(offset);
        const float limit = radius - 16.0f;
        if (length > limit) offset *= limit / length;
        return centre + offset;
    }
};

struct MapWaypoint {
    std::optional<glm::vec2> position;

    bool toggle(glm::vec2 pointer, const MapViewport& viewport) {
        if (pointer.x < viewport.left || pointer.y < viewport.top ||
            pointer.x > viewport.left + viewport.width ||
            pointer.y > viewport.top + viewport.height) return false;
        const float scale = map_pixels_per_metre(viewport);
        const glm::vec2 world = glm::vec2{viewport.world_center_x, viewport.world_center_z} +
            (pointer - glm::vec2{viewport.left + viewport.width * .5f,
                                  viewport.top + viewport.height * .5f}) / scale;
        if (std::abs(world.x) > city::kWorldHalfMetres ||
            std::abs(world.y) > city::kWorldHalfMetres) return false;
        if (position && glm::length(*position - world) * scale <= 18.0f)
            position.reset();
        else position = world;
        return true;
    }
};

// Clip convex map geometry against a 48-sided radar disc. Fixed storage keeps
// the per-frame path allocation-free; a quad can gain at most 48 vertices.
struct RadarPolygon {
    std::array<glm::vec2, 56> points{};
    int count = 0;
};

inline RadarPolygon clip_radar_polygon(RadarPolygon polygon,
                                       glm::vec2 centre, float radius) {
    static const auto normals = [] {
        std::array<glm::vec2, 48> result{};
        for (int i = 0; i < 48; ++i) {
            const float angle = (static_cast<float>(i) + .5f) * 6.28318530718f / 48;
            result[static_cast<std::size_t>(i)] = {std::cos(angle), std::sin(angle)};
        }
        return result;
    }();
    const float bound = radius * std::cos(3.14159265359f / 48);
    glm::vec2 lo{1e9f}, hi{-1e9f};
    bool all_inside = true;
    for (int i = 0; i < polygon.count; ++i) {
        const auto p = polygon.points[static_cast<std::size_t>(i)] - centre;
        lo = glm::min(lo, p); hi = glm::max(hi, p);
        all_inside &= glm::dot(p, p) <= bound * bound;
    }
    if (all_inside) return polygon;
    if (lo.x > radius || lo.y > radius || hi.x < -radius || hi.y < -radius) return {};
    for (const auto normal : normals) {
        if (polygon.count == 0) break;
        RadarPolygon output;
        for (int i = 0; i < polygon.count; ++i) {
            const auto a = polygon.points[static_cast<std::size_t>(i)];
            const auto b = polygon.points[static_cast<std::size_t>((i + 1) % polygon.count)];
            const float da = glm::dot(a - centre, normal) - bound;
            const float db = glm::dot(b - centre, normal) - bound;
            if (da <= 0) output.points[static_cast<std::size_t>(output.count++)] = a;
            if ((da <= 0) != (db <= 0))
                output.points[static_cast<std::size_t>(output.count++)] = glm::mix(a, b, da / (da - db));
        }
        polygon = output;
    }
    return polygon;
}

} // namespace apricot
