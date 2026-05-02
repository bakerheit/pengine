#include "game/traffic.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "render/mesh.h"
#include "scene/aabb.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/heightmap.h"

namespace pengine {

namespace {

constexpr glm::vec3 CAR_PALETTE[] = {
    {0.85f, 0.20f, 0.20f}, {0.20f, 0.30f, 0.85f}, {0.95f, 0.92f, 0.85f},
    {0.30f, 0.30f, 0.32f}, {0.95f, 0.78f, 0.20f}, {0.20f, 0.55f, 0.30f},
    {0.55f, 0.40f, 0.25f},
};

constexpr glm::vec3 CAR_FULL_EXTENTS{2.f, 1.0f, 4.0f};
constexpr float     CAR_LENGTH      = CAR_FULL_EXTENTS.z;
constexpr float     CAR_BODY_Y      = 0.55f;

// IDM-lite tuning.
constexpr float ACCEL_MAX     = 5.f;    // m/s^2
constexpr float BRAKE_MAX     = 9.f;    // m/s^2
constexpr float TIME_HEADWAY  = 1.4f;   // seconds of gap to leader
constexpr float MIN_GAP       = 2.5f;   // metres at standstill

// Stop line: how far back from the next intersection a car waits at red.
constexpr float STOP_BACK     = 6.f;

// Traffic light cycle.
constexpr float LIGHT_PERIOD  = 12.f;   // full N-S + E-W cycle, seconds
constexpr float LIGHT_HALF    = LIGHT_PERIOD * 0.5f;
constexpr float YELLOW_TIME   = 1.5f;   // hard-stop window at end of green

struct DirInfo {
    int       di, dj;
    glm::vec3 right_offset;  // unit, multiplied by lane_offset later
    float     yaw_deg;
};

inline DirInfo dir_info(GridDir d) {
    switch (d) {
        case GridDir::East:  return {+1,  0, { 0,0,-1},   0.f};
        case GridDir::North: return { 0, +1, {+1,0, 0},  90.f};
        case GridDir::West:  return {-1,  0, { 0,0,+1}, 180.f};
        case GridDir::South: return { 0, -1, {-1,0, 0}, 270.f};
    }
    return {1, 0, {0,0,-1}, 0.f};
}

inline bool is_ew(GridDir d) {
    return d == GridDir::East || d == GridDir::West;
}

// Per-intersection traffic-light state at `t` seconds. Returns true if the
// car's direction has green. Each intersection has a deterministic phase
// offset so the city doesn't blink in unison.
bool light_is_green(int i, int j, GridDir car_dir, double t) {
    std::uint32_t hash = static_cast<std::uint32_t>(i) * 0x9E3779B1u
                       ^ static_cast<std::uint32_t>(j) * 0x85EBCA77u;
    float offset = static_cast<float>(hash & 0xFFFFu) / 65535.f * LIGHT_PERIOD;
    float local  = static_cast<float>(std::fmod(t + offset, LIGHT_PERIOD));
    bool ew_green = (local < LIGHT_HALF);
    // Treat last YELLOW_TIME of green as red so cars approaching the line
    // start braking instead of running it.
    if (ew_green && local > LIGHT_HALF - YELLOW_TIME) ew_green = false;
    if (!ew_green && local > LIGHT_PERIOD - YELLOW_TIME) ew_green = true; // NS yellow
    return is_ew(car_dir) ? ew_green : !ew_green;
}

} // namespace

void TrafficSystem::init(Scene* scene, const Mesh* cube_mesh,
                          const Texture* car_texture,
                          RoadGraph* graph, int target_count) {
    scene_   = scene;
    mesh_    = cube_mesh;
    texture_ = car_texture;
    graph_   = graph;
    target_  = target_count;
}

void TrafficSystem::shutdown() {
    while (!cars_.empty()) destroy_car(cars_.size() - 1);
    scene_ = nullptr;
    graph_ = nullptr;
}

bool TrafficSystem::try_spawn(const glm::vec3& camera_pos, double /*time_seconds*/) {
    if (!graph_ || graph_->loaded_cell_count() == 0) return false;

    for (int attempt = 0; attempt < 16; ++attempt) {
        int i, j;
        if (!graph_->random_loaded_intersection(rng_, i, j)) return false;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(i, j, options);
        if (n == 0) continue;
        GridDir dir = options[rng_() % static_cast<unsigned>(n)];
        DirInfo di  = dir_info(dir);

        float along = static_cast<float>(rng_() & 0xFFFFu) / 65535.f * ROAD_PITCH;

        glm::vec3 from_pos = RoadGraph::intersection_pos(i, j, 0.f);
        glm::vec3 unit{static_cast<float>(di.di), 0.f, static_cast<float>(di.dj)};
        glm::vec3 spawn_xz = from_pos + unit * along + di.right_offset * lane_offset_;

        float dx = spawn_xz.x - camera_pos.x;
        float dz = spawn_xz.z - camera_pos.z;
        float dist = std::sqrt(dx*dx + dz*dz);
        if (dist < spawn_min_ || dist > spawn_max_) continue;

        Car car;
        car.i = i; car.j = j;
        car.dir = dir;
        car.distance_along = along;
        car.target_speed = 9.f + static_cast<float>(rng_() & 0xFFFu) / 4096.f * 7.f; // 9..16 m/s
        car.speed = car.target_speed * 0.6f; // start moving at a reasonable pace
        car.tint = CAR_PALETTE[rng_() % (sizeof(CAR_PALETTE)/sizeof(CAR_PALETTE[0]))];

        car.node = scene_->create_node();
        AABB local;
        local.min = -CAR_FULL_EXTENTS * 0.5f;
        local.max =  CAR_FULL_EXTENTS * 0.5f;
        car.node->renderable = Renderable{mesh_, local, car.tint,
                                           glm::vec2{1.f, 1.f}, texture_};
        cars_.push_back(car);
        update_visual(cars_.back());
        return true;
    }
    return false;
}

void TrafficSystem::update_speed(std::size_t idx, float dt, double time_seconds) {
    Car& c = cars_[idx];

    // Constraint 1: free-flow desired speed.
    float target = c.target_speed;

    // Constraint 2: leader car directly ahead, same segment + lane + direction.
    {
        float best_gap = std::numeric_limits<float>::infinity();
        for (std::size_t k = 0; k < cars_.size(); ++k) {
            if (k == idx) continue;
            const Car& o = cars_[k];
            if (o.i != c.i || o.j != c.j || o.dir != c.dir) continue;
            if (o.distance_along <= c.distance_along) continue;
            float gap = o.distance_along - c.distance_along - CAR_LENGTH;
            if (gap < best_gap) best_gap = gap;
        }
        if (std::isfinite(best_gap)) {
            float leader_target = (best_gap - MIN_GAP) / TIME_HEADWAY;
            target = std::min(target, std::max(0.f, leader_target));
        }
    }

    // Constraint 3: traffic light at the segment's end intersection.
    {
        DirInfo info = dir_info(c.dir);
        int next_i = c.i + info.di;
        int next_j = c.j + info.dj;
        if (!light_is_green(next_i, next_j, c.dir, time_seconds)) {
            float stop_at = ROAD_PITCH - STOP_BACK;
            float gap     = stop_at - c.distance_along;
            if (gap > 0.f) {
                float light_target = (gap - MIN_GAP) / TIME_HEADWAY;
                target = std::min(target, std::max(0.f, light_target));
            }
        }
    }

    // Move c.speed toward target with bounded accel / brake.
    float diff = target - c.speed;
    float delta = (diff > 0.f) ? std::min(diff, ACCEL_MAX * dt)
                                : std::max(diff, -BRAKE_MAX * dt);
    c.speed += delta;
    if (c.speed < 0.f) c.speed = 0.f;
}

void TrafficSystem::advance(std::size_t idx, float dt) {
    Car& c = cars_[idx];
    c.distance_along += c.speed * dt;
    while (c.distance_along >= ROAD_PITCH) {
        c.distance_along -= ROAD_PITCH;

        DirInfo cur = dir_info(c.dir);
        c.i += cur.di;
        c.j += cur.dj;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(c.i, c.j, options);
        if (n == 0) {
            c.dir = opposite(c.dir);
            c.distance_along = 0.f;
            continue;
        }
        GridDir back = opposite(c.dir);
        std::array<GridDir, 4> non_uturn;
        std::size_t m = 0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(n); ++k)
            if (options[k] != back) non_uturn[m++] = options[k];
        c.dir = (m > 0) ? non_uturn[rng_() % m] : back;
    }
    update_visual(c);
}

