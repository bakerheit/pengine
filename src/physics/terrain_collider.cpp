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

// The step for line_of_sight_blocked(), and the reason it differs is written
// on that function. kVertexSpacingMetres is the spacing of the drawn
// lattice; sampling finer than the geometry only re-measures a triangle whose
// shape its corners already fixed.
constexpr float kSightMarchStep = kVertexSpacingMetres;
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

// --- prop buckets --------------------------------------------------------------

// Sixteen metres: a building covers a handful of buckets and a bucket holds a
// few dozen props even in the densest interiors, against the ~14,000 boxes
// every query read before.
constexpr float kPropCellMetres = 16.0f;
// A prop spanning more buckets than this (a 512 m square) goes in the list
// every query reads instead. Only terrain-sized test slabs get there.
constexpr int64_t kMaxPropCells = 1024;
// A query wider than this many buckets reads everything; nothing in the game
// asks one.
constexpr int64_t kMaxQueryCells = 16384;
// Past a thousand kilometres a coordinate is not a place. Refusing to bucket
// it keeps the cell arithmetic well inside int32 without a range check per
// query.
constexpr float kPropGridLimit = 1.0e6f;

struct CellSpan {
    int32_t x0 = 0, z0 = 0, x1 = -1, z1 = -1;
    int64_t cells() const {
        return (int64_t{x1} - x0 + 1) * (int64_t{z1} - z0 + 1);
    }
};

// Monotonic in its argument, which is the whole of why a point inside a box's
// bounds always lands in one of that box's buckets: both sides use this, so
// min <= p <= max gives cell(min) <= cell(p) <= cell(max) with no rounding
// argument needed.
int32_t prop_cell(float v) {
    return static_cast<int32_t>(std::floor(v * (1.0f / kPropCellMetres)));
}

// False for anything that is not a finite place; callers then fall back to
// reading every prop, which is the answer the linear scan gave.
bool prop_span(glm::vec2 lo, glm::vec2 hi, CellSpan& out) {
    if (!(std::fabs(lo.x) < kPropGridLimit && std::fabs(lo.y) < kPropGridLimit &&
          std::fabs(hi.x) < kPropGridLimit && std::fabs(hi.y) < kPropGridLimit))
        return false;
    out.x0 = prop_cell(lo.x);
    out.z0 = prop_cell(lo.y);
    out.x1 = prop_cell(hi.x);
    out.z1 = prop_cell(hi.y);
    return out.x1 >= out.x0 && out.z1 >= out.z0;
}

uint64_t prop_cell_key(int32_t x, int32_t z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(z));
}

void insert_slot(std::vector<uint32_t>& list, uint32_t slot) {
    // Registration appends in slot order, so this is nearly always a
    // push_back; only a kinematic prop moving to a new bucket takes the
    // sorted insert.
    if (list.empty() || list.back() < slot) {
        list.push_back(slot);
        return;
    }
    const auto at = std::lower_bound(list.begin(), list.end(), slot);
    if (at == list.end() || *at != slot) list.insert(at, slot);
}

void erase_slot(std::vector<uint32_t>& list, uint32_t slot) {
    const auto at = std::lower_bound(list.begin(), list.end(), slot);
    if (at != list.end() && *at == slot) list.erase(at);
}

// The broad XZ rectangle of a ground rect, padded. Its footprint test runs in
// the rect's own rotated frame with a 0.1 mm tolerance, and a point that
// passes it must not fall outside the buckets through float rounding in the
// rotation; 5 cm is a hundred times the worst of that at island coordinates.
void ground_rect_bounds(const StaticGroundRect& r, glm::vec2& lo, glm::vec2& hi) {
    constexpr float kPad = 0.05f;
    const glm::vec2 half{
        std::fabs(r.axis_x.x) * r.half_extents.x +
            std::fabs(r.axis_z.x) * r.half_extents.y + kPad,
        std::fabs(r.axis_x.y) * r.half_extents.x +
            std::fabs(r.axis_z.y) * r.half_extents.y + kPad};
    lo = r.centre - half;
    hi = r.centre + half;
}

glm::vec2 xz_min(const AABB& b) { return {b.min.x, b.min.z}; }
glm::vec2 xz_max(const AABB& b) { return {b.max.x, b.max.z}; }

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

