// The lane graph — the contract traffic and police will be written against.
//
// Two of these matter more than the rest and are worth naming: pose() and
// project_onto() must be exact inverses, because a follower that disagrees
// with a projector by half a metre steers into the kerb; and choose_next()
// must be a pure function of a STABLE lane identity, because the reference's
// shared std::mt19937 made every agent's behaviour depend on how many other
// agents had drawn before it, which is the one thing streaming takes away.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "road/lane_graph.h"
#include "road_fixture.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

struct Net {
    RoadGraph graph;
    LaneGraph lanes;
};

Net build(const GroundSampler& ground = GroundSampler{},
          bool drive_on_right = true) {
    Net n;
    n.graph.build(make_test_spines(), RoadGraphParams{}, ground);
    LaneBuildParams p;
    p.drive_on_right = drive_on_right;
    n.lanes.build(n.graph, ground, p);
    return n;
}

uint32_t node_at(const RoadGraph& g, glm::vec2 p) {
    for (uint32_t i = 0; i < g.node_count(); ++i)
        if (glm::length(g.node(i).pos - p) < 0.5f) return i;
    return 0xFFFFFFFFu;
}

// A lane belonging to a given authored spine, travelling in +tangent order.
LaneRef lane_of_spine(const Net& n, uint32_t spine_id, bool forward) {
    for (LaneRef r = 0; r < n.lanes.lane_count(); ++r) {
        const Lane& l = n.lanes.lane(r);
        if (n.graph.edge(l.edge).spine_id == spine_id && l.forward == forward &&
            l.index == 0)
            return r;
    }
    return kInvalidLane;
}

glm::vec2 xz(glm::vec3 p) { return glm::vec2{p.x, p.z}; }

void test_lane_counts_follow_the_hierarchy() {
    const Net n = build();
    std::size_t expect = 0;
    for (const RoadEdge& e : n.graph.edges())
        expect += static_cast<std::size_t>(2 * road_class_def(e.cls).lanes_per_dir);
    REQUIRE(n.lanes.lane_count() == expect);
    REQUIRE(n.lanes.lane_count() == 34);
    REQUIRE(n.lanes.junction_count() == n.graph.node_count());

    // Freeway edges carry three lanes each way; a street carries one.
    for (const RoadEdge& e : n.graph.edges()) {
        const std::vector<LaneRef> ls = n.lanes.lanes_of_edge(0);
        (void)ls;
        if (e.cls != RoadClass::Freeway) continue;
        std::size_t on_edge = 0;
        for (LaneRef r = 0; r < n.lanes.lane_count(); ++r)
            if (n.graph.edge(n.lanes.lane(r).edge).spine_id == e.spine_id) ++on_edge;
        REQUIRE(on_edge == 6);
    }
    std::printf("      %zu lanes over %zu edges\n", n.lanes.lane_count(),
                n.graph.edge_count());
    pass("lane count is 2 x lanes_per_dir per edge, straight off the table");
}

void test_pose_and_project_onto_are_inverses() {
    const Net n = build();
    std::size_t samples = 0;
    double worst_d = 0.0;
    double worst_lat = 0.0;
    for (LaneRef r = 0; r < n.lanes.lane_count(); ++r) {
        const float len = n.lanes.length(r);
        for (int k = 0; k <= 10; ++k) {
            const float d = len * static_cast<float>(k) / 10.0f;
            const LanePose p = n.lanes.pose(r, d);
            const LaneProjection q = n.lanes.project_onto(r, xz(p.position));
            REQUIRE(q.valid());
            worst_d = std::max(worst_d, std::fabs(static_cast<double>(q.dist_along_m - d)));
            worst_lat = std::max(worst_lat, std::fabs(static_cast<double>(q.lateral_m)));
            ++samples;
        }
    }
    REQUIRE(samples > 300);
    REQUIRE_MSG(worst_d < 1e-2, "pose -> project_onto must return the same arc length", "d");
    REQUIRE_MSG(worst_lat < 1e-3, "a pose on the centreline has zero lateral", "lat");
    std::printf("      %zu round trips, worst arc error %.6f m, worst lateral %.6f m\n",
                samples, worst_d, worst_lat);
    pass("pose() and project_onto() are inverses on every lane");
}

