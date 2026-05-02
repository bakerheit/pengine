#include "game/traffic.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "render/mesh.h"
#include "scene/aabb.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/city_layout.h"
#include "world/heightmap.h"

namespace pengine {

namespace {

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
constexpr float LIGHT_PERIOD  = 20.f;   // full N-S + E-W cycle, seconds
constexpr float LIGHT_HALF    = LIGHT_PERIOD * 0.5f;
constexpr float YELLOW_TIME   = 2.0f;   // hard-stop window at end of green

struct DirInfo {
    int       di, dj;
    glm::vec3 right_offset;  // unit, multiplied by lane_offset later
    float     yaw_deg;
};

inline DirInfo dir_info(GridDir d) {
    // yaw_deg rotates the car's local -Z forward (engine convention, matches
    // Vehicle::forward()) into world motion via R_y(yaw_deg) under right-
    // handed Y rotation. R_y(θ) * -Z = (-sinθ, 0, -cosθ).
    switch (d) {
        case GridDir::East:  return {+1,  0, { 0,0,-1}, 270.f};
        case GridDir::North: return { 0, +1, {+1,0, 0}, 180.f};
        case GridDir::West:  return {-1,  0, { 0,0,+1},  90.f};
        case GridDir::South: return { 0, -1, {-1,0, 0},   0.f};
    }
    return {1, 0, {0,0,-1}, 270.f};
}

inline bool is_ew(GridDir d) {
    return d == GridDir::East || d == GridDir::West;
}

enum class LightPhase { Red, Yellow, Green };

// Per-intersection traffic-light phase at `t` seconds. Each intersection has
// a deterministic phase offset so the city doesn't blink in unison.
LightPhase light_phase(int i, int j, GridDir car_dir, double t) {
    std::uint32_t hash = static_cast<std::uint32_t>(i) * 0x9E3779B1u
                       ^ static_cast<std::uint32_t>(j) * 0x85EBCA77u;
    float offset = static_cast<float>(hash & 0xFFFFu) / 65535.f * LIGHT_PERIOD;
    float local  = static_cast<float>(std::fmod(t + offset, LIGHT_PERIOD));

    // E/W has green during the first half of the cycle; the last YELLOW_TIME
    // seconds of each green window are yellow. The other axis is red whenever
    // this one is green or yellow.
    bool   ew_active = (local < LIGHT_HALF);
    float  into_window = ew_active ? local : (local - LIGHT_HALF);
    bool   yellow = (into_window > LIGHT_HALF - YELLOW_TIME);

    bool   showing_ew = is_ew(car_dir);
    if (showing_ew == ew_active) {
        return yellow ? LightPhase::Yellow : LightPhase::Green;
    }
    return LightPhase::Red;
}

bool light_is_green(int i, int j, GridDir car_dir, double t) {
    return light_phase(i, j, car_dir, t) == LightPhase::Green;
}

// Bright (lit) and dim (off) bulb colors. Dim colors hint at which lamp the
// circle is, so a green-on-bottom traffic-light shape reads correctly even
// when red is the active bulb.
constexpr glm::vec3 RED_BRIGHT    {0.95f, 0.15f, 0.15f};
constexpr glm::vec3 YELLOW_BRIGHT {0.95f, 0.80f, 0.15f};
constexpr glm::vec3 GREEN_BRIGHT  {0.20f, 0.90f, 0.30f};
constexpr glm::vec3 RED_DIM       {0.30f, 0.10f, 0.10f};
constexpr glm::vec3 YELLOW_DIM    {0.32f, 0.28f, 0.10f};
constexpr glm::vec3 GREEN_DIM     {0.10f, 0.28f, 0.13f};
constexpr glm::vec3 METAL_TINT    {0.16f, 0.16f, 0.18f};

// One signal per car approach direction at each intersection. Pole sits on
// the right-hand sidewalk corner past the intersection; arm extends
// perpendicular to the road, so the housing hangs over the lane the
// approaching driver is in. Bulbs face the incoming car.
struct ApproachSpec {
    GridDir car_dir;            // direction the cars covered by this signal travel
    float   corner_dx, corner_dz; // pole corner sign relative to intersection
    float   arm_dx,    arm_dz;    // unit direction the arm extends from the pole
};
constexpr ApproachSpec APPROACHES[4] = {
    {GridDir::East,  +1.f, -1.f,  0.f, +1.f},  // pole SE, arm goes north
    {GridDir::North, +1.f, +1.f, -1.f,  0.f},  // pole NE, arm goes west
    {GridDir::West,  -1.f, +1.f,  0.f, -1.f},  // pole NW, arm goes south
    {GridDir::South, -1.f, -1.f, +1.f,  0.f},  // pole SW, arm goes east
};

constexpr float POLE_HEIGHT     = 6.0f;   // ground → pole top
constexpr float POLE_THICK      = 0.20f;
constexpr float ARM_LENGTH      = 4.0f;   // pole inward across road
constexpr float ARM_THICK       = 0.18f;
constexpr float HOUSING_W       = 0.40f;
constexpr float HOUSING_H       = 1.50f;
constexpr float HOUSING_D       = 0.40f;
constexpr float BULB_SIZE       = 0.32f;
constexpr float BULB_SPACING    = 0.50f;  // vertical gap between bulb centers
constexpr float CORNER_INSET    = 1.5f;
constexpr float CORNER_DIST     = STREET_WIDTH * 0.5f + CORNER_INSET;

inline std::uint64_t pack_ij(int i, int j) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)) << 32)
         |  static_cast<std::uint64_t>(static_cast<std::uint32_t>(j));
}

} // namespace

