#include "physics/terrain_collider.h"

#include "terrain/chunk.h"  // mesh_height_at / mesh_normal_at

#include <algorithm>
#include <cmath>

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
    return mesh_height_at(seed_, x, z);
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

// --- props -------------------------------------------------------------------

void TerrainCollider::add_static_box(const AABB& bounds, Surface material) {
    // An inverted (never-expanded) AABB passes every containment test it is
    // given and would become a prop covering the entire world.
    if (!bounds.valid()) return;
    boxes_.push_back(StaticBox{bounds, material});
}

void TerrainCollider::clear_static_boxes() { boxes_.clear(); }

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
    glm::vec3 origin, float max_distance) const {
    float surface_y = height(origin.x, origin.z);
    glm::vec3 surface_n = normal(origin.x, origin.z);
    Surface mat = material(origin.x, origin.z);
    bool prop = false;

    for (const StaticBox& b : boxes_) {
        if (origin.x < b.bounds.min.x || origin.x > b.bounds.max.x) continue;
        if (origin.z < b.bounds.min.z || origin.z > b.bounds.max.z) continue;
        // Started underneath the box: its top is not what we are standing on.
        if (origin.y < b.bounds.min.y) continue;
        // Buried in the hillside, or lower than what we already found.
        if (b.bounds.max.y <= surface_y) continue;

        surface_y = b.bounds.max.y;
        surface_n = glm::vec3{0.0f, 1.0f, 0.0f};
        mat = b.material;
        prop = true;
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
        float t = 0.0f;
        glm::vec3 n{0.0f};
        if (!ray_vs_box(b.bounds, origin, d, found ? best_t : max_distance, t,
                        n)) {
            continue;
        }
        if (found && t >= best_t) continue;
        best_t = t;
        best_n = n;
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
    return out;
}

}  // namespace apricot
