#include "terrain/chunk.h"

#include <cmath>

#include "terrain/heightmap.h"

namespace apricot {

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

    constexpr int kSpan = kChunkVerts - 1;
    constexpr float kStep = kChunkMetres / static_cast<float>(kSpan);
    const glm::vec2 origin = chunk_origin(coord);

    mesh.vertices.reserve(static_cast<std::size_t>(kChunkVerts) *
                          static_cast<std::size_t>(kChunkVerts));

    for (int j = 0; j < kChunkVerts; ++j) {
        for (int i = 0; i < kChunkVerts; ++i) {
            // Evaluated at the absolute WORLD coordinate, not a chunk-local
            // one. That is what makes a shared edge bit-identical between
            // neighbours: both chunks ask height_at() the same question.
            const float wx = origin.x + static_cast<float>(i) * kStep;
            const float wz = origin.y + static_cast<float>(j) * kStep;

            TerrainVertex v;
            v.position = glm::vec3{wx, height_at(seed, wx, wz), wz};
            v.normal = normal_at(seed, wx, wz);
            v.uv = glm::vec2{wx, wz} * (1.0f / kChunkMetres);

            mesh.bounds.expand(v.position);
            mesh.vertices.push_back(v);
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(kSpan) *
                         static_cast<std::size_t>(kSpan) * 6u);
    for (int j = 0; j < kSpan; ++j) {
        for (int i = 0; i < kSpan; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * kChunkVerts + i);
            const uint32_t b = a + 1u;
            const uint32_t c = a + static_cast<uint32_t>(kChunkVerts);
            const uint32_t d = c + 1u;
            // Counter-clockwise when viewed from above (+Y), matching the
            // engine's front-face winding.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    return mesh;
}

}  // namespace apricot
