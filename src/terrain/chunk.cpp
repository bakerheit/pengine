#include "terrain/chunk.h"

#include <cmath>

#include "core/log.h"
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

LatticeCell lattice_cell_at(float x, float z, float s) {
    const float gx = std::floor(x / s);
    const float gz = std::floor(z / s);
    LatticeCell c;
    c.x0 = gx * s;
    c.z0 = gz * s;
    c.tx = (x - c.x0) / s;
    c.tz = (z - c.z0) / s;
    return c;
}

LatticeCell lattice_cell(float x, float z) {
    return lattice_cell_at(x, z, kVertexSpacingMetres);
}

// The chunk's perimeter vertex indices, as one closed ring.
//
// THE ORDER IS LOAD-BEARING and it is: bottom row toward +X, right column
// toward +Z, top row toward -X, left column toward -Z. That traversal makes
// the skirt quads below come out facing OUTWARD under the engine's
// counter-clockwise front-face rule. Reverse it and the entire curtain is
// back-facing, which under GL_CULL_FACE means it is invisible — and an
// invisible skirt does not read as "the winding is wrong", it reads as "skirts
// do not work", which is a much longer afternoon.
std::vector<uint32_t> perimeter_ring(int verts) {
    std::vector<uint32_t> ring;
    if (verts < 2) return ring;
    const uint32_t v = static_cast<uint32_t>(verts);
    ring.reserve(static_cast<std::size_t>(4 * (verts - 1)));

    const auto at = [v](uint32_t i, uint32_t j) { return j * v + i; };

    for (uint32_t i = 0; i + 1 < v; ++i) ring.push_back(at(i, 0));
    for (uint32_t j = 0; j + 1 < v; ++j) ring.push_back(at(v - 1u, j));
    for (uint32_t i = v - 1u; i > 0; --i) ring.push_back(at(i, v - 1u));
    for (uint32_t j = v - 1u; j > 0; --j) ring.push_back(at(0, j));

    return ring;
}

