// Pinatty's roads — is the network a place you can drive, and does it sit on
// the ground?
//
// Two jobs, and like city_map_tests.cpp they are different jobs.
//
// THE FIRST IS A MEASUREMENT, and it is the one this suite exists for. A road
// ribbon is baked onto the LEVEL 0 drawn surface. If the terrain under it is
// drawn at level 3 the two are no longer the same surface and the road floats
// or sinks — measured over the quarry before any of this, by up to 1.020 m,
// which is why road draw distance was capped at 640 m. The fix is a terrain
// operator that carves the corridor so every level agrees about where the road
// bed is, and the only way to know whether it worked is to measure the DRAWN
// GEOMETRY at every level and print the number. There is a budget asserted, but
// the numbers are printed on every run because the right response to them is a
// draw distance, not a threshold somebody tunes until the test goes quiet.
//
// THE SECOND IS THE ACCEPTANCE TEST FOR THE WHOLE MAP: can you drive from one
// named district to another? That is answered by breadth-first search over the
// REAL RoadGraph built from the REAL map_spines(), not by counting table rows.
// A table of ninety roads that do not touch each other is ninety dead ends.
//
// EVERYTHING BELOW RUNS THE REAL PRODUCER. map_spines() -> RoadGraph::build ->
// bake_ribbons, on the real terrain, at kMapSeed. The one thing this file
// hand-builds is nothing at all, which is the point: a consumer test with
// hand-made inputs passes happily while the producer feeds garbage.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <vector>

#include "city/districts.h"
#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "city/terrain_ops.h"
#include "road/ribbon.h"
#include "road/road_graph.h"
#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "terrain/streamer.h"
#include "test_assert.h"

using namespace apricot;
using apricot::city::kMapSeed;
using apricot::city::kRoadCount;
using apricot::city::kRoads;
using apricot::city::Road;

namespace {

// ---------------------------------------------------------------------------
//  The one build everything measures
// ---------------------------------------------------------------------------

struct Built {
    std::vector<RoadSpine> spines;
    TerrainGround ground{kMapSeed};
    RoadGraph graph;
    RibbonBake bake;