void TrafficSystem::update_visual(Car& car) {
    DirInfo info = dir_info(car.dir);
    glm::vec3 from = RoadGraph::intersection_pos(car.i, car.j, 0.f);
    glm::vec3 unit{static_cast<float>(info.di), 0.f, static_cast<float>(info.dj)};

    glm::vec3 xz = from + unit * car.distance_along + info.right_offset * lane_offset_;
    float ground = Heightmap::sample(xz.x, xz.z);

    car.node->transform.position = {xz.x, ground + CAR_BODY_Y, xz.z};
    car.node->transform.rotation = glm::angleAxis(glm::radians(info.yaw_deg),
                                                    glm::vec3{0.f, 1.f, 0.f});
    car.node->transform.scale = CAR_FULL_EXTENTS;
    car.node->mark_dirty();
}

void TrafficSystem::destroy_car(std::size_t idx) {
    if (idx >= cars_.size()) return;
    if (cars_[idx].node && scene_) scene_->remove_node(cars_[idx].node);
    cars_[idx] = std::move(cars_.back());
    cars_.pop_back();
}

void TrafficSystem::update(float dt, double time_seconds,
                            const glm::vec3& camera_pos) {
    if (!graph_) return;

    // Despawn out-of-range or in unloaded cells.
    for (std::size_t i = cars_.size(); i-- > 0;) {
        const Car& c = cars_[i];
        if (!graph_->is_intersection_loaded(c.i, c.j)) { destroy_car(i); continue; }
        glm::vec3 p = c.node->transform.position;
        float dx = p.x - camera_pos.x, dz = p.z - camera_pos.z;
        if (dx*dx + dz*dz > despawn_dist_ * despawn_dist_) { destroy_car(i); continue; }
    }

    // Spawn up to a few per frame to refill toward target.
    int budget = 2;
    while (static_cast<int>(cars_.size()) < target_ && budget-- > 0) {
        if (!try_spawn(camera_pos, time_seconds)) break;
    }

    // Update speed (IDM-lite + lights), then advance.
    for (std::size_t i = 0; i < cars_.size(); ++i) update_speed(i, dt, time_seconds);
    for (std::size_t i = 0; i < cars_.size(); ++i) advance(i, dt);
}

} // namespace pengine
