// O'Haven's roads — is the network a place you can drive, and does it sit on
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
#include <initializer_list>
#include <queue>
#include <vector>

#include "city/districts.h"
#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "city/terrain_ops.h"
#include "core/fixed_step.h"
#include "core/input_frame.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
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
        case RoadLayer::WhiteMarking:
        case RoadLayer::YellowMarking:
            lift = kDrapeEpsM + RibbonParams{}.marking_lift_m;
            return true;
        case RoadLayer::Walk:
            lift = kDrapeEpsM + kKerbHeightM;
            return true;
        case RoadLayer::Kerb:
        case RoadLayer::Structure:
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

void rimway_creek_bridge_uses_its_own_municipal_profile() {
    const Road* rimway = nullptr;
    const Road* kessel = nullptr;
    int municipal = 0;
    for (const Road& road : kRoads) {
        if (road.bridge_detail_style == city::BridgeDetailStyle::Municipal)
            ++municipal;
        if (road.id == 5) rimway = &road;
        if (road.id == 2) kessel = &road;
    }
    REQUIRE(rimway != nullptr);
    REQUIRE(kessel != nullptr);
    REQUIRE(rimway->structure == city::RoadStructure::Bridge);
    REQUIRE(rimway->bridge_detail_style == city::BridgeDetailStyle::Municipal);
    REQUIRE(rimway->count == 2);
    REQUIRE_NEAR(rimway->deck_y_m, 6.0f, 1e-6f);
    REQUIRE_NEAR(rimway->width_m, 0.0f, 1e-6f);  // retain class width
    REQUIRE(kessel->bridge_detail_style == city::BridgeDetailStyle::None);
    REQUIRE(municipal == 1);

    const Built& b = built();
    bool mapped = false;
    RoadSpine target_spine;
    for (const RoadSpine& spine : b.spines) {
        if (spine.id != 5) continue;
        mapped = true;
        target_spine = spine;
        REQUIRE(spine.bridge_detail_style == BridgeDetailStyle::Municipal);
        REQUIRE(spine.structure == RoadStructure::Bridge);
        REQUIRE(spine.points.size() == 2);
    }
    REQUIRE(mapped);

    RoadGraph target_graph;
    target_graph.build({target_spine}, RoadGraphParams{}, b.ground.sampler());
    const RibbonBake target_bake =
        bake_ribbons(target_graph, b.ground.sampler());
    const RoadCollision target_collision = build_road_collision(target_bake);
    REQUIRE(!target_bake.layer(RoadLayer::Structure).empty());
    REQUIRE(target_bake.solids.size() > 100);
    REQUIRE(target_collision.solids.size() == target_bake.solids.size());

    const glm::vec2 tangent = glm::normalize(target_spine.points.back() -
                                             target_spine.points.front());
    const glm::vec2 side{-tangent.y, tangent.x};
    int pier_columns = 0;
    for (std::size_t i = 0; i < target_bake.solids.size(); ++i) {
        const auto& solid = target_bake.solids[i];
        const auto& collider = target_collision.solids[i];
        REQUIRE_NEAR(glm::length(solid.centre - collider.centre), 0.0f, 1e-6f);
        REQUIRE_NEAR(glm::length(solid.half - collider.half), 0.0f, 1e-6f);
        if (std::fabs(solid.half.x - 1.05f) < 0.01f &&
            std::fabs(solid.half.z - 1.45f) < 0.01f)
            ++pier_columns;

        if (solid.centre.y + solid.half.y >
            rimway->deck_y_m + kDrapeEpsM + 0.01f) {
            const glm::vec2 from_start{solid.centre.x - target_spine.points[0].x,
                                       solid.centre.z - target_spine.points[0].y};
            const float lateral = std::fabs(glm::dot(from_start, side));
            REQUIRE_MSG(lateral - solid.half.x >= 15.0f - 0.001f,
                        "bridge furniture intrudes into the freeway surface",
                        "Rimway Creek Bridge");
        }
    }
    REQUIRE_MSG(pier_columns >= 2,
                "the municipal bridge has no visible pier support",
                "Rimway Creek Bridge");
    for (const TerrainVertex& v :
         target_bake.layer(RoadLayer::Carriageway).vertices)
        REQUIRE_NEAR(v.position.y, rimway->deck_y_m + kDrapeEpsM, 1e-5f);
    apricot_test::pass("Rimway Creek alone opts into the municipal bridge kit");
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

    // O'Haven is one connected road component. Florangia is a separate state
    // across open water, so its first highway is deliberately a second
    // component until a ferry or bridge is actually authored.
    std::size_t reached = 0;
    std::size_t florangia_nodes = 0;
    uint32_t florangia_start =
        static_cast<uint32_t>(b.graph.node_count());
    for (const float v : dist) {
        if (v >= 0.0f) ++reached;
    }
    for (uint32_t node = 0; node < b.graph.node_count(); ++node) {
        if (dist[node] >= 0.0f) continue;
        bool belongs_to_florangia = true;
        for (const uint32_t edge : b.graph.node(node).edges) {
            const uint32_t id = b.graph.edge(edge).spine_id;
            belongs_to_florangia = belongs_to_florangia && id >= 220u &&
                                   id <= 234u;
        }
        REQUIRE_MSG(belongs_to_florangia,
                    "an O'Haven road fell out of its connected component",
                    "connectivity");
        if (florangia_start == static_cast<uint32_t>(b.graph.node_count()))
            florangia_start = node;
        ++florangia_nodes;
    }

    std::vector<bool> in_florangia_component(nodes, false);
    std::queue<uint32_t> fq;
    REQUIRE(florangia_start < b.graph.node_count());
    in_florangia_component[florangia_start] = true;
    fq.push(florangia_start);
    while (!fq.empty()) {
        const uint32_t n = fq.front();
        fq.pop();
        for (const uint32_t ei : b.graph.node(n).edges) {
            const RoadEdge& e = b.graph.edge(ei);
            const uint32_t other = (e.node_a == n) ? e.node_b : e.node_a;
            if (in_florangia_component[other]) continue;
            in_florangia_component[other] = true;
            fq.push(other);
        }
    }
    for (uint32_t node = 0; node < b.graph.node_count(); ++node) {
        if (dist[node] < 0.0f)
            REQUIRE_MSG(in_florangia_component[node],
                        "Florangia split into multiple road components",
                        "connectivity");
    }
    std::printf("    %zu of %zu nodes reachable\n", reached, nodes);
    REQUIRE(florangia_nodes >= 16);
    REQUIRE(reached + florangia_nodes == nodes);

    apricot_test::pass("O'Haven stays connected and Florangia forms one road component");
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

    // The regional-hospital superblock deliberately removes fourteen internal
    // crossings. The surrounding district remains a dense choice-heavy grid.
    REQUIRE_MSG(four_way >= 80,
                "Vellum Row lost too much of its surrounding street grid",
                "vellum grid");
    apricot_test::pass("Vellum Row stays a dense grid around the hospital superblock");
}

