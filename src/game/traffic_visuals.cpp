// traffic_visuals.cpp — per-frame scene-graph sync, debug overlay, and the
// traffic-light visual machinery for TrafficSystem.
//
// Owns: sync_visuals (per-car chassis/body/wheel pose), draw_wheels
// (single-call instanced wheel draw), debug_draw (route + stop-line overlay),
// sync_lights_to_loaded / spawn_lights_at / destroy_lights_at /
// update_light_visuals (the per-intersection signal poles + bulb tints).
// Extracted from traffic.cpp in PBD-007 commit 5.

#include "game/traffic.h"
#include "game/traffic_internal.h"

#include "game/car_models.h"
#include "game/traffic_ai.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "render/debug_draw.h"
#include "render/mesh.h"
#include "render/shader.h"
#include "scene/aabb.h"
#include "scene/frustum.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/city_layout.h"
#include "world/heightmap.h"
#include "world/road_graph.h"

namespace pengine {

namespace {

// Bulb tints — bright when on, dim when off.
constexpr glm::vec3 RED_BRIGHT    {0.95f, 0.15f, 0.15f};
constexpr glm::vec3 YELLOW_BRIGHT {0.95f, 0.80f, 0.15f};
constexpr glm::vec3 GREEN_BRIGHT  {0.20f, 0.90f, 0.30f};
constexpr glm::vec3 RED_DIM       {0.30f, 0.10f, 0.10f};
constexpr glm::vec3 YELLOW_DIM    {0.32f, 0.28f, 0.10f};
constexpr glm::vec3 GREEN_DIM     {0.10f, 0.28f, 0.13f};
constexpr glm::vec3 METAL_TINT    {0.16f, 0.16f, 0.18f};

// Geometry of the four light approaches at every intersection.
struct ApproachSpec {
    GridDir car_dir;
    float   corner_dx, corner_dz;
    float   arm_dx,    arm_dz;
};
constexpr ApproachSpec APPROACHES[4] = {
    {GridDir::East,  +1.f, -1.f,  0.f, +1.f},
    {GridDir::North, +1.f, +1.f, -1.f,  0.f},
    {GridDir::West,  -1.f, +1.f,  0.f, -1.f},
    {GridDir::South, -1.f, -1.f, +1.f,  0.f},
};
constexpr float POLE_HEIGHT     = 6.0f;
constexpr float POLE_THICK      = 0.20f;
constexpr float ARM_LENGTH      = 4.0f;
constexpr float ARM_THICK       = 0.18f;
constexpr float HOUSING_W       = 0.40f;
constexpr float HOUSING_H       = 1.50f;
constexpr float HOUSING_D       = 0.40f;
constexpr float BULB_SIZE       = 0.32f;
constexpr float BULB_SPACING    = 0.50f;
constexpr float CORNER_INSET    = 1.5f;
constexpr float CORNER_DIST     = STREET_WIDTH * 0.5f + CORNER_INSET;

// Pack an (i, j) intersection coordinate into a single uint64 key for the
// loaded-intersections lookup set.
inline std::uint64_t pack_ij(int i, int j) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)) << 32)
         |  static_cast<std::uint64_t>(static_cast<std::uint32_t>(j));
}

} // anonymous namespace

void TrafficSystem::draw_wheels(Shader& shader, const Frustum& frustum) const {
    if (!assets_ || !assets_->wheel_ok || cars_.empty()) return;

    // Reuse storage across frames so we don't reallocate every render.
    static thread_local std::vector<glm::mat4> mats;
    mats.clear();
    mats.reserve(cars_.size() * 4);

    for (const auto& car : cars_) {
        if (!car) continue;
        // Cull whole car: if its body is off-screen, skip its 4 wheels.
        if (car->body_visual_node && car->body_visual_node->renderable
            && frustum.cull(car->body_visual_node->world_aabb())) continue;
        for (int wi = 0; wi < 4; ++wi) {
            if (car->wheel_nodes[wi])
                mats.push_back(car->wheel_nodes[wi]->world_matrix());
        }
    }
    if (mats.empty()) return;

    shader.use();
    assets_->wheel_tex.bind(0);
    assets_->wheel_mesh.draw_instanced(mats.data(),
                                        static_cast<int>(mats.size()));
}