    // A SECOND BAKE OF ONLY THE ROADS THAT DRAPE, and it exists because of a
    // false positive that cost an afternoon.
    //
    // The drape measurement below has to exclude bridge and tunnel geometry: a
    // deck is authored, never draped, so asking how far the ground moves under
    // it is asking about ground nothing is resting on. The obvious test for
    // "was this vertex draped" is "is it sitting exactly on the level 0
    // surface" -- and that is right everywhere except the one place it matters,
    // the BRIDGEHEAD, where the ground has been graded up to meet the deck and
    // the two are equal by construction. Twenty metres out over the Kessel
    // Channel the deck is still flat while the ground has fallen away, and the
    // measurement was reporting half a metre of "drape error" on a bridge.
    //
    // So the decked spines are removed before the bake instead of after it.
    // Everything in `draped_bake` is a road that genuinely lies on the ground.
    RoadGraph draped_graph;
    RibbonBake draped_bake;
};

Built& built() {
    // Function-local and built once, because the bake is the expensive part and
    // every check below wants the same one. It is not a cache in the sense the
    // purity rule forbids: nothing here is a generator, this is a test fixture,
    // and it is never consulted from inside a pure function.
    static Built b = [] {
        Built out;
        out.spines = city::map_spines();
        out.graph.build(out.spines, RoadGraphParams{}, out.ground.sampler());
        out.bake = bake_ribbons(out.graph, out.ground.sampler());

        std::vector<RoadSpine> on_ground;
        for (const RoadSpine& sp : out.spines) {
            if (!road_structure_is_decked(sp.structure)) on_ground.push_back(sp);
        }
        out.draped_graph.build(on_ground, RoadGraphParams{},
                               out.ground.sampler());
        out.draped_bake = bake_ribbons(out.draped_graph, out.ground.sampler());
        return out;
    }();
    return b;
}

// How far above the drawn terrain each layer's vertices sit. Kerb is absent on
// purpose: its faces are vertical, so "how high above the ground is it" has no
// single answer and nothing rests on one.
bool layer_lift(RoadLayer l, float& lift) {
    switch (l) {
        case RoadLayer::Carriageway:
        case RoadLayer::Unpaved:
        case RoadLayer::Plate:
            lift = kDrapeEpsM;
            return true;
        case RoadLayer::Crosswalk:
            // Crosswalks are lifted a further 3 cm so they read over the plate
            // they are painted on (ribbon.cpp).
            lift = kDrapeEpsM + 0.03f;
            return true;
        case RoadLayer::Walk:
            lift = kDrapeEpsM + kKerbHeightM;
            return true;
        case RoadLayer::Kerb:
            return false;
    }
    return false;
}

const char* layer_name_of(RoadLayer l) { return road_layer_name(l); }

// ---------------------------------------------------------------------------
//  1. what got built
// ---------------------------------------------------------------------------

void the_network_is_the_size_the_table_says() {
    const Built& b = built();

    REQUIRE_MSG(b.spines.size() == static_cast<std::size_t>(kRoadCount),
                "map_spines() lost or invented a road", "network");
    REQUIRE_MSG(b.graph.edge_count() >= b.spines.size(),
                "the graph has fewer edges than spines, so spines were dropped",
                "network");
    REQUIRE_MSG(!b.graph.junctions().empty(),
                "no junctions at all: the roads do not touch each other",
                "network");

    double centreline = 0.0;
    for (std::size_t i = 0; i < b.graph.edge_count(); ++i) {
        centreline += static_cast<double>(b.graph.edge(static_cast<uint32_t>(i)).length_m);
    }

    int deg[4] = {0, 0, 0, 0};  // dead end, continuation, junction, 4+
    for (const RoadNode& n : b.graph.nodes()) {
        const std::size_t d = n.edges.size();
        if (d == 1) ++deg[0];
        else if (d == 2) ++deg[1];
        else if (d == 3) ++deg[2];
        else ++deg[3];
    }

    std::printf("\n  network: %zu spines -> %zu nodes, %zu edges, %zu junctions\n",
                b.spines.size(), b.graph.node_count(), b.graph.edge_count(),
                b.graph.junctions().size());
    std::printf("           %.0f m of carriageway centreline\n", centreline);
    std::printf("           nodes by degree: %d dead ends, %d continuations, "
                "%d three-way, %d four-way-or-more\n",
                deg[0], deg[1], deg[2], deg[3]);
    std::printf("           bake: %zu triangles, %zu plates, %zu crosswalks\n",
                b.bake.total_triangles(), b.bake.plates_baked,
                b.bake.crosswalks_baked);
    for (std::size_t l = 0; l < kRoadLayerCount; ++l) {
        const RoadLayer lay = static_cast<RoadLayer>(l);
        std::printf("             %-11s %7zu verts %7zu tris\n",
                    layer_name_of(lay), b.bake.layer(lay).vertices.size(),
                    b.bake.layer(lay).triangle_count());
    }

    REQUIRE_MSG(centreline > 40000.0,
                "the island lost most of its road network", "network");
    apricot_test::pass("the network builds, and it is the size the table says");
}

// ---------------------------------------------------------------------------
//  2. THE DRAPE MEASUREMENT — over the real baked vertices
// ---------------------------------------------------------------------------

// THE NUMBER THE ROAD DRAW DISTANCE IS SET FROM.
//
// For every vertex the baker actually emitted for a road that lies on the
// ground, on every layer that drapes: how far does the drawn ground move under
// it when the terrain beneath is drawn at a coarser level?
//
// Measured over Built::draped_bake, which is the same producer over the same
// terrain with the decked spines taken out first. See the note on that member
// for why "is this vertex sitting on the level 0 surface" is not the same
// question and gets the wrong answer at a bridgehead.
void the_road_sits_on_the_ground_at_every_level() {
    const Built& b = built();

    float worst[kMaxChunkLod + 1] = {0.0f, 0.0f, 0.0f, 0.0f};
    double total[kMaxChunkLod + 1] = {0.0, 0.0, 0.0, 0.0};
    long draped = 0;
    float worst_xz[2] = {0.0f, 0.0f};

    for (std::size_t l = 0; l < kRoadLayerCount; ++l) {
        const RoadLayer lay = static_cast<RoadLayer>(l);
        float lift = 0.0f;
        if (!layer_lift(lay, lift)) continue;

        for (const TerrainVertex& v : b.draped_bake.layer(lay).vertices) {
            const float x = v.position.x;
            const float z = v.position.z;
            const float ground = mesh_height_at(kMapSeed, x, z);
            // Every vertex here BELONGS to a road that drapes, so it must be
            // on the ground. If one is not, the baker stopped draping and the
            // whole measurement below is measuring the wrong thing.
            REQUIRE_MSG(std::fabs(v.position.y - (ground + lift)) < 1e-3f,
                        "a vertex of a Ground road is not on the level 0 "
                        "surface, so the ribbon baker has stopped draping",
                        road_layer_name(lay));
            ++draped;
            for (int lod = 1; lod <= kMaxChunkLod; ++lod) {
                const float d = std::fabs(
                    mesh_height_at_lod(kMapSeed, x, z, lod) - ground);
                total[lod] += static_cast<double>(d);
                if (d > worst[lod]) {
                    worst[lod] = d;
                    if (lod == kMaxChunkLod) {
                        worst_xz[0] = x;
                        worst_xz[1] = z;
                    }
                }
            }
        }
    }

    REQUIRE_MSG(draped > 10000,
                "almost nothing was draped, so this measured almost nothing",
                "drape");

    std::size_t decked_spines = 0;
    for (const RoadSpine& sp : b.spines) {
        if (road_structure_is_decked(sp.structure)) ++decked_spines;
    }
    std::printf("\n  DRAPE, over %ld baked vertices of roads that lie on the "
                "ground (%zu decked spines left out of this bake):\n",
                draped, decked_spines);
    for (int lod = 1; lod <= kMaxChunkLod; ++lod) {
        std::printf("    lod %d (%.0f m spacing): mean %.4f m, worst %.4f m\n",
                    lod, static_cast<double>(lod_spacing_metres(lod)),
                    total[lod] / static_cast<double>(draped),
                    static_cast<double>(worst[lod]));
    }
    std::printf("    worst level %d vertex at (%.0f, %.0f), in %s\n",
                kMaxChunkLod, static_cast<double>(worst_xz[0]),
                static_cast<double>(worst_xz[1]),
                city::district_name(city::district_at(worst_xz[0], worst_xz[1])));

    // THE BUDGET, AND IT IS AN ANGLE RATHER THAN A DISTANCE.
    //
    // "Under 25 cm" would be a number somebody picked, and the first person to
    // author a slightly steeper road would tune it until it went quiet. What
    // actually matters is whether a player can SEE the road floating, and that
    // depends on how far away it is — which the streamer already decides,
    // because a chunk is only drawn at level L beyond the ring for L.
    //
    // So the budget is: a road may never be off its ground by more than one
    // part in 2000 of its distance from the camera. That is 0.029 degrees,
    // about half a pixel at 1280 across with a 60 degree field of view, at the
    // CLOSEST the terrain under it is ever drawn that coarsely.
    //
    // It also ties the budget to the ring configuration, so moving lod_ring
    // re-asks the question instead of silently invalidating the answer.
    const StreamerConfig cfg;
    const float ring_start[kMaxChunkLod + 1] = {
        0.0f,
        static_cast<float>(cfg.lod_ring[0]) * kChunkMetres,
        static_cast<float>(cfg.lod_ring[1]) * kChunkMetres,
        static_cast<float>(cfg.lod_ring[2]) * kChunkMetres,
    };
    constexpr float kMaxAngularError = 1.0f / 2000.0f;

    std::printf("    against the rings the streamer actually uses:\n");
    for (int lod = 1; lod <= kMaxChunkLod; ++lod) {
        const float ang = worst[lod] / ring_start[lod];
        std::printf("      lod %d terrain starts at %5.0f m: worst error is "
                    "%.6f of that distance (budget %.6f)\n",
                    lod, static_cast<double>(ring_start[lod]),
                    static_cast<double>(ang),
                    static_cast<double>(kMaxAngularError));
        REQUIRE_MSG(ang < kMaxAngularError,
                    "a road is off its ground by more than one part in 2000 of "
                    "the closest distance the terrain under it is drawn at "
                    "this level. Some road needs shapes_ground, or a wider "
                    "corridor margin, or a level platform at a hairpin",
                    "drape");
    }

    // And a hard ceiling as well, because the angular budget alone would let a
    // single catastrophic road hide behind a large ring radius. 0.5 m is half
    // the 1.020 m that pinned road draw distance to 640 m before any of this
    // existed; the 7 m cliff at the Shoulder's hairpin would have tripped it.
    REQUIRE_MSG(worst[kMaxChunkLod] < 0.5f,
                "a baked road vertex moves half a metre when the terrain under "
                "it is drawn at the coarsest level",
                "drape");
    apricot_test::pass("baked road vertices stay on the ground at every level");
}

// The same question asked per road, which is what tells you WHICH road to fix.
// Sampled across the ribbon's own footprint rather than at baked vertices,
// because a road whose ribbon was trimmed away entirely at a junction would
// otherwise vanish from the report.
void which_roads_are_worst() {
    struct Row {
        const char* name;
        float worst;
        bool shapes;
        float x;
        float z;
    };
    std::vector<Row> rows;
    rows.reserve(static_cast<std::size_t>(kRoadCount));

    for (int i = 0; i < kRoadCount; ++i) {
        const Road& r = kRoads[i];
        if (city::road_structure_is_decked(r.structure)) continue;

        const float half = r.ribbon_half_m();
        float worst = 0.0f;
        float wx = 0.0f, wz = 0.0f;
        for (int seg = 0; seg + 1 < r.count; ++seg) {
            const float ax = r.path[seg].x, az = r.path[seg].z;
            const float bx = r.path[seg + 1].x, bz = r.path[seg + 1].z;
            const float dx = bx - ax, dz = bz - az;
            const float len = std::sqrt(dx * dx + dz * dz);
            const int steps = std::max(2, static_cast<int>(len / 3.0f));
            const float nx = -dz / len, nz = dx / len;
            for (int s = 0; s <= steps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                for (int k = -3; k <= 3; ++k) {
                    const float off = half * static_cast<float>(k) / 3.0f;
                    const float x = ax + dx * t + nx * off;
                    const float z = az + dz * t + nz * off;
                    const float d = std::fabs(
                        mesh_height_at_lod(kMapSeed, x, z, kMaxChunkLod) -
                        mesh_height_at(kMapSeed, x, z));
                    if (d > worst) {
                        worst = d;
                        wx = x;
                        wz = z;
                    }
                }
            }
        }
        rows.push_back(Row{r.name, worst, r.shapes_ground, wx, wz});
    }

    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& c) { return a.worst > c.worst; });

    std::printf("\n  worst level %d drape under each road's footprint "
                "(top 12 of %zu):\n", kMaxChunkLod, rows.size());
    for (std::size_t i = 0; i < rows.size() && i < 12; ++i) {
        std::printf("    %7.4f m  %-30s %-12s at (%.0f, %.0f)\n",
                    static_cast<double>(rows[i].worst), rows[i].name,
                    rows[i].shapes ? "(graded)" : "(on a plate)",
                    static_cast<double>(rows[i].x), static_cast<double>(rows[i].z));
    }

    // A road that does NOT grade its corridor is claiming the ground under it
    // is already flat enough. That claim is checked here rather than trusted,
    // because `shapes_ground = false` is the cheap option and cheap options get
    // chosen by accident.
    for (const Row& row : rows) {
        if (row.shapes) continue;
        REQUIRE_MSG(row.worst < 0.25f,
                    "a road with shapes_ground = false is not on flat ground "
                    "after all; give it a corridor. (This threshold is tighter "
                    "than the network-wide one on purpose: a road that claims "
                    "it does not need a corridor should be comfortably right, "
                    "not marginally right.)",
                    row.name);
    }
    apricot_test::pass("every road that declines a corridor can afford to");
}