void test_lateral_sign_is_one_convention() {
    // If pose()'s right and project_onto()'s lateral disagree in sign, an
    // overtake steers into the traffic it was avoiding.
    const Net n = build();
    const LaneRef r = lane_of_spine(n, 1, true);
    REQUIRE(r != kInvalidLane);
    const float mid = n.lanes.length(r) * 0.5f;
    for (float off : {-3.0f, -1.0f, 1.0f, 3.0f}) {
        const LanePose p = n.lanes.pose(r, mid, off);
        const LaneProjection q = n.lanes.project_onto(r, xz(p.position));
        REQUIRE_NEAR(q.lateral_m, off, 1e-3f);
    }

    // And the right vector really is cross(up, tangent).
    const LanePose p = n.lanes.pose(r, mid);
    const glm::vec3 want = glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, p.tangent);
    REQUIRE_NEAR(glm::length(p.right - want), 0.0f, 1e-5f);
    REQUIRE_NEAR(glm::length(p.tangent), 1.0f, 1e-5f);
    pass("one lateral sign convention across pose(), project_onto() and right");
}

void test_pose_clamps_and_never_wraps() {
    const Net n = build();
    const LaneRef r = lane_of_spine(n, 1, true);
    const float len = n.lanes.length(r);
    const LanePose end = n.lanes.pose(r, len);
    const LanePose past = n.lanes.pose(r, len + 500.0f);
    REQUIRE_NEAR(glm::length(past.position - end.position), 0.0f, 1e-5f);
    const LanePose start = n.lanes.pose(r, 0.0f);
    const LanePose before = n.lanes.pose(r, -50.0f);
    REQUIRE_NEAR(glm::length(before.position - start.position), 0.0f, 1e-5f);
    pass("pose() clamps past both ends instead of wrapping");
}

void test_opposing_and_neighbour() {
    const Net n = build();
    const LaneRef r = lane_of_spine(n, 1, true);  // arterial, inner lane
    REQUIRE(r != kInvalidLane);
    const Lane& l = n.lanes.lane(r);
    REQUIRE_NEAR(l.lateral_offset_m, 2.75f, 1e-5f);  // 22 m / 2, inner of two

    const LaneRef opp = n.lanes.opposing(r);
    REQUIRE(opp != kInvalidLane);
    REQUIRE(n.lanes.lane(opp).forward != l.forward);
    REQUIRE(n.lanes.lane(opp).index == l.index);
    REQUIRE(n.lanes.opposing(opp) == r);

    // The header promises the oncoming lane sits at -2 * lateral_offset_m in
    // THIS lane's frame. Measure it rather than trusting the arithmetic.
    const LanePose mid = n.lanes.pose(r, n.lanes.length(r) * 0.5f);
    const LaneProjection q =
        n.lanes.project_onto(r, xz(n.lanes.pose(opp, n.lanes.length(opp) * 0.5f).position));
    REQUIRE_NEAR(q.lateral_m, -2.0f * l.lateral_offset_m, 1e-2f);
    (void)mid;

    const LaneRef outer = n.lanes.neighbour(r, 1);
    REQUIRE(outer != kInvalidLane);
    REQUIRE(n.lanes.lane(outer).index == 1);
    REQUIRE(n.lanes.lane(outer).forward == l.forward);
    REQUIRE(n.lanes.neighbour(outer, 1) == kInvalidLane);  // off the carriageway
    REQUIRE(n.lanes.neighbour(r, -1) == kInvalidLane);
    pass("opposing() is the oncoming lane at -2x the offset; neighbour() stops at the kerb");
}