// Roof exposure scales the pack exactly as lit.frag scales the drawn cover:
// bare under a building, drifted in from the edges of an open canopy.
float TerrainCollider::snow_depth_at(float x, float base_y, float z) const {
    const float exposure = snow_shelter_ ? snow_shelter_->exposure(x, base_y, z) : 1.0f;
    if (exposure <= 0.0f) return 0.0f;
    return exposure * (snow_clearance_ ? snow_clearance_->depth_at(x, base_y, z,
        raw_snow_depth_metres_) : raw_snow_depth_metres_);
}

float TerrainCollider::local_snow_collision_depth(float x, float base_y,
                                                  float z) const {
    const float exposure = snow_shelter_ ? snow_shelter_->exposure(x, base_y, z) : 1.0f;
    if (exposure <= 0.0f) return 0.0f;
    if (!snow_clearance_ && exposure >= 1.0f) return snow_collision_depth_metres_;
    return std::min(snow_collision_depth_metres_, static_cast<float>(
        snowpack_collision_from_depth(snow_depth_at(x, base_y, z)).depth_m));
}

// --- prop buckets --------------------------------------------------------------

void TerrainCollider::PropGrid::insert(uint32_t slot, glm::vec2 lo, glm::vec2 hi) {
    CellSpan span;
    if (!prop_span(lo, hi, span) || span.cells() > kMaxPropCells) {
        insert_slot(oversized, slot);
        return;
    }
    for (int32_t z = span.z0; z <= span.z1; ++z)
        for (int32_t x = span.x0; x <= span.x1; ++x)
            insert_slot(cells[prop_cell_key(x, z)], slot);
}

// Must be given the SAME rectangle the slot was inserted with: that is what
// finds every bucket it is in.
void TerrainCollider::PropGrid::erase(uint32_t slot, glm::vec2 lo, glm::vec2 hi) {
    CellSpan span;
    if (!prop_span(lo, hi, span) || span.cells() > kMaxPropCells) {
        erase_slot(oversized, slot);
        return;
    }
    for (int32_t z = span.z0; z <= span.z1; ++z)
        for (int32_t x = span.x0; x <= span.x1; ++x) {
            const auto it = cells.find(prop_cell_key(x, z));
            if (it != cells.end()) erase_slot(it->second, slot);
        }
}

void TerrainCollider::index_box(std::size_t slot) {
    const AABB& b = boxes_[slot].bounds;
    box_grid_.insert(static_cast<uint32_t>(slot), xz_min(b), xz_max(b));
}

void TerrainCollider::unindex_box(std::size_t slot) {
    const AABB& b = boxes_[slot].bounds;
    box_grid_.erase(static_cast<uint32_t>(slot), xz_min(b), xz_max(b));
}

template <class Visit>
void TerrainCollider::visit_props_at(const PropGrid& grid, std::size_t count,
                                     float x, float z, Visit&& visit) const {
    CellSpan span;
    if (!broad_phase_ || !prop_span({x, z}, {x, z}, span)) {
        for (std::size_t i = 0; i < count; ++i) visit(static_cast<uint32_t>(i));
        return;
    }
    // The bucket and the oversized list are each ascending; merge them so
    // the visit order is the slot order the linear scan used.
    const auto it = grid.cells.find(prop_cell_key(span.x0, span.z0));
    const uint32_t* a = nullptr;
    const uint32_t* a_end = nullptr;
    if (it != grid.cells.end()) {
        a = it->second.data();
        a_end = a + it->second.size();
    }
    const uint32_t* b = grid.oversized.data();
    const uint32_t* b_end = b + grid.oversized.size();
    while (a != a_end || b != b_end) {
        if (b == b_end || (a != a_end && *a < *b)) {
            visit(*a++);
        } else if (a == a_end || *b < *a) {
            visit(*b++);
        } else {
            visit(*a++);
            ++b;
        }
    }
}

void TerrainCollider::all_boxes(std::vector<uint32_t>& out) const {
    out.resize(boxes_.size());
    for (std::size_t i = 0; i < boxes_.size(); ++i)
        out[i] = static_cast<uint32_t>(i);
}

