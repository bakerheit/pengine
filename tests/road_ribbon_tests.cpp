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

void test_every_layer_is_populated() {
    const Baked b = bake_fixture(GroundSampler{});
    REQUIRE(!b.bake.layer(RoadLayer::Carriageway).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Unpaved).empty());  // the dirt road
    REQUIRE(!b.bake.layer(RoadLayer::Walk).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Kerb).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Plate).empty());
    REQUIRE(!b.bake.layer(RoadLayer::Crosswalk).empty());
    // Three real crossings, plus one plate at the degree-2 node where the
    // street becomes an alley: 14 m of carriageway butting onto 6 m leaves a
    // notch, and a mitre cannot close a step in width.
    REQUIRE(b.bake.plates_baked == kFixtureJunctions + 1);
    // One zebra per approach at each of the three real crossings: 4 + 3 + 3.
    // The width-step plate gets none, because a bend is not a crossing.
    REQUIRE(b.bake.crosswalks_baked == 10);
    std::printf("      bake: %zu triangles over %zu layers, %zu plates\n",
                b.bake.total_triangles(), kRoadLayerCount, b.bake.plates_baked);
    pass("all six layers bake, and crosswalks land only at real crossings");
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

void test_collision_is_the_baked_geometry() {
    const Baked b = bake_fixture(GroundSampler{});
    const RoadCollision col = build_road_collision(b.bake);

    // Every non-kerb triangle survives except genuine zero-area slivers.
    std::size_t flat_tris = 0;
    for (std::size_t li = 0; li < kRoadLayerCount; ++li)
        if (static_cast<RoadLayer>(li) != RoadLayer::Kerb)
            flat_tris += b.bake.layers[li].triangle_count();
    REQUIRE(!col.triangles.empty());
    REQUIRE(col.triangles.size() <= flat_tris);
    REQUIRE(col.triangles.size() > flat_tris * 9 / 10);

    for (const RoadCollisionTri& t : col.triangles) {
        REQUIRE(t.layer != RoadLayer::Kerb);
        REQUIRE(t.geom.normal.y > 0.0f);
        REQUIRE_NEAR(glm::length(t.geom.normal), 1.0f, 1e-4f);
        REQUIRE(t.material == (t.layer == RoadLayer::Unpaved ? Surface::Gravel
                                                             : Surface::Rock));
    }
    std::printf("      %zu collision triangles from %zu drawn flat triangles\n",
                col.triangles.size(), flat_tris);
    pass("collision covers the drawn flat surface and excludes the kerb risers");
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
    test_surface_rides_the_drawn_terrain();
    test_bridge_deck_is_authored_and_never_draped();
    test_collision_is_the_baked_geometry();
    test_collision_follows_the_bake_and_cannot_re_derive_it();
    test_bake_is_deterministic();
    test_normals_follow_a_grade();
    return apricot_test::done("road_ribbon_tests");
}
