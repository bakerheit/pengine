// traffic_spawn.cpp — vehicle spawn and despawn for TrafficSystem.
//
// Owns: try_spawn_ai, try_spawn_police, create_car_at_pose, spawn_player_car,
// destroy_car. Extracted from traffic.cpp in PBD-007 commit 2.

#include "game/traffic.h"
#include "game/traffic_internal.h"

#include "game/car_models.h"
#include "game/traffic_ai.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include <glm/glm.hpp>

#include "scene/aabb.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/city_layout.h"
#include "world/heightmap.h"
#include "world/road_graph.h"

namespace pengine {

namespace {

// Police-specific spawn tuning. These are spawn-only; the drive/lifecycle code
// in traffic.cpp uses its own POLICE_MAX_CARS / POLICE_DESPAWN_DIST.
constexpr int   POLICE_MODEL_ID  = 0; // Car5 sedan
constexpr int   POLICE_PAINT_IDX = 3; // car5_police.png
constexpr float POLICE_SPAWN_MIN = 55.f;
constexpr float POLICE_SPAWN_MAX = 135.f;

} // namespace

TrafficSystem::Car* TrafficSystem::create_car_at_pose(const glm::vec3& pos,
                                                      float yaw_deg,
                                                      int model_id,
                                                      int paint_idx,
                                                      Driver driver) {
    if (!scene_ || !assets_) return nullptr;
    if (model_id < 0 || model_id >= NUM_CAR_MODELS) model_id = 0;
    const CarModelDef& def = CAR_MODELS[model_id];
    auto&              ma  = assets_->models[static_cast<std::size_t>(model_id)];
    if (!ma.body_ok) return nullptr;

    auto car = std::make_unique<Car>();
    car->driver    = driver;
    car->model_id  = model_id;
    // Clamp paint index to this model's paint palette.
    car->paint_idx = std::clamp(paint_idx, 0, def.paint_count - 1);

    // Apply per-model tuning (mass, max_speed, engine_force, …) before spawn.
    apply_model_tuning(car->vehicle, def);

    // Configure the rigid body. spawn() resets velocity + sets default
    // wheel mounts and a chassis-box visual AABB; we then override both
    // with the model's mesh-derived values.
    car->vehicle.spawn(pos, yaw_deg);
    for (int wi = 0; wi < 4; ++wi)
        car->vehicle.set_wheel_mount(wi, ma.wheel_mount[wi]);
    if (ma.body_ok)
        car->vehicle.set_visual_aabb_local(ma.visual_aabb_min,
                                           ma.visual_aabb_max);

    // ---- Scene graph: chassis (rigid-body pose) → body_visual + 4 wheels --
    car->chassis_node     = scene_->create_node();
    car->body_visual_node = scene_->create_node(car->chassis_node);

    if (ma.body_ok) {
        AABB local;
        local.min = ma.body_mesh.bounds_min();
        local.max = ma.body_mesh.bounds_max();
        car->body_visual_node->renderable = Renderable{
            &ma.body_mesh, local, glm::vec3{1.f, 1.f, 1.f},
            glm::vec2{1.f, 1.f},
            &ma.paints[static_cast<std::size_t>(car->paint_idx)]};
    } else if (light_vis_.cube_mesh) {
        AABB cube_aabb;
        cube_aabb.min = light_vis_.cube_mesh->bounds_min();
        cube_aabb.max = light_vis_.cube_mesh->bounds_max();
        car->body_visual_node->renderable = Renderable{
            light_vis_.cube_mesh, cube_aabb,
            glm::vec3{0.85f, 0.20f, 0.18f}, glm::vec2{1.f, 1.f},
            light_vis_.checker_tex};
    }

    // Dynamic wheels — skipped entirely for models that already have wheels
    // baked into the body mesh, otherwise we'd render two sets at the same
    // wheel arches. draw_wheels and sync_visuals both null-check the
    // wheel_nodes, so leaving them null disables both paths cleanly.
    if (!def.body_has_built_in_wheels) {
        const Mesh*    wm = assets_->wheel_ok ? &assets_->wheel_mesh
                                              : light_vis_.cube_mesh;
        const Texture* wt = assets_->wheel_ok ? &assets_->wheel_tex
                                              : light_vis_.checker_tex;
        AABB waabb;
        waabb.min = wm ? wm->bounds_min() : -glm::vec3{0.5f};
        waabb.max = wm ? wm->bounds_max() :  glm::vec3{0.5f};
        glm::vec3 wtint = assets_->wheel_ok ? glm::vec3{1.f}
                                             : glm::vec3{0.10f, 0.10f, 0.10f};
        for (int wi = 0; wi < 4; ++wi) {
            car->wheel_nodes[wi] = scene_->create_node(car->chassis_node);
            // When the wheel asset loaded, wheels are drawn in one instanced
            // call via TrafficSystem::draw_wheels — leave renderable empty
            // so Scene::draw skips them. Cube fallback uses the per-node path.
            if (!assets_->wheel_ok) {
                car->wheel_nodes[wi]->renderable = Renderable{
                    wm, waabb, wtint, glm::vec2{1.f, 1.f}, wt};
            }
        }
    }

    Car* raw = car.get();
    cars_.push_back(std::move(car));
    sync_visuals(*raw); // initial sync so first frame renders correctly
    return raw;
}

TrafficSystem::Car* TrafficSystem::spawn_player_car(const glm::vec3& pos,
                                                    float yaw_deg) {
    // Player drives Car5 (model 0) by default.
    Car* c = create_car_at_pose(pos, yaw_deg, /*model_id=*/ 0,
                                 /*paint_idx=*/ 0, Driver::Player);
    if (c) player_car_ = c;
    return c;
}

void TrafficSystem::destroy_car(std::size_t idx) {
    if (idx >= cars_.size()) return;
    Car& c = *cars_[idx];
    if (player_car_ == &c) player_car_ = nullptr;
    if (scene_) {
        for (SceneNode* w : c.wheel_nodes) if (w) scene_->remove_node(w);
        if (c.body_visual_node) scene_->remove_node(c.body_visual_node);
        if (c.chassis_node)     scene_->remove_node(c.chassis_node);
    }
    cars_[idx] = std::move(cars_.back());
    cars_.pop_back();
}

bool TrafficSystem::try_spawn_ai(const glm::vec3& camera_pos) {
    if (!graph_ || graph_->loaded_cell_count() == 0) return false;
    TrafficLaneGraph lane_graph(*graph_);

    for (int attempt = 0; attempt < 24; ++attempt) {
        int i, j;
        if (!graph_->random_loaded_intersection(rng_, i, j)) return false;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(i, j, options);
        if (n == 0) continue;
        GridDir dir = options[rng_() % static_cast<unsigned>(n)];
        LaneId start_lane{i, j, dir};
        if (!lane_graph.lane_loaded(start_lane)) continue;

        float along = static_cast<float>(rng_() & 0xFFFFu) / 65535.f * ROAD_PITCH;
        TrafficRoute route = lane_graph.make_route(start_lane, ROUTE_LANES, rng_);
        if (route.lanes.size() < 2) continue;

        glm::vec3 spawn_xz = lane_graph.lane_pose(start_lane, along,
                                                  lane_offset_, 0.f);
        float dx = spawn_xz.x - camera_pos.x;
        float dz = spawn_xz.z - camera_pos.z;
        float dist = std::sqrt(dx*dx + dz*dz);
        if (dist < spawn_min_ || dist > spawn_max_) continue;

        bool occupied = false;
        for (const auto& other : cars_) {
            glm::vec3 p = other->vehicle.position();
            float ox = p.x - spawn_xz.x;
            float oz = p.z - spawn_xz.z;
            if ((ox*ox + oz*oz) < 64.f) {
                occupied = true;
                break;
            }
        }
        if (occupied) continue;

        // Pick model by spawn weight (so e.g. trucks are rarer than sedans),
        // then a random paint within that model's palette. Done up front so
        // we can use the model's ride height when placing the chassis.
        int total_weight = 0;
        for (int m = 0; m < NUM_CAR_MODELS; ++m) total_weight += CAR_MODELS[m].spawn_weight;
        int r = static_cast<int>(rng_() % static_cast<std::uint32_t>(total_weight));
        int model_id = 0;
        for (int m = 0; m < NUM_CAR_MODELS; ++m) {
            r -= CAR_MODELS[m].spawn_weight;
            if (r < 0) { model_id = m; break; }
        }
        int paint_count = CAR_MODELS[model_id].paint_count;
        if (model_id == POLICE_MODEL_ID)
            paint_count = std::min(paint_count, POLICE_PAINT_IDX);
        int paint_idx = static_cast<int>(rng_() %
                            static_cast<std::uint32_t>(paint_count));

        // Place the chassis at the lane position, lifted to chassis-centre
        // height so the visual matches a player-driven car.
        float ground = Heightmap::sample(spawn_xz.x, spawn_xz.z);
        glm::vec3 spawn_pos{spawn_xz.x,
                             ground + assets_->models[
                                 static_cast<std::size_t>(model_id)
                             ].ride_height_at_rest,
                             spawn_xz.z};
        TrafficDirInfo info = traffic_dir_info(dir);
        float yaw_deg = info.yaw_deg - 90.f;

        Car* c = create_car_at_pose(spawn_pos, yaw_deg,
                                     model_id, paint_idx, Driver::AI);
        if (!c) return false;

        c->ai_profile = random_driver_profile(rng_);
        c->ai_route = std::move(route);
        c->ai_route_index = 0;
        c->ai_lane = start_lane;
        c->ai_next_lane = c->ai_route.lanes[1];
        c->ai_prev_lane = start_lane;
        c->ai_distance_along = along;
        c->ai_target_speed = 9.f
            + static_cast<float>(rng_() & 0xFFFu) / 4096.f * 7.f; // 9..16 m/s
        c->ai_target_speed *= c->ai_profile.speed_mul;
        c->ai_speed = c->ai_target_speed * 0.6f;
        c->ai_state = TrafficAgentState::Cruise;
        return true;
    }
    return false;
}

bool TrafficSystem::try_spawn_police() {
    if (!graph_ || graph_->loaded_cell_count() == 0 || !assets_) return false;
    TrafficLaneGraph lane_graph(*graph_);

    for (int attempt = 0; attempt < 32; ++attempt) {
        int i, j;
        if (!graph_->random_loaded_intersection(rng_, i, j)) return false;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(i, j, options);
        if (n == 0) continue;
        GridDir dir = options[rng_() % static_cast<unsigned>(n)];
        LaneId lane{i, j, dir};
        if (!lane_graph.lane_loaded(lane)) continue;

        float along = static_cast<float>(rng_() & 0xFFFFu) / 65535.f * ROAD_PITCH;
        glm::vec3 spawn_xz = lane_graph.lane_pose(lane, along,
                                                  lane_offset_, 0.f);
        float dx = spawn_xz.x - police_target_pos_.x;
        float dz = spawn_xz.z - police_target_pos_.z;
        float dist = std::sqrt(dx*dx + dz*dz);
        if (dist < POLICE_SPAWN_MIN || dist > POLICE_SPAWN_MAX) continue;

        bool occupied = false;
        for (const auto& other : cars_) {
            glm::vec3 p = other->vehicle.position();
            float ox = p.x - spawn_xz.x;
            float oz = p.z - spawn_xz.z;
            if ((ox*ox + oz*oz) < 100.f) {
                occupied = true;
                break;
            }
        }
        if (occupied) continue;

        float ground = Heightmap::sample(spawn_xz.x, spawn_xz.z);
        const auto& ma = assets_->models[static_cast<std::size_t>(POLICE_MODEL_ID)];
        glm::vec3 spawn_pos{spawn_xz.x, ground + ma.ride_height_at_rest, spawn_xz.z};
        TrafficDirInfo info = traffic_dir_info(dir);

        int paint_idx = std::min(POLICE_PAINT_IDX,
                                 CAR_MODELS[POLICE_MODEL_ID].paint_count - 1);
        Car* c = create_car_at_pose(spawn_pos, info.yaw_deg - 90.f,
                                     POLICE_MODEL_ID, paint_idx,
                                     Driver::Police);
        if (!c) return false;
        c->police_unit = true;

        // Slightly hotter than civilian sedans, but still using the same
        // suspension/collision setup so police rams stay fair.
        c->vehicle.max_speed    = std::max(c->vehicle.max_speed, 34.f);
        c->vehicle.engine_force = std::max(c->vehicle.engine_force, 21000.f);
        c->vehicle.brake_force  = std::max(c->vehicle.brake_force, 42000.f);
        c->vehicle.lateral_grip = std::max(c->vehicle.lateral_grip, 12.f);
        return true;
    }
    return false;
}

} // namespace pengine
