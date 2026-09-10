// The road graph: the hierarchy table, and turning spines into a planar graph.
//
// The table assertions are golden values in the same sense the hash constants
// are: the five widths ALREADY carry the reference's +2 m widen, and the whole
// failure mode this module inherits is somebody applying it a second time.
// Changing a number here has to be a deliberate act with a stated reason.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "road/road_graph.h"
#include "terrain/chunk.h"
#include "road_fixture.h"
#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

RoadGraph build_fixture(bool split = true) {
    RoadGraph g;
    RoadGraphParams p;
    p.split_crossings = split;
    g.build(make_test_spines(), p, GroundSampler{});
    return g;
}

uint32_t node_at(const RoadGraph& g, glm::vec2 p) {
    for (uint32_t i = 0; i < g.node_count(); ++i)
        if (glm::length(g.node(i).pos - p) < 0.5f) return i;
    return 0xFFFFFFFFu;
}

void test_hierarchy_widths_already_carry_the_widen() {
    // pinatty.md section 3, which took four of these five straight from the
    // reference's POST-migration table. Do not re-apply +2.
    REQUIRE_NEAR(road_class_def(RoadClass::Freeway).carriageway_width_m, 30.0f, 0.0f);
    REQUIRE_NEAR(road_class_def(RoadClass::Arterial).carriageway_width_m, 22.0f, 0.0f);
    REQUIRE_NEAR(road_class_def(RoadClass::Street).carriageway_width_m, 14.0f, 0.0f);
    REQUIRE_NEAR(road_class_def(RoadClass::Alley).carriageway_width_m, 6.0f, 0.0f);
    REQUIRE_NEAR(road_class_def(RoadClass::Dirt).carriageway_width_m, 9.0f, 0.0f);

    REQUIRE(road_class_def(RoadClass::Freeway).lanes_per_dir == 3);
    REQUIRE(road_class_def(RoadClass::Arterial).lanes_per_dir == 2);
    REQUIRE(road_class_def(RoadClass::Street).lanes_per_dir == 1);
    REQUIRE(road_class_def(RoadClass::Alley).lanes_per_dir == 1);
    REQUIRE(road_class_def(RoadClass::Dirt).lanes_per_dir == 1);

    REQUIRE(!road_class_def(RoadClass::Alley).sidewalks);
    REQUIRE(!road_class_def(RoadClass::Dirt).sidewalks);
    REQUIRE(road_class_def(RoadClass::Street).sidewalks);
    REQUIRE(!road_is_paved(RoadClass::Dirt));
    REQUIRE(road_uses_stop_signs(RoadClass::Dirt));
    REQUIRE(road_is_grade_separated(RoadClass::Freeway));
    REQUIRE(road_surface(RoadClass::Street) == Surface::Rock);
    REQUIRE(road_surface(RoadClass::Dirt) == Surface::Gravel);
    pass("road hierarchy widths are the +2 m table, unmodified");
}

void test_lane_offsets_reduce_to_quarter_width() {
    // One lane per direction must land on EXACTLY width/4 — that is the
    // reference's measured offset and re-deriving it differently is how the
    // oncoming-lane scan ends up looking at empty tarmac.
    REQUIRE_NEAR(lane_centre_offset_m(14.0f, 1, 0), 3.5f, 1e-6f);
    REQUIRE_NEAR(lane_centre_offset_m(9.0f, 1, 0), 2.25f, 1e-6f);
    REQUIRE_NEAR(lane_centre_offset_m(6.0f, 1, 0), 1.5f, 1e-6f);

    // Multi-lane roads spread evenly across the half carriageway.
    REQUIRE_NEAR(lane_centre_offset_m(22.0f, 2, 0), 2.75f, 1e-6f);
    REQUIRE_NEAR(lane_centre_offset_m(22.0f, 2, 1), 8.25f, 1e-6f);
    REQUIRE_NEAR(lane_centre_offset_m(30.0f, 3, 0), 2.5f, 1e-6f);
    REQUIRE_NEAR(lane_centre_offset_m(30.0f, 3, 1), 7.5f, 1e-6f);
    REQUIRE_NEAR(lane_centre_offset_m(30.0f, 3, 2), 12.5f, 1e-6f);

    // No lane may hang off the ribbon that draws it.
    for (std::size_t c = 0; c < kRoadClassCount; ++c) {
        const RoadClass cls = static_cast<RoadClass>(c);
        const RoadClassDef& def = road_class_def(cls);
        for (int i = 0; i < def.lanes_per_dir; ++i) {
            const float off = lane_centre_offset_m(cls, i);
            REQUIRE_MSG(off > 0.0f && off < def.carriageway_width_m * 0.5f,
                        "lane centre must sit inside the carriageway", def.name);
        }
    }
    pass("lane offsets: width/4 at one lane, inside the ribbon at every class");
}

