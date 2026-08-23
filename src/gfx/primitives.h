#pragma once

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

}  // namespace apricot