void vellum_north_extension_has_two_connected_streets() {
    const Built& b = built();
    const auto road_with_id = [](uint32_t id) -> const Road* {
        for (const auto& road : kRoads) {
            if (road.id == id) return &road;
        }
        return nullptr;
    };
    const std::array<uint32_t, 2> extension_ids{{205u, 206u}};
    const std::array<float, 2> extension_south{{-372.0f, -434.0f}};
    const std::array<float, 4> north_south_east{{
        92.0f, 184.0f, 276.0f, 368.0f,
    }};
    for (std::size_t row = 0; row < extension_ids.size(); ++row) {
        const Road* road = road_with_id(extension_ids[row]);
        REQUIRE(road != nullptr);
        REQUIRE(road->count == 2);
        const auto west = city::vellum_road_point(92.0f, extension_south[row]);
        const auto east = city::vellum_road_point(368.0f, extension_south[row]);
        REQUIRE_NEAR(road->path[0].x, west.x, 0.01f);
        REQUIRE_NEAR(road->path[0].z, west.z, 0.01f);
        REQUIRE_NEAR(road->path[1].x, east.x, 0.01f);
        REQUIRE_NEAR(road->path[1].z, east.z, 0.01f);
        for (const float local_east : north_south_east) {
            const auto point = city::vellum_road_point(local_east,
                                                        extension_south[row]);
            const std::size_t expected_edges =
                row == 0 ? (local_east == 92.0f || local_east == 368.0f ? 3u : 4u)
                         : (local_east == 92.0f || local_east == 368.0f ? 2u : 3u);
            bool connected = false;
            for (uint32_t node_index = 0; node_index < b.graph.node_count();
                 ++node_index) {
                const RoadNode& node = b.graph.node(node_index);
                if (glm::length(node.pos - glm::vec2{point.x, point.z}) < 0.25f &&
                    node.edges.size() >= expected_edges) {
                    connected = true;
                    break;
                }
            }
            REQUIRE_MSG(connected, "north Vellum cross street did not connect",
                        road->name);
        }
    }
    apricot_test::pass("Eleventh and Twelfth form a compact east-side Vellum extension");
}