// ---------------------------------------------------------------------------
//  3. THE ACCEPTANCE TEST — can you drive from one district to another?
// ---------------------------------------------------------------------------

// Which districts an edge passes through, sampled along it. Sampling rather
// than testing the endpoints, because a road can cross a district without
// having a node in it — the Strand is one edge 2.2 km long.
void districts_touched(const RoadEdge& e, bool* out) {
    for (std::size_t i = 0; i + 1 < e.points.size(); ++i) {
        const glm::vec2 a = e.points[i];
        const glm::vec2 b = e.points[i + 1];
        const float len = glm::length(b - a);
        const int steps = std::max(2, static_cast<int>(len / 20.0f));
        for (int s = 0; s <= steps; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const glm::vec2 p = a + (b - a) * t;
            const city::DistrictId d = city::district_at(p.x, p.y);
            if (d != city::DistrictId::Count) {
                out[static_cast<int>(d)] = true;
            }
        }
    }
}

void you_can_drive_from_any_district_to_any_other() {
    const Built& b = built();
    const std::size_t nodes = b.graph.node_count();

    // One anchor node per district: an endpoint of some edge that passes
    // through it.
    std::vector<uint32_t> anchor(static_cast<std::size_t>(city::kDistrictCount),
                                 0xFFFFFFFFu);
    for (std::size_t i = 0; i < b.graph.edge_count(); ++i) {
        const RoadEdge& e = b.graph.edge(static_cast<uint32_t>(i));
        bool touched[city::kDistrictCount] = {};
        districts_touched(e, touched);
        for (int d = 0; d < city::kDistrictCount; ++d) {
            if (touched[d] && anchor[static_cast<std::size_t>(d)] == 0xFFFFFFFFu) {
                anchor[static_cast<std::size_t>(d)] = e.node_a;
            }
        }
    }

    for (int d = 0; d < city::kDistrictCount; ++d) {
        REQUIRE_MSG(anchor[static_cast<std::size_t>(d)] != 0xFFFFFFFFu,
                    "a district has no road running through it at all",
                    city::district_name(static_cast<city::DistrictId>(d)));
    }

    // Breadth-first over the graph from Vellum Row, carrying distance so the
    // report says how far apart the districts actually are.
    const uint32_t start = anchor[static_cast<std::size_t>(city::DistrictId::VellumRow)];
    std::vector<float> dist(nodes, -1.0f);
    std::vector<int> hops(nodes, 0);
    std::queue<uint32_t> q;
    dist[start] = 0.0f;
    q.push(start);
    while (!q.empty()) {
        const uint32_t n = q.front();
        q.pop();
        for (const uint32_t ei : b.graph.node(n).edges) {
            const RoadEdge& e = b.graph.edge(ei);
            const uint32_t other = (e.node_a == n) ? e.node_b : e.node_a;
            if (dist[other] >= 0.0f) continue;
            dist[other] = dist[n] + e.length_m;
            hops[other] = hops[n] + 1;
            q.push(other);
        }
    }

    std::printf("\n  from Vellum Row, by road:\n");
    for (int d = 0; d < city::kDistrictCount; ++d) {
        const uint32_t a = anchor[static_cast<std::size_t>(d)];
        std::printf("    %-16s %s",
                    city::district_name(static_cast<city::DistrictId>(d)),
                    dist[a] >= 0.0f ? "" : "UNREACHABLE");
        if (dist[a] >= 0.0f) {
            std::printf("%6.0f m over %2d edges", static_cast<double>(dist[a]),
                        hops[a]);
        }
        std::printf("\n");
    }

    for (int d = 0; d < city::kDistrictCount; ++d) {
        REQUIRE_MSG(dist[anchor[static_cast<std::size_t>(d)]] >= 0.0f,
                    "you cannot drive to this district from Vellum Row",
                    city::district_name(static_cast<city::DistrictId>(d)));
    }

    // And the whole network is one piece, not two islands that each happen to
    // touch five districts.
    std::size_t reached = 0;
    for (const float v : dist) {
        if (v >= 0.0f) ++reached;
    }
    std::printf("    %zu of %zu nodes reachable\n", reached, nodes);
    REQUIRE_MSG(reached == nodes,
                "part of the road network is not connected to the rest",
                "connectivity");

    apricot_test::pass("you can drive from any district to any other");
}