void test_topology_of_the_fixture() {
    const RoadGraph g = build_fixture();
    REQUIRE(g.edge_count() == kFixtureEdges);
    REQUIRE(g.node_count() == kFixtureNodes);
    REQUIRE(g.junctions().size() == kFixtureJunctions);

    // The arterial was authored as one spine and comes out as four edges,
    // because three other roads meet it.
    std::size_t arterial_edges = 0;
    for (const RoadEdge& e : g.edges())
        if (e.spine_id == 1) ++arterial_edges;
    REQUIRE(arterial_edges == 4);

    // The bent street kept its shape point instead of becoming two edges.
    std::size_t bent = 0;
    for (const RoadEdge& e : g.edges())
        if (e.spine_id == 7) {
            ++bent;
            REQUIRE(e.points.size() == 3);
        }
    REQUIRE(bent == 1);
    pass("fixture topology: 11 edges, 14 nodes, 3 junctions");
}

void test_crossing_and_t_junctions() {
    const RoadGraph g = build_fixture();

    const uint32_t x = node_at(g, {0.0f, 0.0f});
    REQUIRE(x != 0xFFFFFFFFu);
    REQUIRE(g.node(x).kind == NodeKind::Junction);
    REQUIRE(g.node(x).edges.size() == 4);  // neither spine authored this
    REQUIRE(g.dominant_class(x) == RoadClass::Arterial);

    for (glm::vec2 p : {glm::vec2{-150.0f, 0.0f}, glm::vec2{150.0f, 0.0f}}) {
        const uint32_t t = node_at(g, p);
        REQUIRE(t != 0xFFFFFFFFu);
        REQUIRE_MSG(g.node(t).kind == NodeKind::Junction,
                    "an endpoint landing on another road is a T junction", "T");
        REQUIRE(g.node(t).edges.size() == 3);
    }

    // Two spines that merely share an endpoint make a degree-2 node, not a
    // junction — but they still break the edge, because the class changes.
    const uint32_t joint = node_at(g, {0.0f, 200.0f});
    REQUIRE(joint != 0xFFFFFFFFu);
    REQUIRE(g.node(joint).kind == NodeKind::Continuation);
    REQUIRE(g.node(joint).edges.size() == 2);
    std::set<RoadClass> classes;
    for (uint32_t ei : g.node(joint).edges) classes.insert(g.edge(ei).cls);
    REQUIRE(classes.size() == 2);
    pass("X from a crossing, T from an endpoint, and a class change is not a junction");
}

void test_crossings_off_is_a_different_graph() {
    // The negative control for the splitter: with it off, the arterial stays
    // one edge and the roads that cross it do not connect at all.
    const RoadGraph g = build_fixture(/*split=*/false);
    std::size_t arterial_edges = 0;
    for (const RoadEdge& e : g.edges())
        if (e.spine_id == 1) ++arterial_edges;
    REQUIRE(arterial_edges == 1);
    REQUIRE(node_at(g, {0.0f, 0.0f}) == 0xFFFFFFFFu);
    REQUIRE(g.junctions().empty());
    pass("split_crossings off: no crossing is discovered (negative control)");
}

void test_edge_keys_survive_reordering_the_table() {
    // THE POINT OF spine_id. An author inserting a road at the top of the
    // table must not renumber every road under it, or every hash keyed on a
    // road changes and the whole city regenerates.
    std::vector<RoadSpine> a = make_test_spines();
    std::vector<RoadSpine> b = make_test_spines();
    std::reverse(b.begin(), b.end());

    RoadGraph ga;
    RoadGraph gb;
    ga.build(a, RoadGraphParams{}, GroundSampler{});
    gb.build(b, RoadGraphParams{}, GroundSampler{});
    REQUIRE(ga.edge_count() == gb.edge_count());

    std::set<uint64_t> ka;
    std::set<uint64_t> kb;
    for (const RoadEdge& e : ga.edges()) ka.insert(e.key());
    for (const RoadEdge& e : gb.edges()) kb.insert(e.key());
    REQUIRE(ka.size() == ga.edge_count());  // keys are unique
    REQUIRE(ka == kb);
    pass("edge keys are stable when the spine table is reordered");
}

void test_build_is_deterministic() {
    const RoadGraph a = build_fixture();
    const RoadGraph b = build_fixture();
    REQUIRE(a.node_count() == b.node_count());
    REQUIRE(a.edge_count() == b.edge_count());
    for (uint32_t i = 0; i < a.edge_count(); ++i) {
        const RoadEdge& x = a.edge(i);
        const RoadEdge& y = b.edge(i);
        REQUIRE(x.node_a == y.node_a && x.node_b == y.node_b);
        REQUIRE(x.key() == y.key());
        REQUIRE(x.points.size() == y.points.size());
        for (std::size_t k = 0; k < x.points.size(); ++k)
            REQUIRE(x.points[k] == y.points[k]);  // bit-exact, not near
        REQUIRE(x.length_m == y.length_m);
    }
    pass("two builds of the same spines are bit-identical");
}

