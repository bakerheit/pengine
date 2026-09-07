// The ribbon bake, and the collision that comes out of it.
//
// The claim this suite exists to defend is the one the repo has already paid
// for twice: THE SOLID THE CAR TOUCHES IS THE SURFACE THAT DRAWS. Both times
// the cause was a second implementation of something that already existed, and
// both times it looked fine until somebody drove there. So there is a negative
// control here — move the baked vertices and require the collision to move
// with them — and it is the most important test in the file.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "road/ribbon.h"
#include "road_fixture.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

struct Baked {
    RoadGraph graph;
    RibbonBake bake;
};

Baked bake_fixture(const GroundSampler& ground) {
    Baked b;
    b.graph.build(make_test_spines(), RoadGraphParams{}, ground);
    b.bake = bake_ribbons(b.graph, ground);
    return b;
}

bool finite(glm::vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float cross2(glm::vec2 a, glm::vec2 b) {
    return a.x * b.y - a.y * b.x;
}

bool triangle_covers(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 p) {
    constexpr float kInsideEps = 1e-4f;
    const float area = cross2(b - a, c - a);
    if (std::fabs(area) < 1e-7f) return false;
    const float u = cross2(b - p, c - p) / area;
    const float v = cross2(c - p, a - p) / area;
    const float w = 1.0f - u - v;
    return u >= -kInsideEps && v >= -kInsideEps && w >= -kInsideEps;
}

bool mesh_covers(const RoadMesh& mesh, glm::vec2 p) {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3 av = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3 bv = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3 cv = mesh.vertices[mesh.indices[i + 2]].position;
        if (triangle_covers({av.x, av.z}, {bv.x, bv.z}, {cv.x, cv.z}, p))
            return true;
    }
    return false;
}

bool mesh_has_vertex(const RoadMesh& mesh,glm::vec2 p,float tolerance=.001f) {
    for(const TerrainVertex& v:mesh.vertices)
        if(glm::length(glm::vec2{v.position.x,v.position.z}-p)<=tolerance)
            return true;
    return false;
}

bool collision_covers(const RoadCollision& collision, RoadLayer layer, glm::vec2 p) {
    for (const RoadCollisionTri& tri : collision.triangles) {
        if (tri.layer != layer) continue;
        if (triangle_covers({tri.geom.a.x, tri.geom.a.z},
                            {tri.geom.b.x, tri.geom.b.z},
                            {tri.geom.c.x, tri.geom.c.z}, p))
            return true;
    }
    return false;
}

void test_every_layer_is_populated() {
    const Baked b = bake_fixture(GroundSampler{});
    REQUIRE(!b.bake.layer(RoadLayer::Carriageway).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Unpaved).empty());  // the dirt road
    REQUIRE(!b.bake.layer(RoadLayer::Walk).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Kerb).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Plate).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Crosswalk).empty());
    REQUIRE(!b.bake.layer(RoadLayer::WhiteMarking).empty());
    REQUIRE(!b.bake.layer(RoadLayer::YellowMarking).empty());
    // Three real crossings, plus one plate at the degree-2 node where the
    // street becomes an alley: 14 m of carriageway butting onto 6 m leaves a
    // notch, and a mitre cannot close a step in width.
    REQUIRE(b.bake.plates_baked == kFixtureJunctions + 1);
    // One zebra per approach where every road carries a sidewalk: 4 + 3.
    // The dirt T and width-step plate get none because their paint would lead
    // pedestrians into a road edge with no continuing walk.
    REQUIRE(b.bake.crosswalks_baked == 7);
    std::printf("      bake: %zu triangles over %zu layers, %zu plates\n",
                b.bake.total_triangles(), kRoadLayerCount, b.bake.plates_baked);
    pass("all eight layers bake, including curved lane paint and crossings");
}