void test_lane_rides_the_carriageway_surface() {
    const uint64_t seed = 0xDEADBEEFull;
    TerrainGround tg{seed};
    const Net n = build(tg.sampler());
    std::size_t checked = 0;
    double worst = 0.0;
    for (LaneRef r = 0; r < n.lanes.lane_count(); ++r) {
        const RoadEdge& e = n.graph.edge(n.lanes.lane(r).edge);
        if (road_structure_is_decked(e.structure)) {
            for (glm::vec3 p : n.lanes.lane(r).centreline)
                REQUIRE_NEAR(p.y, 26.0f + kDrapeEpsM, 1e-4f);
            continue;
        }
        for (glm::vec3 p : n.lanes.lane(r).centreline) {
            const float want = mesh_height_at(seed, p.x, p.z) + kDrapeEpsM;
            worst = std::max(worst, std::fabs(static_cast<double>(p.y - want)));
            ++checked;
        }
    }
    REQUIRE(checked > 50);
    REQUIRE_MSG(worst < 1e-4, "a lane must ride the drawn carriageway", "lane y");
    pass("lanes ride the same drawn surface the ribbon baked");
}

void test_junction_control_follows_the_hierarchy() {
    const Net n = build();
    const uint32_t four_way = node_at(n.graph, {0.0f, 0.0f});
    const uint32_t t_street = node_at(n.graph, {-150.0f, 0.0f});
    const uint32_t t_dirt = node_at(n.graph, {150.0f, 0.0f});
    const uint32_t bend = node_at(n.graph, {0.0f, 200.0f});

    REQUIRE(n.lanes.junction_control(four_way) == JunctionControl::Signal);
    REQUIRE(n.lanes.junction_control(t_street) == JunctionControl::Signal);
    // Dirt takes stop signs, and the stop wins even against an arterial.
    REQUIRE(n.lanes.junction_control(t_dirt) == JunctionControl::Stop);
    REQUIRE(n.lanes.junction_control(bend) == JunctionControl::None);

    // A four-way of plain streets is signalled; a T of plain streets is not.
    {
        std::vector<RoadSpine> s;
        RoadSpine a;
        a.id = 1;
        a.cls = RoadClass::Street;
        a.points = {{-100.0f, 0.0f}, {100.0f, 0.0f}};
        s.push_back(a);
        RoadSpine b;
        b.id = 2;
        b.cls = RoadClass::Street;
        b.points = {{0.0f, 0.0f}, {0.0f, 100.0f}};  // T
        s.push_back(b);
        Net t;
        t.graph.build(s, RoadGraphParams{}, GroundSampler{});
        t.lanes.build(t.graph, GroundSampler{});
        const uint32_t j = node_at(t.graph, {0.0f, 0.0f});
        REQUIRE(t.graph.node(j).edges.size() == 3);
        REQUIRE(t.lanes.junction_control(j) == JunctionControl::None);

        s[1].points = {{0.0f, -100.0f}, {0.0f, 100.0f}};  // now a 4-way
        Net f;
        f.graph.build(s, RoadGraphParams{}, GroundSampler{});
        f.lanes.build(f.graph, GroundSampler{});
        const uint32_t k = node_at(f.graph, {0.0f, 0.0f});
        REQUIRE(f.graph.node(k).edges.size() == 4);
        REQUIRE(f.lanes.junction_control(k) == JunctionControl::Signal);
    }
    pass("signal / stop / none follow the class table, not the degree alone");
}