// ---------------------------------------------------------------------------
//  4. the districts have to CHASE differently, and that is geometry
// ---------------------------------------------------------------------------

void vellum_row_is_a_grid_of_four_way_junctions() {
    const Built& b = built();
    int four_way = 0;
    int lesser = 0;
    for (const uint32_t ni : b.graph.junctions()) {
        const RoadNode& n = b.graph.node(ni);
        if (city::district_at(n.pos.x, n.pos.y) != city::DistrictId::VellumRow) {
            continue;
        }
        if (n.edges.size() >= 4) ++four_way;
        else ++lesser;
    }
    std::printf("\n  Vellum Row: %d four-way junctions, %d three-way\n",
                four_way, lesser);

    // NINETY-NINE CROSSINGS, EVERY ONE OF THEM FOUR CHOICES. That is the whole
    // district: you escape by reading the pursuit, not by out-driving it. Nine
    // streets crossing eleven is 99, and none of those crossings is authored —
    // the graph finds every one of them itself.
    REQUIRE_MSG(four_way >= 90,
                "Vellum Row is not a grid any more: fewer than ninety of its "
                "junctions offer four choices",
                "vellum grid");
    apricot_test::pass("Vellum Row is a grid, and every crossing is four choices");
}

void the_strand_has_no_turnoffs() {
    const Built& b = built();

    // Find the Strand's edges by spine id and require the whole 2.2 km to be
    // ONE edge. An edge is split at every node, so more than one edge means
    // something joined it in the middle — which is the district deleted.
    int edges = 0;
    float length = 0.0f;
    for (std::size_t i = 0; i < b.graph.edge_count(); ++i) {
        const RoadEdge& e = b.graph.edge(static_cast<uint32_t>(i));
        if (e.spine_id != 8) continue;
        ++edges;
        length += e.length_m;
    }
    std::printf("\n  the Strand: %d edge(s), %.0f m\n", edges,
                static_cast<double>(length));
    REQUIRE_MSG(edges == 1,
                "Route 1 - the Strand is no longer a single unbroken edge, so "
                "something now joins it between its ends",
                "the Strand");
    REQUIRE_MSG(length > 2100.0f,
                "the Strand is under 2.1 km and is supposed to be 2.2",
                "the Strand");
    apricot_test::pass("the Strand is 2.2 km of one edge with no turnoffs");
}

