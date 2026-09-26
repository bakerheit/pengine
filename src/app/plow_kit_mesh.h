#pragma once

// THE PLOW KIT: a commercial pickup plow and a roof service light bar, built
// as geometry and FITTED to a real cooked truck body. Header-only and GL-free
// so plow_kit_tests can build the exact meshes the renderer uploads, against
// the exact .emesh the truck draws, and check that nothing floats or clips.
//
// The kit is authored in METRES about a fit anchor (the bumper face, on the
// ground, on the centreline), then mapped into the body's source space by the
// inverse of the body fit scale. That way an 8 ft blade is 2.44 m on the road
// whatever the catalog does to the truck's proportions, and the whole kit
// still rides the body transform, so it pitches and rolls with the cab.
//
// What is equipment and what is truck: the push beams, clevis ears, headgear,
// plow lights, lift ram and the roof bar are bolted to the truck and never
// move. The A-frame, trip rail, moldboard, springs and markers pivot about the
// clevis pins when the blade lifts.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/emesh_reader.h"
#include "core/rng.h"
#include "core/transform.h"
#include "game/plow_blade.h"
#include "gfx/primitives.h"

namespace apricot {

enum class PlowPart : std::size_t {
    // Ride the lift.
    BladePaint,   // moldboard, ribs, end plates, back channels
    BladeFrame,   // A-frame, trip rail, hinge plates, angle-ram barrels
    BladeSteel,   // cutting edge, bolts, springs, chrome ram rods, pins
    BladeRubber,  // top deflector flap
    BladeMarker,  // blade-end guide rods
    // Bolted to the truck.
    MountFrame,   // push beams, clevis, headgear, lamp housings, bar body, feet
    MountSteel,   // lift-ram rod, chain, lamp bezels, pins
    LampLens,     // the two plow headlamps
    BarLens0,     // amber light-bar modules, four flash groups left to right
    BarLens1,
    BarLens2,
    BarLens3,
    kCount,
};
inline constexpr std::size_t kPlowPartCount = static_cast<std::size_t>(PlowPart::kCount);
inline constexpr std::size_t kPlowMovingPartCount = 5;
inline constexpr std::size_t kPlowBarLensGroups = 4;
inline constexpr bool plow_part_moves(std::size_t part) {
    return part < kPlowMovingPartCount;
}
inline constexpr std::size_t plow_part(PlowPart part) {
    return static_cast<std::size_t>(part);
}

// Per-truck equipment choices, in world metres.
struct PlowKitSpec {
    float blade_width_m = 2.30f;   // 7'6" compact blade
    float blade_height_m = 0.66f;
    float bar_length_m = 1.12f;
    glm::vec3 blade_paint{0.70f, 0.055f, 0.04f};
};

// Measured off the real cooked body, in its SOURCE space (+Z nose, Y up).
struct PlowKitFit {
    bool valid = false;
    float ground_y = 0.0f;
    float body_half_x = 0.0f;
    float bumper_front_z = 0.0f;
    float bumper_bottom_y = 0.0f;
    float bumper_top_y = 0.0f;
    float hood_front_y = 0.0f;
    float roof_y = 0.0f;
    float roof_front_z = 0.0f;
    float roof_rear_z = 0.0f;
    float roof_half_x = 0.0f;
};

struct PlowKitMeshes {
    std::array<MeshData, kPlowPartCount> parts;
    PlowKitFit fit;
    // Source space. The lift pivots the moving parts about the clevis-pin
    // axis, which runs along X through this point.
    glm::vec3 pivot{0.0f};
    // Source space, blade down: the cutting-edge centre on the road.
    glm::vec3 edge_centre{0.0f};
    float blade_half_width = 0.0f;  // source units
    float lift_radians = 0.0f;      // negative: rotates the edge upward
    glm::vec3 source_per_metre{1.0f};
    // Source space: the lamp lens centres, for glow and tests.
    std::array<glm::vec3, 2> lamp_centres{};
    glm::vec3 bar_centre{0.0f};
    std::array<glm::vec2, 2> bar_feet_xz{};
    float bar_bottom_y = 0.0f;
};

namespace plow_kit_detail {

struct UvRect {
    glm::vec2 lo;
    glm::vec2 hi;
};
// The kit atlas is 2 x 2: powder coat, zinc/steel, LED lens, lamp reflector.
inline constexpr UvRect kPaintUv{{0.0f, 0.0f}, {0.5f, 0.5f}};
inline constexpr UvRect kMetalUv{{0.5f, 0.0f}, {1.0f, 0.5f}};
inline constexpr UvRect kLedUv{{0.0f, 0.5f}, {0.5f, 1.0f}};
inline constexpr UvRect kReflectorUv{{0.5f, 0.5f}, {1.0f, 1.0f}};

inline MeshData& part(PlowKitMeshes& kit, PlowPart p) { return kit.parts[plow_part(p)]; }

inline void add(MeshData& out, const MeshData& source, const glm::mat3& basis,
                glm::vec3 offset, UvRect uv) {
    const auto base = static_cast<uint32_t>(out.vertices.size());
    const glm::mat3 normal_matrix = glm::transpose(glm::inverse(basis));
    for (auto vertex : source.vertices) {
        vertex.position = offset + basis * vertex.position;
        vertex.normal = glm::normalize(normal_matrix * vertex.normal);
        vertex.uv = glm::mix(uv.lo, uv.hi, glm::clamp(vertex.uv, 0.0f, 1.0f));
        vertex.material_weights = glm::vec4{0.0f};
        out.bounds.expand(vertex.position);
        out.vertices.push_back(vertex);
    }
    for (const auto index : source.indices) out.indices.push_back(base + index);
}

// Columns x, y, z; y is the segment direction.
inline glm::mat3 frame_along_y(glm::vec3 direction, glm::vec3 side_hint) {
    const glm::vec3 y = glm::normalize(direction);
    glm::vec3 x = side_hint - y * glm::dot(side_hint, y);
    if (glm::length(x) < 1e-4f) {
        x = std::fabs(y.x) < 0.9f ? glm::vec3{1, 0, 0} : glm::vec3{0, 0, 1};
        x -= y * glm::dot(x, y);
    }
    x = glm::normalize(x);
    const glm::vec3 z = glm::cross(x, y);
    return glm::mat3{x, y, z};
}

// Columns x, y, z; z is the segment direction.
inline glm::mat3 frame_along_z(glm::vec3 direction, glm::vec3 up_hint) {
    const glm::vec3 z = glm::normalize(direction);
    glm::vec3 y = up_hint - z * glm::dot(up_hint, z);
    if (glm::length(y) < 1e-4f) {
        y = std::fabs(z.y) < 0.9f ? glm::vec3{0, 1, 0} : glm::vec3{1, 0, 0};
        y -= z * glm::dot(y, z);
    }
    y = glm::normalize(y);
    const glm::vec3 x = glm::cross(y, z);
    return glm::mat3{x, y, z};
}

// A bevelled box. Every hard edge on real equipment is rolled a few
// millimetres, and that highlight is what separates steel from a grey cube.
inline void rbox(MeshData& out, glm::vec3 centre, glm::vec3 half, float bevel,
                 UvRect uv, const glm::mat3& basis = glm::mat3{1.0f}) {
    add(out, make_rounded_box(half, bevel, 3), basis, centre, uv);
}

// Rectangular tube from a to b. Section is (width across, height).
inline void beam(MeshData& out, glm::vec3 a, glm::vec3 b, glm::vec2 section,
                 float bevel, UvRect uv, glm::vec3 up = {0, 1, 0}) {
    const glm::vec3 d = b - a;
    const float length = glm::length(d);
    if (length < 1e-4f) return;
    rbox(out, (a + b) * 0.5f, {section.x * 0.5f, section.y * 0.5f, length * 0.5f},
         bevel, uv, frame_along_z(d, up));
}

inline void tube(MeshData& out, glm::vec3 a, glm::vec3 b, float radius,
                 int segments, UvRect uv) {
    const glm::vec3 d = b - a;
    const float length = glm::length(d);
    if (length < 1e-4f) return;
    add(out, make_cylinder(radius, length * 0.5f, segments),
        frame_along_y(d, {1, 0, 0}), (a + b) * 0.5f, uv);
}

// Emit a quad that faces the way its vertex normals do, whatever order the
// caller walked it in.
inline void quad(MeshData& m, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const auto& va = m.vertices[a];
    const glm::vec3 n = va.normal + m.vertices[b].normal + m.vertices[c].normal +
                        m.vertices[d].normal;
    const glm::vec3 g =
        glm::cross(m.vertices[b].position - va.position,
                   m.vertices[c].position - va.position) +
        glm::cross(m.vertices[c].position - va.position,
                   m.vertices[d].position - va.position);
    if (glm::dot(g, n) >= 0.0f) m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
    else m.indices.insert(m.indices.end(), {a, d, c, a, c, b});
}

inline uint32_t vertex(MeshData& m, glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
    const auto id = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({p, n, uv, glm::vec4{0.0f}});
    m.bounds.expand(p);
    return id;
}

// Surface of revolution about +Y. Profile points are (radius, y) walked
// bottom to top along the OUTSIDE; repeat a point to crease there.
inline MeshData lathe(const std::vector<glm::vec2>& profile, int segments) {
    MeshData m;
    const std::size_t n = profile.size();
    if (n < 2) return m;
    std::vector<glm::vec2> edge_normal(n - 1, glm::vec2{0.0f});
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const glm::vec2 d = profile[i + 1] - profile[i];
        const float length = glm::length(d);
        edge_normal[i] = length > 1e-6f ? glm::vec2{d.y, -d.x} / length : glm::vec2{0.0f};
    }
    std::vector<glm::vec2> normal(n);
    for (std::size_t i = 0; i < n; ++i) {
        glm::vec2 sum{0.0f};
        const bool dup_next = i + 1 < n && glm::length(profile[i + 1] - profile[i]) < 1e-6f;
        const bool dup_prev = i > 0 && glm::length(profile[i] - profile[i - 1]) < 1e-6f;
        if (i > 0 && !dup_next) sum += edge_normal[i - 1];
        if (i + 1 < n && !dup_prev) sum += edge_normal[i];
        if (glm::length(sum) < 1e-6f) {
            if (i > 0) sum += edge_normal[i - 1];
            if (i + 1 < n) sum += edge_normal[i];
        }
        normal[i] = glm::length(sum) > 1e-6f ? glm::normalize(sum) : glm::vec2{1, 0};
    }
    constexpr float kTau = 6.28318530717958647692f;
    const auto columns = static_cast<std::size_t>(segments) + 1u;
    for (std::size_t j = 0; j < columns; ++j) {
        const float t = static_cast<float>(j) / static_cast<float>(segments);
        const float c = std::cos(kTau * t), s = std::sin(kTau * t);
        for (std::size_t i = 0; i < n; ++i) {
            const glm::vec2 p = profile[i];
            vertex(m, {p.x * c, p.y, p.x * s}, {normal[i].x * c, normal[i].y, normal[i].x * s},
                   {t, static_cast<float>(i) / static_cast<float>(n - 1)});
        }
    }
    for (std::size_t j = 0; j + 1 < columns; ++j) {
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const auto a = static_cast<uint32_t>(j * n + i);
            const auto c = a + 1u;
            const auto b = static_cast<uint32_t>((j + 1) * n + i);
            const auto d = b + 1u;
            m.indices.insert(m.indices.end(), {a, c, d, a, d, b});
        }
    }
    return m;
}

