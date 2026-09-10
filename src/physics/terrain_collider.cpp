#include "physics/terrain_collider.h"

#include "terrain/chunk.h"  // mesh_height_at / mesh_normal_at

#include <algorithm>
#include <cmath>
#include <limits>

#include "road/ribbon.h"
#include "game/snow_clearance.h"
#include "physics/snow_shelter.h"
#include "terrain/heightmap.h"
#include "terrain/surface.h"  // surface_kind_at

namespace apricot {
namespace {

// --- ray helpers -------------------------------------------------------------

// March step for the general terrain raycast, in metres. Small enough that a
// ray cannot skip a ridge at the height field's steepest gradient, large enough
// that a 500 m trace is still cheap. The suspension does not use this path —
// probe_down() is exact — so this only has to be right, not fast.
constexpr float kMarchStep = 0.25f;
constexpr int kBisectIterations = 24;

float with_ground_snow(float base_height, float snow_depth) {
    // Keep the zero-depth path bit-identical to the old collider, including
    // unusual signed-zero terrain samples.
    return snow_depth > 0.0f ? base_height + snow_depth : base_height;
}

bool ray_vs_box(const AABB& b, glm::vec3 origin, glm::vec3 dir, float max_t,
                float& out_t, glm::vec3& out_normal) {
    // Slab test. Written with explicit per-axis branches rather than a
    // divide-by-zero-and-let-infinity-sort-it-out trick, because the second
    // form produces a NaN (not an infinity) when the origin sits exactly on a
    // slab plane with zero direction, and a NaN compares false against every
    // bound so the box silently stops existing.
    float t_min = 0.0f;
    float t_max = max_t;
    int hit_axis = -1;
    float hit_sign = 1.0f;

    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {dir.x, dir.y, dir.z};
    const float lo[3] = {b.min.x, b.min.y, b.min.z};
    const float hi[3] = {b.max.x, b.max.y, b.max.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(d[axis]) < 1e-8f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
            continue;
        }
        const float inv = 1.0f / d[axis];
        float t0 = (lo[axis] - o[axis]) * inv;
        float t1 = (hi[axis] - o[axis]) * inv;
        float sign = -1.0f;
        if (t0 > t1) {
            std::swap(t0, t1);
            sign = 1.0f;
        }
        if (t0 > t_min) {
            t_min = t0;
            hit_axis = axis;
            hit_sign = sign;
        }
        t_max = std::min(t_max, t1);
        if (t_min > t_max) return false;
    }

    out_t = t_min;
    out_normal = glm::vec3{0.0f};
    if (hit_axis >= 0) {
        out_normal[hit_axis] = hit_sign;
    } else {
        // Origin started inside the box. There is no entry face; report the
        // surface as pointing back the way the ray came so a caller pushing
        // out of penetration still gets a usable direction.
        out_normal = -dir;
    }
    return true;
}

bool make_oriented_box(glm::vec3 centre, glm::vec3 half, float yaw,
                       Surface material, StaticBox& out) {
    if (!std::isfinite(yaw)) return false;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(centre[i]) || !std::isfinite(half[i]) || half[i] <= 0.0f)
            return false;
    }
    const float c = std::cos(yaw), s = std::sin(yaw);
    const glm::vec3 broad_half{std::fabs(c)*half.x + std::fabs(s)*half.z,
                               half.y, std::fabs(s)*half.x + std::fabs(c)*half.z};
    out.bounds = {centre - broad_half, centre + broad_half};
    out.material = material;
    out.oriented = true;
    out.local_bounds = {-half, half};
    out.centre = centre;
    out.axis_x = {c, -s};
    out.axis_z = {s, c};
    return true;
}

}  // namespace

// --- the smooth field --------------------------------------------------------

float TerrainCollider::field_height(float x, float z) const {
    return height_at(seed_, x, z);
}

glm::vec3 TerrainCollider::field_normal(float x, float z) const {
    return normal_at(seed_, x, z);
}

// --- the meshed surface ------------------------------------------------------