void there_is_one_paved_way_up_ferrone_hill() {
    const Built& b = built();

    // Every edge that crosses the 60 m contour on the hill, by class. Paved
    // ones are the roadblock question: block them all and the hill is sealed
    // to anyone who has not found the dirt.
    int paved = 0;
    int unpaved = 0;
    std::printf("\n  crossing 60 m on Ferrone Hill:\n");
    for (std::size_t i = 0; i < b.graph.edge_count(); ++i) {
        const RoadEdge& e = b.graph.edge(static_cast<uint32_t>(i));
        bool below = false;
        bool above = false;
        for (const glm::vec2 p : e.points) {
            if (p.x < 300.0f || p.x > 1700.0f) continue;
            if (p.y < -1900.0f || p.y > -900.0f) continue;
            const float h = height_at(kMapSeed, p.x, p.y);
            if (h < 60.0f) below = true;
            if (h > 60.0f) above = true;
        }
        if (!(below && above)) continue;
        const bool is_paved = road_is_paved(e.cls);
        std::printf("    %-9s spine %u run %u  %s\n",
                    road_class_def(e.cls).name, e.spine_id, e.spine_run,
                    is_paved ? "PAVED" : "unpaved");
        if (is_paved) ++paved;
        else ++unpaved;
    }

    // THE BEST ROADBLOCK IN THE GAME depends on this number being one. Add a
    // second paved climb and Ferrone Hill stops being a hill you can seal.
    REQUIRE_MSG(paved == 1,
                "Ferrone Hill has more than one paved way up (or none), so "
                "blocking the Shoulder no longer seals it",
                "Ferrone Hill");
    REQUIRE_MSG(unpaved >= 1,
                "the fire road is gone, so a sealed hill is a death sentence "
                "rather than a puzzle",
                "Ferrone Hill");
    apricot_test::pass("one paved way up Ferrone Hill, and one unpaved way off");
}