void TrafficSystem::debug_draw(DebugDraw& debug) const {
    if (!graph_) return;
    TrafficLaneGraph lane_graph(*graph_);
    for (const auto& car : cars_) {
        if (!car || car->driver != Driver::AI) continue;
        const Car& c = *car;

        glm::vec3 cur = lane_graph.lane_pose(c.ai_lane, c.ai_distance_along,
                                             lane_offset_,
                                             c.ai_lateral_offset);
        cur.y = Heightmap::sample(cur.x, cur.z) + 0.18f;
        debug.cross(cur, c.ai_honking ? 0.55f : 0.28f);

        float look = c.ai_distance_along;
        LaneId lane = c.ai_lane;
        for (int k = 0; k < 3; ++k) {
            float next_d = (k == 0) ? std::min(ROAD_PITCH, look + 16.f)
                                    : std::min(ROAD_PITCH, 16.f);
            glm::vec3 a = lane_graph.lane_pose(lane,
                                               (k == 0) ? look : 0.f,
                                               lane_offset_, 0.f);
            glm::vec3 b = lane_graph.lane_pose(lane, next_d,
                                               lane_offset_, 0.f);
            a.y = Heightmap::sample(a.x, a.z) + 0.12f;
            b.y = Heightmap::sample(b.x, b.z) + 0.12f;
            debug.line(a, b);
            std::size_t idx = c.ai_route_index + static_cast<std::size_t>(k) + 1u;
            if (idx >= c.ai_route.lanes.size()) break;
            lane = c.ai_route.lanes[idx];
            look = 0.f;
        }

        glm::vec3 stop = lane_graph.lane_pose(c.ai_lane, ROAD_PITCH - STOP_BACK,
                                              lane_offset_, 0.f);
        stop.y = Heightmap::sample(stop.x, stop.z) + 0.14f;
        debug.cross(stop, 0.18f);
    }
}

void TrafficSystem::sync_visuals(Car& c) {
    if (!c.chassis_node) return;

    // chassis_node carries the rigid-body pose.
    c.chassis_node->transform.position = c.vehicle.position();
    c.chassis_node->transform.rotation = c.vehicle.orientation();
    c.chassis_node->transform.scale    = {1.f, 1.f, 1.f};
    c.chassis_node->mark_dirty();

    // Body visual child: model offset + scale, plus the model-axis fix-up.
    const auto& ma = assets_->models[static_cast<std::size_t>(c.model_id)];
    const CarModelDef& def = CAR_MODELS[c.model_id];
    if (ma.body_ok && c.body_visual_node) {
        c.body_visual_node->transform.position = ma.body_visual_offset;
        c.body_visual_node->transform.rotation = glm::angleAxis(
            glm::radians(def.yaw_offset_deg), glm::vec3{0.f, 1.f, 0.f});
        c.body_visual_node->transform.scale    = ma.body_visual_scale;
        c.body_visual_node->mark_dirty();
    } else if (c.body_visual_node) {
        c.body_visual_node->transform.position = {0.f, 0.f, 0.f};
        c.body_visual_node->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        c.body_visual_node->transform.scale    = c.vehicle.chassis_full_extents;
        c.body_visual_node->mark_dirty();
    }

    // Wheels — position by mount + suspension drop, rotation = steer * roll.
    if (c.wheel_nodes[0]) {
        const float spin       = c.wheel_spin_rad;
        const float bottom_align = assets_->wheel_ok
            ? (c.vehicle.wheel_radius - assets_->wheel_visible_radius) : 0.f;
        const glm::vec3 wheel_scale = assets_->wheel_ok
            ? glm::vec3{assets_->wheel_visual_scale}
            : glm::vec3{0.32f, 2.f * c.vehicle.wheel_radius,
                              2.f * c.vehicle.wheel_radius};
        const auto& wheels = c.vehicle.wheels();
        const bool kinematic = (c.driver == Driver::AI);
        for (std::size_t i = 0; i < 4; ++i) {
            glm::vec3 pos = wheels[i].mount_local;
            // AI cars don't run wheel raycasts (kinematic), so use the
            // precomputed static-rest drop instead of the live one.
            float drop = kinematic ? ma.static_visual_drop
                                    : wheels[i].visual_drop;
            pos.y -= drop + bottom_align;
            c.wheel_nodes[i]->transform.position = pos;

            glm::quat steer{1.f, 0.f, 0.f, 0.f};
            if (wheels[i].is_steering) {
                steer = glm::angleAxis(-c.vehicle.steer_rad(),
                                        glm::vec3{0.f, 1.f, 0.f});
            }
            glm::quat roll = assets_->wheel_ok
                ? glm::angleAxis(-spin, glm::vec3{1.f, 0.f, 0.f})
                : glm::quat{1.f, 0.f, 0.f, 0.f};
            c.wheel_nodes[i]->transform.rotation = steer * roll;
            c.wheel_nodes[i]->transform.scale    = wheel_scale;
            c.wheel_nodes[i]->mark_dirty();
        }
    }
}