inline void lathe_along(MeshData& out, const std::vector<glm::vec2>& profile,
                        glm::vec3 base, glm::vec3 axis, int segments, UvRect uv) {
    add(out, lathe(profile, segments), frame_along_y(axis, {1, 0, 0}), base, uv);
}

// A closed ribbon between two (z, y) polylines, extruded across X from x0 to
// x1. Faces between consecutive points share a normal when they bend less
// than ~40 degrees, so a curved moldboard shades as one sweep and its edges
// stay crisp.
inline void extrude_ribbon_x(MeshData& out, const std::vector<glm::vec2>& front,
                             const std::vector<glm::vec2>& back, float x0,
                             float x1, UvRect uv) {
    const std::size_t n = std::min(front.size(), back.size());
    if (n < 2) return;
    // The loop: front bottom->top, then back top->bottom.
    std::vector<glm::vec2> loop;
    for (std::size_t i = 0; i < n; ++i) loop.push_back(front[i]);
    for (std::size_t i = n; i-- > 0;) loop.push_back(back[i]);
    const std::size_t m = loop.size();
    float area = 0.0f;
    for (std::size_t i = 0; i < m; ++i) {
        const glm::vec2 a = loop[i], b = loop[(i + 1) % m];
        area += a.x * b.y - b.x * a.y;
    }
    const float orientation = area >= 0.0f ? 1.0f : -1.0f;
    std::vector<glm::vec2> edge_normal(m);
    for (std::size_t i = 0; i < m; ++i) {
        const glm::vec2 d = loop[(i + 1) % m] - loop[i];
        const float length = glm::length(d);
        edge_normal[i] = length > 1e-7f ? glm::vec2{d.y, -d.x} * (orientation / length)
                                        : glm::vec2{0.0f};
    }
    const float smooth_cos = 0.76f;
    float perimeter = 0.0f;
    for (std::size_t i = 0; i < m; ++i) perimeter += glm::length(loop[(i + 1) % m] - loop[i]);
    float along = 0.0f;
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t next = (i + 1) % m;
        const glm::vec2 en = edge_normal[i];
        if (glm::length(en) < 0.5f) continue;
        const auto end_normal = [&](std::size_t neighbour) {
            const glm::vec2 other = edge_normal[neighbour];
            return glm::dot(other, en) > smooth_cos ? glm::normalize(other + en) : en;
        };
        const glm::vec2 na = end_normal((i + m - 1) % m);
        const glm::vec2 nb = end_normal(next);
        const float step = glm::length(loop[next] - loop[i]);
        const float u0 = along / perimeter, u1 = (along + step) / perimeter;
        along += step;
        const glm::vec2 pa = loop[i], pb = loop[next];
        const uint32_t v0 = vertex(out, {x0, pa.y, pa.x}, {0, na.y, na.x}, glm::mix(uv.lo, uv.hi, glm::vec2{u0, 0}));
        const uint32_t v1 = vertex(out, {x0, pb.y, pb.x}, {0, nb.y, nb.x}, glm::mix(uv.lo, uv.hi, glm::vec2{u1, 0}));
        const uint32_t v2 = vertex(out, {x1, pb.y, pb.x}, {0, nb.y, nb.x}, glm::mix(uv.lo, uv.hi, glm::vec2{u1, 1}));
        const uint32_t v3 = vertex(out, {x1, pa.y, pa.x}, {0, na.y, na.x}, glm::mix(uv.lo, uv.hi, glm::vec2{u0, 1}));
        quad(out, v0, v1, v2, v3);
    }
    // End caps: the ribbon is a strip between the two polylines.
    for (const float x : {x0, x1}) {
        const glm::vec3 normal{x == x0 ? -1.0f : 1.0f, 0, 0};
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(n - 1);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(n - 1);
            const uint32_t a = vertex(out, {x, front[i].y, front[i].x}, normal, glm::mix(uv.lo, uv.hi, glm::vec2{0, t0}));
            const uint32_t b = vertex(out, {x, front[i + 1].y, front[i + 1].x}, normal, glm::mix(uv.lo, uv.hi, glm::vec2{0, t1}));
            const uint32_t c = vertex(out, {x, back[i + 1].y, back[i + 1].x}, normal, glm::mix(uv.lo, uv.hi, glm::vec2{0.1f, t1}));
            const uint32_t d = vertex(out, {x, back[i].y, back[i].x}, normal, glm::mix(uv.lo, uv.hi, glm::vec2{0.1f, t0}));
            quad(out, a, b, c, d);
        }
    }
}