void test_turn_kinds_and_priorities() {
    const Net n = build();
    const uint32_t j = node_at(n.graph, {0.0f, 0.0f});
    const LaneJunction& jn = n.lanes.junction(j);
    REQUIRE(jn.degree == 4);

    bool saw_major = false;
    bool saw_yield = false;
    bool saw_normal = false;
    bool saw_minor = false;
    for (LaneRef in_r : jn.incoming) {
        const Lane& in_l = n.lanes.lane(in_r);
        for (const TurnLink& t : n.lanes.outgoing(in_r)) {
            if (t.junction != j) continue;
            if (in_l.cls == RoadClass::Arterial) {
                if (t.kind == TurnKind::Straight) {
                    REQUIRE(t.priority == TurnPriority::Major);
                    saw_major = true;
                } else if (t.kind == TurnKind::Left) {
                    REQUIRE(t.priority == TurnPriority::Yield);
                    saw_yield = true;
                } else if (t.kind == TurnKind::Right) {
                    REQUIRE(t.priority == TurnPriority::Normal);
                    saw_normal = true;
                }
            } else if (t.kind != TurnKind::Left && t.kind != TurnKind::UTurn) {
                // A street arriving at an arterial gives way, whatever it does.
                REQUIRE(t.priority == TurnPriority::Minor);
                saw_minor = true;
            }
        }
    }
    REQUIRE(saw_major && saw_yield && saw_normal && saw_minor);
    REQUIRE(turn_priority_rank(TurnPriority::Major) >
            turn_priority_rank(TurnPriority::Yield));
    pass("through on the arterial is Major, its left yields, the side street is Minor");
}

void test_drive_on_left_mirrors_everything() {
    const Net right = build(GroundSampler{}, true);
    const Net left = build(GroundSampler{}, false);
    const LaneRef rr = lane_of_spine(right, 1, true);
    const LaneRef lr = lane_of_spine(left, 1, true);
    REQUIRE_NEAR(right.lanes.lane(rr).lateral_offset_m,
                 -left.lanes.lane(lr).lateral_offset_m, 1e-6f);

    // The turn that yields swaps sides with the handedness.
    const uint32_t j = node_at(left.graph, {0.0f, 0.0f});
    bool saw = false;
    for (LaneRef in_r : left.lanes.junction(j).incoming) {
        if (left.lanes.lane(in_r).cls != RoadClass::Arterial) continue;
        for (const TurnLink& t : left.lanes.outgoing(in_r)) {
            if (t.kind == TurnKind::Right) {
                REQUIRE(t.priority == TurnPriority::Yield);
                saw = true;
            }
            if (t.kind == TurnKind::Left) REQUIRE(t.priority != TurnPriority::Yield);
        }
    }
    REQUIRE(saw);
    pass("driving on the left mirrors lane offsets and swaps the yielding turn");
}

void test_no_lane_is_a_dead_stop_where_a_way_out_exists() {
    // A LANE WITH NO SUCCESSOR IS A CAR PARKED FOREVER IN A LIVE LANE. The
    // lane-discipline filter is allowed to prefer, never to strand.
    const Net n = build();
    std::size_t stranded = 0;
    std::size_t u_turn_only = 0;
    for (LaneRef r = 0; r < n.lanes.lane_count(); ++r) {
        const LaneJunction& jn = n.lanes.junction(n.lanes.lane(r).junction_to);
        bool way_out = false;
        for (LaneRef o : jn.outgoing)
            if (o != r) way_out = true;
        if (!way_out) continue;
        const std::vector<TurnLink>& outs = n.lanes.outgoing(r);
        if (outs.empty()) {
            ++stranded;
            continue;
        }
        bool all_u = true;
        for (const TurnLink& t : outs)
            if (t.kind != TurnKind::UTurn) all_u = false;
        if (all_u) ++u_turn_only;
    }
    REQUIRE(stranded == 0);
    // A dead end offers nothing but turning around, and the count is exact:
    // ten dead-end nodes, one arriving lane each except the two-lane arterial
    // ends and the three-lane freeway ends. That proves the U-turn fallback
    // actually fired rather than the check passing because every lane happened
    // to have a normal successor anyway.
    REQUIRE(u_turn_only == 16);
    std::printf("      0 stranded lanes; %zu dead ends offer only a U-turn\n",
                u_turn_only);
    pass("every lane with somewhere to go has a successor");
}

