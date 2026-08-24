#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "city/road_types.h"

namespace apricot {

// The AUTHORING road graph. The editor edits THIS — a true node/edge graph
// with first-class intersections — and everything else is a derived view:
// to_polylines() feeds the road ribbon mesh, and the same call adapted feeds
// the lane graph the traffic drives on.
//
// A road is an Edge between two Nodes with a RoadType, a carriageway width and
// a Shape (Straight / quadratic Bezier / Freeform). Dropping a node onto an
// existing edge mid-span SPLITS that edge, which is what makes a T or an X
// junction a real topological fact rather than two lines that happen to cross.
// Two polylines that merely overlap look identical on screen and are not a
// junction to anything that has to drive through one.
//
// Free of the host layer, the terrain and the lane graph, so it stays a pure
// data structure a headless suite can hammer.
//
// PROVENANCE. Lifted from probablecause (PENG-29). The .roadgraph FILE layer
// did not come with it — see the note at the bottom of road_author.cpp for
// why, and note that serialize()/deserialize() below are the whole graph in an
// in-memory blob, which is what a file layer would be built on.
using RoadNodeId = std::uint32_t;
using RoadEdgeId = std::uint32_t;
inline constexpr std::uint32_t kInvalidRoadId = 0xFFFFFFFFu;

class RoadGraphAuthor {
public:
    enum class Shape : std::uint8_t { Straight, Bezier, Freeform };

    struct Node {
        glm::vec2               pos{0.f};
        std::vector<RoadEdgeId> edges;   // incident edges (rebuilt, not persisted)
    };
    struct Edge {
        RoadNodeId             a = kInvalidRoadId;  // a == kInvalidRoadId => dead slot
        RoadNodeId             b = kInvalidRoadId;
        RoadType               type  = RoadType::Street;
        float                  width = 12.f;        // carriageway width (m)
        Shape                  shape = Shape::Straight;
        bool                   sidewalks = true;    // walkable strips + ped paths each side
        // Per-road spawn weighting (1 = baseline; >1 busier, 0 = none). Edited by
        // the Settings tool; the traffic / pedestrian spawners weight lane choice
        // by these so a road can be made quiet or bustling.
        float                  traffic_density = 1.f;
        float                  ped_density     = 1.f;
        std::vector<glm::vec2> controls;            // Bezier: 1 ctrl; Freeform: interior pts
    };
    // Tessellated edge: world-XZ centerline (endpoints included) + width/type.
    // Fed to the ribbon renderer and (adapted) to LaneGraph::RoadPath. `sidewalks`
    // carries through so the renderer bakes kerb strips and the ped-path producer
    // emits walkable lanes only for edges that opted in.
    struct Polyline {
        std::vector<glm::vec2> points;
        float                  width = 12.f;
        RoadType               type  = RoadType::Street;
        bool                   sidewalks = true;
        float                  traffic_density = 1.f;
        float                  ped_density     = 1.f;
    };

    // ---- Topology authoring ------------------------------------------------
    // Find-or-create a node within `merge_radius` of `pos`: snaps to an existing
    // node; else if `pos` lands on an existing edge it SPLITS that edge at the
    // projection and returns the new node; else appends a fresh node.
    RoadNodeId node_at(glm::vec2 pos, float merge_radius = 1.5f);
    RoadEdgeId add_edge(RoadNodeId a, RoadNodeId b, RoadType type, float width,
                        Shape shape, std::vector<glm::vec2> controls,
                        bool sidewalks = true);
    void       set_edge_type(RoadEdgeId e, RoadType type, float width);
    void       set_edge_density(RoadEdgeId e, float traffic_density, float ped_density);
    void       remove_edge(RoadEdgeId e);
    void       remove_node(RoadNodeId n);
    void       remove_last_edge();         // pop the most-recent edge (editor convenience)
    void       clear();

    // ---- Picking (snap / upgrade / bulldoze) -------------------------------
    RoadNodeId pick_node(glm::vec2 pos, float radius) const;     // kInvalidRoadId if none
    RoadEdgeId pick_edge(glm::vec2 pos, float radius,
                         glm::vec2& out_proj, float& out_t) const;

    // ---- Derivation --------------------------------------------------------
    std::vector<Polyline>  to_polylines(float step_m = 4.f) const;
    std::vector<glm::vec2> tessellate(RoadEdgeId e, float step_m = 4.f) const;

    // ---- Dirty flag --------------------------------------------------------
    // Every mutation above sets it. A file layer clears it after a successful
    // write; nothing here does, because nothing here writes.
    bool dirty() const { return dirty_; }
    void clear_dirty() const { dirty_ = false; }

    // ---- In-memory snapshot ------------------------------------------------
    // The whole graph as a compacted binary blob, and back. This is what undo
    // is built on, and it is also everything a file layer needs: the bytes
    // below are exactly what a .roadgraph contains.
    std::string serialize() const;
    bool        deserialize(const std::string& blob);

    // ---- Introspection -----------------------------------------------------
    std::size_t node_count() const { return nodes_.size(); }
    std::size_t edge_count() const { return edges_.size(); }
    std::size_t live_edge_count() const;
    const Node& node(RoadNodeId n) const { return nodes_[n]; }
    const Edge& edge(RoadEdgeId e) const { return edges_[e]; }
    bool        edge_alive(RoadEdgeId e) const { return edges_[e].a != kInvalidRoadId; }

private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    mutable bool      dirty_ = false;

    void       rebuild_adjacency();
    RoadNodeId split_edge_at(RoadEdgeId e, glm::vec2 pos);
    void       compact();                    // drop dead edges + orphan nodes, remap ids
    void       build_compacted(std::vector<Node>& out_nodes,
                               std::vector<Edge>& out_edges) const;
};

}  // namespace apricot
