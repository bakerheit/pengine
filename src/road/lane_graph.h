#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "road/road_graph.h"

namespace apricot {

// The lane graph. This is the surface traffic and police code against, so the
// header is written as a contract rather than as an implementation note.
//
// A lane is a directed strip of road with a centreline, an arc length, and a
// successor set. Everything a driving agent does is one of four questions:
//
//   where am I           -> nearest_lane() once, project_onto() every step
//   where should I be    -> pose()
//   where can I go next  -> outgoing() / choose_next() / plan_route()
//   may I go now         -> junction_control() / TurnLink::priority
//
// LaneRef is an index into one build and is NOT stable across a rebuild. Do
// not persist one. Lane::key IS stable — it is derived from the authored spine
// — and it is what per-agent entropy must be keyed on.

using LaneRef = uint32_t;
inline constexpr LaneRef kInvalidLane = 0xFFFFFFFFu;

enum class TurnKind : uint8_t {
    Straight = 0,
    Left,
    Right,
    UTurn,
};

const char* turn_kind_name(TurnKind k);

// How a junction is controlled, from the road hierarchy in pinatty.md §3.
enum class JunctionControl : uint8_t {
    None = 0,  // uncontrolled: proceed on priority
    Signal,    // traffic lights, two phases (see approach_group_a)
    Stop,      // all-way stop signs
};

const char* junction_control_name(JunctionControl c);

// Right of way for one movement. HIGHER WINS, and the comparison is the whole
// API: when two movements conflict, the lower rank gives way. Deciding WHICH
// movements conflict is the traffic system's job, not this module's — it needs
// to know about vehicle extents and reaction times, and this module knows
// about geometry.
enum class TurnPriority : uint8_t {
    Yield = 0,   // crosses opposing traffic, or reverses direction
    Minor = 1,   // arriving off a lesser road than the one it joins
    Normal = 2,  // a kerbside turn off the priority road
    Major = 3,   // the through movement on the priority road
};

constexpr int turn_priority_rank(TurnPriority p) { return static_cast<int>(p); }

struct TurnLink {
    LaneRef from = kInvalidLane;
    LaneRef to = kInvalidLane;
    uint32_t junction = 0;  // the LaneGraph junction index this crosses
    TurnKind kind = TurnKind::Straight;
    TurnPriority priority = TurnPriority::Normal;

    // Relative likelihood for a wandering driver. Straight dominates, U-turns
    // are a last resort. Never used for routing — plan_route ignores it.
    float weight = 1.0f;
};

struct Lane {
    // At least two points, already draped and already offset to the lane
    // centre. Travel runs front to back.
    std::vector<glm::vec3> centreline;

    // Cumulative arc length, same size as centreline, cum[0] == 0.
    std::vector<float> cum;
    float length_m = 0.0f;

    // LaneGraph junction indices at each end. These are ALSO RoadGraph node
    // indices — the two are 1:1 by construction, so a caller holding one can
    // look up the other with no remap table.
    uint32_t junction_from = 0;
    uint32_t junction_to = 0;

    // The RoadGraph edge this came from, and its stable authored key.
    uint32_t edge = 0;

    // STABLE IDENTITY. Survives a rebuild; survives reordering the spine
    // table. Per-agent entropy keys on this and never on the LaneRef:
    //   hash_coord3(map_seed, low32(key), high32(key), slot)
    // A key derived from a build index makes an agent's behaviour depend on
    // how many lanes happened to be built before it.
    uint64_t key = 0;

    RoadClass cls = RoadClass::Street;

    // 0 is the lane nearest the road centreline; lanes_per_dir - 1 is the
    // kerbside one.
    uint8_t index = 0;

    // True when travel runs along the RoadEdge's own point order (node_a to
    // node_b), false for the returning lane.
    bool forward = true;