void test_multi_lane_discipline() {
    // On the arterial, a through movement stays in its own lane rather than
    // being offered every lane of the road it continues into.
    const Net n = build();
    const uint32_t j = node_at(n.graph, {0.0f, 0.0f});
    std::size_t checked = 0;
    for (LaneRef in_r : n.lanes.junction(j).incoming) {
        const Lane& in_l = n.lanes.lane(in_r);
        if (in_l.cls != RoadClass::Arterial) continue;
        for (const TurnLink& t : n.lanes.outgoing(in_r)) {
            if (t.kind != TurnKind::Straight) continue;
            REQUIRE(n.lanes.lane(t.to).index == in_l.index);
            ++checked;
        }
    }
    REQUIRE(checked >= 2);
    pass("a through movement keeps its lane index across the junction");
}

void test_choose_next_is_pure_and_population_independent() {
    const Net a = build();
    const Net b = build();
    const uint64_t seed = 0xA5A5A5A5A5A5A5A5ull;

    // Same lane, same seed, same decision index -> same answer, no matter how
    // many other lanes were asked in between. If a stream were hiding in here
    // the interleaving would change the result.
    std::vector<LaneRef> straight;
    for (LaneRef r = 0; r < a.lanes.lane_count(); ++r) {
        if (a.lanes.outgoing(r).size() < 2) continue;
        straight.push_back(r);
    }
    REQUIRE(straight.size() >= 4);

    for (uint32_t k = 0; k < 64; ++k) {
        for (LaneRef r : straight) {
            const LaneRef first = a.lanes.choose_next(r, seed, k);
            for (LaneRef other : straight) (void)a.lanes.choose_next(other, seed, k + 7);
            REQUIRE(a.lanes.choose_next(r, seed, k) == first);
            REQUIRE(b.lanes.choose_next(r, seed, k) == first);
        }
    }

    // ...and the weights actually bite: straight beats every turn.
    const LaneRef r = straight.front();
    std::size_t counts[4] = {0, 0, 0, 0};
    for (uint32_t k = 0; k < 4000; ++k) {
        const LaneRef to = a.lanes.choose_next(r, seed, k);
        for (const TurnLink& t : a.lanes.outgoing(r))
            if (t.to == to) ++counts[static_cast<std::size_t>(t.kind)];
    }
    REQUIRE(counts[0] > counts[1] + counts[2] + counts[3]);
    std::printf("      4000 draws on one lane: straight %zu, left %zu, right %zu, u %zu\n",
                counts[0], counts[1], counts[2], counts[3]);
    pass("choose_next is a pure function of the lane key, seed and decision index");
}

void test_plan_route() {
    const Net n = build();
    const LaneRef from = lane_of_spine(n, 1, true);   // arterial, west to east
    const LaneRef to = lane_of_spine(n, 3, true);     // street C, north
    REQUIRE(from != kInvalidLane && to != kInvalidLane);

    const std::vector<LaneRef> route = n.lanes.plan_route(from, to);
    REQUIRE(!route.empty());
    REQUIRE(route.front() == from);
    REQUIRE(route.back() == to);
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        bool linked = false;
        for (const TurnLink& t : n.lanes.outgoing(route[i]))
            if (t.to == route[i + 1]) linked = true;
        REQUIRE_MSG(linked, "route steps must be real turn links", "contiguity");
    }
    REQUIRE(n.lanes.plan_route(from, from).size() == 1);

    // The bridge is a separate island in this fixture, so it is genuinely
    // unreachable — and an empty route is the real answer, not an error.
    const LaneRef island = lane_of_spine(n, 6, true);
    REQUIRE(island != kInvalidLane);
    REQUIRE(n.lanes.plan_route(from, island).empty());
    std::printf("      route: %zu lanes from the arterial onto street C\n", route.size());
    pass("plan_route returns a contiguous chain, and empty when there is none");
}

