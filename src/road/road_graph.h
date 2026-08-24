#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "road/road_class.h"

namespace apricot {

// The road network as a planar graph: nodes, edges, and the junctions where
// spines meet. Sim-side, pure, and it owns no resource of any kind.
//
// IT IS CALLED RoadGraph AND NOT RoadNetwork ON PURPOSE. probablecause has a
// `RoadNetwork` and it is a rendering object — it holds five uploaded meshes,
// five textures and a render() method, which is precisely why none of its 1038
// lines could come across and this had to be rewritten instead of ported. The
// name is left behind with the design. What is here is a graph; what draws is
// a separate bake (ribbon.h) that emits plain vertex arrays and hands them to
// the host layer to own.

// ---------------------------------------------------------------------------
//  Ground sampling
// ---------------------------------------------------------------------------

// How the road module asks "how high is the ground here".
//
// It is a parameter and not a call to the height field, because the answer has
// to be the DRAWN terrain and not the underlying continuous field: those are
// different numbers by centimetres on a grade, and a ribbon draped on the field
// while the terrain draws the mesh is a road that sinks into the hill it is
// visibly lying on. terrain_ground() below binds the right one. Tests bind a
// flat plane, which is also why this is a parameter.
struct GroundSampler {
    using Fn = float (*)(const void* ctx, float x, float z);
    Fn fn = nullptr;
    const void* ctx = nullptr;

    // A null sampler is a flat world at y = 0. That is a legitimate answer for
    // a test, and it is never silently wrong in the field because the real
    // caller has to pass terrain_ground() to get anything else.
    float at(float x, float z) const { return fn ? fn(ctx, x, z) : 0.0f; }
};

// Binds the sampler to the meshed terrain surface for one seed. Hold it for as
// long as the sampler is in use — sampler() captures `this`.
struct TerrainGround {
    uint64_t seed = 0;
    GroundSampler sampler() const { return GroundSampler{&sample, this}; }

private:
    static float sample(const void* ctx, float x, float z);
};

// ---------------------------------------------------------------------------
//  Input: spines
// ---------------------------------------------------------------------------

// How a road relates to the ground it crosses. Only the vertical consequence
// is implemented here; the terrain side of Cut/Fill is a terrain-operator job
// and belongs to the map module, not to this one.
enum class RoadStructure : uint8_t {
    Ground = 0,  // drapes onto the terrain.
    Bridge = 1,  // authored deck height, flat. NOT draped.
    Tunnel = 2,  // authored deck height, flat, under the terrain.

    // Treated as Ground here. The terrain operator that flattens the corridor
    // is the map module's, and once it lands the drape lands on the carved
    // ground for free — which is why these are separate values rather than an
    // alias: the ribbon must not carve its own copy of the terrain.
    Cut = 3,
    Fill = 4,
};

constexpr bool road_structure_is_decked(RoadStructure s) {
    return s == RoadStructure::Bridge || s == RoadStructure::Tunnel;
}

// One authored road, as the map hands it over.
//
// THIS IS THE SEAM WITH THE MAP MODULE. PENG-41 owns src/city/ and the spine
// tables; this module takes spines as a parameter and knows nothing about
// where they came from. When the tables land, wiring is one call:
// RoadGraph::build(map_spines(), params).
struct RoadSpine {
    // World XZ control points, at least two, already tessellated to whatever
    // resolution the author wanted. Arcs and bulges are resolved upstream: a
    // spine that reaches this module is a polyline.
    std::vector<glm::vec2> points;

    RoadClass cls = RoadClass::Street;
    RoadStructure structure = RoadStructure::Ground;

    // Deck height for Bridge / Tunnel, in metres. Ignored for Ground.
    // Authored and never draped: a bridge that follows the terrain is a
    // causeway, and the channel under the Kessel Bridge is 26 m of carve that
    // a drape would happily drive into.
    float deck_y_m = 0.0f;

    // Carriageway width override, metres. <= 0 means "use the class table".
    // A set value is used EXACTLY as given and is never snapped toward the
    // class width — see the PCG-170 note in road_class.h.
    float width_m = 0.0f;

    // Roadblock staging quality, straight from the authored edge. 0 means
    // never stage here (a tunnel, a blind junction); 255 means this is what
    // this road is for. Carried through the graph untouched so police can read
    // it off an edge later; nothing in this module interprets it.
    uint8_t block_quality = 128;

    // Population scalars from the district the spine runs through, carried
    // onto every lane. 1.0 is baseline.
    float traffic_density = 1.0f;
    float ped_density = 1.0f;

    // Stable authored identity. Entropy keyed to a road must key on THIS and
    // never on the spine's index in the input vector, which changes the moment
    // somebody inserts a road above it in the table.
    uint32_t id = 0;
};

// ---------------------------------------------------------------------------
//  Output: nodes and edges
// ---------------------------------------------------------------------------

enum class NodeKind : uint8_t {
    DeadEnd = 0,   // degree 1
    Continuation,  // degree 2 — a bend or a class change, not a crossing
    Junction,      // degree 3+ — a real crossing
};

struct RoadNode {
    glm::vec2 pos{0.0f};
    float y_m = 0.0f;  // ground (or deck) height at pos
    NodeKind kind = NodeKind::DeadEnd;