// A coil spring: a lathe of alternating radii reads as coils at any distance
// the player will see one from, for a fraction of a true helix's triangles.
inline void spring(MeshData& out, glm::vec3 a, glm::vec3 b, float radius, UvRect uv) {
    const float length = glm::length(b - a);
    if (length < 0.02f) return;
    std::vector<glm::vec2> profile{{0.0f, 0.0f}, {radius * 0.55f, 0.0f}};
    const int coils = std::max(4, static_cast<int>(length / 0.022f));
    for (int i = 0; i <= coils * 2; ++i) {
        const float y = length * (0.06f + 0.88f * static_cast<float>(i) / static_cast<float>(coils * 2));
        profile.push_back({i % 2 == 0 ? radius : radius * 0.72f, y});
    }
    profile.push_back({radius * 0.55f, length});
    profile.push_back({0.0f, length});
    lathe_along(out, profile, a, b - a, 10, uv);
}

// Hydraulic ram: a painted barrel over the lower part, a chrome rod above.
inline void ram(MeshData& barrel_out, MeshData& rod_out, glm::vec3 a, glm::vec3 b,
                float radius, float barrel_share, UvRect paint, UvRect metal) {
    const glm::vec3 split = glm::mix(a, b, barrel_share);
    const float r = radius;
    const float barrel = glm::length(split - a);
    lathe_along(barrel_out,
                {{0, 0}, {r * 0.9f, 0}, {r, r * 0.3f}, {r, barrel - r * 0.3f},
                 {r * 0.9f, barrel}, {r * 0.45f, barrel}, {r * 0.45f, barrel}, {0, barrel}},
                a, split - a, 14, paint);
    tube(rod_out, split - glm::normalize(b - a) * 0.01f, b, r * 0.45f, 10, metal);
    // Clevis eyes at both ends.
    const glm::vec3 axis{1, 0, 0};
    tube(barrel_out, a - axis * (r * 0.9f), a + axis * (r * 0.9f), r * 0.7f, 10, paint);
    tube(rod_out, b - axis * (r * 0.9f), b + axis * (r * 0.9f), r * 0.6f, 10, metal);
}

// --- fitting -----------------------------------------------------------------