void test_geometry_is_finite_and_indexed_in_range() {
    const Baked b = bake_fixture(GroundSampler{});
    for (std::size_t li = 0; li < kRoadLayerCount; ++li) {
        const RoadMesh& m = b.bake.layers[li];
        const char* name = road_layer_name(static_cast<RoadLayer>(li));
        REQUIRE_MSG(m.indices.size() % 3 == 0, "index count is a multiple of 3", name);
        for (const TerrainVertex& v : m.vertices) {
            REQUIRE_MSG(finite(v.position), "vertex position is finite", name);
            REQUIRE_MSG(finite(v.normal), "vertex normal is finite", name);
        }
        for (uint32_t i : m.indices)
            REQUIRE_MSG(i < m.vertices.size(), "index in range", name);
        if (!m.vertices.empty()) REQUIRE_MSG(m.bounds.valid(), "bounds valid", name);
    }
    pass("no NaN, no out-of-range index, bounds valid on every layer");
}

void test_flat_layers_face_up_and_kerbs_do_not() {
    // FRONT FACES ARE COUNTER-CLOCKWISE. A road wound the other way is not
    // subtly wrong, it is invisible from above once culling is on.
    const Baked b = bake_fixture(GroundSampler{});
    std::size_t flat = 0;
    for (std::size_t li = 0; li < kRoadLayerCount; ++li) {
        const RoadLayer layer = static_cast<RoadLayer>(li);
        if (layer == RoadLayer::Structure) continue; // closed solids have all face directions
        const RoadMesh& m = b.bake.layers[li];
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const glm::vec3 a = m.vertices[m.indices[i]].position;
            const glm::vec3 c0 = m.vertices[m.indices[i + 1]].position;
            const glm::vec3 c1 = m.vertices[m.indices[i + 2]].position;
            const glm::vec3 n = glm::cross(c0 - a, c1 - a);
            if (glm::length(n) < 1e-9f) continue;  // degenerate sliver
            if (layer == RoadLayer::Kerb) {
                REQUIRE_MSG(std::fabs(glm::normalize(n).y) < 0.05f,
                            "a kerb riser is a vertical face", "kerb");
            } else {
                REQUIRE_MSG(n.y > 0.0f, "flat road triangle must wind upward",
                            road_layer_name(layer));
                ++flat;
            }
        }
    }
    REQUIRE(flat > 100);  // not vacuously true on an empty bake
    std::printf("      %zu upward-wound flat triangles checked\n", flat);
    pass("every flat triangle winds upward; every kerb face is vertical");
}

