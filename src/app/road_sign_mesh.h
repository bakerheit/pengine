#pragma once

#include <array>
#include <string_view>

#include "app/traffic_signal_mesh.h"
#include "gfx/glyph_atlas.h"

namespace apricot {
namespace road_sign_detail {
inline void face(MeshData& mesh, int sides, float radius, float height, float z,
                 float phase) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float angle = phase + 6.28318530718f * static_cast<float>(i) /
                                   static_cast<float>(sides);
        const glm::vec3 point{radius * std::cos(angle),
                              height + radius * std::sin(angle), z};
        mesh.vertices.push_back({point, {0, 0, 1}, {0, 0}, glm::vec4{0}});
        mesh.bounds.expand(point);
    }
    for (uint32_t i = 1; i + 1 < static_cast<uint32_t>(sides); ++i)
        mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
}

inline void lettering(MeshData& mesh, std::string_view text, float width,
                      float height, float centre_y, float z) {
    const float cell_x = width / static_cast<float>(text.size() * 6u - 1u);
    const float cell_y = height / 7.0f;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto& glyph = kFont5x7[glyph_cell(text[i])];
        for (int x = 0; x < 5; ++x) for (int y = 0; y < 7; ++y) {
            if (!(glyph[x] & (1u << y))) continue;
            const float px = -width * 0.5f +
                (static_cast<float>(i * 6u) + static_cast<float>(x)) * cell_x;
            const float py = centre_y + height * 0.5f - static_cast<float>(y + 1) * cell_y;
            signal_mesh_detail::quad(mesh, {px, py, z}, {px + cell_x, py, z},
                {px + cell_x, py + cell_y, z}, {px, py + cell_y, z});
        }
    }
}
} // namespace road_sign_detail

// Local +Z faces the driver. Three instanced parts: metal, white, red.
// Geometry carries the lettering so signs need no private asset or texture.
inline std::array<MeshData, 3> make_road_sign_mesh(bool yield, bool all_way) {
    std::array<MeshData, 3> out;
    auto& metal = out[0]; auto& white = out[1]; auto& red = out[2];
    signal_mesh_detail::append(metal, make_cylinder(0.04f, 1.30f, 8), {0, 1.30f, 0});
    const int sides = yield ? 3 : 8;
    const float radius = yield ? 0.80f : 0.65f;
    const float height = yield ? 2.65f : 2.50f;
    const float phase = glm::radians(yield ? -90.0f : 22.5f);
    road_sign_detail::face(metal, sides, radius, height, -0.05f, phase);
    // A back face with reversed winding keeps the bare metal visible to cars
    // leaving the junction, without mirrored STOP lettering on the reverse.
    for (std::size_t i = 0; i < metal.indices.size(); i += 3) {
        if (metal.vertices[metal.indices[i]].normal.z != 1.0f) continue;
        // The pole already has proper closed geometry; only reverse the plate.
        if (metal.vertices[metal.indices[i]].position.z != -0.05f) continue;
        std::swap(metal.indices[i + 1], metal.indices[i + 2]);
    }
    for (auto& vertex : metal.vertices)
        if (vertex.position.z == -0.05f && vertex.normal.z == 1.0f)
            vertex.normal = {0, 0, -1};
    road_sign_detail::face(yield ? red : white, sides, radius, height, 0.055f, phase);
    road_sign_detail::face(yield ? white : red, sides,
                          yield ? 0.62f : 0.585f, height, 0.06f, phase);
    road_sign_detail::lettering(yield ? red : white, yield ? "YIELD" : "STOP",
        yield ? 0.61f : 0.92f, yield ? 0.15f : 0.27f,
        height + (yield ? 0.10f : 0.0f), 0.067f);
    if (all_way) {
        signal_mesh_detail::box(white, {0, 1.65f, 0.025f}, {0.84f, 0.24f, 0.06f});
        road_sign_detail::lettering(metal, "ALL WAY", 0.72f, 0.13f, 1.65f, 0.06f);
    }
    return out;
}

// A row of give-way teeth, pointing toward approaching traffic (local +Z).
// The renderer scales X to the inbound carriageway, and aligns it to the road.
inline MeshData make_yield_marking() {
    MeshData out;
    for (int i = 0; i < 5; ++i) {
        const float x = -0.5f + static_cast<float>(i) * 0.2f;
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        for (glm::vec3 p : {glm::vec3{x, 0, -0.3f},
                            glm::vec3{x + 0.085f, 0, 0.3f},
                            glm::vec3{x + 0.17f, 0, -0.3f}}) {
            out.vertices.push_back({p, {0, 1, 0}, {0, 0}, glm::vec4{0}});
            out.bounds.expand(p);
        }
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
    }
    return out;
}
} // namespace apricot