// Surface samples: every vertex plus a barycentric grid over large faces, so
// a low-poly shell whose bumper is one quad still reports where that quad is.
inline std::vector<glm::vec3> surface_samples(const StaticEmesh& body) {
    std::vector<glm::vec3> out;
    out.reserve(body.indices.size() + body.vertices.size());
    const auto at = [&](uint32_t i) {
        const auto& v = body.vertices[i];
        return glm::vec3{v.px, v.py, v.pz};
    };
    for (std::size_t t = 0; t + 2 < body.indices.size(); t += 3) {
        const glm::vec3 a = at(body.indices[t]), b = at(body.indices[t + 1]),
                        c = at(body.indices[t + 2]);
        // About 3 cm apart along both edges, so a long thin bumper face
        // is sampled across its length as densely as a small one.
        const int ku = std::clamp(static_cast<int>(std::ceil(glm::length(b - a) / 0.03f)), 1, 160);
        const int kv = std::clamp(static_cast<int>(std::ceil(glm::length(c - a) / 0.03f)), 1, 160);
        for (int i = 0; i <= ku; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(ku);
            for (int j = 0; j <= kv; ++j) {
                const float v = static_cast<float>(j) / static_cast<float>(kv);
                if (u + v > 1.0001f) break;
                out.push_back(a + (b - a) * u + (c - a) * v);
            }
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace plow_kit_detail

// Measure the truck from its surface samples. ground_y is where the tyres
// touch, in source units.
inline PlowKitFit fit_plow_kit(const StaticEmesh& body,
                               const std::vector<glm::vec3>& samples, float ground_y) {
    PlowKitFit fit;
    if (body.vertices.empty() || !body.bounds.valid() || samples.empty()) return fit;
    fit.ground_y = ground_y;
    fit.body_half_x = std::max(std::fabs(body.bounds.min.x), std::fabs(body.bounds.max.x));
    const float centre_x = 0.5f * fit.body_half_x;
    fit.bumper_front_z = -1e9f;
    for (const auto& p : samples) {
        if (std::fabs(p.x) < centre_x && p.y > ground_y + 0.15f && p.y < ground_y + 0.95f)
            fit.bumper_front_z = std::max(fit.bumper_front_z, p.z);
    }
    fit.bumper_bottom_y = 1e9f;
    fit.bumper_top_y = -1e9f;
    for (const auto& p : samples) {
        if (std::fabs(p.x) >= centre_x) continue;
        if (p.z > fit.bumper_front_z - 0.10f) fit.bumper_bottom_y = std::min(fit.bumper_bottom_y, p.y);
        if (p.z > fit.bumper_front_z - 0.045f) fit.bumper_top_y = std::max(fit.bumper_top_y, p.y);
    }
    fit.hood_front_y = -1e9f;
    for (const auto& p : samples) {
        if (std::fabs(p.x) < 0.4f * fit.body_half_x && p.z > fit.bumper_front_z - 0.55f)
            fit.hood_front_y = std::max(fit.hood_front_y, p.y);
    }
    fit.roof_y = -1e9f;
    for (const auto& p : samples)
        if (std::fabs(p.x) < 0.35f * fit.body_half_x) fit.roof_y = std::max(fit.roof_y, p.y);
    fit.roof_front_z = -1e9f;
    fit.roof_rear_z = 1e9f;
    for (const auto& p : samples) {
        if (p.y > fit.roof_y - 0.05f && std::fabs(p.x) < 0.35f * fit.body_half_x) {
            fit.roof_front_z = std::max(fit.roof_front_z, p.z);
            fit.roof_rear_z = std::min(fit.roof_rear_z, p.z);
        }
        if (p.y > fit.roof_y - 0.08f) fit.roof_half_x = std::max(fit.roof_half_x, std::fabs(p.x));
    }
    fit.valid = fit.bumper_front_z > -1e8f && fit.bumper_bottom_y < fit.bumper_top_y &&
                fit.hood_front_y > fit.bumper_top_y && fit.roof_y > fit.hood_front_y &&
                fit.roof_front_z > fit.roof_rear_z && fit.roof_half_x > 0.3f;
    return fit;
}

inline PlowKitFit fit_plow_kit(const StaticEmesh& body, float ground_y) {
    return fit_plow_kit(body, plow_kit_detail::surface_samples(body), ground_y);
}

// Highest body surface in a small square about (x, z), or -1e9 if none.
inline float plow_kit_surface_y(const std::vector<glm::vec3>& samples, float x, float z,
                                float radius) {
    float best = -1e9f;
    for (const auto& p : samples)
        if (std::fabs(p.x - x) < radius && std::fabs(p.z - z) < radius) best = std::max(best, p.y);
    return best;
}

// Frontmost body surface inside an x band and y range, or -1e9 if none.
inline float plow_kit_front_z(const std::vector<glm::vec3>& samples, float x0, float x1,
                              float y0, float y1) {
    float best = -1e9f;
    for (const auto& p : samples)
        if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) best = std::max(best, p.z);
    return best;
}

// Moldboard cross-section in metres, blade frame: origin at the cutting-edge
// centre on the road, +z forward, +y up. A near-vertical lower sheet leaning
// back 22 degrees, rolling through a circular arc into a top that curls 40
// degrees forward of vertical: the shape that rolls snow instead of pushing
// it. Front face points, bottom to top.
inline std::vector<glm::vec2> plow_moldboard_profile(float height_m, float base_y) {
    constexpr float kPi = 3.14159265358979323846f;
    const float a0 = 112.0f * kPi / 180.0f;
    const float a1 = 50.0f * kPi / 180.0f;
    const float straight = 0.32f * height_m;
    const float span = height_m - base_y;
    const float radius = (span - straight * std::sin(a0)) / (std::cos(a1) - std::cos(a0));
    std::vector<glm::vec2> out;
    const glm::vec2 start{0.0f, base_y};
    out.push_back(start);
    const glm::vec2 corner = start + glm::vec2{std::cos(a0), std::sin(a0)} * straight;
    out.push_back(glm::mix(start, corner, 0.5f));
    out.push_back(corner);
    constexpr int kArc = 12;
    for (int i = 1; i <= kArc; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(kArc);
        out.push_back(corner + radius * glm::vec2{std::sin(a0) - std::sin(a), std::cos(a) - std::cos(a0)});
    }
    return out;
}

// Forward-facing unit normals of a profile (the concave side).
inline std::vector<glm::vec2> plow_profile_normals(const std::vector<glm::vec2>& profile) {
    std::vector<glm::vec2> out(profile.size());
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const glm::vec2 a = profile[i == 0 ? 0 : i - 1];
        const glm::vec2 b = profile[std::min(i + 1, profile.size() - 1)];
        const glm::vec2 d = glm::normalize(b - a);
        out[i] = {d.y, -d.x};  // (z, y): rotate the upward tangent toward +z
    }
    return out;
}

// Build the kit for one fitted body. source_per_metre maps authored metres to
// body source units per axis (the inverse of the body fit scale).
inline PlowKitMeshes make_plow_kit(const StaticEmesh& body, float ground_y,
                                   glm::vec3 source_per_metre,
                                   const PlowKitSpec& spec) {
    using namespace plow_kit_detail;
    PlowKitMeshes kit;
    const std::vector<glm::vec3> samples = surface_samples(body);
    kit.fit = fit_plow_kit(body, samples, ground_y);
    kit.source_per_metre = source_per_metre;
    if (!kit.fit.valid) return kit;
    const PlowKitFit& f = kit.fit;
    const glm::vec3 k = source_per_metre;
    const glm::vec3 origin{0.0f, f.ground_y, f.bumper_front_z};
    // Metres, relative to the anchor.
    const auto metric_y = [&](float y) { return (y - origin.y) / k.y; };
    const auto metric_z = [&](float z) { return (z - origin.z) / k.z; };
    const float half_body = f.body_half_x / k.x;
    const float bumper_bottom = metric_y(f.bumper_bottom_y);
    const float bumper_top = metric_y(f.bumper_top_y);
    const float hood = metric_y(f.hood_front_y);

    auto& blade_paint = part(kit, PlowPart::BladePaint);
    auto& blade_frame = part(kit, PlowPart::BladeFrame);
    auto& blade_steel = part(kit, PlowPart::BladeSteel);
    auto& blade_rubber = part(kit, PlowPart::BladeRubber);
    auto& blade_marker = part(kit, PlowPart::BladeMarker);
    auto& mount_frame = part(kit, PlowPart::MountFrame);
    auto& mount_steel = part(kit, PlowPart::MountSteel);
    auto& lamp_lens = part(kit, PlowPart::LampLens);

    // --- truck-side mount: push beams, receiver, clevis ------------------------
    // The receivers come out from under the bumper, top face just below its
    // lower edge. Behind the bumper face they run back under the truck until
    // they meet the body (a valance, an air dam, the frame horns), so they
    // read as bolted to it rather than hanging in front of it.
    const float beam_y = std::max(0.10f, bumper_bottom - 0.065f);
    const float pin_z = 0.13f;
    const float pin_x = 0.40f;
    for (const float side : {-1.0f, 1.0f}) {
        const float lo_x = (side * pin_x - 0.045f) * k.x, hi_x = (side * pin_x + 0.045f) * k.x;
        float obstruction = -1e9f;
        for (const auto& p : samples) {
            if (p.x < std::min(lo_x, hi_x) || p.x > std::max(lo_x, hi_x)) continue;
            if (p.z > f.bumper_front_z - 0.01f || p.z < f.bumper_front_z - 0.45f * k.z) continue;
            const float y = metric_y(p.y);
            if (y < beam_y - 0.05f || y > beam_y + 0.05f) continue;
            obstruction = std::max(obstruction, metric_z(p.z));
        }
        const float rear = obstruction > -1e8f ? obstruction + 0.004f : -0.34f;
        beam(mount_frame, {side * pin_x, beam_y, rear}, {side * pin_x, beam_y, pin_z - 0.03f},
             {0.075f, 0.10f}, 0.008f, kPaintUv);
        for (const float ear : {-0.052f, 0.052f}) {
            rbox(mount_frame, {side * pin_x + ear, beam_y, pin_z - 0.01f}, {0.009f, 0.075f, 0.075f},
                 0.004f, kPaintUv);
        }
        tube(mount_steel, {side * pin_x - 0.075f, beam_y, pin_z}, {side * pin_x + 0.075f, beam_y, pin_z},
             0.017f, 12, kMetalUv);
        // Hairpin clips on the pins.
        tube(mount_steel, {side * pin_x + 0.08f, beam_y - 0.03f, pin_z}, {side * pin_x + 0.08f, beam_y + 0.03f, pin_z},
             0.004f, 6, kMetalUv);
    }
    beam(mount_frame, {-pin_x - 0.07f, beam_y - 0.005f, 0.035f}, {pin_x + 0.07f, beam_y - 0.005f, 0.035f},
         {0.085f, 0.085f}, 0.01f, kPaintUv, {0, 1, 0});

    // --- headgear: uprights, lamp bar, lift arm ---------------------------------
    const float upright_x = std::min(0.36f, 0.42f * half_body);
    const float top_y = std::max(hood + 0.15f, bumper_top + 0.34f);
    const float top_source_y = origin.y + top_y * k.y;
    float clear_z = 0.0f;  // frontmost body ahead of the uprights, metres
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * upright_x * k.x;
        const float band = 0.06f * k.x;
        const float front = plow_kit_front_z(samples, std::min(x - band, x + band),
                                             std::max(x - band, x + band),
                                             f.bumper_bottom_y, top_source_y);
        if (front > -1e8f) clear_z = std::max(clear_z, metric_z(front));
    }
    const float upright_z = clear_z + 0.075f;
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * upright_x;
        beam(mount_frame, {x, beam_y - 0.04f, upright_z}, {x, top_y + 0.02f, upright_z},
             {0.055f, 0.07f}, 0.008f, kPaintUv, {0, 0, 1});
        // Gusset from the receiver up the upright.
        beam(mount_frame, {x, beam_y + 0.02f, 0.06f}, {x, beam_y + 0.16f, upright_z - 0.02f},
             {0.012f, 0.06f}, 0.003f, kPaintUv, {0, 0, 1});
    }
    beam(mount_frame, {-upright_x - 0.03f, bumper_top + 0.05f, upright_z}, {upright_x + 0.03f, bumper_top + 0.05f, upright_z},
         {0.05f, 0.06f}, 0.008f, kPaintUv);
    const float lamp_x = std::min(0.64f, 0.70f * half_body);
    tube(mount_frame, {-lamp_x - 0.02f, top_y, upright_z}, {lamp_x + 0.02f, top_y, upright_z}, 0.028f, 14, kPaintUv);
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * lamp_x;
        // Lamp bracket and a housing with a chrome bezel and a faceted lens.
        rbox(mount_frame, {x, top_y + 0.045f, upright_z}, {0.03f, 0.045f, 0.03f}, 0.006f, kPaintUv);
        const glm::vec3 housing{x, top_y + 0.155f, upright_z + 0.01f};
        rbox(mount_frame, housing, {0.125f, 0.075f, 0.065f}, 0.03f, kPaintUv);
        rbox(mount_frame, housing + glm::vec3{0, 0.082f, -0.01f}, {0.13f, 0.008f, 0.07f}, 0.004f, kPaintUv);
        rbox(mount_steel, housing + glm::vec3{0, 0, 0.062f}, {0.118f, 0.068f, 0.008f}, 0.008f, kMetalUv);
        const glm::vec3 lens = housing + glm::vec3{0, 0.004f, 0.071f};
        rbox(lamp_lens, lens, {0.102f, 0.052f, 0.006f}, 0.005f, kReflectorUv);
        kit.lamp_centres[side < 0.0f ? 0u : 1u] = lens;
        // Amber turn/park strip under the lens.
        rbox(part(kit, side < 0.0f ? PlowPart::BarLens0 : PlowPart::BarLens3),
             housing + glm::vec3{0, -0.058f, 0.068f}, {0.07f, 0.009f, 0.004f}, 0.003f, kLedUv);
    }
    // Lift arm off the lamp bar, a chain down to the A-frame, and the ram.
    const float arm_tip_z = upright_z + 0.44f;
    const float arm_tip_y = top_y - 0.07f;
    beam(mount_frame, {0, top_y + 0.01f, upright_z - 0.02f}, {0, arm_tip_y, arm_tip_z}, {0.06f, 0.075f}, 0.008f, kPaintUv);
    for (const float ear : {-0.045f, 0.045f})
        rbox(mount_frame, {ear, top_y, upright_z + 0.02f}, {0.008f, 0.06f, 0.07f}, 0.003f, kPaintUv);
    const float ram_base_y = bumper_top + 0.05f;
    ram(mount_frame, mount_steel, {0, ram_base_y, upright_z + 0.045f},
        {0, (top_y + arm_tip_y) * 0.5f - 0.01f, upright_z + 0.24f}, 0.037f, 0.62f, kPaintUv, kMetalUv);

    // --- A-frame, the blade and everything that lifts with it ------------------
    const float edge_z = pin_z + 0.80f;         // cutting edge, metres ahead of the bumper
    const float blade_half = spec.blade_width_m * 0.5f;
    const float frame_front_z = edge_z - 0.34f;
    const float rail_y = 0.155f;
    // A-frame arms converge from the clevis pins to the pivot quadrant.
    for (const float side : {-1.0f, 1.0f}) {
        beam(blade_frame, {side * pin_x, beam_y, pin_z}, {side * 0.11f, rail_y + 0.03f, frame_front_z},
             {0.08f, 0.10f}, 0.009f, kPaintUv);
        // Pin bosses.
        tube(blade_frame, {side * pin_x - 0.04f, beam_y, pin_z}, {side * pin_x + 0.04f, beam_y, pin_z}, 0.045f, 14, kPaintUv);
    }
    beam(blade_frame, {-0.30f, glm::mix(beam_y, rail_y, 0.35f) + 0.01f, glm::mix(pin_z, frame_front_z, 0.35f)},
         {0.30f, glm::mix(beam_y, rail_y, 0.35f) + 0.01f, glm::mix(pin_z, frame_front_z, 0.35f)}, {0.07f, 0.07f}, 0.008f, kPaintUv);
    // Pivot quadrant plate and the lift-chain lug.
    rbox(blade_frame, {0, rail_y + 0.035f, frame_front_z + 0.04f}, {0.16f, 0.012f, 0.13f}, 0.006f, kPaintUv);
    const float lug_z = arm_tip_z;
    const float lug_y = glm::mix(beam_y, rail_y, (lug_z - pin_z) / (frame_front_z - pin_z)) + 0.07f;
    rbox(blade_frame, {0, lug_y, lug_z}, {0.012f, 0.03f, 0.04f}, 0.004f, kPaintUv);
    // Chain links from the arm tip to the lug (truck side; hangs, never lifts).
    {
        const glm::vec3 top{0, arm_tip_y - 0.04f, arm_tip_z};
        const glm::vec3 bottom{0, lug_y + 0.03f, lug_z};
        const int links = std::max(3, static_cast<int>(glm::length(top - bottom) / 0.045f));
        for (int i = 0; i < links; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(links);
            const glm::vec3 c = glm::mix(top, bottom, t);
            const glm::mat3 twist = i % 2 == 0 ? glm::mat3{1.0f}
                : glm::mat3{glm::vec3{0, 0, 1}, glm::vec3{0, 1, 0}, glm::vec3{-1, 0, 0}};
            rbox(mount_steel, c, {0.006f, 0.026f, 0.016f}, 0.005f, kMetalUv, twist);
        }
    }

    // The moldboard.
    const std::vector<glm::vec2> face = plow_moldboard_profile(spec.blade_height_m, 0.03f);
    const std::vector<glm::vec2> normals = plow_profile_normals(face);
    const std::size_t n = face.size();
    std::vector<glm::vec2> front(n), back(n), rib(n);
    constexpr float kSheet = 0.011f;
    for (std::size_t i = 0; i < n; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(n - 1);
        front[i] = face[i] + glm::vec2{edge_z, 0.0f};
        back[i] = front[i] - normals[i] * kSheet;
        rib[i] = back[i] - glm::vec2{0.15f - 0.10f * s, 0.0f};
        rib[i].y = std::max(rib[i].y, 0.05f);
    }
    extrude_ribbon_x(blade_paint, front, back, -blade_half, blade_half, kPaintUv);
    // Vertical ribs and the two end plates, which carry the same section.
    constexpr int kRibs = 5;
    for (int r = 0; r < kRibs; ++r) {
        const float x = blade_half * 0.86f * (-1.0f + 2.0f * static_cast<float>(r) / static_cast<float>(kRibs - 1));
        extrude_ribbon_x(blade_paint, back, rib, x - 0.008f, x + 0.008f, kPaintUv);
    }
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * (blade_half - 0.006f);
        std::vector<glm::vec2> end_front = front;
        extrude_ribbon_x(blade_paint, end_front, rib, x - 0.007f, x + 0.007f, kPaintUv);
    }
    // Back channels, bottom and mid height, running the width.
    const auto rib_at = [&](float s) {
        const float f_index = s * static_cast<float>(n - 1);
        const auto i = std::min(static_cast<std::size_t>(f_index), n - 2);
        const float t = f_index - static_cast<float>(i);
        return glm::mix(rib[i], rib[i + 1], t);
    };
    const auto back_at = [&](float s) {
        const float f_index = s * static_cast<float>(n - 1);
        const auto i = std::min(static_cast<std::size_t>(f_index), n - 2);
        const float t = f_index - static_cast<float>(i);
        return glm::mix(back[i], back[i + 1], t);
    };
    for (const float s : {0.06f, 0.55f, 0.97f}) {
        const glm::vec2 a = back_at(s), b = rib_at(s);
        const glm::vec2 c = glm::mix(a, b, s > 0.9f ? 0.35f : 0.75f);
        beam(blade_paint, {-blade_half + 0.012f, c.y, c.x}, {blade_half - 0.012f, c.y, c.x},
             {0.06f, s > 0.9f ? 0.03f : 0.055f}, 0.006f, kPaintUv, {0, 0, 1});
    }
    // Cutting edge bolted over the bottom of the sheet, bolts along it.
    {
        const glm::vec2 dir = glm::normalize(face[2] - face[0]);
        const glm::vec2 nrm = normals[0];
        const float edge_height = 0.155f;
        const glm::vec2 centre = glm::vec2{edge_z, 0.006f} + dir * (edge_height * 0.5f) + nrm * 0.006f;
        const glm::mat3 basis{glm::vec3{1, 0, 0}, glm::vec3{0, dir.y, dir.x}, glm::vec3{0, nrm.y, nrm.x}};
        rbox(blade_steel, {0, centre.y, centre.x}, {blade_half + 0.004f, edge_height * 0.5f, 0.011f}, 0.004f, kMetalUv, basis);
        const int bolts = std::max(6, static_cast<int>(spec.blade_width_m / 0.30f));
        for (int b = 0; b < bolts; ++b) {
            const float x = (blade_half - 0.12f) * (-1.0f + 2.0f * static_cast<float>(b) / static_cast<float>(bolts - 1));
            const glm::vec2 p = centre + dir * 0.015f + nrm * 0.012f;
            rbox(blade_steel, {x, p.y, p.x}, {0.017f, 0.017f, 0.006f}, 0.006f, kMetalUv, basis);
        }
    }
    // Rubber snow deflector on the top edge, clamped by a steel strip.
    {
        const glm::vec2 top = front[n - 1];
        const glm::vec2 dir = glm::normalize(front[n - 1] - front[n - 3]);
        const glm::vec2 nrm = normals[n - 1];
        std::vector<glm::vec2> flap_front{top - dir * 0.05f - nrm * 0.013f,
                                          top + dir * 0.03f - nrm * 0.013f,
                                          top + dir * 0.11f - nrm * 0.013f};
        std::vector<glm::vec2> flap_back;
        for (const auto& p : flap_front) flap_back.push_back(p - nrm * 0.012f);
        extrude_ribbon_x(blade_rubber, flap_front, flap_back, -blade_half + 0.05f, blade_half - 0.05f, kPaintUv);
        const glm::vec2 strip = top - dir * 0.02f - nrm * 0.032f;
        const glm::mat3 basis{glm::vec3{1, 0, 0}, glm::vec3{0, dir.y, dir.x}, glm::vec3{0, nrm.y, nrm.x}};
        rbox(blade_steel, {0, strip.y, strip.x}, {blade_half - 0.06f, 0.028f, 0.005f}, 0.003f, kMetalUv, basis);
    }
    // Trip rail, hinge plates to the rib feet, and the trip springs.
    const float rail_z = edge_z - 0.235f;
    beam(blade_frame, {-blade_half * 0.80f, rail_y, rail_z}, {blade_half * 0.80f, rail_y, rail_z},
         {0.075f, 0.075f}, 0.008f, kPaintUv, {0, 0, 1});
    for (int r = 0; r < kRibs; ++r) {
        const float x = blade_half * 0.86f * (-1.0f + 2.0f * static_cast<float>(r) / static_cast<float>(kRibs - 1));
        if (std::fabs(x) > blade_half * 0.8f) continue;
        const glm::vec2 foot = rib_at(0.02f);
        beam(blade_frame, {x + 0.02f, foot.y + 0.01f, foot.x + 0.01f}, {x + 0.02f, rail_y, rail_z + 0.02f},
             {0.012f, 0.07f}, 0.003f, kPaintUv, {0, 0, 1});
        tube(blade_steel, {x - 0.01f, rail_y, rail_z + 0.035f}, {x + 0.05f, rail_y, rail_z + 0.035f}, 0.014f, 8, kMetalUv);
    }
    for (const float xs : {-0.62f, -0.22f, 0.22f, 0.62f}) {
        const float x = xs * blade_half;
        const glm::vec2 hook = rib_at(0.74f);
        const glm::vec3 bottom{x, rail_y - 0.01f, rail_z - 0.05f};
        const glm::vec3 top{x, hook.y, hook.x - 0.015f};
        spring(blade_steel, bottom, top, 0.027f, kMetalUv);
        rbox(blade_paint, top + glm::vec3{0, 0.02f, 0.02f}, {0.016f, 0.03f, 0.03f}, 0.004f, kPaintUv);
    }
    // Angle rams from the A-frame to the rail.
    for (const float side : {-1.0f, 1.0f}) {
        const glm::vec3 a{side * 0.27f, glm::mix(beam_y, rail_y, 0.55f) + 0.04f, glm::mix(pin_z, frame_front_z, 0.55f)};
        const glm::vec3 b{side * blade_half * 0.52f, rail_y + 0.05f, rail_z - 0.045f};
        ram(blade_frame, blade_steel, a, b, 0.03f, 0.58f, kPaintUv, kMetalUv);
    }
    // Blade guides: fibreglass rods at the ends, the driver's width gauge.
    for (const float side : {-1.0f, 1.0f}) {
        const glm::vec2 root = rib_at(0.95f);
        const glm::vec3 base{side * (blade_half - 0.035f), root.y, root.x + 0.03f};
        rbox(blade_frame, base, {0.018f, 0.03f, 0.022f}, 0.004f, kPaintUv);
        tube(blade_marker, base, base + glm::vec3{0, 0.62f, -0.02f}, 0.0085f, 8, kPaintUv);
        rbox(blade_marker, base + glm::vec3{0, 0.64f, -0.02f}, {0.018f, 0.03f, 0.018f}, 0.012f, kPaintUv);
    }

    // --- roof service light bar ------------------------------------------------
    {
        const float roof = metric_y(f.roof_y);
        const float roof_front = metric_z(f.roof_front_z);
        const float roof_rear = metric_z(f.roof_rear_z);
        const float roof_half = f.roof_half_x / k.x;
        const float length = std::min(spec.bar_length_m, 2.0f * roof_half * 0.86f);
        const float bar_z = std::max(roof_rear + 0.25f, roof_front - 0.30f);
        const float foot_x = length * 0.5f - 0.13f;
        // The roof skin under each foot: a crowned roof is lower at the feet
        // than on the centreline, and the bar must sit on both.
        const auto roof_under = [&](float side) {
            const float y = plow_kit_surface_y(samples, side * foot_x * k.x,
                                               origin.z + bar_z * k.z, 0.07f);
            return y > -1e8f ? metric_y(y) : roof;
        };
        const float roof_under_left = roof_under(-1.0f);
        const float roof_under_right = roof_under(1.0f);
        const float bottom = std::max(roof_under_left, roof_under_right) + 0.065f;
        kit.bar_bottom_y = origin.y + bottom * k.y;
        kit.bar_feet_xz = {glm::vec2{-foot_x * k.x, origin.z + bar_z * k.z},
                           glm::vec2{foot_x * k.x, origin.z + bar_z * k.z}};
        // Feet: a bracket to a rubber pad on the roof skin.
        for (const float side : {-1.0f, 1.0f}) {
            const float under = side < 0.0f ? roof_under_left : roof_under_right;
            const float x = side * foot_x;
            rbox(mount_frame, {x, (under + bottom) * 0.5f + 0.004f, bar_z}, {0.028f, (bottom - under) * 0.5f, 0.05f}, 0.006f, kPaintUv);
            rbox(mount_frame, {x, under + 0.006f, bar_z}, {0.055f, 0.007f, 0.085f}, 0.005f, kPaintUv);
        }
        // Extruded aluminium base, black, with rolled end caps.
        const float base_h = 0.042f;
        rbox(mount_frame, {0, bottom + base_h * 0.5f, bar_z}, {length * 0.5f, base_h * 0.5f, 0.15f}, 0.014f, kPaintUv);
        const float lens_y = bottom + base_h + 0.036f;
        rbox(mount_frame, {-length * 0.5f + 0.028f, lens_y - 0.004f, bar_z}, {0.03f, 0.044f, 0.145f}, 0.022f, kPaintUv);
        rbox(mount_frame, {length * 0.5f - 0.028f, lens_y - 0.004f, bar_z}, {0.03f, 0.044f, 0.145f}, 0.022f, kPaintUv);
        // Segmented amber lens modules on a black spine, flashed in four groups.
        const float usable = length - 0.11f;
        const int modules = std::max(4, (static_cast<int>(std::lround(usable / 0.136f)) + 2) / 4 * 4);
        const float pitch = usable / static_cast<float>(modules);
        rbox(mount_frame, {0, lens_y - 0.01f, bar_z}, {usable * 0.5f, 0.02f, 0.10f}, 0.008f, kPaintUv);
        for (int i = 0; i < modules; ++i) {
            const float x = -usable * 0.5f + pitch * (static_cast<float>(i) + 0.5f);
            const auto group = static_cast<std::size_t>(i * static_cast<int>(kPlowBarLensGroups) / modules);
            auto& lens = kit.parts[plow_part(PlowPart::BarLens0) + group];
            rbox(lens, {x, lens_y, bar_z}, {pitch * 0.5f - 0.007f, 0.034f, 0.128f}, 0.024f, kLedUv);
            rbox(mount_frame, {x + pitch * 0.5f, lens_y + 0.002f, bar_z}, {0.006f, 0.036f, 0.13f}, 0.004f, kPaintUv);
        }
        kit.bar_centre = origin + k * glm::vec3{0, lens_y, bar_z};
    }

    // --- metres to body source space -------------------------------------------
    for (auto& mesh : kit.parts) {
        AABB bounds;
        for (auto& v : mesh.vertices) {
            v.position = origin + k * v.position;
            v.normal = glm::normalize(v.normal / k);
            bounds.expand(v.position);
        }
        mesh.bounds = bounds;
    }
    for (auto& c : kit.lamp_centres) c = origin + k * c;
    kit.pivot = origin + k * glm::vec3{0, beam_y, pin_z};
    kit.edge_centre = origin + k * glm::vec3{0, 0.0f, edge_z};
    kit.blade_half_width = blade_half * k.x;
    // Rotate the edge up by the lift height about the pin axis.
    constexpr float kLiftM = 0.24f;
    const float reach = edge_z - pin_z;
    const float drop = beam_y;
    const float radius = std::sqrt(reach * reach + drop * drop);
    const float start = std::atan2(-drop, reach);
    const float end = std::asin(std::clamp((kLiftM - drop) / radius, -1.0f, 1.0f));
    kit.lift_radians = -(end - start);
    return kit;
}