void test_carriageway_spans_the_authored_width() {
    // The street spine runs due north-south at x = 0, so the carriageway's
    // half width IS the vertex's |x|. 14 m class -> 7 m either side.
    std::vector<RoadSpine> s;
    RoadSpine a;
    a.id = 1;
    a.cls = RoadClass::Street;
    a.points = {{0.0f, -100.0f}, {0.0f, 100.0f}};
    s.push_back(a);

    RoadGraph g;
    g.build(s, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bake = bake_ribbons(g, GroundSampler{});

    float widest = 0.0f;
    for (const TerrainVertex& v : bake.layer(RoadLayer::Carriageway).vertices)
        widest = std::max(widest, std::fabs(v.position.x));
    REQUIRE_NEAR(widest, 7.0f, 1e-4f);

    // The sidewalk sits outboard of the kerb and stands one kerb height up.
    float walk_out = 0.0f;
    for (const TerrainVertex& v : bake.layer(RoadLayer::Walk).vertices) {
        walk_out = std::max(walk_out, std::fabs(v.position.x));
        REQUIRE_NEAR(v.position.y, kDrapeEpsM + kKerbHeightM, 1e-5f);
    }
    REQUIRE_NEAR(walk_out, 7.0f + kSidewalkWidthM, 1e-4f);

    for (const TerrainVertex& v : bake.layer(RoadLayer::Carriageway).vertices)
        REQUIRE_NEAR(v.position.y, kDrapeEpsM, 1e-5f);
    pass("carriageway is 14 m wide, sidewalk is outboard and a kerb higher");
}

void test_markings_match_road_class_and_have_real_gaps() {
    std::vector<RoadSpine> streets;
    RoadSpine street;
    street.id = 80;
    street.cls = RoadClass::Street;
    street.points = {{0.0f, -100.0f}, {0.0f, 100.0f}};
    streets.push_back(street);

    RoadGraph street_graph;
    street_graph.build(streets, RoadGraphParams{}, GroundSampler{});
    const RibbonBake street_bake = bake_ribbons(street_graph, GroundSampler{});
    REQUIRE(street_bake.layer(RoadLayer::WhiteMarking).empty());
    const RoadMesh& street_yellow = street_bake.layer(RoadLayer::YellowMarking);
    REQUIRE(!street_yellow.empty());
    for (const TerrainVertex& v : street_yellow.vertices) {
        REQUIRE(std::fabs(v.position.x) <=
                RibbonParams{}.marking_width_m * 0.51f);
        REQUIRE_NEAR(v.position.y,
                     kDrapeEpsM + RibbonParams{}.marking_lift_m, 1e-5f);
        REQUIRE_MSG(std::fabs(v.position.z) > 2.5f,
                    "a centre dash gap must remain empty", "street paint");
    }

    std::vector<RoadSpine> majors;
    RoadSpine arterial;
    arterial.id = 81;
    arterial.cls = RoadClass::Arterial;
    arterial.points = {{-100.0f, 0.0f}, {100.0f, 0.0f}};
    majors.push_back(arterial);
    RoadGraph arterial_graph;
    arterial_graph.build(majors, RoadGraphParams{}, GroundSampler{});
    const RibbonBake arterial_bake = bake_ribbons(arterial_graph, GroundSampler{});
    REQUIRE(!arterial_bake.layer(RoadLayer::WhiteMarking).empty());
    REQUIRE(!arterial_bake.layer(RoadLayer::YellowMarking).empty());

    std::vector<RoadSpine> dirt;
    RoadSpine track = arterial;
    track.id = 82;
    track.cls = RoadClass::Dirt;
    dirt.push_back(track);
    RoadGraph dirt_graph;
    dirt_graph.build(dirt, RoadGraphParams{}, GroundSampler{});
    const RibbonBake dirt_bake = bake_ribbons(dirt_graph, GroundSampler{});
    REQUIRE(dirt_bake.layer(RoadLayer::WhiteMarking).empty());
    REQUIRE(dirt_bake.layer(RoadLayer::YellowMarking).empty());
    pass("paint follows road class: street dashes, major lines, no dirt paint");
}

void test_surface_rides_the_drawn_terrain() {
    // The other half of "collision derives from the geometry that draws": the
    // ribbon has to drape on mesh_height_at (the DRAWN triangle) and not on
    // height_at (the continuous field underneath it). They differ by
    // centimetres on a grade, which is exactly enough for a car to sink.
    const uint64_t seed = 0xDEADBEEFull;
    TerrainGround tg{seed};
    const Baked b = bake_fixture(tg.sampler());

    std::size_t checked = 0;
    double worst = 0.0;
    for (const TerrainVertex& v : b.bake.layer(RoadLayer::Carriageway).vertices) {
        // The bridge is in this layer too and is deliberately NOT draped.
        if (std::fabs(v.position.z + 250.0f) <= 20.0f) continue;
        const float want = mesh_height_at(seed, v.position.x, v.position.z) + kDrapeEpsM;
        worst = std::max(worst, std::fabs(static_cast<double>(v.position.y - want)));
        ++checked;
    }
    REQUIRE(checked > 500);
    REQUIRE_MSG(worst < 1e-4, "carriageway must sit on the drawn terrain", "drape");
    std::printf("      %zu carriageway vertices, worst drape error %.9f m\n",
                checked, worst);
    pass("the carriageway drapes on the drawn terrain, not on the field");
}

void test_bridge_deck_is_authored_and_never_draped() {
    const uint64_t seed = 0xDEADBEEFull;
    TerrainGround tg{seed};
    const Baked b = bake_fixture(tg.sampler());

    // The freeway is the only decked spine, and it is the only 30 m road, so
    // its vertices are the ones near z = -250.
    std::size_t deck = 0;
    for (const TerrainVertex& v : b.bake.layer(RoadLayer::Carriageway).vertices) {
        if (std::fabs(v.position.z + 250.0f) > 20.0f) continue;
        REQUIRE_NEAR(v.position.y, 26.0f + kDrapeEpsM, 1e-4f);
        ++deck;
    }
    REQUIRE(deck > 100);
    std::printf("      %zu bridge-deck vertices, all flat at 26 m\n", deck);
    pass("a bridge deck is authored flat: draping it would make it a causeway");
}

void test_municipal_bridge_kit_is_structural_and_keeps_lanes_clear() {
    RoadSpine bridge;
    bridge.id = 505;
    bridge.cls = RoadClass::Freeway;
    bridge.structure = RoadStructure::Bridge;
    bridge.deck_y_m = 6.0f;
    bridge.points = {{0.0f, 0.0f}, {0.0f, 120.0f}};
    bridge.bridge_detail_style = BridgeDetailStyle::Municipal;

    RoadGraph graph;
    graph.build({bridge}, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bake = bake_ribbons(graph, GroundSampler{});
    const RoadCollision collision = build_road_collision(bake);
    const float half_road = road_class_def(RoadClass::Freeway)
                                .carriageway_width_m * 0.5f;

    REQUIRE(!bake.layer(RoadLayer::Structure).empty());
    REQUIRE(bake.solids.size() > 100);
    REQUIRE(collision.solids.size() == bake.solids.size());

    bool deck_slab = false;
    bool low_parapet = false;
    bool upper_rail = false;
    bool rail_post = false;
    bool pier = false;
    bool pier_cap = false;
    bool end_diaphragm = false;
    for (std::size_t i = 0; i < bake.solids.size(); ++i) {
        const RibbonBake::Solid& drawn = bake.solids[i];
        const RibbonBake::Solid& collided = collision.solids[i];
        REQUIRE_NEAR(glm::length(drawn.centre - collided.centre), 0.0f, 1e-6f);
        REQUIRE_NEAR(glm::length(drawn.half - collided.half), 0.0f, 1e-6f);
        REQUIRE_NEAR(drawn.yaw, collided.yaw, 1e-6f);
        REQUIRE(drawn.half.x > 0.0f && drawn.half.y > 0.0f &&
                drawn.half.z > 0.0f);

        deck_slab |= drawn.half.x > half_road - 0.01f &&
                     drawn.half.y > 0.55f && drawn.half.z > 50.0f;
        low_parapet |= std::fabs(drawn.half.x - 0.28f) < 0.01f &&
                        std::fabs(drawn.half.y - 0.34f) < 0.01f;
        upper_rail |= std::fabs(drawn.half.x - 0.10f) < 0.01f &&
                       std::fabs(drawn.half.y - 0.09f) < 0.01f;
        rail_post |= std::fabs(drawn.half.x - 0.13f) < 0.01f &&
                      std::fabs(drawn.half.z - 0.13f) < 0.01f;
        pier |= std::fabs(drawn.half.x - 1.05f) < 0.01f &&
                std::fabs(drawn.half.z - 1.45f) < 0.01f;
        pier_cap |= drawn.half.x > 10.0f && drawn.half.x < 11.0f &&
                    std::fabs(drawn.half.z - 0.72f) < 0.01f;
        end_diaphragm |= drawn.half.x > 14.0f && drawn.half.x < 14.2f &&
                         std::fabs(drawn.half.z - 0.65f) < 0.01f;

        // Anything rising above the asphalt belongs outside the full 30 m
        // traffic surface. The deck, piers and caps all remain below it.
        if (drawn.centre.y + drawn.half.y >
            6.0f + kDrapeEpsM + 0.01f) {
            REQUIRE(std::fabs(drawn.centre.x) - drawn.half.x >=
                    half_road - 0.001f);
        }
    }
    REQUIRE(deck_slab && low_parapet && upper_rail && rail_post);
    REQUIRE(pier && pier_cap && end_diaphragm);

    for (const TerrainVertex& v : bake.layer(RoadLayer::Carriageway).vertices)
        REQUIRE_NEAR(v.position.y, 6.0f + kDrapeEpsM, 1e-5f);

    RoadSpine bare = bridge;
    bare.id = 506;
    bare.bridge_detail_style = BridgeDetailStyle::None;
    RoadGraph bare_graph;
    bare_graph.build({bare}, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bare_bake = bake_ribbons(bare_graph, GroundSampler{});
    REQUIRE(bare_bake.layer(RoadLayer::Structure).empty());
    REQUIRE(bare_bake.solids.empty());
    pass("municipal bridge kit adds restrained structure without narrowing lanes");
}

void test_collision_is_the_baked_geometry() {
    const Baked b = bake_fixture(GroundSampler{});
    const RoadCollision col = build_road_collision(b.bake);

    // Every structural flat triangle survives except genuine zero-area slivers.
    // Paint is visual only: putting it in collision makes a tyre climb 2 cm
    // every time it crosses a lane line.
    std::size_t flat_tris = 0;
    for (std::size_t li = 0; li < kRoadLayerCount; ++li) {
        const RoadLayer layer = static_cast<RoadLayer>(li);
        if (layer != RoadLayer::Kerb && layer != RoadLayer::Structure && layer != RoadLayer::Crosswalk &&
            layer != RoadLayer::WhiteMarking &&
            layer != RoadLayer::YellowMarking)
            flat_tris += b.bake.layers[li].triangle_count();
    }
    REQUIRE(!col.triangles.empty());
    REQUIRE(col.triangles.size() <= flat_tris);
    REQUIRE(col.triangles.size() > flat_tris * 9 / 10);

    for (const RoadCollisionTri& t : col.triangles) {
        REQUIRE(t.layer != RoadLayer::Kerb);
        REQUIRE(t.layer != RoadLayer::Crosswalk);
        REQUIRE(t.layer != RoadLayer::WhiteMarking);
        REQUIRE(t.layer != RoadLayer::YellowMarking);
        REQUIRE(t.geom.normal.y > 0.0f);
        REQUIRE_NEAR(glm::length(t.geom.normal), 1.0f, 1e-4f);
        REQUIRE(t.material == (t.layer == RoadLayer::Unpaved ? Surface::Gravel
                                                             : Surface::Rock));
    }
    std::printf("      %zu collision triangles from %zu drawn flat triangles\n",
                col.triangles.size(), flat_tris);
    pass("collision covers structural road surfaces and excludes visual paint");
}

void test_collision_follows_the_bake_and_cannot_re_derive_it() {
    // THE NEGATIVE CONTROL, and the reason this file exists.
    //
    // Move the baked vertices and the collision must move with them. A
    // build_road_collision that quietly re-derived the surface from the graph
    // — which is exactly how the height, normal and material bugs were each
    // introduced — would ignore this and return the old geometry.
    Baked b = bake_fixture(GroundSampler{});
    const RoadCollision before = build_road_collision(b.bake);

    const float lift = 5.0f;
    for (std::size_t li = 0; li < kRoadLayerCount; ++li)
        for (TerrainVertex& v : b.bake.layers[li].vertices) v.position.y += lift;

    const RoadCollision after = build_road_collision(b.bake);
    REQUIRE(after.triangles.size() == before.triangles.size());
    for (std::size_t i = 0; i < after.triangles.size(); ++i) {
        REQUIRE_NEAR(after.triangles[i].geom.a.y,
                     before.triangles[i].geom.a.y + lift, 1e-4f);
        REQUIRE_NEAR(after.triangles[i].geom.b.y,
                     before.triangles[i].geom.b.y + lift, 1e-4f);
        REQUIRE_NEAR(after.triangles[i].geom.c.y,
                     before.triangles[i].geom.c.y + lift, 1e-4f);
    }
    pass("moving the drawn geometry moves the collision by exactly as much");
}

// A slight bend where a street widens into an arterial used to intersect its
// almost parallel edges hundreds of metres away, creating a solid asphalt
// wedge over unrelated roads (the West Ramp / Route One failure).
void test_nearly_straight_width_change_stays_local() {
    RoadSpine street;
    street.id = 1;
    street.cls = RoadClass::Street;
    street.points = {{-78.5f, -387.4f}, {-147.5f, 269.0f}};
    RoadSpine ramp;
    ramp.id = 2;
    ramp.cls = RoadClass::Arterial;
    ramp.points = {{-147.5f, 269.0f}, {-160.0f, 380.0f}};
    RoadGraph graph;
    graph.build({street, ramp}, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bake = bake_ribbons(graph, GroundSampler{});
    const RoadMesh& plate = bake.layer(RoadLayer::Plate);
    REQUIRE(!plate.empty());
    for (const auto& v : plate.vertices) {
        REQUIRE_MSG(glm::length(glm::vec2{v.position.x + 147.5f,
                                         v.position.z - 269.0f}) < 30.0f,
                    "a width-change junction must not extend onto distant roads",
                    "West Ramp junction");
    }
    pass("nearly straight width changes keep their asphalt fill local");
}

void test_same_count_taper_keeps_paint_on_the_live_lanes() {
    RoadSpine gore;
    gore.id = 450;
    gore.cls = RoadClass::Freeway;
    gore.points = {{0.0f, 0.0f}, {60.0f, 0.0f}};
    gore.width_start_m = 40.0f;
    gore.width_end_m = 30.0f;
    gore.lanes_start_per_dir = 3;
    gore.lanes_end_per_dir = 3;

    RoadGraph graph;
    graph.build({gore}, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bake = bake_ribbons(graph, GroundSampler{});

    float road_edge = 0.0f;
    for (const TerrainVertex& v : bake.layer(RoadLayer::Carriageway).vertices)
        road_edge = std::max(road_edge, std::fabs(v.position.z));
    REQUIRE_NEAR(road_edge, 20.0f, 0.01f);

    float paint_edge = 0.0f;
    for (const TerrainVertex& v : bake.layer(RoadLayer::WhiteMarking).vertices)
        paint_edge = std::max(paint_edge, std::fabs(v.position.z));
    REQUIRE_MSG(paint_edge < 15.0f,
                "gore paint must stay beside the three live lanes",
                "same-count taper");
    REQUIRE_MSG(paint_edge > 14.5f,
                "the live carriageway still needs a shoulder line",
                "same-count taper");
    pass("a shoulder-only taper does not sweep its paint across the gore");
}

void test_lane_connected_ramp_buries_its_square_cap() {
    RoadSpine freeway;
    freeway.id = 460;
    freeway.cls = RoadClass::Freeway;
    freeway.points = {{-100.0f, 0.0f}, {100.0f, 0.0f}};
    freeway.width_start_m = 40.0f;
    freeway.width_end_m = 40.0f;
    freeway.lanes_start_per_dir = 4;
    freeway.lanes_end_per_dir = 4;

    RoadSpine ramp;
    ramp.id = 461;
    ramp.cls = RoadClass::Alley;
    ramp.width_m = 8.0f;
    ramp.one_way = true;
    ramp.lane_connect_start = true;
    ramp.points = {{0.0f, 17.5f}, {100.0f, 30.0f}};

    RoadGraph graph;
    graph.build({freeway, ramp}, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bake = bake_ribbons(graph, GroundSampler{});
    const RoadMesh& carriageway = bake.layer(RoadLayer::Carriageway);
    const RoadMesh& paint = bake.layer(RoadLayer::WhiteMarking);

    bool buried_throat_asphalt = false;
    for (const TerrainVertex& v : carriageway.vertices) {
        const glm::vec2 p{v.position.x, v.position.z};
        // The authored ramp begins at x=0. Its lower asphalt edge now
        // continues roughly 40 m back into the freeway, proving the cap is
        // buried. The freeway's own vertices sit at different z offsets.
        buried_throat_asphalt |= p.x < -35.0f && p.y > 8.0f && p.y < 9.5f;
    }
    REQUIRE_MSG(buried_throat_asphalt,
                "a lane-connected ramp ribbon must overlap the freeway",
                "ramp throat");
    for (const TerrainVertex& v : paint.vertices) {
        const glm::vec2 p{v.position.x, v.position.z};
        REQUIRE_MSG(!(p.x < -1.0f && p.y > 8.0f && p.y < 9.5f),
                    "the buried asphalt must not drag ramp paint backward",
                    "ramp throat");
    }
    REQUIRE(bake.plates_baked == 0);
    pass("a lane-connected ramp buries its cap without dragging its edge paint");
}

void test_narrow_curb_cut_tee_keeps_the_main_road_whole() {
    RoadSpine main;
    main.id = 500;
    main.cls = RoadClass::Arterial;
    main.points = {{0.0f, -100.0f}, {0.0f, 100.0f}};

    RoadSpine access;
    access.id = 501;
    access.cls = RoadClass::Alley;
    access.width_m = 8.0f;
    access.curb_cut_tee = true;
    access.points = {{-40.0f, 0.0f}, {0.0f, 0.0f}};

    RoadGraph graph;
    graph.build({main, access}, RoadGraphParams{}, GroundSampler{});
    const RibbonBake bake = bake_ribbons(graph, GroundSampler{});
    const RoadMesh& plate = bake.layer(RoadLayer::Plate);

    REQUIRE(bake.plates_baked == 1);
    REQUIRE(!plate.empty());
    for (const TerrainVertex& v : plate.vertices) {
        REQUIRE_MSG(v.position.x >= -14.01f && v.position.x <= -10.99f,
                    "curb-cut asphalt stays between the near walk edge and kerb",
                    "local curb cut");
        REQUIRE_MSG(std::fabs(v.position.z) <= 6.01f,
                    "curb returns stay close to the narrow access",
                    "local curb cut");
    }

    const RoadMesh& walk = bake.layer(RoadLayer::Walk);
    REQUIRE_MSG(mesh_covers(walk, {12.5f, 0.0f}),
                "the opposite sidewalk stays continuous", "local curb cut");
    REQUIRE_MSG(!mesh_covers(walk, {-12.5f, 0.0f}),
                "only the entrance-side sidewalk is opened", "local curb cut");
    REQUIRE_MSG(mesh_covers(walk, {-12.5f, 8.0f}),
                "the near sidewalk resumes beside the curb return", "local curb cut");

    REQUIRE_MSG(mesh_covers(bake.layer(RoadLayer::Carriageway), {0.0f, 0.0f}),
                "the through carriageway is not trimmed", "local curb cut");
    const float centre_line = RibbonParams{}.marking_width_m * 1.15f;
    REQUIRE_MSG(mesh_covers(bake.layer(RoadLayer::YellowMarking),
                            {centre_line, 0.0f}),
                "the through-road centre line stays continuous", "local curb cut");
    REQUIRE_MSG(collision_covers(build_road_collision(bake), RoadLayer::Plate,
                                 {-12.5f, 0.0f}),
                "the visible throat is part of the collision bake", "local curb cut");
    pass("a narrow curb-cut T opens one sidewalk without breaking the main road");
}

void test_skewed_curb_cut_matches_the_access_cap() {
    RoadSpine main;
    main.id=510;
    main.cls=RoadClass::Arterial;
    main.points={{0.f,-100.f},{0.f,100.f}};

    RoadSpine access;
    access.id=511;
    access.cls=RoadClass::Alley;
    access.width_m=8.f;
    access.curb_cut_tee=true;
    access.points={{-40.f,-8.f},{0.f,0.f}};

    RoadGraph graph;
    graph.build({main,access},RoadGraphParams{},GroundSampler{});
    const RibbonBake bake=bake_ribbons(graph,GroundSampler{});
    const RoadMesh& plate=bake.layer(RoadLayer::Plate);
    REQUIRE(!plate.empty());

    const glm::vec2 away=glm::normalize(access.points.front()-access.points.back());
    const glm::vec2 main_normal{-1.f,0.f};
    const float outer=(road_class_def(RoadClass::Arterial).carriageway_width_m*.5f+
                       kSidewalkWidthM)/glm::dot(main_normal,away);
    const glm::vec2 cap_centre=away*outer;
    const glm::vec2 cap_side{-away.y,away.x};
    REQUIRE_MSG(mesh_has_vertex(plate,cap_centre+cap_side*4.f),
                "one curb-cut cheek meets the angled access cap", "skewed curb cut");
    REQUIRE_MSG(mesh_has_vertex(plate,cap_centre-cap_side*4.f),
                "the other curb-cut cheek meets the angled access cap", "skewed curb cut");
    REQUIRE_MSG(collision_covers(build_road_collision(bake),RoadLayer::Plate,
                                 cap_centre),
                "the closed skewed seam is part of collision", "skewed curb cut");
    pass("a skewed curb-cut throat lands flush on its access-road cap");
}

void test_bake_is_deterministic() {
    const Baked a = bake_fixture(GroundSampler{});
    const Baked b = bake_fixture(GroundSampler{});
    for (std::size_t li = 0; li < kRoadLayerCount; ++li) {
        const RoadMesh& x = a.bake.layers[li];
        const RoadMesh& y = b.bake.layers[li];
        REQUIRE(x.vertices.size() == y.vertices.size());
        REQUIRE(x.indices == y.indices);
        for (std::size_t i = 0; i < x.vertices.size(); ++i) {
            REQUIRE(x.vertices[i].position == y.vertices[i].position);
            REQUIRE(x.vertices[i].normal == y.vertices[i].normal);
        }
    }
    pass("two bakes of one graph are bit-identical");
}

// Ferrone Hill, from docs/design/pinatty.md: the island's vertical district.
constexpr float kHillShiftX = 1150.0f;
constexpr float kHillShiftZ = -900.0f;

void test_normals_follow_a_grade() {
    // On flat ground every normal is straight up; over real terrain they must
    // not be, or the road lights like a decal pasted on the hill.
    const Baked flat = bake_fixture(GroundSampler{});
    for (const TerrainVertex& v : flat.bake.layer(RoadLayer::Carriageway).vertices)
        REQUIRE_NEAR(v.normal.y, 1.0f, 1e-5f);

    // Sampled through a shift onto sloping ground, and that shift is the whole
    // point of this paragraph. The fixture sits at the origin, and since PENG-41
    // the origin is Vellum Row — an authored FLAT plate, 99.8% under 5 degrees.
    // A road draped there is legitimately dead level, so this control measured
    // nothing and failed. Ferrone Hill is 45.6% flat over a 131 m range, so a
    // road across it has to grade or the drape is broken.
    //
    // Four other suites hit this same trap when the map landed. An authored
    // plate is flat and seed-independent BY DESIGN, which makes it the one place
    // a terrain-sensitivity control cannot be run.
    struct HillGround {
        uint64_t seed = 0;
        // mesh_height_at, not height_at: the drawn triangle is what a road must
        // drape onto, same rule the baker follows.
        static float sample(const void* ctx, float x, float z) {
            const auto* self = static_cast<const HillGround*>(ctx);
            return mesh_height_at(self->seed, x + kHillShiftX, z + kHillShiftZ);
        }
        GroundSampler sampler() const { return GroundSampler{&sample, this}; }
    };
    const HillGround hill{0xDEADBEEFull};
    const Baked hilly = bake_fixture(hill.sampler());
    float most_tilted = 1.0f;
    for (const TerrainVertex& v : hilly.bake.layer(RoadLayer::Carriageway).vertices)
        most_tilted = std::min(most_tilted, v.normal.y);
    REQUIRE_MSG(most_tilted < 0.999f,
                "a road over real terrain must have tilted normals", "grade");
    std::printf("      steepest carriageway normal over terrain: n.y = %.4f\n",
                static_cast<double>(most_tilted));
    pass("normals are flat on flat ground and tilt with a grade");
}

}  // namespace

int main() {
    test_every_layer_is_populated();
    test_geometry_is_finite_and_indexed_in_range();
    test_flat_layers_face_up_and_kerbs_do_not();
    test_carriageway_spans_the_authored_width();
    test_markings_match_road_class_and_have_real_gaps();
    test_surface_rides_the_drawn_terrain();
    test_bridge_deck_is_authored_and_never_draped();
    test_municipal_bridge_kit_is_structural_and_keeps_lanes_clear();
    test_collision_is_the_baked_geometry();
    test_collision_follows_the_bake_and_cannot_re_derive_it();
    test_nearly_straight_width_change_stays_local();
    test_same_count_taper_keeps_paint_on_the_live_lanes();
    test_lane_connected_ramp_buries_its_square_cap();
    test_narrow_curb_cut_tee_keeps_the_main_road_whole();
    test_skewed_curb_cut_matches_the_access_cap();
    test_bake_is_deterministic();
    test_normals_follow_a_grade();
    return apricot_test::done("road_ribbon_tests");
}