void TerrainCollider::boxes_near(glm::vec2 lo, glm::vec2 hi,
                                 std::vector<uint32_t>& out) const {
    CellSpan span;
    if (!broad_phase_ || !prop_span(lo, hi, span) ||
        span.cells() > kMaxQueryCells) {
        all_boxes(out);
        return;
    }
    out.clear();
    for (int32_t z = span.z0; z <= span.z1; ++z)
        for (int32_t x = span.x0; x <= span.x1; ++x) {
            const auto it = box_grid_.cells.find(prop_cell_key(x, z));
            if (it != box_grid_.cells.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
        }
    out.insert(out.end(), box_grid_.oversized.begin(), box_grid_.oversized.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// The buckets a thickened XZ segment passes through, swept one column at a
// time: within a column the segment's z range is known exactly, so the sweep
// reads the buckets the segment crosses and none of the ones it only passes
// near. Thickened by 5 cm on every side, so a hit point that float rounding
// nudges off the line (an oriented box's corner, a ray grazing a bucket edge)
// still lands in a bucket that was read.
void TerrainCollider::boxes_along(glm::vec2 a, glm::vec2 b,
                                  std::vector<uint32_t>& out) const {
    constexpr float kPad = 0.05f;
    const glm::vec2 seg_lo = glm::min(a, b);
    const glm::vec2 seg_hi = glm::max(a, b);
    CellSpan span;
    if (!broad_phase_ || !prop_span(seg_lo - kPad, seg_hi + kPad, span) ||
        span.cells() > kMaxQueryCells * 64) {
        all_boxes(out);
        return;
    }
    out.clear();
    const glm::vec2 delta = b - a;
    const bool steep = std::fabs(delta.x) <= 1e-6f;
    for (int32_t x = span.x0; x <= span.x1; ++x) {
        float z_lo = seg_lo.y;
        float z_hi = seg_hi.y;
        if (!steep) {
            // This column's x range, thickened, then clipped to the segment.
            const float u0 = std::clamp(
                static_cast<float>(x) * kPropCellMetres - kPad, seg_lo.x, seg_hi.x);
            const float u1 = std::clamp(
                static_cast<float>(x + 1) * kPropCellMetres + kPad, seg_lo.x, seg_hi.x);
            const float za = a.y + (u0 - a.x) / delta.x * delta.y;
            const float zb = a.y + (u1 - a.x) / delta.x * delta.y;
            z_lo = std::max(seg_lo.y, std::min(za, zb));
            z_hi = std::min(seg_hi.y, std::max(za, zb));
        }
        const int32_t z0 = prop_cell(z_lo - kPad);
        const int32_t z1 = prop_cell(z_hi + kPad);
        for (int32_t z = z0; z <= z1; ++z) {
            const auto it = box_grid_.cells.find(prop_cell_key(x, z));
            if (it != box_grid_.cells.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
    out.insert(out.end(), box_grid_.oversized.begin(), box_grid_.oversized.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// --- props -------------------------------------------------------------------

void TerrainCollider::add_static_box(const AABB& bounds, Surface material) {
    // An inverted (never-expanded) AABB passes every containment test it is
    // given and would become a prop covering the entire world.
    if (!bounds.valid()) return;
    boxes_.push_back(StaticBox{bounds, material});
    index_box(boxes_.size() - 1);
}

void TerrainCollider::clear_static_boxes() {
    boxes_.clear();
    box_grid_.clear();
    kinematic_boxes_.clear();
    road_solid_slots_.clear();
}

void TerrainCollider::add_static_oriented_box(glm::vec3 centre, glm::vec3 half,
                                              float yaw, Surface material) {
    StaticBox box;
    if (!make_oriented_box(centre, half, yaw, material, box)) return;
    boxes_.push_back(box);
    index_box(boxes_.size() - 1);
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
    unindex_box(id);
    boxes_[id] = StaticBox{bounds, boxes_[id].material};
    boxes_[id].is_vehicle=is_vehicle;
    boxes_[id].breakaway_speed=breakaway_speed;
    boxes_[id].breakaway_id=breakaway_id;
    index_box(id);
    return true;
}

std::size_t TerrainCollider::add_kinematic_oriented_box(glm::vec3 centre,
                                                       glm::vec3 half, float yaw) {
    StaticBox box;
    if (!make_oriented_box(centre, half, yaw, Surface::Rock, box))
        return static_cast<std::size_t>(-1);
    // Bucketed by box.bounds inside add_kinematic_box; the assignment below
    // keeps those bounds, so the buckets stay right.
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
    unindex_box(id);
    boxes_[id] = box;
    index_box(id);
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
    glm::vec2 lo{0.0f}, hi{0.0f};
    ground_rect_bounds(ground_rects_.back(), lo, hi);
    rect_grid_.insert(static_cast<uint32_t>(ground_rects_.size() - 1), lo, hi);
}

void TerrainCollider::clear_static_ground_rects() {
    ground_rects_.clear();
    rect_grid_.clear();
}

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
    // Both loops below read only the props bucketed at this point, in slot
    // order (the header says why the order matters); a prop outside the
    // bucket would have failed its footprint test before touching any state.
    const glm::vec2 p{origin.x, origin.z};
    visit_props_at(rect_grid_, ground_rects_.size(), origin.x, origin.z,
                   [&](uint32_t slot) {
        const StaticGroundRect& ground = ground_rects_[slot];
        const glm::vec2 delta = p - ground.centre;
        const float local_x = glm::dot(delta, ground.axis_x);
        const float local_z = glm::dot(delta, ground.axis_z);
        if (std::fabs(local_x) >
                ground.half_extents.x + kGroundRectEdgeTolerance ||
            std::fabs(local_z) >
                ground.half_extents.y + kGroundRectEdgeTolerance) {
            return;
        }
        const float ground_y = with_ground_snow(
            ground.height, local_snow_collision_depth(origin.x, ground.height, origin.z));
        const float gap = origin.y - ground_y;
        if (gap < -kGroundRectPenetrationAllowance || gap > max_distance) {
            return;
        }
        if (ground_y <= surface_y) return;

        surface_y = ground_y;
        surface_n = glm::vec3{0.0f, 1.0f, 0.0f};
        mat = ground.material;
        prop = false;
        road = false;
        snow_depth = snow_depth_at(origin.x, ground.height, origin.z);
    });

    visit_props_at(box_grid_, boxes_.size(), origin.x, origin.z,
                   [&](uint32_t slot) {
        const StaticBox& b = boxes_[slot];
        if (!b.enabled || (b.is_vehicle && vehicles == ProbeVehicles::Exclude)) return;
        if (origin.x < b.bounds.min.x || origin.x > b.bounds.max.x) return;
        if (origin.z < b.bounds.min.z || origin.z > b.bounds.max.z) return;
        if (b.oriented) {
            const glm::vec3 local_origin = b.local_point(origin);
            if (local_origin.x < b.local_bounds.min.x || local_origin.x > b.local_bounds.max.x ||
                local_origin.z < b.local_bounds.min.z || local_origin.z > b.local_bounds.max.z) return;
        }
        // Started underneath the box: its top is not what we are standing on.
        if (origin.y < b.bounds.min.y) return;
        // Buried in the hillside, or lower than what we already found.
        if (b.bounds.max.y <= surface_y) return;

        surface_y = b.bounds.max.y;
        surface_n = glm::vec3{0.0f, 1.0f, 0.0f};
        mat = b.material;
        prop = true;
        road = false;
        snow_depth = 0.0f;
    });

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

bool TerrainCollider::line_of_sight_blocked(glm::vec3 from, glm::vec3 to,
                                            float slack_metres) const {
    const glm::vec3 delta = to - from;
    const float distance = glm::length(delta);
    if (!(distance > 1e-6f)) return false;
    const glm::vec3 d = delta / distance;

    // Anything nearer than this does not count as blocking: the far end of the
    // segment is the target itself, and without the slack the ground under the
    // target's feet blocks every view of it.
    const float limit = distance - std::max(slack_metres, 0.0f);
    if (!(limit > 0.0f)) return false;

    // Boxes first. A building is the usual answer in a city and settles the
    // question without touching the height field. Only the boxes bucketed
    // along the line: any box the ray enters before `limit` has its entry
    // point on the segment, and so sits in a bucket the segment crosses.
    std::vector<uint32_t> along;
    const glm::vec3 far_end = from + d * limit;
    boxes_along({from.x, from.z}, {far_end.x, far_end.z}, along);
    for (const uint32_t slot : along) {
        const StaticBox& b = boxes_[slot];
        if (!b.enabled) continue;
        float t = 0.0f;
        glm::vec3 n{0.0f};
        if (ray_vs_box(b.collision_bounds(), b.local_point(from),
                       b.local_direction(d), limit, t, n) && t < limit) {
            return true;
        }
    }

    // Then the terrain, at the lattice spacing rather than a quarter of it.
    // Starting at the first step rather than zero for the same reason raycast()
    // does: both ends of a sight line sit just above the ground they stand on.
    const auto above = [&](float t) {
        const glm::vec3 p = from + d * t;
        return p.y - height(p.x, p.z);
    };
    if (above(0.0f) <= 0.0f) return true;
    for (float t = kSightMarchStep; t < limit; t += kSightMarchStep) {
        if (above(t) <= 0.0f) return true;
    }
    return above(limit) <= 0.0f;
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
