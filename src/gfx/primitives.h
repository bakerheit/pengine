#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "terrain/chunk.h"

namespace apricot {

// Procedural geometry. No GL, no files, no loader — header-only and pure, so a
// headless test can build the exact geometry the renderer uploads and check it
// rather than checking a hand-written copy of it. Winding and normals are the
// two things that fail silently here: a back-to-front box under GL_CULL_FACE is
// invisible, and an inverted normal is a face that lights from the wrong side.

// The engine has exactly ONE vertex format, and terrain defined it first.
// Reusing it rather than declaring a near-identical gfx twin means the terrain
// mesher and these primitives feed the same VAO wiring and the same shader,
// with no conversion step in between to get subtly wrong.
using MeshVertex = TerrainVertex;

// CPU-side geometry ready to upload. Structurally a ChunkMesh; named separately
// because "chunk" means something specific in terrain and a unit cube is not
// one of them.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
};

// FRONT FACES ARE COUNTER-CLOCKWISE, matching GL's default glFrontFace(GL_CCW).
// Every generator below obeys it. Getting it backwards on one primitive gives
// you a hole in the world that only appears once culling is switched on, which
// is usually a long way from where the mistake was made.

// Axis-aligned box centred on the origin. Each face gets its own four vertices
// so normals stay hard — sharing corner vertices would average the three face
// normals together and round the cube off into a lumpy sphere.
// UVs run 0..1 across each face; tile with the per-instance uv_scale.
inline MeshData make_box(glm::vec3 half_extents) {
    MeshData m;
    m.vertices.reserve(24);
    m.indices.reserve(36);

    // n, u, v per face, chosen so cross(u, v) == n. That identity is what makes
    // the corner order below counter-clockwise when seen from outside.
    struct Face {
        glm::vec3 n, u, v;
    };
    const Face faces[6] = {
        {{ 1.0f, 0.0f, 0.0f}, { 0.0f, 0.0f,-1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f, 1.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f,-1.0f}},
        {{ 0.0f,-1.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f, 0.0f, 1.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f, 0.0f,-1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    };

    // TerrainVertex carries four-way material weights for the terrain splat
    // shader. These primitives are demo geometry, not terrain, so they pick a
    // single channel rather than leaving the field uninitialised — a zero
    // vector sums to 0 and the splat divides by that.
    constexpr glm::vec4 kSolidRock{1.0f, 0.0f, 0.0f, 0.0f};

    for (const Face& f : faces) {
        // Half-extent along each of the face's own axes.
        const glm::vec3 he = glm::abs(half_extents);
        const glm::vec3 centre = f.n * he;
        const glm::vec3 du = f.u * glm::abs(glm::dot(f.u, he));
        const glm::vec3 dv = f.v * glm::abs(glm::dot(f.v, he));

        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({centre - du - dv, f.n, {0.0f, 0.0f}, kSolidRock});
        m.vertices.push_back({centre + du - dv, f.n, {1.0f, 0.0f}, kSolidRock});
        m.vertices.push_back({centre + du + dv, f.n, {1.0f, 1.0f}, kSolidRock});
        m.vertices.push_back({centre - du + dv, f.n, {0.0f, 1.0f}, kSolidRock});

        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
    }

    for (const MeshVertex& v : m.vertices) m.bounds.expand(v.position);
    return m;
}

// Closed triangular gable in the same centred unit volume as make_box().
// Base y=-.5, ridge y=.5 at x=0, extrusion z=-.5..+.5. Every face has
// independent outward normals and CCW winding, including both end triangles.
inline MeshData make_gable_prism() {
    MeshData m;
    constexpr glm::vec4 rock{1.0f,0.0f,0.0f,0.0f};
    const glm::vec3 a{-.5f,-.5f,-.5f}, b{.5f,-.5f,-.5f}, c{0,.5f,-.5f};
    const glm::vec3 d{-.5f,-.5f,.5f}, e{.5f,-.5f,.5f}, f{0,.5f,.5f};
    const auto triangle = [&](glm::vec3 p, glm::vec3 q, glm::vec3 r) {
        const auto base = static_cast<uint32_t>(m.vertices.size());
        const glm::vec3 normal = glm::normalize(glm::cross(q-p,r-p));
        for (const glm::vec3 v : {p,q,r})
            m.vertices.push_back({v,normal,{v.x+.5f,v.y+.5f},rock});
        m.indices.insert(m.indices.end(),{base,base+1u,base+2u});
    };
    triangle(a,c,b); triangle(d,e,f); // exposed gable ends
    triangle(a,b,e); triangle(a,e,d); // bottom
    triangle(a,d,f); triangle(a,f,c); // left pitch
    triangle(b,c,f); triangle(b,f,e); // right pitch
    for (const auto& v : m.vertices) m.bounds.expand(v.position);
    return m;
}

// One upward-facing unit quad at the TOP of the unit-box volume. Authored
// paint pieces can therefore keep BuildingPiece's bottom/height convention:
// bottom + height is the visible paint plane. Unlike a very thin box this has
// no vertical faces for headlights to catch, so a zebra stripe reads as paint
// instead of a 2 cm kerb.
inline MeshData make_decal_quad() {
    MeshData m;
    constexpr glm::vec3 n{0.0f, 1.0f, 0.0f};
    constexpr glm::vec4 rock{1.0f, 0.0f, 0.0f, 0.0f};
    m.vertices = {
        {{-0.5f, 0.5f, -0.5f}, n, {0.0f, 0.0f}, rock},
        {{-0.5f, 0.5f,  0.5f}, n, {0.0f, 1.0f}, rock},
        {{ 0.5f, 0.5f,  0.5f}, n, {1.0f, 1.0f}, rock},
        {{ 0.5f, 0.5f, -0.5f}, n, {1.0f, 0.0f}, rock},
    };
    m.indices = {0, 1, 2, 0, 2, 3};
    for (const MeshVertex& v : m.vertices) m.bounds.expand(v.position);
    return m;
}

// One forward-facing unit quad on the FRONT of the unit-box volume. Billboard
// records can keep the same centre/scale transform as every other authored
// piece while drawing only the image plane, with no thin-box edge to catch
// headlights and no back face showing the ad in reverse.
inline MeshData make_billboard_quad() {
    MeshData m;
    constexpr glm::vec3 n{0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 rock{1.0f, 0.0f, 0.0f, 0.0f};
    m.vertices = {
        {{-0.5f, -0.5f, 0.5f}, n, {0.0f, 0.0f}, rock},
        {{ 0.5f, -0.5f, 0.5f}, n, {1.0f, 0.0f}, rock},
        {{ 0.5f,  0.5f, 0.5f}, n, {1.0f, 1.0f}, rock},
        {{-0.5f,  0.5f, 0.5f}, n, {0.0f, 1.0f}, rock},
    };
    m.indices = {0, 1, 2, 0, 2, 3};
    for (const MeshVertex& v : m.vertices) m.bounds.expand(v.position);
    return m;
}

// A box as a Minkowski sum of a smaller box and a sphere. The face grid is
// projected onto that rounded hull, so broad panels stay flat while edges and
// corners receive continuous normals. This is the shared prop primitive:
// pumps and service equipment get a real highlight roll without shipping one
// bespoke mesh per box-sized fixture.
inline MeshData make_rounded_box(glm::vec3 half_extents, float radius,
                                 int subdivisions) {
    const glm::vec3 half = glm::max(glm::abs(half_extents), glm::vec3{0.001f});
    radius = std::clamp(std::fabs(radius), 0.001f,
                        std::min({half.x, half.y, half.z}) * 0.49f);
    subdivisions = std::max(subdivisions, 2);

    MeshData m;
    const std::size_t side = static_cast<std::size_t>(subdivisions + 1);
    m.vertices.reserve(6u * side * side);
    m.indices.reserve(6u * static_cast<std::size_t>(subdivisions) *
                      static_cast<std::size_t>(subdivisions) * 6u);

    struct Face {
        glm::vec3 n, u, v;
    };
    const Face faces[6] = {
        {{ 1.0f, 0.0f, 0.0f}, { 0.0f, 0.0f,-1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f, 1.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f,-1.0f}},
        {{ 0.0f,-1.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f, 0.0f, 1.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f, 0.0f,-1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    };
    constexpr glm::vec4 kSolidRock{1.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 inner = glm::max(half - glm::vec3{radius}, glm::vec3{0.0f});

    for (const Face& face : faces) {
        const float half_u = glm::dot(glm::abs(face.u), half);
        const float half_v = glm::dot(glm::abs(face.v), half);
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());

        for (int j = 0; j <= subdivisions; ++j) {
            const float tv = static_cast<float>(j) /
                             static_cast<float>(subdivisions);
            for (int i = 0; i <= subdivisions; ++i) {
                const float tu = static_cast<float>(i) /
                                 static_cast<float>(subdivisions);
                const glm::vec3 raw =
                    face.n * half + face.u * glm::mix(-half_u, half_u, tu) +
                    face.v * glm::mix(-half_v, half_v, tv);
                const glm::vec3 core = glm::clamp(raw, -inner, inner);
                const glm::vec3 delta = raw - core;
                const glm::vec3 normal = glm::normalize(delta);
                const glm::vec3 position = core + normal * radius;
                m.vertices.push_back(
                    {position, normal, {tu, tv}, kSolidRock});
                m.bounds.expand(position);
            }
        }

        const uint32_t line = static_cast<uint32_t>(subdivisions + 1);
        for (int j = 0; j < subdivisions; ++j) {
            for (int i = 0; i < subdivisions; ++i) {
                const uint32_t a = base + static_cast<uint32_t>(j) * line +
                                   static_cast<uint32_t>(i);
                const uint32_t b = a + 1u;
                const uint32_t c = a + line;
                const uint32_t d = c + 1u;
                m.indices.insert(m.indices.end(), {a, b, d, a, d, c});
            }
        }
    }
    return m;
}

// Closed cylinder along +Y. Side and cap vertices are deliberately separate:
// averaging their normals would round the cap edge into a pinched barrel.
inline MeshData make_cylinder(float radius, float half_height, int segments) {
    MeshData m;
    radius = std::max(std::fabs(radius), 0.001f);
    half_height = std::max(std::fabs(half_height), 0.001f);
    segments = std::max(segments, 3);
    constexpr float kTau = 6.28318530717958647692f;
    constexpr glm::vec4 kSolidRock{1.0f, 0.0f, 0.0f, 0.0f};

    m.vertices.reserve(static_cast<std::size_t>(segments + 1) * 4u + 2u);
    m.indices.reserve(static_cast<std::size_t>(segments) * 12u);

    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = kTau * t;
        const glm::vec3 normal{std::cos(angle), 0.0f, std::sin(angle)};
        const glm::vec3 radial = normal * radius;
        m.vertices.push_back(
            {{radial.x, -half_height, radial.z}, normal, {t, 0.0f}, kSolidRock});
        m.vertices.push_back(
            {{radial.x, half_height, radial.z}, normal, {t, 1.0f}, kSolidRock});
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = static_cast<uint32_t>(i * 2);
        const uint32_t b = a + 2u;
        const uint32_t c = a + 1u;
        const uint32_t d = a + 3u;
        m.indices.insert(m.indices.end(), {a, c, d, a, d, b});
    }

    const uint32_t top_center = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0.0f, half_height, 0.0f}, {0.0f, 1.0f, 0.0f},
                          {0.5f, 0.5f}, kSolidRock});
    const uint32_t top_rim = static_cast<uint32_t>(m.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float angle = kTau * static_cast<float>(i) /
                            static_cast<float>(segments);
        const float x = std::cos(angle);
        const float z = std::sin(angle);
        m.vertices.push_back({{x * radius, half_height, z * radius},
                              {0.0f, 1.0f, 0.0f},
                              {x * 0.5f + 0.5f, z * 0.5f + 0.5f}, kSolidRock});
    }
    for (int i = 0; i < segments; ++i) {
        m.indices.insert(m.indices.end(),
                         {top_center, top_rim + static_cast<uint32_t>(i + 1),
                          top_rim + static_cast<uint32_t>(i)});
    }

    const uint32_t bottom_center = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0.0f, -half_height, 0.0f}, {0.0f, -1.0f, 0.0f},
                          {0.5f, 0.5f}, kSolidRock});
    const uint32_t bottom_rim = static_cast<uint32_t>(m.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float angle = kTau * static_cast<float>(i) /
                            static_cast<float>(segments);
        const float x = std::cos(angle);
        const float z = std::sin(angle);
        m.vertices.push_back({{x * radius, -half_height, z * radius},
                              {0.0f, -1.0f, 0.0f},
                              {x * 0.5f + 0.5f, z * 0.5f + 0.5f}, kSolidRock});
    }
    for (int i = 0; i < segments; ++i) {
        m.indices.insert(m.indices.end(),
                         {bottom_center, bottom_rim + static_cast<uint32_t>(i),
                          bottom_rim + static_cast<uint32_t>(i + 1)});
    }

    m.bounds.expand({-radius, -half_height, -radius});
    m.bounds.expand({radius, half_height, radius});
    return m;
}

