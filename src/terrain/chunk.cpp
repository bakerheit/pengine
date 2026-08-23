#include "terrain/chunk.h"

#include <cmath>

#include "terrain/heightmap.h"
#include "terrain/surface.h"

namespace apricot {
namespace {

// The quad split, written once and referred to everywhere else in this file.
//
// For the quad whose corners are (i,j) (i+1,j) (i,j+1) (i+1,j+1), named
// a, b, c, d respectively, the mesher emits triangles {a, c, b} and {b, c, d}.
// The diagonal therefore runs from c=(i,j+1) to b=(i+1,j), and the first
// triangle owns the half of the quad where tx + tz <= 1.
//
// THIS SPLIT IS A PUBLISHED CONTRACT, not an implementation detail.
// mesh_height_at() reconstructs it to answer ground queries, and the two must
// agree exactly or the ground the car stands on is a different plane from the
// one it is drawn on. If you change the winding here, change it there in the
// same commit, and expect terrain_collision_tests to tell you if you did not.

// Height of the planar triangle over a lattice cell, given the four corner
// heights and the position within the cell in [0, 1].
float triangle_height(float h00, float h10, float h01, float h11, float tx,
                      float tz) {
    if (tx + tz <= 1.0f) {
        // Triangle {a, c, b} = {(0,0), (0,1), (1,0)}.
        return h00 + (h10 - h00) * tx + (h01 - h00) * tz;
    }
    // Triangle {b, c, d} = {(1,0), (0,1), (1,1)}, expressed from the (1,1)
    // corner so both branches are exact at the shared diagonal.
    return h11 + (h01 - h11) * (1.0f - tx) + (h10 - h11) * (1.0f - tz);
}

// Unit face normal of the same triangle. Derived from the cross product of the
// triangle's edges and then simplified, so it is a normalise and nothing else.
glm::vec3 triangle_normal(float h00, float h10, float h01, float h11, float tx,
                          float tz) {
    constexpr float s = kVertexSpacingMetres;
    if (tx + tz <= 1.0f) {
        return glm::normalize(glm::vec3{h00 - h10, s, h00 - h01});
    }
    return glm::normalize(glm::vec3{h01 - h11, s, h10 - h11});
}

// The lattice cell containing a world XZ, and the position within it.
struct LatticeCell {
    float x0;  // world coordinate of the cell's minimum corner
    float z0;
    float tx;  // position within the cell, [0, 1)
    float tz;
};

LatticeCell lattice_cell(float x, float z) {
    constexpr float s = kVertexSpacingMetres;
    const float gx = std::floor(x / s);
    const float gz = std::floor(z / s);
    LatticeCell c;
    c.x0 = gx * s;
    c.z0 = gz * s;
    c.tx = (x - c.x0) / s;
    c.tz = (z - c.z0) / s;
    return c;
}

}  // namespace

ChunkCoord chunk_at(float world_x, float world_z) {
    return ChunkCoord{
        static_cast<int32_t>(std::floor(world_x / kChunkMetres)),
        static_cast<int32_t>(std::floor(world_z / kChunkMetres))};
}

glm::vec2 chunk_origin(ChunkCoord c) {
    return glm::vec2{static_cast<float>(c.x) * kChunkMetres,
                     static_cast<float>(c.z) * kChunkMetres};
}

ChunkMesh build_chunk(uint64_t seed, ChunkCoord coord) {
    ChunkMesh mesh;
    mesh.coord = coord;

    constexpr float kStep = kVertexSpacingMetres;
    const glm::vec2 origin = chunk_origin(coord);

    mesh.vertices.reserve(static_cast<std::size_t>(kChunkVerts) *
                          static_cast<std::size_t>(kChunkVerts));

    for (int j = 0; j < kChunkVerts; ++j) {
        for (int i = 0; i < kChunkVerts; ++i) {
            // Evaluated at the absolute WORLD coordinate, not a chunk-local
            // one. That is what makes a shared edge bit-identical between
            // neighbours: both chunks ask height_at() the same question.
            //
            // Note that this is a multiply from the origin rather than an
            // accumulated `wx += kStep`. Accumulation drifts by a rounding
            // step per column, which would make a chunk's last edge column
            // land at a very slightly different coordinate from its
            // neighbour's first — and "very slightly different" is exactly the
            // crack this whole scheme exists to avoid.
            const float wx = origin.x + static_cast<float>(i) * kStep;
            const float wz = origin.y + static_cast<float>(j) * kStep;

            TerrainVertex v;
            const float h = height_at(seed, wx, wz);
            const glm::vec3 n = normal_at(seed, wx, wz);

            v.position = glm::vec3{wx, h, wz};
            v.normal = n;
            v.uv = glm::vec2{wx, wz} * (1.0f / kChunkMetres);

            // Classified from the height and normal we already have, rather
            // than by a second surface_at() that would recompute both. Same
            // answer, five fewer field evaluations per vertex, and no way for
            // the two to disagree.
            v.material_weights =
                classify_surface(seed, wx, wz, h, n).weights;

            mesh.bounds.expand(v.position);
            mesh.vertices.push_back(v);
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(kChunkQuads) *
                         static_cast<std::size_t>(kChunkQuads) * 6u);
    for (int j = 0; j < kChunkQuads; ++j) {
        for (int i = 0; i < kChunkQuads; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * kChunkVerts + i);
            const uint32_t b = a + 1u;
            const uint32_t c = a + static_cast<uint32_t>(kChunkVerts);
            const uint32_t d = c + 1u;
            // Counter-clockwise when viewed from above (+Y), matching the
            // engine's front-face winding. See the quad-split note above.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    return mesh;
}

ChunkCollision build_chunk_collision(const ChunkMesh& mesh) {
    ChunkCollision out;
    out.coord = mesh.coord;
    out.bounds = mesh.bounds;

    const std::size_t tri_count = mesh.indices.size() / 3u;
    out.triangles.reserve(tri_count);

    for (std::size_t t = 0; t < tri_count; ++t) {
        const uint32_t ia = mesh.indices[t * 3u + 0u];
        const uint32_t ib = mesh.indices[t * 3u + 1u];
        const uint32_t ic = mesh.indices[t * 3u + 2u];

        // A malformed index buffer would read out of bounds. Skipping is the
        // wrong answer for a mesh WE built — it cannot happen — but this is a
        // public entry point taking a caller-supplied mesh, and reading past
        // the end of their vector is a far worse failure than dropping a
        // triangle we were never going to be able to place.
        const std::size_t n = mesh.vertices.size();
        if (ia >= n || ib >= n || ic >= n) continue;

        CollisionTri tri;
        tri.a = mesh.vertices[ia].position;
        tri.b = mesh.vertices[ib].position;
        tri.c = mesh.vertices[ic].position;

        const glm::vec3 face = glm::cross(tri.b - tri.a, tri.c - tri.a);
        const float len = glm::length(face);
        if (len > 0.0f) {
            tri.normal = face / len;
            // A height field never overhangs, so a downward normal here means
            // the winding is inverted. Flip rather than propagate it: a
            // collision normal pointing into the ground pushes the car THROUGH
            // the surface instead of out of it.
            if (tri.normal.y < 0.0f) tri.normal = -tri.normal;
        } else {
            // Degenerate triangle (two coincident vertices). Cannot happen on
            // the regular lattice, but a zero-length normalise is a NaN that
            // would propagate into the physics solver silently.
            tri.normal = glm::vec3{0.0f, 1.0f, 0.0f};
        }

        out.triangles.push_back(tri);
    }

    return out;
}

float mesh_height_at(uint64_t seed, float x, float z) {
    constexpr float s = kVertexSpacingMetres;
    const LatticeCell c = lattice_cell(x, z);

    const float h00 = height_at(seed, c.x0,     c.z0);
    const float h10 = height_at(seed, c.x0 + s, c.z0);
    const float h01 = height_at(seed, c.x0,     c.z0 + s);
    const float h11 = height_at(seed, c.x0 + s, c.z0 + s);

    return triangle_height(h00, h10, h01, h11, c.tx, c.tz);
}

glm::vec3 mesh_normal_at(uint64_t seed, float x, float z) {
    constexpr float s = kVertexSpacingMetres;
    const LatticeCell c = lattice_cell(x, z);

    const float h00 = height_at(seed, c.x0,     c.z0);
    const float h10 = height_at(seed, c.x0 + s, c.z0);
    const float h01 = height_at(seed, c.x0,     c.z0 + s);
    const float h11 = height_at(seed, c.x0 + s, c.z0 + s);

    return triangle_normal(h00, h10, h01, h11, c.tx, c.tz);
}

}  // namespace apricot
