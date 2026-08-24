#pragma once

#include <cstdint>

#include "gfx/renderer.h"
#include "road/ribbon.h"
#include "scene/scene.h"

namespace apricot {

// The host-side owner of a baked road ribbon.
//
// src/road/ bakes six RoadMesh — plain vertices, indices and bounds over the
// engine's TerrainVertex — and deliberately stops there. Its own header says
// why, at length: probablecause's road network held five uploaded meshes, five
// textures and a render() method, so its geometry and its hardware handles were
// born in the same class and none of its thousand lines could be ported. The
// seam was cut at birth this time. THIS FILE IS THE FAR SIDE OF THAT SEAM and
// it is the only place a road becomes a GPU resource.
//
// It is small, and that is the point. If this file ever grows a bake, a graph,
// a width or a lane, the seam has been crossed from the wrong side.
class RoadMeshes {
public:
    // Procedural materials for the six layers. Call once, after the renderer.
    bool init(Renderer& renderer, uint64_t seed);

    // Upload a bake, replacing whatever was uploaded before.
    //
    // Empty layers are skipped rather than uploaded as empty meshes: a bake
    // with no crosswalks is normal, and Mesh::upload refuses empty geometry
    // loudly and correctly. Returns false if a non-empty layer failed to
    // upload, having already released the rest — a half-uploaded road is worse
    // than none, because the missing half is the half you do not notice.
    bool upload(Renderer& renderer, const RibbonBake& bake);

    // Create one scene node per uploaded layer.
    //
    // Ribbon vertices are ABSOLUTE WORLD COORDINATES, exactly like chunk
    // vertices, so the node transform is identity and the bounds go in as
    // given. Anything else would double-apply the position the baker already
    // put in the vertices.
    //
    // `draw_distance` is not decoration and it is not a performance knob. Road
    // ribbons are draped onto the LEVEL 0 drawn surface via mesh_height_at();
    // past the level 0/1 rings the terrain under them is drawn coarser and the
    // two stop being the same surface. Measured on this terrain
    // (tests/terrain_lod_tests.cpp): level 1 disagrees with level 0 by 0.025 m
    // on average and 1.22 m at worst, level 3 by 0.258 m and 6.13 m. So a road
    // drawn out at the level 3 ring floats or sinks by metres. The caller
    // passes the ring radius it is willing to stand behind.
    void attach(Scene& scene, float draw_distance);

    // Remove the nodes. Does not free the meshes; call release() for that.
    void detach(Scene& scene);

    // Free every uploaded mesh. The caller must have detached first — the same
    // ordering the streamer obeys, for the same reason.
    void release(Renderer& renderer);

    bool has_geometry() const { return uploaded_layers_ > 0; }
    std::size_t layer_count() const { return uploaded_layers_; }
    std::size_t triangle_count() const { return triangles_; }
    std::size_t gpu_bytes() const { return bytes_; }

private:
    struct Layer {
        MeshId mesh = kInvalidId;
        MaterialId material = kInvalidId;
        NodeId node = kInvalidId;
        AABB bounds;
    };

    Layer layers_[kRoadLayerCount];
    std::size_t uploaded_layers_ = 0;
    std::size_t triangles_ = 0;
    std::size_t bytes_ = 0;
};

}  // namespace apricot