    // Incident edge indices, in build order. An edge that begins and ends here
    // (a loop) appears twice, so edges.size() is the degree.
    std::vector<uint32_t> edges;
};

struct RoadEdge {
    uint32_t node_a = 0;
    uint32_t node_b = 0;

    // Polyline from node_a to node_b inclusive, world XZ. Always >= 2 points,
    // and points.front()/back() sit exactly on the two nodes' positions.
    std::vector<glm::vec2> points;

    RoadClass cls = RoadClass::Street;
    RoadStructure structure = RoadStructure::Ground;
    float deck_y_m = 0.0f;
    float width_m = 0.0f;  // resolved: the override if set, else the class width
    uint8_t block_quality = 128;
    float traffic_density = 1.0f;
    float ped_density = 1.0f;

    // Planar length of `points`, metres.
    float length_m = 0.0f;

    // Which authored spine this came from (RoadSpine::id), and which run of
    // that spine — a spine cut by two junctions yields three edges numbered
    // 0, 1, 2. Together they are the edge's STABLE IDENTITY: they survive
    // reordering the spine table and they survive another spine being added
    // that splits this one, as long as the author did not move this road.
    uint32_t spine_id = 0;
    uint32_t spine_run = 0;

    // The 64-bit key everything downstream should hash on. See pinatty §6:
    // every generated thing keys on a stable authored identity, never on an
    // iteration order.
    uint64_t key() const {
        return (static_cast<uint64_t>(spine_id) << 32) |
               static_cast<uint64_t>(spine_run);
    }

    bool sidewalks() const { return road_class_def(cls).sidewalks; }
    float half_width_m() const { return width_m * 0.5f; }
};

// Where a road's DRAWN surface sits above a point, for one edge.
//
// One answer to that question, shared by the ribbon baker and the lane graph.
// Two answers is how the lane a car follows ends up somewhere the asphalt is
// not — and on a bridge it is not a subtle miss, it is 26 m.
struct RoadSurface {
    const GroundSampler* ground = nullptr;
    bool decked = false;
    float deck_y_m = 0.0f;

    float at(glm::vec2 p) const {
        if (decked) return deck_y_m;
        return ground ? ground->at(p.x, p.y) : 0.0f;
    }

    static RoadSurface of(const RoadEdge& e, const GroundSampler& g) {
        return RoadSurface{&g, road_structure_is_decked(e.structure), e.deck_y_m};
    }
};

struct RoadGraphParams {
    // Two positions within this distance are the same node. Also the tolerance
    // for "this spine's endpoint lands ON that spine", which is what turns a
    // street running into an arterial into a T junction instead of two roads
    // that quietly pass through each other.
    float weld_tolerance_m = 1.0f;

    // Interior crossings of two spines are split into a shared node. Turn this
    // off only if the map guarantees every crossing is authored as a shared
    // node; leaving it on for an already-noded map is harmless (no crossing is
    // found) and leaving it off for one that is not produces roads that
    // overlap without connecting, which reads in game as traffic driving
    // through each other at full speed.
    bool split_crossings = true;
};

class RoadGraph {
public:
    // Build from scratch. Deterministic: the same spines in the same order
    // always produce the same nodes, edges and indices, and nothing in here
    // iterates a hash container to decide an output.
    void build(const std::vector<RoadSpine>& spines,
               const RoadGraphParams& params, const GroundSampler& ground);

    void clear();

    std::size_t node_count() const { return nodes_.size(); }
    std::size_t edge_count() const { return edges_.size(); }
    const RoadNode& node(uint32_t i) const { return nodes_[i]; }
    const RoadEdge& edge(uint32_t i) const { return edges_[i]; }
    const std::vector<RoadNode>& nodes() const { return nodes_; }
    const std::vector<RoadEdge>& edges() const { return edges_; }

    // Nodes where three or more edges meet. This is the list the ribbon baker
    // fills with plates and crosswalks and the lane graph hangs turns off.
    const std::vector<uint32_t>& junctions() const { return junctions_; }

    // Widest half carriageway of any edge incident to `n`, plus the caller's
    // margin. The ribbon baker pulls every approach back by this so ribbons
    // stop at the junction instead of piling into its centre.
    float junction_trim_m(uint32_t n, float margin_m) const;

    // Highest class meeting at `n` (Freeway beats Arterial beats Street...).
    // Returns Dirt for an isolated node.
    RoadClass dominant_class(uint32_t n) const;

    // World-space bounds of every edge polyline, at ground height.
    const AABB& bounds() const { return bounds_; }

private:
    std::vector<RoadNode> nodes_;
    std::vector<RoadEdge> edges_;
    std::vector<uint32_t> junctions_;
    AABB bounds_;
};

}  // namespace apricot