// The pose of the lifting parts, in body source space, at raised in [0, 1].
inline Transform plow_kit_lift_transform(const PlowKitMeshes& kit, float raised) {
    const float r = std::clamp(raised, 0.0f, 1.0f);
    const glm::quat q = glm::angleAxis(kit.lift_radians * r, glm::vec3{1, 0, 0});
    Transform out;
    out.rotation = q;
    out.position = kit.pivot - q * kit.pivot;
    return out;
}

// Chassis-space blade numbers for the sim, from the kit and the same body
// transform the renderer places the truck with.
inline PlowBladeMount plow_blade_mount(const PlowKitMeshes& kit, const Transform& body_local) {
    PlowBladeMount out;
    const glm::vec3 down = body_local.transform_point(kit.edge_centre);
    const glm::vec3 up = body_local.transform_point(
        plow_kit_lift_transform(kit, 1.0f).transform_point(kit.edge_centre));
    out.edge_forward_m = -down.z;
    out.edge_drop_m = -down.y;
    out.half_width_m = kit.blade_half_width * body_local.scale.x;
    out.lift_m = up.y - down.y;
    return out;
}

// Farthest-forward point of the kit in the chassis frame (metres ahead of the
// origin), blade down: where the car-car footprint has to reach.
inline float plow_kit_reach_m(const PlowKitMeshes& kit, const Transform& body_local) {
    float reach = 0.0f;
    for (std::size_t p = 0; p < kPlowMovingPartCount; ++p)
        for (const auto& v : kit.parts[p].vertices)
            reach = std::max(reach, -body_local.transform_point(v.position).z);
    return reach;
}