// Horizontal plane on XZ centred at the origin, normal +Y, spanning
// [-half, +half] on both axes and split into `cells` x `cells` quads.
//
// It is subdivided rather than one big quad on purpose: per-vertex lighting
// interpolation and fog both need geometry to interpolate ACROSS, and a
// two-triangle ground plane fogs along its diagonal in a way you cannot
// un-see. UVs run 0..1 over the whole plane; tile with uv_scale.
inline MeshData make_plane(float half, int cells) {
    MeshData m;
    if (cells < 1) cells = 1;

    const int line = cells + 1;
    m.vertices.reserve(static_cast<std::size_t>(line) *
                       static_cast<std::size_t>(line));

    for (int j = 0; j < line; ++j) {
        const float tz = static_cast<float>(j) / static_cast<float>(cells);
        for (int i = 0; i < line; ++i) {
            const float tx = static_cast<float>(i) / static_cast<float>(cells);
            MeshVertex v;
            v.position = {glm::mix(-half, half, tx), 0.0f, glm::mix(-half, half, tz)};
            v.normal = {0.0f, 1.0f, 0.0f};
            v.uv = {tx, tz};
            m.vertices.push_back(v);
        }
    }

    m.indices.reserve(static_cast<std::size_t>(cells) *
                      static_cast<std::size_t>(cells) * 6u);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * line + i);
            const uint32_t b = a + 1;
            const uint32_t c = a + static_cast<uint32_t>(line);
            const uint32_t d = c + 1;
            // (a, c, b) and (b, c, d) both wind counter-clockwise seen from +Y.
            m.indices.push_back(a);
            m.indices.push_back(c);
            m.indices.push_back(b);
            m.indices.push_back(b);
            m.indices.push_back(c);
            m.indices.push_back(d);
        }
    }

    for (const MeshVertex& v : m.vertices) m.bounds.expand(v.position);
    return m;
}