void nickel_heights_punishes_panic() {
    const Built& b = built();
    int dead_ends = 0;
    for (const RoadNode& n : b.graph.nodes()) {
        if (n.edges.size() != 1) continue;
        if (city::district_at(n.pos.x, n.pos.y) != city::DistrictId::NickelHeights) {
            continue;
        }
        ++dead_ends;
    }
    std::printf("\n  Nickel Heights: %d dead ends\n", dead_ends);
    REQUIRE_MSG(dead_ends >= 3,
                "Nickel Heights has fewer than three dead ends, so panicking "
                "there costs nothing",
                "Nickel Heights");
    apricot_test::pass("Nickel Heights still punishes a wrong turn");
}

// ---------------------------------------------------------------------------
//  5. the roads have to be ON the island, and drivable
// ---------------------------------------------------------------------------

void no_road_runs_through_the_sea() {
    // COLLECTED AND PRINTED FIRST, ASSERTED SECOND. A REQUIRE inside the loop
    // stops at the first road in the sea and says nothing about the other
    // eighty-three, which is exactly the information you want when a terrain
    // change has just moved the coastline.
    struct Low {
        const char* name;
        float y;
    };
    std::vector<Low> lows;
    lows.reserve(static_cast<std::size_t>(kRoadCount));

    for (int i = 0; i < kRoadCount; ++i) {
        const Road& r = kRoads[i];
        float low = 1e9f;
        for (int seg = 0; seg + 1 < r.count; ++seg) {
            const int steps = 40;
            for (int s = 0; s <= steps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                const float x = r.path[seg].x + (r.path[seg + 1].x - r.path[seg].x) * t;
                const float z = r.path[seg].z + (r.path[seg + 1].z - r.path[seg].z) * t;
                // A decked road is at its deck; a ground road is at the ground
                // the operators left, which is the ground it will drape onto.
                const float y = city::road_structure_is_decked(r.structure)
                                    ? r.deck_y_m
                                    : height_at(kMapSeed, x, z);
                low = std::min(low, y);
            }
        }
        lows.push_back(Low{r.name, low});
    }

    std::sort(lows.begin(), lows.end(),
              [](const Low& a, const Low& b) { return a.y < b.y; });
    std::printf("\n  lowest ground under each road (sea level is 0.0 m), "
                "the six closest to it:\n");
    for (std::size_t i = 0; i < lows.size() && i < 6; ++i) {
        std::printf("    %7.2f m  %s\n", static_cast<double>(lows[i].y),
                    lows[i].name);
    }
    for (const Low& l : lows) {
        REQUIRE_MSG(l.y > 0.5f,
                    "this road is at or under sea level somewhere along it",
                    l.name);
    }
    apricot_test::pass("no road runs through the sea");
}