float TerrainCollider::height(float x, float z) const {
    // Delegates to terrain's own reconstruction rather than repeating it. This
    // used to rebuild the lattice cell and blend height_at() at its corners --
    // a second derivation of the surface that draws, which is the one thing
    // this engine's collision rule forbids. The two agreed to 15 microns, so it
    // was not wrong; it was a copy waiting to drift the first time the mesher
    // changed its triangulation.
    const float base = mesh_height_at(seed_, x, z);
    return with_ground_snow(base, local_snow_collision_depth(x, base, z));
}

glm::vec3 TerrainCollider::normal(float x, float z) const {
    // FACE normal of the drawn triangle, not a blend of vertex normals.
    //
    // This is a real bug fixed, not a tidy-up. The blended version was the
    // SHADING normal, and measured against the canonical face normal it was out
    // by up to 1.02 -- for unit vectors, most of a right angle. The suspension
    // builds its tyre axes from this, so contact was being resolved against a
    // plane the geometry does not have, and the car never fully settled.
    const glm::vec3 n = mesh_normal_at(seed_, x, z);

    // A caller passing a non-finite coordinate still lands here, and a NaN
    // normal propagates straight into the tyre axes.
    const float len = glm::length(n);
    if (!(len > 1e-6f)) return glm::vec3{0.0f, 1.0f, 0.0f};
    return n / len;
}

void TerrainCollider::set_snow_collision_depth(float depth_metres) {
    snow_collision_depth_metres_ =
        std::isfinite(depth_metres) ? std::max(depth_metres, 0.0f) : 0.0f;
}

void TerrainCollider::set_snow_clearance(const SnowClearanceField* field,
                                        float raw_depth) {
    snow_clearance_ = field;
    raw_snow_depth_metres_ = std::isfinite(raw_depth) ? std::max(raw_depth, 0.0f) : 0.0f;
}

float TerrainCollider::snow_depth_at(float x, float base_y, float z) const {
    if (snow_shelter_ && snow_shelter_->covered(x, base_y, z)) return 0.0f;
    return snow_clearance_ ? snow_clearance_->depth_at(x, base_y, z,
        raw_snow_depth_metres_) : raw_snow_depth_metres_;
}

float TerrainCollider::local_snow_collision_depth(float x, float base_y,
                                                  float z) const {
    if (snow_shelter_ && snow_shelter_->covered(x, base_y, z)) return 0.0f;
    if (!snow_clearance_) return snow_collision_depth_metres_;
    return std::min(snow_collision_depth_metres_, static_cast<float>(
        snowpack_collision_from_depth(snow_depth_at(x, base_y, z)).depth_m));
}

// --- props -------------------------------------------------------------------

void TerrainCollider::add_static_box(const AABB& bounds, Surface material) {
    // An inverted (never-expanded) AABB passes every containment test it is
    // given and would become a prop covering the entire world.
    if (!bounds.valid()) return;
    boxes_.push_back(StaticBox{bounds, material});
}

void TerrainCollider::clear_static_boxes() {
    boxes_.clear();
    kinematic_boxes_.clear();
    road_solid_slots_.clear();
}

void TerrainCollider::add_static_oriented_box(glm::vec3 centre, glm::vec3 half,
                                              float yaw, Surface material) {
    StaticBox box;
    if (make_oriented_box(centre, half, yaw, material, box)) boxes_.push_back(box);
}

std::size_t TerrainCollider::add_kinematic_box(const AABB& bounds) {
    if (!bounds.valid()) return static_cast<std::size_t>(-1);
    const std::size_t id = boxes_.size();
    add_static_box(bounds, Surface::Rock);
    kinematic_boxes_.push_back(id);
    return id;
}

bool TerrainCollider::set_kinematic_enabled(std::size_t id, bool enabled) {
    if (id>=boxes_.size() || std::find(kinematic_boxes_.begin(),kinematic_boxes_.end(),id)==kinematic_boxes_.end()) return false;
    boxes_[id].enabled=enabled;
    return true;
}