// Horizontal filled disc, normal +Y. Used for small ground marks where a
// square plane reads as a decal bug. The duplicated closing rim vertex keeps
// the seam's UVs exact and lets every triangle use the same simple fan rule.
inline MeshData make_disc(float radius, int segments) {
    MeshData m;
    radius = std::max(std::fabs(radius), 0.001f);
    segments = std::max(segments, 3);
    m.vertices.reserve(static_cast<std::size_t>(segments) + 2u);
    m.indices.reserve(static_cast<std::size_t>(segments) * 3u);
    constexpr glm::vec4 kSolidRock{1.0f, 0.0f, 0.0f, 0.0f};
    m.vertices.push_back(
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
         {0.5f, 0.5f}, kSolidRock});
    constexpr float kTau = 6.28318530717958647692f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = kTau * static_cast<float>(i) /
                            static_cast<float>(segments);
        const float x = std::cos(angle);
        const float z = std::sin(angle);
        m.vertices.push_back(
            {{x * radius, 0.0f, z * radius}, {0.0f, 1.0f, 0.0f},
             {x * 0.5f + 0.5f, z * 0.5f + 0.5f}, kSolidRock});
    }
    for (int i = 0; i < segments; ++i) {
        // Positive-angle motion around XZ winds clockwise from above, so the
        // next rim point comes before the current one for a +Y front face.
        m.indices.push_back(0u);
        m.indices.push_back(static_cast<uint32_t>(i + 2));
        m.indices.push_back(static_cast<uint32_t>(i + 1));
    }
    for (const MeshVertex& vertex : m.vertices) m.bounds.expand(vertex.position);
    return m;
}

}  // namespace apricot