// A road that does not grade its corridor still carries an authored bed height,
// and an authored number nobody checks is an authored number that goes stale
// and then gets believed.
void authored_heights_match_the_ground_they_claim() {
    struct Miss {
        const char* name;
        float err;
        float x;
        float z;
        float ground;
    };
    std::vector<Miss> misses;

    for (int i = 0; i < kRoadCount; ++i) {
        const Road& r = kRoads[i];
        if (r.shapes_ground) continue;  // the ground conforms to it, not vice versa
        if (city::road_structure_is_decked(r.structure)) continue;
        Miss m{r.name, 0.0f, 0.0f, 0.0f, 0.0f};
        for (int p = 0; p < r.count; ++p) {
            const float g = height_at(kMapSeed, r.path[p].x, r.path[p].z);
            const float d = std::fabs(g - r.path[p].y);
            if (d > m.err) {
                m.err = d;
                m.x = r.path[p].x;
                m.z = r.path[p].z;
                m.ground = g;
            }
        }
        misses.push_back(m);
    }
    std::sort(misses.begin(), misses.end(),
              [](const Miss& a, const Miss& b) { return a.err > b.err; });

    std::printf("\n  authored bed height vs measured ground, on the roads that "
                "do NOT grade their corridor (worst six):\n");
    for (std::size_t i = 0; i < misses.size() && i < 6; ++i) {
        std::printf("    %6.3f m off  %-24s at (%.0f, %.0f), ground is "
                    "%.2f m\n",
                    static_cast<double>(misses[i].err), misses[i].name,
                    static_cast<double>(misses[i].x),
                    static_cast<double>(misses[i].z),
                    static_cast<double>(misses[i].ground));
    }
    for (const Miss& m : misses) {
        REQUIRE_MSG(m.err < 1.0f,
                    "a road that does not shape the ground claims a bed height "
                    "the ground does not have. Usually this means something "
                    "ELSE grades near it and put the road on an embankment",
                    m.name);
    }
    apricot_test::pass("authored bed heights match the ground that carries them");
}

void nothing_climbs_faster_than_a_car_can() {
    float worst = 0.0f;
    const char* worst_name = "";
    for (int i = 0; i < kRoadCount; ++i) {
        const float g = kRoads[i].max_grade();
        if (g > worst) {
            worst = g;
            worst_name = kRoads[i].name;
        }
    }
    std::printf("  steepest authored grade: %.1f%% (%s)\n",
                static_cast<double>(worst) * 100.0, worst_name);
    // Road::well_formed() already refuses anything over 25% at compile time;
    // this prints the number so the design's "9 per cent switchbacks" claim is
    // a measurement rather than an assurance.
    REQUIRE_MSG(worst <= 0.25f, "an authored grade is over 25 per cent",
                worst_name);
    apricot_test::pass("every authored grade is one a car can climb");
}