bool TerrainCollider::set_kinematic_vehicle(std::size_t id, bool is_vehicle) {
    if (id>=boxes_.size() || std::find(kinematic_boxes_.begin(),kinematic_boxes_.end(),id)==kinematic_boxes_.end()) return false;
    boxes_[id].is_vehicle=is_vehicle;
    return true;
}

bool TerrainCollider::set_kinematic_breakaway(std::size_t id, uint32_t owner_id,
                                             float speed) {
    if (id >= boxes_.size() || !std::isfinite(speed) || speed <= 0.0f ||
        std::find(kinematic_boxes_.begin(), kinematic_boxes_.end(), id) ==
            kinematic_boxes_.end()) return false;
    boxes_[id].breakaway_id = owner_id;
    boxes_[id].breakaway_speed = speed;
    return true;
}

bool TerrainCollider::set_kinematic_box(std::size_t id, const AABB& bounds) {
    if (!bounds.valid() || id >= boxes_.size() ||
        std::find(kinematic_boxes_.begin(), kinematic_boxes_.end(), id) ==
            kinematic_boxes_.end()) return false;
    const bool is_vehicle=boxes_[id].is_vehicle;
    const float breakaway_speed=boxes_[id].breakaway_speed;
    const uint32_t breakaway_id=boxes_[id].breakaway_id;
    boxes_[id] = StaticBox{bounds, boxes_[id].material};
    boxes_[id].is_vehicle=is_vehicle;
    boxes_[id].breakaway_speed=breakaway_speed;
    boxes_[id].breakaway_id=breakaway_id;
    return true;
}

std::size_t TerrainCollider::add_kinematic_oriented_box(glm::vec3 centre,
                                                       glm::vec3 half, float yaw) {
    StaticBox box;
    if (!make_oriented_box(centre, half, yaw, Surface::Rock, box))
        return static_cast<std::size_t>(-1);
    const std::size_t id = add_kinematic_box(box.bounds);
    boxes_[id] = box;
    return id;
}

bool TerrainCollider::set_kinematic_oriented_box(std::size_t id, glm::vec3 centre,
                                                 glm::vec3 half, float yaw) {
    if (id >= boxes_.size() ||
        std::find(kinematic_boxes_.begin(), kinematic_boxes_.end(), id) ==
            kinematic_boxes_.end()) return false;
    StaticBox box;
    if (!make_oriented_box(centre, half, yaw, boxes_[id].material, box)) return false;
    // Pose updates keep the actor's collision/audio classification.
    box.is_vehicle = boxes_[id].is_vehicle;
    box.breakaway_speed = boxes_[id].breakaway_speed;
    box.breakaway_id = boxes_[id].breakaway_id;
    boxes_[id] = box;
    return true;
}

void TerrainCollider::add_static_ground_rect(glm::vec2 centre, float height,
                                             glm::vec2 half_extents,
                                             float yaw_radians,
                                             Surface material) {
    if (!std::isfinite(centre.x) || !std::isfinite(centre.y) ||
        !std::isfinite(height) || !std::isfinite(half_extents.x) ||
        !std::isfinite(half_extents.y) || !std::isfinite(yaw_radians) ||
        !(half_extents.x > 0.0f) || !(half_extents.y > 0.0f)) {
        return;
    }

    const float c = std::cos(yaw_radians);
    const float s = std::sin(yaw_radians);
    ground_rects_.push_back(StaticGroundRect{
        centre, {c, -s}, {s, c}, half_extents, height, material});
}

void TerrainCollider::clear_static_ground_rects() { ground_rects_.clear(); }

// --- baked road surfaces ----------------------------------------------------