void TrafficSystem::init(Scene* scene, const Visuals& vis,
                          RoadGraph* graph, int target_count) {
    scene_  = scene;
    vis_    = vis;
    graph_  = graph;
    target_ = target_count;
}

void TrafficSystem::shutdown() {
    while (!cars_.empty()) destroy_car(cars_.size() - 1);
    auto remove = [&](SceneNode* n) {
        if (n && scene_) scene_->remove_node(n);
    };
    while (!lights_.empty()) {
        Light& L = lights_.back();
        remove(L.pole);   remove(L.arm);    remove(L.housing);
        remove(L.bulb_r); remove(L.bulb_y); remove(L.bulb_g);
        lights_.pop_back();
    }
    light_intersections_.clear();
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

        // Choose mesh + paint. Skinned/static model preferred; fall back to
        // the cube when the model wasn't loaded.
        const Mesh*    body_mesh = vis_.car_mesh ? vis_.car_mesh : vis_.light_mesh;
        const Texture* body_tex  = vis_.light_tex;
        if (vis_.car_mesh && !vis_.car_paints.empty()) {
            car.paint_idx = static_cast<int>(rng_() % vis_.car_paints.size());
            body_tex      = vis_.car_paints[static_cast<std::size_t>(car.paint_idx)];
        }

        car.node = scene_->create_node();
        AABB local;
        if (vis_.car_mesh) {
            local.min = vis_.car_mesh->bounds_min();
            local.max = vis_.car_mesh->bounds_max();
        } else {
            local.min = -CAR_FULL_EXTENTS * 0.5f;
            local.max =  CAR_FULL_EXTENTS * 0.5f;
        }
        car.node->renderable = Renderable{body_mesh, local, glm::vec3{1.f, 1.f, 1.f},
                                           glm::vec2{1.f, 1.f}, body_tex};
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

    float y_above = vis_.car_mesh
        ? -vis_.car_mesh->bounds_min().y * vis_.car_scale.y  // model bottom on ground
        : CAR_BODY_Y;                                         // cube: half-height
    car.node->transform.position = {xz.x, ground + y_above, xz.z};
    car.node->transform.rotation = glm::angleAxis(
        glm::radians(info.yaw_deg + vis_.car_yaw_offset_deg),
        glm::vec3{0.f, 1.f, 0.f});
    car.node->transform.scale = vis_.car_mesh ? vis_.car_scale : CAR_FULL_EXTENTS;
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

    // Traffic lights: keep visuals in sync with loaded cells, recolour bulbs.
    sync_lights_to_loaded();
    update_light_visuals(time_seconds);
}