    // SIGNED lateral offset of this lane from the road centreline, in the
    // lane's own frame: positive is to the right of travel. The opposing lane
    // of the same road sits at -2 * lateral_offset_m in this lane's frame,
    // which is the number an overtake or a go-around aims at. It is stored
    // rather than recomputed because the reference shipped a global 2 m
    // constant for it, which under-shot every road wider than 8 m and left the
    // overtake scan looking at empty tarmac.
    float lateral_offset_m = 0.0f;

    float width_m = 0.0f;  // the road's full carriageway width
    float speed_limit_mps = 0.0f;

    // Carried straight off the authored spine. Nothing here interprets them.
    float traffic_density = 1.0f;
    float ped_density = 1.0f;
    uint8_t block_quality = 128;
};

// Where a car at lane-distance d should be. Returned whole rather than as a
// position alone, because every caller that gets only a position immediately
// re-derives the heading from two samples — and does it slightly differently
// each time.
struct LanePose {
    glm::vec3 position{0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};  // unit, direction of travel
    glm::vec3 right{0.0f, 0.0f, -1.0f};   // unit, cross(up, tangent)
};

// Where a car IS on the network.
struct LaneProjection {
    LaneRef lane = kInvalidLane;
    float dist_along_m = 0.0f;

    // Signed offset from the centreline, same sign convention as
    // LanePose::right: positive is to the right of travel. This is the number
    // that says "you have drifted into oncoming".
    float lateral_m = 0.0f;

    // Squared planar distance from the query point to the centreline.
    float dist2 = 0.0f;

    bool valid() const { return lane != kInvalidLane; }
};

struct LaneJunction {
    // 1:1 with RoadGraph nodes, so junction index == node index.
    glm::vec3 pos{0.0f};
    std::vector<LaneRef> incoming;
    std::vector<LaneRef> outgoing;
    uint32_t degree = 0;  // incident ROADS, not lanes
    JunctionControl control = JunctionControl::None;
};

struct LaneBuildParams {
    // Whole-world handedness. Mirrors every directed lane across the road
    // centreline and swaps which turn crosses oncoming traffic.
    bool drive_on_right = true;

    // Cell size for the nearest-lane index. Bigger cells mean fewer cells and
    // longer candidate lists; the default is roughly a city block.
    float index_cell_m = 32.0f;
};

class LaneGraph {
public:
    // Emit lanes_per_dir lanes in each direction for every edge of the graph,
    // draped on the same surface the ribbon was baked on, and link them across
    // every node. Deterministic in the graph and the parameters.
    void build(const RoadGraph& graph, const GroundSampler& ground,
               const LaneBuildParams& params = LaneBuildParams{});

    void clear();

    // --- lanes ------------------------------------------------------------
    std::size_t lane_count() const { return lanes_.size(); }
    const Lane& lane(LaneRef r) const { return lanes_[r]; }
    const std::vector<Lane>& lanes() const { return lanes_; }
    bool valid(LaneRef r) const { return r < lanes_.size(); }
    float length(LaneRef r) const { return lanes_[r].length_m; }

    // --- THE TWO EVERYTHING ELSE IS BUILT ON ------------------------------

    // Pose at arc-length `d` along lane `r`, clamped to [0, length]. Clamped
    // and not wrapped: running off the end of a lane is a routing bug, and a
    // silent wrap turns it into a car that teleports to the start of the
    // street it was leaving.
    LanePose pose(LaneRef r, float d) const;

    // The same, shifted `lateral_m` along the pose's right vector. Use this
    // for an overtake target or a parked offset; do not add the offset by hand
    // afterwards, because the right vector rotates through a bend.
    LanePose pose(LaneRef r, float d, float lateral_m) const;

    // Project a world XZ point onto a KNOWN lane. This is the per-step call:
    // a car that already knows its lane never searches.
    LaneProjection project_onto(LaneRef r, glm::vec2 xz) const;

    // Find the lane a point is on, with no prior. This is the cold-start call
    // — spawning, re-snapping after a rebuild, working out which lane the
    // player's car is on for a roadblock. O(lanes near the point), not O(all
    // lanes), via a uniform index built at build() time.
    LaneProjection nearest_lane(glm::vec2 xz, float max_radius_m = 60.0f) const;