// road/road_class.h says an at-grade freeway crossing is an AUTHORING ERROR and
// that rejecting it is the MAP VALIDATOR'S job, not the road module's. This is
// the map validator.
void no_freeway_crosses_anything_at_grade() {
    const Built& b = built();
    int ramps = 0;
    int bad = 0;

    for (const uint32_t ni : b.graph.junctions()) {
        const RoadNode& n = b.graph.node(ni);
        int freeway_arms = 0;
        for (const uint32_t ei : n.edges) {
            if (b.graph.edge(ei).cls == RoadClass::Freeway) ++freeway_arms;
        }
        if (freeway_arms == 0) continue;
        ++ramps;

        // A freeway junction is a MERGE or a RAMP. Two freeway arms and two
        // others at one node is a crossroads on a motorway, which is the thing
        // road/road_class.h refuses to invent a traffic light for and says
        // plainly is the map validator's job to reject. This is the map
        // validator.
        const int others = static_cast<int>(n.edges.size()) - freeway_arms;
        if (freeway_arms >= 2 && others >= 2) {
            ++bad;
            std::printf("    AT GRADE at (%.0f, %.0f): %d freeway arms and %d "
                        "others ->", static_cast<double>(n.pos.x),
                        static_cast<double>(n.pos.y), freeway_arms, others);
            for (const uint32_t ei : n.edges) {
                std::printf(" %s(spine %u)", road_class_def(b.graph.edge(ei).cls).name,
                            b.graph.edge(ei).spine_id);
            }
            std::printf("\n");
        }
    }
    std::printf("  %d junctions touch Route 1; %d of them are at-grade "
                "crossings\n", ramps, bad);
    REQUIRE_MSG(bad == 0,
                "a freeway crosses another road at grade. Terminate the minor "
                "road ON the freeway as a ramp, and give the two sides "
                "SEPARATE ramp nodes -- a T from both sides at one point is "
                "still a crossroads",
                "freeway");
    apricot_test::pass("no freeway crosses another road at grade");
}

// ---------------------------------------------------------------------------
//  6. what the corridors cost, and what the map still guarantees
// ---------------------------------------------------------------------------

void the_operator_table_is_still_cheap() {
    std::printf("\n  operators: %d (%d authored + %d derived from %d roads), "
                "worst bucket holds %d of %d\n",
                city::kTerrainOpCount, city::kBaseOpCount,
                city::kRoadGradeOpCount, kRoadCount, city::kOpIndex.max_in_bucket,
                city::kMaxOpsPerBucket);

    long total = 0;
    long samples = 0;
    int worst = 0;
    for (int j = -24; j <= 24; ++j) {
        for (int i = -24; i <= 24; ++i) {
            const float x = static_cast<float>(i) * 120.0f;
            const float z = static_cast<float>(j) * 120.0f;
            const int bx = city::op_bucket_axis(x);
            const int bz = city::op_bucket_axis(z);
            if (bx < 0 || bz < 0) continue;
            const int c = city::kOpIndex.count[bz * city::kOpBucketsPerSide + bx];
            total += c;
            worst = std::max(worst, c);
            ++samples;
        }
    }
    std::printf("             a sample inside the world box touches %.2f "
                "operators on average, %d at worst\n",
                static_cast<double>(total) / static_cast<double>(samples), worst);

    // The index is what keeps height_at() from walking the whole table, and the
    // table just grew from 22 entries to sixty-odd. If a bucket ever fills, the
    // static_assert in terrain_ops.h fires first — this is here so the cost is
    // VISIBLE while there is still headroom, rather than a surprise the day it
    // runs out.
    REQUIRE_MSG(city::kOpIndex.max_in_bucket < city::kMaxOpsPerBucket,
                "the operator index has no headroom left", "op index");
    apricot_test::pass("the operator index still has headroom");
}

}  // namespace

int main() {
    std::printf("city_roads_tests\n");
    the_network_is_the_size_the_table_says();
    // The per-road diagnostic runs BEFORE the assertion it diagnoses. A suite
    // that asserts first and explains second tells you the number is wrong and
    // then exits without telling you which road it was.
    which_roads_are_worst();
    the_road_sits_on_the_ground_at_every_level();
    you_can_drive_from_any_district_to_any_other();
    vellum_row_is_a_grid_of_four_way_junctions();
    the_strand_has_no_turnoffs();
    there_is_one_paved_way_up_ferrone_hill();
    nickel_heights_punishes_panic();
    no_road_runs_through_the_sea();
    authored_heights_match_the_ground_they_claim();
    nothing_climbs_faster_than_a_car_can();
    no_freeway_crosses_anything_at_grade();
    the_operator_table_is_still_cheap();
    return apricot_test::done("city_roads_tests");
}