// Hang the curtain. See the long note in chunk.h for why this and not a
// stitched transition row.
void append_skirt(ChunkMesh& mesh, int verts) {
    const std::vector<uint32_t> ring = perimeter_ring(verts);
    if (ring.size() < 3u) return;

    // --- depth, MEASURED -----------------------------------------------------
    //
    // The crack at a boundary is the gap between one side's surface and the
    // other side's CHORD across the coarsest cell either of them might use. So
    // the quantity to measure is the relief over a span that wide — not the
    // relief between adjacent vertices.
    //
    // Measuring adjacent steps was the first thing tried here and it is subtly
    // wrong in exactly one direction. A level 0 chunk has 1 m steps, so its
    // measured relief is small and its skirt comes out short; but the thing it
    // has to hide is a level 3 neighbour's 8 m chord, which departs from the
    // field by far more than any single 1 m step does. The fine side is
    // therefore the side that cracks, which is the opposite of the intuition,
    // and it showed up as the 0|1 boundary having the thinnest margin of all
    // six pairings while 2|3 had the fattest.
    //
    // `window` is the coarsest neighbour cell expressed in THIS chunk's
    // vertices: 8 at level 0, 1 at level 3. Every height involved was already
    // computed above, so this still costs no extra height_at() calls.
    const int window = lod_step(kMaxChunkLod) / lod_step(mesh.lod);
    const std::size_t span =
        std::min(static_cast<std::size_t>(window), ring.size() - 1u);

    float max_relief = 0.0f;
    for (std::size_t k = 0; k < ring.size(); ++k) {
        const float here = mesh.vertices[ring[k]].position.y;
        for (std::size_t d = 1; d <= span; ++d) {
            const std::size_t n = (k + d) % ring.size();
            const float dh = here - mesh.vertices[ring[n]].position.y;
            const float a = dh < 0.0f ? -dh : dh;
            if (a > max_relief) max_relief = a;
        }
    }

    float depth = max_relief * kSkirtReliefFactor;
    if (depth < kMinSkirtMetres) depth = kMinSkirtMetres;
    mesh.skirt_depth = depth;

    // --- the dropped copies --------------------------------------------------
    // Each perimeter vertex gets a twin directly below it, carrying the SAME
    // normal, uv and material weights. Sharing them is deliberate: the skirt is
    // only ever seen through a crack, edge on, and shading it like the ground
    // it hangs from is what stops the crack reading as a dark line instead of
    // as nothing at all.
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.reserve(mesh.vertices.size() + ring.size());
    for (const uint32_t idx : ring) {
        TerrainVertex v = mesh.vertices[idx];
        v.position.y -= depth;
        mesh.vertices.push_back(v);
        mesh.bounds.expand(v.position);
    }

    // --- the quads -----------------------------------------------------------
    mesh.indices.reserve(mesh.indices.size() + ring.size() * 6u);
    for (uint32_t k = 0; k < static_cast<uint32_t>(ring.size()); ++k) {
        const uint32_t n = (k + 1u) % static_cast<uint32_t>(ring.size());
        const uint32_t top_a = ring[k];
        const uint32_t top_b = ring[n];
        const uint32_t low_a = base + k;
        const uint32_t low_b = base + n;
        mesh.indices.insert(mesh.indices.end(),
                            {top_a, top_b, low_a, top_b, low_b, low_a});
    }
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

ChunkMesh build_chunk(uint64_t seed, ChunkCoord coord, int lod) {
    ChunkMesh mesh;
    mesh.coord = coord;
    mesh.lod = lod < 0 ? 0 : (lod > kMaxChunkLod ? kMaxChunkLod : lod);

    const int verts = lod_verts(mesh.lod);
    const int quads = lod_quads(mesh.lod);
    const float kStep = lod_spacing_metres(mesh.lod);
    const glm::vec2 origin = chunk_origin(coord);

    mesh.vertices.reserve(static_cast<std::size_t>(verts) *
                          static_cast<std::size_t>(verts));

    for (int j = 0; j < verts; ++j) {
        for (int i = 0; i < verts; ++i) {
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
            //
            // At lod > 0 the stride is wider, but the coordinates are still
            // points of the SAME global lattice, so a coarse chunk and a fine
            // one agree bit for bit wherever they both have a vertex.
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

    mesh.indices.reserve(static_cast<std::size_t>(quads) *
                         static_cast<std::size_t>(quads) * 6u);
    for (int j = 0; j < quads; ++j) {
        for (int i = 0; i < quads; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * verts + i);
            const uint32_t b = a + 1u;
            const uint32_t c = a + static_cast<uint32_t>(verts);
            const uint32_t d = c + 1u;
            // Counter-clockwise when viewed from above (+Y), matching the
            // engine's front-face winding. See the quad-split note above.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    // Everything emitted so far is ground. The skirt is appended after it, and
    // this line is what lets collision tell the two apart by construction
    // rather than by inspecting a normal.
    mesh.surface_index_count = mesh.indices.size();

    append_skirt(mesh, verts);

    return mesh;
}

ChunkCollision build_chunk_collision(const ChunkMesh& mesh) {
    ChunkCollision out;
    out.coord = mesh.coord;
    out.bounds = mesh.bounds;

    // A coarsened mesh is the "downsampled copy" this engine forbids collision
    // from being derived from, and returning plausible triangles for one is how
    // a car ends up resting on a chord while the player watches it float over
    // the ridge that chord cuts across. Refuse, say so, hand back nothing.
    if (mesh.lod != 0) {
        AP_ERROR("chunk collision: refusing a lod %d mesh at (%d, %d). "
                 "Collision comes from the level 0 surface or it comes from "
                 "nowhere -- see the note on build_chunk_collision().",
                 mesh.lod, mesh.coord.x, mesh.coord.z);
        return out;
    }

    // Only the top surface. The skirt's faces are vertical, so their normals
    // are horizontal, and the +Y flip below would turn each one into a contact
    // plane the geometry does not have.
    const std::size_t tri_count = mesh.surface_index_count / 3u;
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

float mesh_height_at_lod(uint64_t seed, float x, float z, int lod) {
    const int l = lod < 0 ? 0 : (lod > kMaxChunkLod ? kMaxChunkLod : lod);
    const float s = lod_spacing_metres(l);
    const LatticeCell c = lattice_cell_at(x, z, s);

    // The same quad split as the mesher, at the coarse stride. It has to be the
    // same split: measuring the drawn surface against a different triangulation
    // would report a disagreement that is the measurement's own fault.
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