    // The same, but rejecting lanes running against `heading`. Without this a
    // car sitting on the centre line snaps to whichever of the two opposing
    // lanes happens to be a millimetre closer, and drives away backwards.
    LaneProjection nearest_lane_along(glm::vec2 xz, glm::vec2 heading,
                                      float max_radius_m = 60.0f) const;

    // --- adjacency --------------------------------------------------------
    const std::vector<TurnLink>& outgoing(LaneRef r) const;

    // A wandering driver's next lane, weighted by TurnLink::weight.
    //
    // NO GENERATOR STATE. The choice is a pure function of the lane's stable
    // key, the map seed and which decision this is for that agent, so two runs
    // of the same seed take the same turns and an agent's behaviour does not
    // depend on how many other agents drew before it. That last property is
    // the one a shared stream destroys, and it is why the reference's
    // std::mt19937 could not come across.
    LaneRef choose_next(LaneRef r, uint64_t seed, uint32_t decision_index) const;

    // Least-travel lane chain from `from` to `to`, inclusive of both. A* over
    // the turn links; cost is the length of the lane being left, and the
    // heuristic is the planar distance between lane start points, which is a
    // lower bound on the road distance and therefore admissible.
    //
    // Returns {from} when from == to, and EMPTY when no route exists or a
    // handle is invalid. An empty result is a real answer — the target is on a
    // disconnected piece of the network — and a caller that treats it as an
    // error stalls the pursuit instead of falling back to a direct steer.
    std::vector<LaneRef> plan_route(LaneRef from, LaneRef to) const;

    // --- junctions --------------------------------------------------------
    std::size_t junction_count() const { return junctions_.size(); }
    const LaneJunction& junction(uint32_t i) const { return junctions_[i]; }
    JunctionControl junction_control(uint32_t i) const {
        return junctions_[i].control;
    }

    // Which half of a two-phase signal cycle an approach belongs to.
    // Approaches are clustered by road AXIS (heading folded to a half circle)
    // against the junction's reference approach: within 45 degrees is group A,
    // the crossing approaches are group B. So a street's two ends share a
    // green and the crossing street gets the other half, at any junction
    // angle rather than only at world-axis-aligned ones. The AI and the signal
    // heads must both call this, or the bulb disagrees with the stop decision.
    bool approach_group_a(uint32_t junction, LaneRef incoming) const;

    // --- same-road neighbours ---------------------------------------------
    // The oncoming lane of the same road at the mirrored index, or
    // kInvalidLane. This is the lane an overtake borrows.
    LaneRef opposing(LaneRef r) const;

    // The next lane outboard (delta +1, toward the kerb) or inboard (-1) in
    // the same direction, or kInvalidLane at the edge of the carriageway.
    LaneRef neighbour(LaneRef r, int delta) const;

    // Every lane of one road edge, forward lanes then reverse lanes.
    std::vector<LaneRef> lanes_of_edge(uint32_t edge) const;

private:
    struct EdgeLanes {
        uint32_t base = 0;      // first LaneRef of this edge
        uint8_t per_dir = 0;    // lanes in each direction
    };

    LaneRef add_lane(Lane&& lane);
    void link_junctions(const RoadGraph& graph, bool drive_on_right);
    void build_index(float cell_m);
    void gather_candidates(glm::vec2 xz, float radius_m,
                           std::vector<LaneRef>& out) const;

    std::vector<Lane> lanes_;
    std::vector<LaneJunction> junctions_;
    std::vector<std::vector<TurnLink>> out_links_;
    std::vector<EdgeLanes> edge_lanes_;

    // Uniform grid over lane geometry, for nearest_lane. std::map so no
    // output ever depends on hash iteration order.
    std::map<int64_t, std::vector<LaneRef>> index_;
    float index_cell_m_ = 32.0f;

    static const std::vector<TurnLink> kNoLinks;
};

}  // namespace apricot