void TrafficSystem::sync_lights_to_loaded() {
    if (!graph_ || !scene_ || !light_vis_.cube_mesh) return;

    auto loaded = graph_->loaded_intersections();
    std::unordered_set<std::uint64_t> wanted;
    wanted.reserve(loaded.size());
    for (const auto& p : loaded) wanted.insert(pack_ij(p.first, p.second));

    for (auto it = light_intersections_.begin(); it != light_intersections_.end(); ) {
        if (wanted.count(*it) == 0) {
            int i = static_cast<int>(static_cast<std::int32_t>(*it >> 32));
            int j = static_cast<int>(static_cast<std::int32_t>(*it & 0xFFFFFFFFu));
            destroy_lights_at(i, j);
            it = light_intersections_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& p : loaded) {
        std::uint64_t k = pack_ij(p.first, p.second);
        if (light_intersections_.insert(k).second) spawn_lights_at(p.first, p.second);
    }
}

void TrafficSystem::spawn_lights_at(int i, int j) {
    glm::vec3 ix = RoadGraph::intersection_pos(i, j, 0.f);

    // Traffic light nodes are static (they never move). Register them in the
    // cell bucket so cull() can skip this entire intersection at once when
    // it's outside the camera frustum.
    CellCoord light_cell = RoadGraph::intersection_cell(i, j);

    auto unit_cube = []() {
        AABB a; a.min = -glm::vec3{0.5f}; a.max = glm::vec3{0.5f}; return a;
    };
    auto make_node = [&](const glm::vec3& pos, const glm::vec3& scale,
                         const glm::vec3& tint) {
        SceneNode* n = scene_->create_node_static(nullptr, light_cell);
        n->renderable = Renderable{light_vis_.cube_mesh, unit_cube(), tint,
                                    glm::vec2{1.f, 1.f}, light_vis_.checker_tex};
        n->transform.position = pos;
        n->transform.scale    = scale;
        n->mark_dirty();
        return n;
    };

    for (int a = 0; a < 4; ++a) {
        const ApproachSpec& s = APPROACHES[a];
        float pole_x = ix.x + s.corner_dx * CORNER_DIST;
        float pole_z = ix.z + s.corner_dz * CORNER_DIST;
        float ground = city_ground_sample(pole_x, pole_z);
        float pole_top_y = ground + POLE_HEIGHT;

        float arm_cx = pole_x + s.arm_dx * (ARM_LENGTH * 0.5f);
        float arm_cz = pole_z + s.arm_dz * (ARM_LENGTH * 0.5f);
        float arm_cy = pole_top_y - ARM_THICK * 0.5f;
        glm::vec3 arm_scale = std::abs(s.arm_dx) > 0.5f
            ? glm::vec3{ARM_LENGTH, ARM_THICK, ARM_THICK}
            : glm::vec3{ARM_THICK, ARM_THICK, ARM_LENGTH};

        float arm_end_x = pole_x + s.arm_dx * ARM_LENGTH;
        float arm_end_z = pole_z + s.arm_dz * ARM_LENGTH;
        float housing_cy = pole_top_y - ARM_THICK - HOUSING_H * 0.5f;
        glm::vec3 housing_pos{arm_end_x, housing_cy, arm_end_z};

        glm::vec3 face{-s.arm_dz, 0.f, s.arm_dx};
        glm::vec3 bulb_offset = face * (HOUSING_D * 0.5f + BULB_SIZE * 0.5f);
        glm::vec3 r_pos = housing_pos + glm::vec3{0.f, +BULB_SPACING, 0.f} + bulb_offset;
        glm::vec3 y_pos = housing_pos                                       + bulb_offset;
        glm::vec3 g_pos = housing_pos + glm::vec3{0.f, -BULB_SPACING, 0.f} + bulb_offset;

        Light L;
        L.i = i; L.j = j; L.approach = a;
        L.pole    = make_node({pole_x, ground + POLE_HEIGHT * 0.5f, pole_z},
                               {POLE_THICK, POLE_HEIGHT, POLE_THICK}, METAL_TINT);
        L.arm     = make_node({arm_cx, arm_cy, arm_cz}, arm_scale, METAL_TINT);
        L.housing = make_node(housing_pos,
                               {HOUSING_W, HOUSING_H, HOUSING_D}, METAL_TINT);
        L.bulb_r  = make_node(r_pos, glm::vec3{BULB_SIZE}, RED_DIM);
        L.bulb_y  = make_node(y_pos, glm::vec3{BULB_SIZE}, YELLOW_DIM);
        L.bulb_g  = make_node(g_pos, glm::vec3{BULB_SIZE}, GREEN_DIM);
        lights_.push_back(L);
    }
}

void TrafficSystem::destroy_lights_at(int i, int j) {
    // Wipe the whole static bucket for this intersection first (O(1)).
    if (scene_) scene_->remove_static_cell(RoadGraph::intersection_cell(i, j));
    auto remove = [&](SceneNode* n) {
        if (n && scene_) scene_->remove_node(n);
    };
    for (std::size_t k = lights_.size(); k-- > 0;) {
        Light& L = lights_[k];
        if (L.i != i || L.j != j) continue;
        remove(L.pole);   remove(L.arm);    remove(L.housing);
        remove(L.bulb_r); remove(L.bulb_y); remove(L.bulb_g);
        lights_[k] = std::move(lights_.back());
        lights_.pop_back();
    }
}

void TrafficSystem::update_light_visuals(double t) {
    auto set_tint = [](SceneNode* n, const glm::vec3& c) {
        if (n && n->renderable) n->renderable->tint = c;
    };
    for (Light& L : lights_) {
        const ApproachSpec& s = APPROACHES[L.approach];
        LightPhase phase = light_phase(L.i, L.j, s.car_dir, t);
        set_tint(L.bulb_r, phase == LightPhase::Red    ? RED_BRIGHT    : RED_DIM);
        set_tint(L.bulb_y, phase == LightPhase::Yellow ? YELLOW_BRIGHT : YELLOW_DIM);
        set_tint(L.bulb_g, phase == LightPhase::Green  ? GREEN_BRIGHT  : GREEN_DIM);
    }
}

} // namespace pengine