void TerrainCollider::set_road_collision(const RoadCollision& road) {
    clear_road_collision();
    for (std::size_t i = 0; i < road.solids.size(); ++i) {
        const auto& solid = road.solids[i];
        if (i >= road_solid_slots_.size())
            road_solid_slots_.push_back(add_kinematic_oriented_box(solid.centre,solid.half,solid.yaw));
        else
            set_kinematic_oriented_box(road_solid_slots_[i],solid.centre,solid.half,solid.yaw);
        set_kinematic_enabled(road_solid_slots_[i],true);
    }
    road_surfaces_.reserve(road.triangles.size());

    for (const RoadCollisionTri& source : road.triangles) {
        if (road_surfaces_.size() >=
            static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
            break;
        }
        const uint32_t index = static_cast<uint32_t>(road_surfaces_.size());
        road_surfaces_.push_back(RoadSurface{source.geom, source.material});

        const float min_x = std::min({source.geom.a.x, source.geom.b.x,
                                      source.geom.c.x});
        const float max_x = std::max({source.geom.a.x, source.geom.b.x,
                                      source.geom.c.x});
        const float min_z = std::min({source.geom.a.z, source.geom.b.z,
                                      source.geom.c.z});
        const float max_z = std::max({source.geom.a.z, source.geom.b.z,
                                      source.geom.c.z});
        const ChunkCoord lo = chunk_at(min_x, min_z);
        const ChunkCoord hi = chunk_at(max_x, max_z);
        for (int32_t cz = lo.z; cz <= hi.z; ++cz) {
            for (int32_t cx = lo.x; cx <= hi.x; ++cx) {
                road_cells_[ChunkCoord{cx, cz}].push_back(index);
            }
        }
    }
}

void TerrainCollider::clear_road_collision() {
    for (std::size_t slot : road_solid_slots_) set_kinematic_enabled(slot,false);
    road_surfaces_.clear();
    road_cells_.clear();
}

bool TerrainCollider::road_surface_at(float x, float z, float origin_y,
                                      float max_distance, float& out_y,
                                      glm::vec3& out_normal,
                                      Surface& out_material, float& out_snow_depth) const {
    const auto bucket = road_cells_.find(chunk_at(x, z));
    if (bucket == road_cells_.end()) return false;

    bool found = false;
    float highest = -std::numeric_limits<float>::infinity();
    // Lets a suspension already a few centimetres into a kerb recover onto the
    // slab, while a car underneath a bridge cannot probe upward through its deck.
    constexpr float kPenetrationAllowance = 0.35f;
    constexpr float kEdgeTolerance = -1e-4f;

    for (const uint32_t index : bucket->second) {
        if (static_cast<std::size_t>(index) >= road_surfaces_.size()) continue;
        const RoadSurface& surface = road_surfaces_[index];
        const glm::vec3& a = surface.geom.a;
        const glm::vec3& b = surface.geom.b;
        const glm::vec3& c = surface.geom.c;

        const float denominator =
            (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(denominator) < 1e-9f) continue;
        const float w0 =
            ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) /
            denominator;
        const float w1 =
            ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) /
            denominator;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < kEdgeTolerance || w1 < kEdgeTolerance ||
            w2 < kEdgeTolerance) {
            continue;
        }

        const float base_y = w0 * a.y + w1 * b.y + w2 * c.y;
        const float y = with_ground_snow(base_y,
            local_snow_collision_depth(x, base_y, z));
        const float gap = origin_y - y;
        if (gap < -kPenetrationAllowance || gap > max_distance) continue;
        if (found && y <= highest) continue;
        found = true;
        highest = y;
        out_y = y;
        out_normal = surface.geom.normal;
        out_material = surface.material;
        out_snow_depth = snow_depth_at(x, base_y, z);
    }
    return found;
}

// --- materials ---------------------------------------------------------------

void TerrainCollider::paint_surface(const AABB& region, Surface material) {
    if (!region.valid()) return;
    paint_.push_back(SurfacePaint{region, material});
}

void TerrainCollider::clear_surface_paint() { paint_.clear(); }