void vellum_connectors_reach_the_requested_arterials() {
    const Built& b = built();
    const auto road_with_id = [](uint32_t id) -> const Road* {
        for (const auto& road : kRoads)
            if (road.id == id) return &road;
        return nullptr;
    };
    const Road* rook = road_with_id(23u);
    const Road* vellum = road_with_id(24u);
    const Road* halloway = road_with_id(35u);
    const Road* north_arm = road_with_id(50u);
    const Road* nickel = road_with_id(182u);
    REQUIRE(rook != nullptr && vellum != nullptr && halloway != nullptr);
    REQUIRE(north_arm != nullptr && nickel != nullptr);

    const auto halloway_end = halloway->path[halloway->count - 1];
    REQUIRE_NEAR(nickel->path[0].x, halloway_end.x, 0.001f);
    REQUIRE_NEAR(nickel->path[0].z, halloway_end.z, 0.001f);
    REQUIRE(std::fabs(nickel->path[0].z - 65.6f) > 40.0f);

    const auto north_end = north_arm->path[north_arm->count - 1];
    const auto vellum_end = vellum->path[vellum->count - 1];
    const auto rook_end = rook->path[rook->count - 1];
    const glm::vec2 north_point{north_end.x, north_end.z};
    REQUIRE_NEAR(north_end.x, vellum_end.x, 0.001f);
    REQUIRE_NEAR(north_end.z, vellum_end.z, 0.001f);
    REQUIRE_NEAR(north_end.y, vellum_end.y, 0.001f);
    REQUIRE(glm::length(north_point - glm::vec2{rook_end.x, rook_end.z}) >
            80.0f);

    const auto node_has_roads = [&](glm::vec2 point,
                                    std::initializer_list<uint32_t> ids,
                                    std::size_t minimum_degree) {
        for (uint32_t node_index = 0; node_index < b.graph.node_count();
             ++node_index) {
            const RoadNode& node = b.graph.node(node_index);
            if (glm::length(node.pos - point) > 0.25f) continue;
            bool all_present = true;
            for (uint32_t id : ids) {
                bool present = false;
                for (uint32_t edge_index : node.edges)
                    present = present ||
                        b.graph.edge(edge_index).spine_id == id;
                all_present = all_present && present;
            }
            return all_present && node.edges.size() >= minimum_degree;
        }
        return false;
    };
    REQUIRE_MSG(node_has_roads({halloway_end.x, halloway_end.z},
                               {35u, 182u}, 2u),
                "Nickel Road did not join Halloway Street", "road graph");
    REQUIRE_MSG(node_has_roads(north_point, {24u, 50u}, 2u),
                "North Arm did not join Vellum Row", "road graph");
    REQUIRE_MSG(!node_has_roads({rook_end.x, rook_end.z}, {23u, 50u}, 2u),
                "North Arm still joins Rook Lane", "road graph");
    apricot_test::pass("Nickel Road joins Halloway Street and North Arm joins Vellum Row");
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

// THE PREMIER ROADBLOCK SITE, PROVED ON THE GROUND.
//
// city_map_tests.cpp does this for the Camber channel, where a Grade composed
// after a Carve reconnects exactly one 35 m strip. The Kessel Channel is the
// opposite claim and needs its own check: Route 1 runs to the water on BOTH
// banks with a 27 m corridor and a 30 m feather, and the only thing stopping
// those two corridors from meeting in the middle and quietly deleting the
// island's best chokepoint is that the bridge between them is DECKED and a
// decked road may never shape the ground.
//
// That rule is a compile-time error in roads.h. This is the measurement that
// says the rule is doing what it was written for.
void the_kessel_bridge_is_the_only_crossing() {
    auto column_is_dry = [](float x) {
        for (float z = -1500.0f; z <= -1100.0f; z += 5.0f) {
            if (height_at(kMapSeed, x, z) <= kSeaLevelMetres) return false;
        }
        return true;
    };

    int bridges = 0;
    bool prev = false;
    for (float x = -2600.0f; x <= 400.0f; x += 5.0f) {
        const bool dry = column_is_dry(x);
        if (dry && !prev) ++bridges;
        prev = dry;
    }
    // Read the deck out of the table rather than repeating it here, or this
    // line becomes a number that used to be true.
    const Road* deck = nullptr;
    for (int i = 0; i < kRoadCount; ++i) {
        if (kRoads[i].id == 2) deck = &kRoads[i];
    }
    REQUIRE_MSG(deck != nullptr, "the Kessel Bridge is gone from the table",
                "Kessel Channel");
    std::printf("\n  Kessel Channel: %d land bridge(s) across it; the Kessel "
                "Bridge is a %.0f m deck at %.0f m above the water\n",
                bridges, static_cast<double>(deck->length_m()),
                static_cast<double>(deck->deck_y_m));
    REQUIRE_MSG(bridges == 0,
                "something has filled in the Kessel Channel, so the Kessel "
                "Bridge is no longer the only way across and blocking it costs "
                "a pursuit nothing",
                "Kessel Channel");
    apricot_test::pass("the Kessel Bridge is the only crossing of the channel");
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

LaneRef authored_lane(const Built& b, const LaneGraph& lanes, uint32_t spine_id,
                      bool forward, uint8_t index, bool at_profiled_end = false) {
    for (LaneRef r = 0; r < lanes.lane_count(); ++r) {
        const Lane& l = lanes.lane(r);
        const RoadEdge& e = b.graph.edge(l.edge);
        if (e.spine_id == spine_id && l.forward == forward && l.index == index &&
            (!at_profiled_end || e.lane_connect_start || e.lane_connect_end))
            return r;
    }
    return kInvalidLane;
}

bool links_to(const LaneGraph& lanes, LaneRef from, LaneRef to) {
    if (!lanes.valid(from) || !lanes.valid(to)) return false;
    for (const TurnLink& link : lanes.outgoing(from))
        if (link.to == to) return true;
    return false;
}

bool planar_boxes_overlap(glm::vec2 a_centre, glm::vec2 a_forward,
                          float a_half_width, float a_half_length,
                          glm::vec2 b_centre, glm::vec2 b_forward,
                          float b_half_width, float b_half_length) {
    a_forward = glm::normalize(a_forward);
    b_forward = glm::normalize(b_forward);
    const glm::vec2 a_right{-a_forward.y, a_forward.x};
    const glm::vec2 b_right{-b_forward.y, b_forward.x};
    const glm::vec2 delta = b_centre - a_centre;
    for (const glm::vec2 axis : {a_forward, a_right, b_forward, b_right}) {
        const float a_radius =
            a_half_width * std::fabs(glm::dot(a_right, axis)) +
            a_half_length * std::fabs(glm::dot(a_forward, axis));
        const float b_radius =
            b_half_width * std::fabs(glm::dot(b_right, axis)) +
            b_half_length * std::fabs(glm::dot(b_forward, axis));
        if (std::fabs(glm::dot(delta, axis)) > a_radius + b_radius)
            return false;
    }
    return true;
}

void halloway_auxiliary_lanes_match_the_four_ramps() {
    const Built& b = built();
    LaneGraph lanes;
    lanes.build(b.graph, b.ground.sampler(), LaneBuildParams{});

    const RoadEdge* west_taper = nullptr;
    const RoadEdge* west_gore = nullptr;
    const RoadEdge* east_gore = nullptr;
    for (const RoadEdge& e : b.graph.edges())
        if (e.spine_id == 18) west_taper = &e;
        else if (e.spine_id == 45) west_gore = &e;
        else if (e.spine_id == 46) east_gore = &e;
    REQUIRE_MSG(west_taper != nullptr, "west auxiliary taper is missing", "Halloway ramps");
    REQUIRE(std::fabs(west_taper->width_start_m - 30.0f) < 0.01f);
    REQUIRE(std::fabs(west_taper->width_end_m - 40.0f) < 0.01f);
    REQUIRE(west_taper->lanes_start_per_dir == 3);
    REQUIRE(west_taper->lanes_end_per_dir == 4);
    REQUIRE_MSG(west_gore != nullptr, "west ramp gore taper is missing", "Halloway ramps");
    REQUIRE_MSG(east_gore != nullptr, "east ramp gore taper is missing", "Halloway ramps");
    REQUIRE_NEAR(west_gore->width_start_m, 40.0f, 0.01f);
    REQUIRE_NEAR(west_gore->width_end_m, 30.0f, 0.01f);
    REQUIRE(west_gore->lanes_start_per_dir == 3);
    REQUIRE(west_gore->lanes_end_per_dir == 3);
    REQUIRE_NEAR(east_gore->width_start_m, 30.0f, 0.01f);
    REQUIRE_NEAR(east_gore->width_end_m, 40.0f, 0.01f);
    REQUIRE(east_gore->lanes_start_per_dir == 3);
    REQUIRE(east_gore->lanes_end_per_dir == 3);

    // The taper is surface, shoulder and paint. It must not fan live traffic
    // across the gore or spawn a phantom fourth lane beside either ramp.
    for (uint32_t spine_id : {45u, 46u}) {
        for (uint8_t i = 0; i < 3; ++i) {
            const Lane& lane = lanes.lane(authored_lane(b, lanes, spine_id, true, i));
            REQUIRE_NEAR(lane.lateral_offset_start_m,
                         lane.lateral_offset_end_m, 0.01f);
        }
    }

    // The three original through lane centres do not wander when the shoulder
    // opens. The fourth lane begins at the old shoulder and reaches full lane
    // width at the end of the taper.
    for (uint8_t i = 0; i < 3; ++i) {
        const LaneRef r = authored_lane(b, lanes, 18, true, i);
        REQUIRE(lanes.valid(r));
        const Lane& l = lanes.lane(r);
        REQUIRE(std::fabs(l.lateral_offset_start_m - l.lateral_offset_end_m) < 0.01f);
    }
    const Lane& born = lanes.lane(authored_lane(b, lanes, 18, true, 3));
    REQUIRE(std::fabs(born.lateral_offset_start_m - 12.5f) < 0.01f);
    REQUIRE(std::fabs(born.lateral_offset_end_m - 17.5f) < 0.01f);

    const LaneRef wb_on = authored_lane(b, lanes, 13, true, 0, true);
    const LaneRef wb_aux = authored_lane(b, lanes, 29, false, 3);
    const LaneRef wb_exit = authored_lane(b, lanes, 14, true, 0, true);
    const LaneRef wb_exit_lane = authored_lane(b, lanes, 43, false, 3);
    const LaneRef eb_on = authored_lane(b, lanes, 15, true, 0, true);
    const LaneRef eb_aux = authored_lane(b, lanes, 43, true, 3);
    const LaneRef eb_exit = authored_lane(b, lanes, 16, true, 0, true);
    const LaneRef eb_exit_lane = authored_lane(b, lanes, 29, true, 3);
    REQUIRE(links_to(lanes, wb_on, wb_aux));
    REQUIRE(links_to(lanes, wb_exit_lane, wb_exit));
    REQUIRE(links_to(lanes, eb_on, eb_aux));
    REQUIRE(links_to(lanes, eb_exit_lane, eb_exit));
    REQUIRE(lanes.outgoing(wb_exit_lane).size() == 1);
    REQUIRE(lanes.outgoing(eb_exit_lane).size() == 1);

    const auto seam = [&](LaneRef from, LaneRef to) {
        const glm::vec3 a = lanes.lane(from).centreline.back();
        const glm::vec3 c = lanes.lane(to).centreline.front();
        return glm::length(glm::vec2{a.x - c.x, a.z - c.z});
    };
    REQUIRE(seam(wb_on, wb_aux) < 0.01f);
    REQUIRE(seam(wb_exit_lane, wb_exit) < 0.01f);
    REQUIRE(seam(eb_on, eb_aux) < 0.01f);
    REQUIRE(seam(eb_exit_lane, eb_exit) < 0.01f);

    // An exit lane starts one lane-width outside the original outer through
    // lane and must peel farther away. The old second control point bent both
    // exits back onto that live lane, so ramp and through traffic occupied the
    // same asphalt for roughly 50 m.
    const LaneRef wb_through = authored_lane(b, lanes, 11, false, 2);
    const LaneRef eb_through = authored_lane(b, lanes, 11, true, 2);
    const auto exit_clearance = [&](LaneRef ramp, LaneRef through) {
        float closest = 1.0e9f;
        const float run = std::min(80.0f, lanes.length(ramp));
        for (float d = 0.0f; d <= run; d += 0.5f) {
            const LanePose p = lanes.pose(ramp, d);
            closest = std::min(closest, std::sqrt(
                lanes.project_onto(through, {p.position.x, p.position.z}).dist2));
        }
        return closest;
    };
    REQUIRE(exit_clearance(wb_exit, wb_through) >= 4.5f);
    REQUIRE(exit_clearance(eb_exit, eb_through) >= 4.5f);

    // Use the largest traffic shell plus 15 cm of breathing room against the
    // actual baked/collided viaduct parapet boxes. This is the physical check
    // the lane-only traffic sim cannot make.
    const auto is_halloway = [](uint32_t id) {
        for (const uint32_t wanted :
             {11u, 13u, 14u, 15u, 16u, 18u, 19u, 29u, 43u, 44u, 45u, 46u})
            if (id == wanted) return true;
        return false;
    };
    float sampled_lane_m = 0.0f;
    for (LaneRef lane_ref = 0; lane_ref < lanes.lane_count(); ++lane_ref) {
        const Lane& lane = lanes.lane(lane_ref);
        const RoadEdge& edge = b.graph.edge(lane.edge);
        if (!is_halloway(edge.spine_id)) continue;
        for (float d = 0.0f; d <= lane.length_m; d += 1.0f) {
            const LanePose pose = lanes.pose(lane_ref, d);
            ++sampled_lane_m;
            for (const RibbonBake::Solid& solid : b.bake.solids) {
                // Halloway's heavy viaduct parapets are the 0.35 x 0.55 m
                // boxes. Deck slabs and piers sit below traffic and are not
                // roadside obstacles.
                if (std::fabs(solid.half.x - 0.35f) > 0.01f ||
                    std::fabs(solid.half.y - 0.55f) > 0.01f)
                    continue;
                if (pose.position.y < solid.centre.y - solid.half.y - 0.1f ||
                    pose.position.y > solid.centre.y + solid.half.y + 0.1f)
                    continue;
                const glm::vec2 solid_forward{
                    std::sin(solid.yaw), std::cos(solid.yaw)};
                const bool overlap = planar_boxes_overlap(
                    {pose.position.x, pose.position.z},
                    {pose.tangent.x, pose.tangent.z}, 1.30f, 2.50f,
                    {solid.centre.x, solid.centre.z}, solid_forward,
                    solid.half.x, solid.half.z);
                if (overlap) {
                    std::printf("    Halloway barrier overlap: spine %u lane %u at %.1f m, solid %.1f %.1f\n",
                                edge.spine_id, lane.index, d,
                                solid.centre.x, solid.centre.z);
                }
                REQUIRE(!overlap);
            }
        }
    }

    std::printf("  Halloway: 3->4 lanes over %.0f m; four seams under 1 cm; %.0f lane-metres clear of parapets\n",
                west_taper->length_m, sampled_lane_m);
    apricot_test::pass("all four Halloway ramps use the right-side auxiliary lanes");
}

// road/road_class.h says an at-grade freeway crossing is an AUTHORING ERROR and
// that rejecting it is the MAP VALIDATOR'S job, not the road module's. This is
// the map validator.
void west_ramp_has_no_raised_asphalt_across_route_one() {
    const Built& b = built();
    TerrainCollider collider{kMapSeed};
    collider.set_road_collision(build_road_collision(b.bake));
    float worst_lift = 0.0f;
    // Both traffic directions and the full carriageway around the reported
    // crossing. The old distant junction miter stood 85 cm above this road.
    const glm::vec2 tangent = glm::normalize(glm::vec2{340.0f, 20.0f});
    const glm::vec2 side{-tangent.y, tangent.x};
    for (float x = -200.0f; x <= -165.0f; x += 0.5f) {
        const glm::vec2 centre{x, 470.0f + (x + 380.0f) * 20.0f / 340.0f};
        for (float offset = -12.0f; offset <= 12.0f; offset += 0.5f) {
            const glm::vec2 p = centre + side * offset;
            const float ground = collider.height(p.x, p.y);
            const auto hit = collider.probe_down({p.x, ground + 3.0f, p.y}, 5.0f);
            REQUIRE(hit.hit);
            const float lift = hit.point.y - ground;
            worst_lift = std::max(worst_lift, lift);
            REQUIRE_MSG(lift < 0.11f,
                        "a remote road plate lies above Route One traffic lanes",
                        "West Ramp");
        }
    }
    std::printf("  West Ramp highway: largest road lift %.3f m\n", worst_lift);
    apricot_test::pass("Route One at the West Ramp has no raised collision wedge");
}

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

// ---------------------------------------------------------------------------
//  7. THE ACCEPTANCE TEST: actually drive it
// ---------------------------------------------------------------------------
//
// Everything above proves the network is CONNECTED. That is a graph property
// and it is not the same claim as "you can drive from one district to another",
// which is a claim about a car, a gearbox, four tyres and the ground. A graph
// can be beautifully connected across a forty per cent side slope.
//
// So: plan a route with the real LaneGraph, put a real VehicleState on it, and
// drive it there through the real step_vehicle() against a real
// TerrainCollider at the real 120 Hz. The only thing invented here is the
// driver, and it is deliberately crude — a proportional steer at a look-ahead
// point and a throttle that holds a speed, with no recovery and no reverse. A
// road a crude driver cannot get down is a road worth knowing about.

struct Journey {
    bool arrived = false;
    float driven_m = 0.0f;
    double sim_s = 0.0;
    float worst_off_route_m = 0.0f;
    float worst_below_ground_m = 0.0f;
    float top_speed_mps = 0.0f;
    int steps_taken = 0;
};

// Where the car is on the route line.
struct OnLine {
    std::size_t seg = 0;
    float off_m = 0.0f;
    glm::vec2 point{0.0f};
};

// Project onto the polyline, searching FORWARD ONLY from `cursor`.
//
// Forward only, because a route can double back on itself — it does, out of
// Vellum Row, where the only merge onto Route 1 faces west — and a free search
// would let the car "progress" by snapping to the far arm.
//
// The search window is at least kMinSegs segments AND at least kMinM metres,
// and needing both is a lesson rather than a belt and braces. A lane centreline
// is sampled by the draper, so a freeway leg can be one 700 m segment while an
// alley is twenty 6 m ones. A window expressed only in metres pins the cursor
// on the long segment forever; one expressed only in segments scans a kilometre
// of alley every step.
OnLine project_forward(const std::vector<glm::vec3>& line, glm::vec2 here,
                       std::size_t& cursor) {
    constexpr std::size_t kMinSegs = 12;
    constexpr float kMinM = 260.0f;

    OnLine best;
    float best_d2 = 1e30f;
    float scanned = 0.0f;
    std::size_t n = 0;
    for (std::size_t i = cursor; i + 1 < line.size(); ++i, ++n) {
        const glm::vec2 a{line[i].x, line[i].z};
        const glm::vec2 b{line[i + 1].x, line[i + 1].z};
        const glm::vec2 e = b - a;
        const float len2 = glm::dot(e, e);
        float t = len2 > 0.0f ? glm::dot(here - a, e) / len2 : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        const glm::vec2 p = a + e * t;
        const float d2 = glm::dot(here - p, here - p);
        if (d2 < best_d2) {
            best_d2 = d2;
            best.seg = i;
            best.point = p;
        }
        scanned += std::sqrt(len2);
        if (n + 1 >= kMinSegs && scanned >= kMinM) break;
    }
    best.off_m = std::sqrt(best_d2);
    cursor = best.seg;
    return best;
}

// A point `ahead` metres further along the line, INTERPOLATED rather than
// snapped to the next vertex. Snapping makes the target jump seven hundred
// metres when the car joins a freeway, and the car weaves after it.
glm::vec2 point_ahead(const std::vector<glm::vec3>& line, const OnLine& at,
                      float ahead) {
    glm::vec2 p = at.point;
    float left = ahead;
    for (std::size_t i = at.seg; i + 1 < line.size(); ++i) {
        const glm::vec2 b{line[i + 1].x, line[i + 1].z};
        const float d = glm::length(b - p);
        if (d >= left) return d > 0.0f ? p + (b - p) * (left / d) : b;
        left -= d;
        p = b;
    }
    return glm::vec2{line.back().x, line.back().z};
}

// Concatenate a route's lanes into one polyline, dropping the duplicate point
// where one lane ends and the next begins.
std::vector<glm::vec3> route_line(const LaneGraph& lanes,
                                  const std::vector<LaneRef>& route) {
    std::vector<glm::vec3> out;
    for (const LaneRef r : route) {
        for (const glm::vec3& p : lanes.lane(r).centreline) {
            if (!out.empty() &&
                glm::length(glm::vec2{p.x - out.back().x,
                                      p.z - out.back().z}) < 0.05f) {
                continue;
            }
            out.push_back(p);
        }
    }
    return out;
}

Journey drive(const std::vector<glm::vec3>& line, glm::vec2 destination,
              const TerrainCollider& collider, float target_speed_mps,
              int max_steps) {
    Journey j;
    if (line.size() < 2) return j;

    VehicleTuning tuning;
    // This harness measures whether the authored roads are physically
    // driveable, so keep it on a normal friction circle. The player profiles'
    // longitudinal-only arcade brake boost intentionally trades cornering
    // authority for shorter panic stops and is tested with those profiles.
    tuning.service_brake_grip_boost = 1.0f;
    const glm::vec2 d0 =
        glm::normalize(glm::vec2{line[1].x - line[0].x, line[1].z - line[0].z});
    // forward = orientation * (0,0,-1) and yaw turns about +Y, so a heading of
    // (fx, fz) is atan2(-fx, -fz).
    VehicleState car = spawn_vehicle(tuning, collider, line[0].x, line[0].z,
                                     std::atan2(-d0.x, -d0.y));

    std::size_t cursor = 0;
    glm::vec3 last = car.position;

    for (int step = 0; step < max_steps; ++step) {
        const glm::vec2 here{car.position.x, car.position.z};

        // ARRIVED MEANS "reached the place we were sent", not "reached the end
        // of the last lane on the route". The last lane can be a 1.5 km
        // perimeter loop whose far end is nowhere near the destination.
        if (glm::length(destination - here) < 25.0f) {
            j.arrived = true;
            break;
        }

        const OnLine on = project_forward(line, here, cursor);
        j.worst_off_route_m = std::max(j.worst_off_route_m, on.off_m);

        const float speed = glm::length(glm::vec2{car.velocity.x, car.velocity.z});
        j.top_speed_mps = std::max(j.top_speed_mps, speed);

        // Look further ahead the faster we go: the whole trick that keeps a
        // proportional controller from weaving.
        const glm::vec2 target = point_ahead(line, on, 10.0f + speed * 0.85f);

        const glm::vec3 fwd = vehicle_forward(car);
        const glm::vec2 f2 = glm::normalize(glm::vec2{fwd.x, fwd.z});
        glm::vec2 tt = target - here;
        tt = glm::length(tt) < 0.01f ? f2 : glm::normalize(tt);
        // Positive cross means the target is to the RIGHT of travel, which is
        // the same sign as InputFrame::steer. Getting this backwards drives the
        // car away from the route in a slow spiral and reads exactly like a map
        // whose roads do not connect.
        const float alpha =
            std::atan2(f2.x * tt.y - f2.y * tt.x, glm::dot(f2, tt));

        InputFrame in;
        in.steer = std::max(-1.0f, std::min(1.0f, alpha * 1.8f));
        // Slow down FOR the corner instead of trying to steer through it at
        // speed. Without this the car understeers off every switchback and the
        // suite measures the driver rather than the road.
        const float want =
            target_speed_mps *
            (1.0f - 0.6f * std::min(1.0f, std::fabs(alpha) * 1.7f));
        if (speed < want) {
            in.throttle = 1.0f;
        } else if (speed > want * 1.1f) {
            in.brake = 0.5f;
        }

        car = step_vehicle(car, tuning, in, collider, static_cast<float>(kSimDt));
        ++j.steps_taken;

        j.driven_m += glm::length(glm::vec2{car.position.x - last.x,
                                            car.position.z - last.z});
        last = car.position;
        j.worst_below_ground_m =
            std::max(j.worst_below_ground_m,
                     collider.height(car.position.x, car.position.z) -
                         car.position.y);
    }
    j.sim_s = static_cast<double>(j.steps_taken) * kSimDt;
    return j;
}

void a_real_car_drives_every_halloway_ramp() {
    const Built& b = built();
    LaneGraph lanes;
    lanes.build(b.graph, b.ground.sampler(), LaneBuildParams{});
    TerrainCollider collider{kMapSeed};
    collider.set_road_collision(build_road_collision(b.bake));

    struct RampDrive {
        const char* name;
        std::vector<LaneRef> route;
    };
    const auto lane = [&](uint32_t id, bool forward, uint8_t index,
                          bool connector = false) {
        const LaneRef r = authored_lane(b, lanes, id, forward, index, connector);
        REQUIRE_MSG(lanes.valid(r), "authored ramp drive lane is missing", "Halloway drive");
        return r;
    };
    const auto linked = [&](LaneRef from, uint32_t id, uint8_t index) {
        for (const TurnLink& turn : lanes.outgoing(from)) {
            const Lane& candidate = lanes.lane(turn.to);
            if (b.graph.edge(candidate.edge).spine_id == id &&
                candidate.index == index) return turn.to;
        }
        return kInvalidLane;
    };
    const LaneRef wb_taper = lane(18,false,3);
    const LaneRef eb_taper = lane(44,true,3);
    const LaneRef wb_after_merge = linked(wb_taper, 6, 2);
    const LaneRef eb_after_merge = linked(eb_taper, 12, 2);
    REQUIRE(lanes.valid(wb_after_merge));
    REQUIRE(lanes.valid(eb_after_merge));
    const std::vector<RampDrive> drives{
        {"westbound on-ramp", {lane(13,true,0,true), lane(29,false,3),
            lane(19,false,3), wb_taper, wb_after_merge}},
        {"westbound off-ramp", {lane(44,false,3), lane(43,false,3),
            lane(14,true,0,true)}},
        {"eastbound on-ramp", {lane(15,true,0,true), lane(43,true,3),
            eb_taper, eb_after_merge}},
        {"eastbound off-ramp", {lane(18,true,3), lane(19,true,3),
            lane(29,true,3), lane(16,true,0,true)}},
    };

    for (const RampDrive& ramp : drives) {
        for (std::size_t i = 0; i + 1 < ramp.route.size(); ++i)
            if (!links_to(lanes, ramp.route[i], ramp.route[i + 1])) {
                std::printf("      broken %u:%u -> %u:%u\n",
                    b.graph.edge(lanes.lane(ramp.route[i]).edge).spine_id,
                    lanes.lane(ramp.route[i]).index,
                    b.graph.edge(lanes.lane(ramp.route[i + 1]).edge).spine_id,
                    lanes.lane(ramp.route[i + 1]).index);
                REQUIRE_MSG(false, "ramp route has a lane-graph break", ramp.name);
            }
        const std::vector<glm::vec3> line = route_line(lanes, ramp.route);
        const glm::vec2 destination{line.back().x, line.back().z};
        const Journey result = drive(line, destination, collider, 12.0f, 18000);
        std::printf("    %-24s %s, drove %.0f m, worst %.1f m off lane, sank %.2f m\n",
                    ramp.name, result.arrived ? "ARRIVED" : "FAILED",
                    result.driven_m, result.worst_off_route_m,
                    result.worst_below_ground_m);
        REQUIRE_MSG(result.arrived, "production vehicle did not clear ramp", ramp.name);
        REQUIRE_MSG(result.worst_below_ground_m < 0.08f,
                    "vehicle fell through ramp collision", ramp.name);
    }
    apricot_test::pass("a real production vehicle drives all four Halloway ramps");
}

void run_journey(const LaneGraph& lanes, const TerrainCollider& collider,
                 const char* what, glm::vec2 from, glm::vec2 to,
                 float speed_mps, int max_steps,
                 std::initializer_list<uint32_t> required_spines = {}) {
    // Start on a lane already pointing the right way. Without the heading the
    // nearest lane is whichever side of the street is a millimetre closer, and
    // half the time that is the one going the other way.
    const LaneProjection a =
        lanes.nearest_lane_along(from, glm::normalize(to - from), 140.0f);
    const LaneProjection b = lanes.nearest_lane(to, 140.0f);
    REQUIRE_MSG(a.valid() && b.valid(),
                "no lane within 140 m of one end of this journey", what);

    const std::vector<LaneRef> route = lanes.plan_route(a.lane, b.lane);
    REQUIRE_MSG(!route.empty(), "the lane graph could not plan this journey",
                what);
    for (uint32_t required : required_spines) {
        bool present = false;
        for (LaneRef lane : route)
            present = present ||
                built().graph.edge(lanes.lane(lane).edge).spine_id == required;
        REQUIRE_MSG(present, "journey bypassed its required authored road", what);
    }

    const std::vector<glm::vec3> line = route_line(lanes, route);
    float plan_m = 0.0f;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        plan_m += glm::length(glm::vec2{line[i + 1].x - line[i].x,
                                        line[i + 1].z - line[i].z});
    }

    const Journey j = drive(line, to, collider, speed_mps, max_steps);
    std::printf("    %-40s %s  %5.0f m planned over %2zu lanes, drove %5.0f m "
                "in %5.1f s, top %4.1f m/s, %4.1f m off the lane at worst, "
                "sank %.2f m\n",
                what, j.arrived ? "ARRIVED" : "STOPPED",
                static_cast<double>(plan_m), route.size(),
                static_cast<double>(j.driven_m), j.sim_s,
                static_cast<double>(j.top_speed_mps),
                static_cast<double>(j.worst_off_route_m),
                static_cast<double>(j.worst_below_ground_m));

    REQUIRE_MSG(j.arrived,
                "a real car could not complete this journey on the authored "
                "roads",
                what);
    // The chassis origin under the drawn ground means the car fell through
    // something. Suspension travel is centimetres; half a metre is a hole.
    REQUIRE_MSG(j.worst_below_ground_m < 0.5f,
                "the car sank below the drawn ground on this journey", what);
}

void a_real_car_drives_from_district_to_district() {
    const Built& b = built();
    LaneGraph lanes;
    lanes.build(b.graph, b.ground.sampler(), LaneBuildParams{});
    REQUIRE_MSG(lanes.lane_count() > 100, "the lane graph is nearly empty",
                "drive");

    TerrainCollider collider{kMapSeed};
    collider.set_road_collision(build_road_collision(b.bake));
    std::printf("\n  DRIVING IT, real vehicle, real terrain, %d Hz: %zu lanes "
                "over %zu junctions\n",
                static_cast<int>(kSimHz), lanes.lane_count(),
                lanes.junction_count());

    run_journey(lanes, collider, "Nickel Road -> Halloway Street",
                glm::vec2{650.0f, 61.0f}, glm::vec2{350.0f, -10.5f},
                12.0f, 30000, {182u, 35u});
    run_journey(lanes, collider, "North Arm -> Vellum Row",
                glm::vec2{-50.0f, 450.0f}, glm::vec2{60.0f, 100.0f},
                12.0f, 30000, {50u, 24u});

    // Downtown to the airport's public frontage: out of Vellum Row's grid,
    // onto Route 1, along the Camber Reach, over the one land bridge in 2.5 km
    // of water, then around runway 09-27 on the airport parkway.
    run_journey(lanes, collider, "Vellum Row -> Camber Point",
                glm::vec2{70.0f, -40.0f}, glm::vec2{600.0f, 2410.0f}, 17.0f,
                180000);

    // Downtown to the top of the hill: the Shoulder's switchbacks at 9.5 per
    // cent. If the hairpin platforms are wrong, this is where it shows.
    run_journey(lanes, collider, "Vellum Row -> Ferrone Hill (the Shoulder)",
                glm::vec2{70.0f, -40.0f}, glm::vec2{880.0f, -1580.0f}, 14.0f,
                180000);

    // Across the water, over the Kessel Bridge. The longest drive on the map.
    run_journey(lanes, collider, "Vellum Row -> Kepler Flats",
                glm::vec2{70.0f, -40.0f}, glm::vec2{-500.0f, -1900.0f}, 17.0f,
                240000);

    // Out to the 2.2 km straight, which is the one place the answer is speed.
    run_journey(lanes, collider, "Vellum Row -> The Strand",
                glm::vec2{70.0f, -40.0f}, glm::vec2{1900.0f, -300.0f}, 20.0f,
                180000);

    // And out to the dirt.
    run_journey(lanes, collider, "Vellum Row -> Marrow",
                glm::vec2{70.0f, -40.0f}, glm::vec2{-1100.0f, 1200.0f}, 15.0f,
                240000);

    apricot_test::pass("a real car drives from one district to another");
}

}  // namespace

