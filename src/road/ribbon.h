#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "road/road_graph.h"
#include "terrain/chunk.h"  // TerrainVertex, CollisionTri

namespace apricot {

// The ribbon baker: a road graph in, plain vertex and index arrays out.
//
// ============================================================================
//  THIS FILE OWNS NO RESOURCE AND MUST NEVER LEARN HOW TO.
// ============================================================================
//
// probablecause's road network is a rendering object. It holds five uploaded
// meshes and five textures behind a render() method, which means its geometry
// and its hardware handles were born in the same class and have been
// inseparable ever since — and that, not the algorithms, is why a thousand
// lines of it could not come across.
//
// So the split is made here at birth. Everything below is a pure function from
// (graph, ground) to arrays of numbers. Uploading them, owning the handles and
// deleting them is the host layer's job and it happens on the other side of a
// plain-data boundary. If a type in this header ever grows a handle, a
// destructor that frees something, or a draw call, the mistake has been made
// again.

// CPU-side geometry, ready for the host layer to upload.
//
// This is structurally gfx::MeshData — the same three members in the same
// order, over the same TerrainVertex the whole engine uses. It is declared
// here rather than reused because MeshData lives in a host-side module and
// dependencies flow sim -> host and never back. Converting is a three-line
// move on the far side of the boundary, which is a fair price for the boundary.
struct RoadMesh {
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;

    bool empty() const { return indices.empty(); }
    std::size_t triangle_count() const { return indices.size() / 3; }
};

// One drawn material per layer. The split is by what the host layer has to
// bind, which is why the kerb risers are their own layer and not part of the
// sidewalk they belong to: the walk slabs are a horizontal surface you stand
// on and the risers are a vertical face you do not, and the collision pass
// below needs to tell them apart without guessing from a normal.
enum class RoadLayer : uint8_t {
    Carriageway = 0,  // paved asphalt base
    Unpaved = 1,      // packed earth, no markings
    Walk = 2,         // raised concrete sidewalk slabs
    Kerb = 3,         // the vertical faces closing those slabs
    Plate = 4,        // junction asphalt, no markings
    Crosswalk = 5,    // zebra bands across a junction approach
    WhiteMarking = 6, // edge lines and same-direction lane dividers
    YellowMarking = 7,// opposing-traffic centre lines
    Structure = 8,   // bridge undersides, parapets and piers
};

inline constexpr std::size_t kRoadLayerCount = 9;

inline constexpr std::size_t road_layer_index(RoadLayer l) {
    return static_cast<std::size_t>(l);
}

const char* road_layer_name(RoadLayer l);

// Everything one bake produced. Eight meshes, because eight materials; the host
// layer uploads each into its own Mesh and draws it with its own texture.
struct RibbonBake {
    struct Solid {
        glm::vec3 centre{0.0f};
        glm::vec3 half{0.0f};
        float yaw = 0.0f;
    };
    // Drawn and collided from the same boxes: parapets and bridge piers.
    std::vector<Solid> solids;
    RoadMesh layers[kRoadLayerCount];

    // Counts worth logging, and worth asserting on in a test.
    //
    // A PLATE IS NOT THE SAME THING AS A JUNCTION. Every real crossing gets
    // one, and so does every degree-2 node where the road actually CHANGES —
    // a sharp corner, or a step in width, surface or sidewalk. Those leave a
    // notch between two ribbons that do not line up, and a plate is what fills
    // it. A gentle bend between two identical roads gets a mitred cap instead
    // and is counted here as neither.
    std::size_t plates_baked = 0;

    // One zebra band per approach, and only at a crossing of three or more:
    // a bend is not a pedestrian crossing.
    std::size_t crosswalks_baked = 0;

    const RoadMesh& layer(RoadLayer l) const { return layers[road_layer_index(l)]; }
    RoadMesh& layer(RoadLayer l) { return layers[road_layer_index(l)]; }

    AABB bounds() const;
    std::size_t total_triangles() const;
};

struct RibbonParams {
    // Cross-section spacing along a centreline. Smaller follows the terrain
    // more closely and costs vertices linearly.
    float step_m = 4.0f;

    // How far past the widest incident half-width a ribbon is pulled back at a
    // junction, so approaches stop at the plate instead of piling into the
    // centre and z-fighting each other.
    float junction_margin_m = kRoadJunctionMarginM;

    // Depth of a zebra band along its approach.
    float crosswalk_depth_m = kRoadCrosswalkDepthM;

    // World metres per texture repeat: asphalt, one concrete slab, and the
    // span of one crosswalk bar pair.
    float uv_tile_m = 8.0f;
    float slab_m = 1.0f;
    float crosswalk_tile_m = 4.0f;

    // Road paint dimensions. Markings are separate curved ribbons instead of
    // being baked into the asphalt texture, so they stay aligned on bends and
    // custom-width roads. Dashes are measured in metres, not UV repeats.
    float marking_width_m = 0.15f;
    float marking_lift_m = 0.018f;
    float dash_length_m = 3.0f;
    float dash_gap_m = 6.0f;

    // A lane-connected ramp's authored endpoint is its traffic seam, not a
    // place where the visible asphalt should end. Carry the ribbon back into
    // the auxiliary deck so its square cap is buried under the freeway.
    float lane_connect_overlap_m = 40.0f;

    // Bottom of a kerb riser, relative to the terrain. Slightly NEGATIVE on
    // purpose: the foot sits a hair under the surface it meets so there is no
    // see-through seam at the kerb line.
    float kerb_foot_m = -0.02f;
};

// Bake the whole graph. Pure: same graph and same ground in, same arrays out,
// with no clock, no shared state and no allocation that outlives the call.
RibbonBake bake_ribbons(const RoadGraph& graph, const GroundSampler& ground,
                        const RibbonParams& params = RibbonParams{});

// ---------------------------------------------------------------------------
//  Collision
// ---------------------------------------------------------------------------

struct RoadCollisionTri {
    CollisionTri geom;
    RoadLayer layer;

    // What this triangle grips like, via road_surface(). There is no Asphalt
    // in terrain::Surface and adding one is not this module's change to make
    // — see the note on road_surface() in road_class.h.
    Surface material;
};

struct RoadCollision {
    std::vector<RoadCollisionTri> triangles;
    std::vector<RibbonBake::Solid> solids;
    AABB bounds;
};

// Collision for the drawn road surfaces.
//
// NOTE THE SIGNATURE, AND NOTE THAT IT IS THE SAME NOTE AS build_chunk_collision.
// It takes the BAKED RIBBON and not the graph, so it is structurally incapable
// of re-deriving the surface. The alternative — a second function that also
// takes the graph and works out where the asphalt went its own way, perhaps
// more coarsely "because collision does not need the detail" — drifts from the
// drawn mesh the moment either side is touched.
//
// That exact bug has been found twice in this repo already: heights and
// normals first, then materials, and both times the cause was a parallel
// implementation of something that already existed. This signature is what
// makes a third one impossible here rather than merely discouraged.
//
// Kerbs and paint layers are excluded, and that exclusion is by LAYER and not
// by testing a normal. Kerbs are vertical; paint is a visual skin slightly
// above the real road and must not make a tyre climb at every lane line.
RoadCollision build_road_collision(const RibbonBake& bake);

}  // namespace apricot