Surface TerrainCollider::material(float x, float z) const {
    // Reverse order: the last paint laid down wins, so a small patch dropped
    // on top of a big one behaves the way anyone painting it would expect.
    for (std::size_t i = paint_.size(); i > 0u; --i) {
        const SurfacePaint& p = paint_[i - 1u];
        if (x >= p.region.min.x && x <= p.region.max.x && z >= p.region.min.z &&
            z <= p.region.max.z) {
            return p.material;
        }
    }
    // Delegates to terrain's classifier rather than repeating it -- the same
    // fix, one layer up, that height()/normal() already took.
    //
    // This file used to hold its own classify_surface(): a hard cutoff on
    // normal.y and a patch-noise coin flip. It was not close. Measured over
    // 11,559 land samples in the home basin it named a DIFFERENT material from
    // the mesher 40.85% of the time, and it never returned sand ANYWHERE on the
    // island, because its sand test was an altitude 27 m below sea level. Every
    // beach in the game gripped like grass. Its own comment predicted the
    // symptom -- "that gravel section grips like tarmac" -- and was right.
    //
    // surface_kind_at() evaluates the field rather than reading a mesh, so this
    // still answers in chunks that have never been meshed. Grip must not depend
    // on streaming state.
    return surface_kind_at(seed_, x, z);
}

float TerrainCollider::grip(float x, float z) const {
    return surface_grip(material(x, z), wetness_);
}

// --- probes ------------------------------------------------------------------

TerrainCollider::GroundHit TerrainCollider::probe_down(
    glm::vec3 origin, float max_distance, ProbeVehicles vehicles) const {
    float surface_y = height(origin.x, origin.z);
    glm::vec3 surface_n = normal(origin.x, origin.z);
    Surface mat = material(origin.x, origin.z);
    bool prop = false;
    bool road = false;
    float snow_depth = snow_depth_at(origin.x,
        mesh_height_at(seed_, origin.x, origin.z), origin.z);

    float road_y = 0.0f;
    glm::vec3 road_n{0.0f, 1.0f, 0.0f};
    Surface road_mat = Surface::Rock;
    float road_snow_depth = 0.0f;
    if (road_surface_at(origin.x, origin.z, origin.y, max_distance, road_y,
                        road_n, road_mat, road_snow_depth) &&
        road_y > surface_y) {
        surface_y = road_y;
        surface_n = road_n;
        mat = road_mat;
        road = true;
        snow_depth = road_snow_depth;
    }

    // Authored plot paving is top-only like a road slab, but keeps its exact
    // rotated footprint. A small recovery allowance fixes feet that begin in
    // the visible slab without pulling somebody under an elevated platform up
    // through it.
    constexpr float kGroundRectPenetrationAllowance = 0.35f;
    constexpr float kGroundRectEdgeTolerance = 1e-4f;
    const glm::vec2 p{origin.x, origin.z};
    for (const StaticGroundRect& ground : ground_rects_) {
        const glm::vec2 delta = p - ground.centre;
        const float local_x = glm::dot(delta, ground.axis_x);
        const float local_z = glm::dot(delta, ground.axis_z);
        if (std::fabs(local_x) >
                ground.half_extents.x + kGroundRectEdgeTolerance ||
            std::fabs(local_z) >
                ground.half_extents.y + kGroundRectEdgeTolerance) {
            continue;
        }
        const float ground_y = with_ground_snow(
            ground.height, local_snow_collision_depth(origin.x, ground.height, origin.z));
        const float gap = origin.y - ground_y;
        if (gap < -kGroundRectPenetrationAllowance || gap > max_distance) {
            continue;
        }
        if (ground_y <= surface_y) continue;

        surface_y = ground_y;
        surface_n = glm::vec3{0.0f, 1.0f, 0.0f};
        mat = ground.material;
        prop = false;
        road = false;
        snow_depth = snow_depth_at(origin.x, ground.height, origin.z);
    }

    for (const StaticBox& b : boxes_) {
        if (!b.enabled || (b.is_vehicle && vehicles == ProbeVehicles::Exclude)) continue;
        if (origin.x < b.bounds.min.x || origin.x > b.bounds.max.x) continue;
        if (origin.z < b.bounds.min.z || origin.z > b.bounds.max.z) continue;
        if (b.oriented) {
            const glm::vec3 local_origin = b.local_point(origin);
            if (local_origin.x < b.local_bounds.min.x || local_origin.x > b.local_bounds.max.x ||
                local_origin.z < b.local_bounds.min.z || local_origin.z > b.local_bounds.max.z) continue;
        }
        // Started underneath the box: its top is not what we are standing on.
        if (origin.y < b.bounds.min.y) continue;
        // Buried in the hillside, or lower than what we already found.
        if (b.bounds.max.y <= surface_y) continue;

        surface_y = b.bounds.max.y;
        surface_n = glm::vec3{0.0f, 1.0f, 0.0f};
        mat = b.material;
        prop = true;
        road = false;
        snow_depth = 0.0f;
    }

    GroundHit out;
    out.distance = origin.y - surface_y;
    // `<=` rather than `<`, and no lower bound: a negative drop means the
    // origin is already under the surface, which is a hit that callers very
    // much need to hear about.
    out.hit = out.distance <= max_distance;
    out.point = glm::vec3{origin.x, surface_y, origin.z};
    out.normal = surface_n;
    out.material = mat;
    out.grip = surface_grip(mat, wetness_);
    out.prop = prop;
    out.road = road;
    out.snow_depth_m = snow_depth;
    return out;
}