void test_nearest_lane_and_the_heading_filter() {
    const Net n = build();
    const LaneRef r = lane_of_spine(n, 1, true);
    const LaneRef opp = n.lanes.opposing(r);
    const LanePose p = n.lanes.pose(r, n.lanes.length(r) * 0.5f);
    const glm::vec2 q = xz(p.position);

    const LaneProjection any = n.lanes.nearest_lane(q);
    REQUIRE(any.valid());
    REQUIRE(any.lane == r);

    const glm::vec2 fwd{p.tangent.x, p.tangent.z};
    REQUIRE(n.lanes.nearest_lane_along(q, fwd).lane == r);
    // A car pointing the other way must NOT be snapped onto this lane, or it
    // drives away backwards down a live carriageway.
    const LaneProjection back = n.lanes.nearest_lane_along(q, -fwd);
    REQUIRE(back.valid());
    REQUIRE(back.lane == opp);

    // Nothing within reach out in the water.
    REQUIRE(!n.lanes.nearest_lane({9000.0f, 9000.0f}, 50.0f).valid());
    pass("nearest_lane finds the right lane, and the heading filter rejects oncoming");
}

void test_approach_group_a_splits_the_crossing_streets() {
    const Net n = build();
    const uint32_t j = node_at(n.graph, {0.0f, 0.0f});
    bool arterial_group = false;
    bool street_group = false;
    bool have_a = false;
    bool have_s = false;
    for (LaneRef r : n.lanes.junction(j).incoming) {
        const bool a = n.lanes.approach_group_a(j, r);
        if (n.lanes.lane(r).cls == RoadClass::Arterial) {
            if (have_a) REQUIRE(a == arterial_group);
            arterial_group = a;
            have_a = true;
        } else {
            if (have_s) REQUIRE(a == street_group);
            street_group = a;
            have_s = true;
        }
    }
    REQUIRE(have_a && have_s);
    REQUIRE(arterial_group != street_group);
    pass("opposing approaches share a signal phase; the crossing street gets the other");
}

void test_build_is_deterministic() {
    const Net a = build();
    const Net b = build();
    REQUIRE(a.lanes.lane_count() == b.lanes.lane_count());
    for (LaneRef r = 0; r < a.lanes.lane_count(); ++r) {
        const Lane& x = a.lanes.lane(r);
        const Lane& y = b.lanes.lane(r);
        REQUIRE(x.key == y.key);
        REQUIRE(x.length_m == y.length_m);
        REQUIRE(x.centreline.size() == y.centreline.size());
        for (std::size_t k = 0; k < x.centreline.size(); ++k)
            REQUIRE(x.centreline[k] == y.centreline[k]);
        REQUIRE(a.lanes.outgoing(r).size() == b.lanes.outgoing(r).size());
        for (std::size_t k = 0; k < a.lanes.outgoing(r).size(); ++k)
            REQUIRE(a.lanes.outgoing(r)[k].to == b.lanes.outgoing(r)[k].to);
    }
    pass("two lane-graph builds are bit-identical");
}

}  // namespace

int main() {
    test_lane_counts_follow_the_hierarchy();
    test_pose_and_project_onto_are_inverses();
    test_lateral_sign_is_one_convention();
    test_pose_clamps_and_never_wraps();
    test_opposing_and_neighbour();
    test_lane_rides_the_carriageway_surface();
    test_junction_control_follows_the_hierarchy();
    test_turn_kinds_and_priorities();
    test_drive_on_left_mirrors_everything();
    test_no_lane_is_a_dead_stop_where_a_way_out_exists();
    test_multi_lane_discipline();
    test_choose_next_is_pure_and_population_independent();
    test_plan_route();
    test_nearest_lane_and_the_heading_filter();
    test_approach_group_a_splits_the_crossing_streets();
    test_build_is_deterministic();
    return apricot_test::done("lane_graph_tests");
}