int main() {
    std::printf("city_roads_tests\n");
    the_network_is_the_size_the_table_says();
    rimway_creek_bridge_uses_its_own_municipal_profile();
    // The per-road diagnostic runs BEFORE the assertion it diagnoses. A suite
    // that asserts first and explains second tells you the number is wrong and
    // then exits without telling you which road it was.
    which_roads_are_worst();
    the_road_sits_on_the_ground_at_every_level();
    you_can_drive_from_any_district_to_any_other();
    vellum_row_is_a_grid_of_four_way_junctions();
    vellum_north_extension_has_two_connected_streets();
    vellum_connectors_reach_the_requested_arterials();
    the_strand_has_no_turnoffs();
    there_is_one_paved_way_up_ferrone_hill();
    the_kessel_bridge_is_the_only_crossing();
    nickel_heights_punishes_panic();
    no_road_runs_through_the_sea();
    authored_heights_match_the_ground_they_claim();
    nothing_climbs_faster_than_a_car_can();
    halloway_auxiliary_lanes_match_the_four_ramps();
    west_ramp_has_no_raised_asphalt_across_route_one();
    no_freeway_crosses_anything_at_grade();
    the_operator_table_is_still_cheap();
    a_real_car_drives_every_halloway_ramp();
    a_real_car_drives_from_district_to_district();
    return apricot_test::done("city_roads_tests");
}