TerrainCollider::GroundHit TerrainCollider::raycast(glm::vec3 origin,
                                                    glm::vec3 dir,
                                                    float max_distance) const {
    GroundHit out;
    const float dir_len = glm::length(dir);
    if (!(dir_len > 1e-8f) || !(max_distance > 0.0f)) return out;
    const glm::vec3 d = dir / dir_len;

    float best_t = max_distance;
    bool found = false;
    glm::vec3 best_n{0.0f, 1.0f, 0.0f};
    bool best_is_prop = false;
    Surface best_mat = Surface::Rock;

    // --- terrain: march until the ray crosses the surface, then bisect ------
    // Signed height above the meshed surface. Marching the SIGN rather than
    // stepping to a fixed tolerance is what makes this exact to within the
    // bisection: once a bracket exists the answer is in it.
    auto above = [&](float t) {
        const glm::vec3 p = origin + d * t;
        return p.y - height(p.x, p.z);
    };

    if (above(0.0f) <= 0.0f) {
        // Already underground at the origin. Report it at t = 0 rather than
        // marching forward and "finding" the far wall of the hill.
        best_t = 0.0f;
        found = true;
        const glm::vec3 p = origin;
        best_n = normal(p.x, p.z);
        best_mat = material(p.x, p.z);
    } else {
        float t_prev = 0.0f;
        for (float t = kMarchStep; !found; t += kMarchStep) {
            const float t_now = std::min(t, max_distance);
            if (above(t_now) <= 0.0f) {
                float lo = t_prev;
                float hi = t_now;
                for (int i = 0; i < kBisectIterations; ++i) {
                    const float mid = (lo + hi) * 0.5f;
                    if (above(mid) > 0.0f) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                best_t = hi;
                found = true;
                const glm::vec3 p = origin + d * best_t;
                best_n = normal(p.x, p.z);
                best_mat = material(p.x, p.z);
                break;
            }
            if (t_now >= max_distance) break;
            t_prev = t_now;
        }
    }

    // --- props: exact, and they can only shorten the answer -----------------
    for (const StaticBox& b : boxes_) {
        if (!b.enabled) continue;
        float t = 0.0f;
        glm::vec3 n{0.0f};
        if (!ray_vs_box(b.collision_bounds(), b.local_point(origin),
                        b.local_direction(d), found ? best_t : max_distance, t,
                        n)) {
            continue;
        }
        if (found && t >= best_t) continue;
        best_t = t;
        best_n = b.world_direction(n);
        best_mat = b.material;
        best_is_prop = true;
        found = true;
    }

    if (!found) return out;

    out.hit = true;
    out.distance = best_t;
    out.point = origin + d * best_t;
    out.normal = best_n;
    out.material = best_mat;
    out.grip = surface_grip(best_mat, wetness_);
    out.prop = best_is_prop;
    out.snow_depth_m = best_is_prop ? 0.0f : snow_depth_at(out.point.x,
        mesh_height_at(seed_, out.point.x, out.point.z), out.point.z);
    return out;
}

}  // namespace apricot
