#include <algorithm>
#include <cmath>
#include <cstdio>

#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "physics/terrain_collider.h"
#include "road/lane_graph.h"
#include "road/ribbon.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool node_has(const RoadGraph& roads, uint32_t node, uint32_t spine) {
    for (uint32_t edge : roads.node(node).edges)
        if (roads.edge(edge).spine_id == spine) return true;
    return false;
}

void ostend_topology_is_unambiguous() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());

    int apron_access = 0;
    int berth_access = 0;
    uint32_t access_node = 0;
    uint32_t rimway_node = 0;
    for (uint32_t node = 0; node < roads.node_count(); ++node) {
        if (node_has(roads, node, 82) && node_has(roads, node, 84)) {
            ++berth_access;
            access_node = node;
        }
        apron_access += node_has(roads, node, 80) && node_has(roads, node, 84);
        if (node_has(roads, node, 3) && node_has(roads, node, 80))
            rimway_node = node;
    }
    REQUIRE(apron_access == 0);
    REQUIRE(berth_access == 1);
    REQUIRE(lanes.junction_control(access_node) != JunctionControl::Signal);
    REQUIRE_NEAR(roads.node(rimway_node).y_m, 8.216f, .02f);
    REQUIRE_NEAR(roads.node(access_node).pos.y,-645.f,.01f);

    const city::Road* access = nullptr;
    for (const city::Road& road : city::kRoads)
        if (road.id == 84) access = &road;
    REQUIRE(access != nullptr);
    REQUIRE_NEAR(access->path[access->count - 1].x, -1979.0f, .001f);
    REQUIRE_NEAR(access->path[access->count - 1].z, -620.0f, .001f);
    apricot_test::pass("Boatworks branches once from the north arm of Berth 2");
}

void road_mesh_stays_above_the_drawn_terrain() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    const RibbonBake bake = bake_ribbons(roads, ground.sampler());
    TerrainCollider terrain{city::kMapSeed};

    constexpr glm::vec2 focus{-1453.31f, -522.05f};
    float smallest_gap = 1.0e9f;
    std::size_t samples = 0;
    for (RoadLayer layer : {RoadLayer::Carriageway, RoadLayer::Plate}) {
        const RoadMesh& mesh = bake.layer(layer);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
            const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
            const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
            const glm::vec3 centre = (a + b + c) / 3.0f;
            if (glm::distance(glm::vec2{centre.x, centre.z}, focus) > 250.0f)
                continue;
            for (int row = 0; row <= 5; ++row) {
                for (int column = 0; column <= 5 - row; ++column) {
                    const float u = static_cast<float>(row) / 5.0f;
                    const float v = static_cast<float>(column) / 5.0f;
                    const glm::vec3 p = a * u + b * v + c * (1.0f - u - v);
                    smallest_gap = std::min(
                        smallest_gap, p.y - terrain.height(p.x, p.z));
                    ++samples;
                }
            }
        }
    }
    REQUIRE(samples > 10000);
    REQUIRE_MSG(smallest_gap > .04f,
                "road triangles must clear terrain between their vertices",
                "Ostend road drape");

    for (const TerrainVertex& vertex : bake.layer(RoadLayer::Crosswalk).vertices)
        REQUIRE(glm::distance(glm::vec2{vertex.position.x, vertex.position.z},
                              focus) > 50.0f);
    std::printf("  %zu surface samples, smallest terrain clearance %.3f m\n",
                samples, smallest_gap);
    apricot_test::pass("Apron and Rimway asphalt has no terrain holes or freeway zebra");
}

void ai_lanes_ride_the_player_collision_surface() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    const RibbonBake bake = bake_ribbons(roads, ground.sampler());
    TerrainCollider collider{city::kMapSeed};
    collider.set_road_collision(build_road_collision(bake));
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());

    float worst = 0.0f;
    float lowest = 1.0e9f;
    float worst_signed = 0.0f;
    glm::vec3 worst_point{0.0f};
    uint32_t worst_spine = 0;
    glm::vec3 lowest_point{0.0f};
    uint32_t lowest_spine = 0;
    std::size_t checked = 0;
    for (LaneRef ref = 0; ref < lanes.lane_count(); ++ref) {
        const Lane& lane = lanes.lane(ref);
        const uint32_t spine = roads.edge(lane.edge).spine_id;
        if (spine != 3 && spine != 80 && spine != 82 && spine != 84) continue;
        for (int sample = 1; sample <= 9; ++sample) {
            const float d = lanes.length(ref) * static_cast<float>(sample) / 10.0f;
            const glm::vec3 p = lanes.pose(ref, d).position;
            if (spine == 3 && glm::distance(glm::vec2{p.x, p.z},
                                           glm::vec2{-1453.31f, -522.05f}) >
                                  300.0f)
                continue;
            const TerrainCollider::GroundHit hit =
                collider.probe_down({p.x, p.y + 1.0f, p.z}, 2.0f);
            REQUIRE(hit.hit);
            const float signed_delta = p.y - hit.point.y;
            const float delta = std::fabs(signed_delta);
            if (signed_delta < lowest) {
                lowest = signed_delta;
                lowest_point = p;
                lowest_spine = spine;
            }
            if (delta > worst) {
                worst = delta;
                worst_signed = signed_delta;
                worst_point = p;
                worst_spine = spine;
            }
            ++checked;
        }
    }
    REQUIRE(checked > 100);
    std::printf("  %zu lane vertices, worst lane/collision delta %+.4f m "
                "on road %u at %.1f, %.1f\n",
                checked, worst_signed, worst_spine, worst_point.x, worst_point.z);
    std::printf("  lowest lane/collision delta %+.4f m on road %u at %.1f, %.1f\n",
                lowest, lowest_spine, lowest_point.x, lowest_point.z);
    REQUIRE_MSG(lowest > -.015f,
                "AI lane must never pass under the player road collision",
                "Ostend lane surface");
    REQUIRE_MSG(worst < .07f,
                "AI lane must remain within the road drape lift",
                "Ostend lane surface");
    apricot_test::pass("Route 1 and dock traffic rides the same solid asphalt as the player");
}

}  // namespace

int main() {
    ostend_topology_is_unambiguous();
    road_mesh_stays_above_the_drawn_terrain();
    ai_lanes_ride_the_player_collision_surface();
    return apricot_test::done("ostend_road_tests");
}