void TrafficSystem::sync_lights_to_loaded() {
    if (!graph_ || !scene_ || !vis_.light_mesh) return;

    auto loaded = graph_->loaded_intersections();
    std::unordered_set<std::uint64_t> wanted;
    wanted.reserve(loaded.size());
    for (const auto& p : loaded) wanted.insert(pack_ij(p.first, p.second));

    // Despawn lights for intersections that are no longer loaded.
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

    // Spawn lights for newly-loaded intersections.
    for (const auto& p : loaded) {
        std::uint64_t k = pack_ij(p.first, p.second);
        if (light_intersections_.insert(k).second) {
            spawn_lights_at(p.first, p.second);
        }
    }
}

void TrafficSystem::spawn_lights_at(int i, int j) {
    glm::vec3 ix = RoadGraph::intersection_pos(i, j, 0.f);

    auto unit_cube = []() {
        AABB a; a.min = -glm::vec3{0.5f}; a.max = glm::vec3{0.5f}; return a;
    };
    auto make_node = [&](const glm::vec3& pos, const glm::vec3& scale,
                         const glm::vec3& tint) {
        SceneNode* n = scene_->create_node();
        n->renderable = Renderable{vis_.light_mesh, unit_cube(), tint,
                                    glm::vec2{1.f, 1.f}, vis_.light_tex};
        n->transform.position = pos;
        n->transform.scale    = scale;
        n->mark_dirty();
        return n;
    };

    for (int a = 0; a < 4; ++a) {
        const ApproachSpec& s = APPROACHES[a];

        // Pole base position (corner of intersection, on sidewalk top).
        float pole_x = ix.x + s.corner_dx * CORNER_DIST;
        float pole_z = ix.z + s.corner_dz * CORNER_DIST;
        float ground = city_ground_sample(pole_x, pole_z);
        float pole_top_y = ground + POLE_HEIGHT;

        // Arm extends from pole top horizontally toward the road.
        float arm_cx  = pole_x + s.arm_dx * (ARM_LENGTH * 0.5f);
        float arm_cz  = pole_z + s.arm_dz * (ARM_LENGTH * 0.5f);
        float arm_cy  = pole_top_y - ARM_THICK * 0.5f;
        glm::vec3 arm_scale =
            std::abs(s.arm_dx) > 0.5f
                ? glm::vec3{ARM_LENGTH, ARM_THICK, ARM_THICK}
                : glm::vec3{ARM_THICK, ARM_THICK, ARM_LENGTH};

        // Housing hangs from arm end.
        float arm_end_x = pole_x + s.arm_dx * ARM_LENGTH;
        float arm_end_z = pole_z + s.arm_dz * ARM_LENGTH;
        float housing_cy = pole_top_y - ARM_THICK - HOUSING_H * 0.5f;
        glm::vec3 housing_pos{arm_end_x, housing_cy, arm_end_z};

        // Face direction = 90° CW rotation of arm direction (toward incoming car).
        glm::vec3 face{-s.arm_dz, 0.f, s.arm_dx};

        // Bulb centres: stacked vertically on the housing's facing side.
        glm::vec3 bulb_offset = face * (HOUSING_D * 0.5f + BULB_SIZE * 0.5f);
        glm::vec3 r_pos = housing_pos + glm::vec3{0.f, +BULB_SPACING, 0.f} + bulb_offset;
        glm::vec3 y_pos = housing_pos                                        + bulb_offset;
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
    auto remove = [&](SceneNode* n) {
        if (n && scene_) scene_->remove_node(n);
    };
    for (std::size_t k = lights_.size(); k-- > 0;) {
        Light& L = lights_[k];
        if (L.i != i || L.j != j) continue;
        remove(L.pole);    remove(L.arm);    remove(L.housing);
        remove(L.bulb_r);  remove(L.bulb_y); remove(L.bulb_g);
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