// ALTERNATING DOUBLE FLASH, keyed to the sim step. Groups 0-1 are the left
// half, 2-3 the right: the left half pops twice, then the right half twice,
// half a second a cycle, with the outer modules a beat behind the inner ones
// so the bar sweeps rather than blinks. Something on the bar is lit nearly
// half the time, which is what makes a service truck read from across a lot
// in a still frame as well as in motion. Pure, so a replay flashes the same
// frames.
inline float plow_light_bar_power(uint64_t step, std::size_t group) {
    constexpr uint64_t kCycle = 60;  // 0.5 s at 120 Hz
    const bool left = group < 2u;
    const bool outer = group == 0u || group == 3u;
    const uint64_t phase = (step + (left ? 0u : kCycle / 2u) + kCycle - (outer ? 3u : 0u)) % kCycle;
    const bool lit = phase < 5u || (phase >= 8u && phase < 13u);
    return lit ? 1.0f : 0.0f;
}

// The kit atlas, RGBA, 2 x 2 regions: powder coat, zinc steel, LED lens and a
// faceted reflector. Tints colour it per part; this carries only the detail.
inline std::vector<uint8_t> make_plow_kit_atlas(int size) {
    const int half = size / 2;
    std::vector<uint8_t> rgba(static_cast<std::size_t>(size * size * 4), 255);
    const auto noise = [](int x, int y, uint64_t salt) {
        const uint64_t h = hash_coord(0x504C4F57ull ^ salt, x, y);
        return static_cast<float>(h & 0xFFFFu) / 65535.0f;
    };
    const auto smooth = [&](float x, float y, uint64_t salt) {
        const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
        const float a = noise(x0, y0, salt), b = noise(x0 + 1, y0, salt);
        const float c = noise(x0, y0 + 1, salt), d = noise(x0 + 1, y0 + 1, salt);
        return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int lx = x % half, ly = y % half;
            const float u = (static_cast<float>(lx) + 0.5f) / static_cast<float>(half);
            const float v = (static_cast<float>(ly) + 0.5f) / static_cast<float>(half);
            float value = 1.0f;
            if (y < half && x < half) {
                // Powder coat: faint orange peel.
                value = 0.93f + 0.05f * smooth(static_cast<float>(lx) / 3.0f, static_cast<float>(ly) / 3.0f, 1) +
                        0.02f * noise(lx, ly, 2);
            } else if (y < half) {
                // Zinc and machined steel: fine streaks along U.
                value = 0.80f + 0.12f * smooth(static_cast<float>(lx) / 22.0f, static_cast<float>(ly) / 1.5f, 3) +
                        0.06f * noise(lx, ly, 4);
            } else if (x < half) {
                // LED module: three emitters behind a prismatic lens.
                value = 0.46f + 0.06f * std::sin(u * 3.14159f * 24.0f);
                for (int e = 0; e < 3; ++e) {
                    const glm::vec2 c{(static_cast<float>(e) + 0.5f) / 3.0f, 0.5f};
                    const float d = glm::length((glm::vec2{u, v} - c) * glm::vec2{1.0f, 1.6f});
                    value = std::max(value, 1.0f - glm::smoothstep(0.035f, 0.13f, d) * 0.54f);
                }
            } else {
                // Reflector: facet rings round a hot centre.
                const float d = glm::length(glm::vec2{u - 0.5f, (v - 0.5f) * 1.8f});
                value = 0.72f + 0.14f * std::cos(d * 60.0f) * (1.0f - d) +
                        0.30f * (1.0f - glm::smoothstep(0.0f, 0.12f, d));
            }
            const auto byte = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            const auto i = static_cast<std::size_t>((y * size + x) * 4);
            rgba[i] = rgba[i + 1] = rgba[i + 2] = byte;
            rgba[i + 3] = 255;
        }
    }
    return rgba;
}

}  // namespace apricot