void test_attributes_reach_the_edges() {
    const RoadGraph g = build_fixture();
    bool saw_bridge = false;
    for (const RoadEdge& e : g.edges()) {
        if (e.spine_id == 1) {
            REQUIRE(e.block_quality == 200);
            REQUIRE_NEAR(e.traffic_density, 1.4f, 1e-6f);
            REQUIRE_NEAR(e.width_m, 22.0f, 1e-6f);
        }
        if (e.spine_id == 6) {
            saw_bridge = true;
            REQUIRE(e.structure == RoadStructure::Bridge);
            REQUIRE_NEAR(e.deck_y_m, 26.0f, 1e-6f);
            REQUIRE(e.block_quality == 255);
        }
    }
    REQUIRE(saw_bridge);
    pass("authored attributes reach every edge derived from a spine");
}

void test_width_override_is_never_snapped() {
    // The PCG-170 lesson: a custom width keeps its identity. A 9 m alley is a
    // 9 m alley, not a 6 m one rounded to its class.
    std::vector<RoadSpine> s;
    RoadSpine a;
    a.id = 1;
    a.cls = RoadClass::Alley;
    a.width_m = 9.0f;
    a.points = {{0.0f, 0.0f}, {100.0f, 0.0f}};
    s.push_back(a);

    RoadGraph g;
    g.build(s, RoadGraphParams{}, GroundSampler{});
    REQUIRE(g.edge_count() == 1);
    REQUIRE_NEAR(g.edge(0).width_m, 9.0f, 0.0f);
    REQUIRE_NEAR(g.edge(0).half_width_m(), 4.5f, 0.0f);
    pass("an authored width override survives untouched");
}

void test_ground_reaches_the_nodes() {
    const uint64_t seed = 0xDEADBEEFull;
    TerrainGround tg{seed};
    RoadGraph g;
    g.build(make_test_spines(), RoadGraphParams{}, tg.sampler());

    std::size_t decked = 0;
    for (uint32_t i = 0; i < g.node_count(); ++i) {
        const RoadNode& n = g.node(i);
        bool all_decked = true;
        for (uint32_t ei : n.edges)
            if (!road_structure_is_decked(g.edge(ei).structure)) all_decked = false;
        if (all_decked && !n.edges.empty()) {
            ++decked;
            REQUIRE_NEAR(n.y_m, 26.0f, 1e-4f);
        } else {
            // NOT height_at: the drawn triangle, which is what the ribbon and
            // therefore the collision sit on.
            REQUIRE_NEAR(n.y_m, mesh_height_at(seed, n.pos.x, n.pos.y), 1e-4f);
        }
    }
    REQUIRE(decked == 2);  // both ends of the bridge
    pass("nodes take the DRAWN terrain height, and a bridge takes its deck");
}

void test_scale() {
    // Not a performance assertion — a measurement, printed. A 40 x 40 street
    // grid is 80 spines and 1600 crossings, which is roughly a Pinatty
    // district and well past anything the authored spine table will hold.
    const int n = 40;
    const std::vector<RoadSpine> s = make_grid_spines(n, 92.0f);
    const auto t0 = std::chrono::steady_clock::now();
    RoadGraph g;
    g.build(s, RoadGraphParams{}, GroundSampler{});
    const auto t1 = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Interior crossings: (n-2)^2 four-way + edge junctions.
    REQUIRE(g.junctions().size() >= static_cast<std::size_t>((n - 2) * (n - 2)));
    REQUIRE(g.edge_count() == static_cast<std::size_t>(2 * n * (n - 1)));
    std::printf("      %d x %d grid: %zu spines -> %zu nodes, %zu edges, "
                "%zu junctions in %.1f ms\n",
                n, n, s.size(), g.node_count(), g.edge_count(),
                g.junctions().size(), ms);
    pass("a district-sized grid builds with the expected topology");
}

}  // namespace

int main() {
    test_hierarchy_widths_already_carry_the_widen();
    test_lane_offsets_reduce_to_quarter_width();
    test_topology_of_the_fixture();
    test_crossing_and_t_junctions();
    test_crossings_off_is_a_different_graph();
    test_edge_keys_survive_reordering_the_table();
    test_build_is_deterministic();
    test_attributes_reach_the_edges();
    test_width_override_is_never_snapped();
    test_ground_reaches_the_nodes();
    test_scale();
    return apricot_test::done("road_graph_tests");
}
