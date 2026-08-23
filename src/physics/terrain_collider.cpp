#include "physics/terrain_collider.h"

#include "terrain/chunk.h"  // mesh_height_at / mesh_normal_at

#include <algorithm>
#include <cmath>

#include "core/rng.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

// --- the mesh lattice --------------------------------------------------------
// One cell of the lattice the mesher walks. Index space matches
// terrain/chunk.cpp exactly: i runs along +X, j along +Z, and the world
// coordinate of a lattice point is (index * kTerrainVertexMetres) — the same
// absolute world value both neighbouring chunks evaluate, which is why their
// shared edge closes and why we can reproduce it without knowing which chunk
// we are in.
// Barycentric weights of (u, v) on whichever of the cell's two triangles
// contains it, in the mesher's vertex order a=(0,0) b=(1,0) c=(0,1) d=(1,1).
//
// terrain/chunk.cpp emits {a, c, b} then {b, c, d}: the shared edge is the
// ANTI-diagonal b--c, so u + v <= 1 is the first triangle and u + v >= 1 the
// second. Get that backwards and the surface is subtly wrong on exactly half
// of every cell — a bug that looks like noise, not like a mistake.
// --- surface classification --------------------------------------------------
// TERRAIN DOES NOT OWN MATERIALS YET. src/terrain/ generates shape and nothing
// else, so the tyre model has nothing to ask. This classifier lives here so
// grip is driven by something real rather than a constant, and it is written
// against PHYSICAL rules — angle of repose, sediment settling low — rather than
// tuned to the current placeholder height field, so replacing that field with
// real ridged terrain does not turn the whole world to rock.
//
// The moment src/terrain/ ships a material or biome field, DELETE this and call
// through to it. Two sources of truth for what the ground is made of will drift
// from what the renderer paints, and the symptom is "that gravel section grips
// like tarmac".

// Broad value noise on its own lattice, so materials come in stage-sized
// patches instead of per-metre confetti.
constexpr float kPatchMetres = 140.0f;
constexpr uint32_t kPatchChannel = 7u;

float patch_lattice(uint64_t seed, int32_t ix, int32_t iz) {
    return static_cast<float>(hash_coord3(seed, ix, iz, kPatchChannel) >> 40) *
           (1.0f / 16777216.0f);
}

float smooth_step(float t) { return t * t * (3.0f - 2.0f * t); }

float patch_noise(uint64_t seed, float x, float z) {
    const float sx = x / kPatchMetres;
    const float sz = z / kPatchMetres;
    const float fx = std::floor(sx);
    const float fz = std::floor(sz);
    const int32_t ix = static_cast<int32_t>(fx);
    const int32_t iz = static_cast<int32_t>(fz);

    const float tx = smooth_step(sx - fx);
    const float tz = smooth_step(sz - fz);

    const float n00 = patch_lattice(seed, ix, iz);
    const float n10 = patch_lattice(seed, ix + 1, iz);
    const float n01 = patch_lattice(seed, ix, iz + 1);
    const float n11 = patch_lattice(seed, ix + 1, iz + 1);

    const float a = n00 + (n10 - n00) * tx;
    const float b = n01 + (n11 - n01) * tx;
    return a + (b - a) * tz;
}

// Cosine of the angle of repose. Loose material does not sit on a face steeper
// than roughly 32 degrees; above that you are driving on what is left, which is
// rock. This is why the threshold is a physical constant and not a feel dial.
constexpr float kReposeCos = 0.848f;  // cos(32 deg)

// Below this fraction of the height field's amplitude counts as valley floor,
// where washed-out sediment collects.
constexpr float kSandFraction = -0.18f;

SurfaceMaterial classify_surface(uint64_t seed, float x, float z) {
    const glm::vec3 n = normal_at(seed, x, z);
    if (n.y < kReposeCos) return SurfaceMaterial::kRock;

    if (height_at(seed, x, z) < kHeightMetres * kSandFraction) {
        return SurfaceMaterial::kSand;
    }

    return patch_noise(seed, x, z) > 0.55f ? SurfaceMaterial::kGravel
                                           : SurfaceMaterial::kGrass;
}

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

void TerrainCollider::add_static_box(const AABB& bounds,
                                     SurfaceMaterial material) {
    // An inverted (never-expanded) AABB passes every containment test it is
    // given and would become a prop covering the entire world.
    if (!bounds.valid()) return;
    boxes_.push_back(StaticBox{bounds, material});
}

void TerrainCollider::clear_static_boxes() { boxes_.clear(); }

// --- materials ---------------------------------------------------------------

void TerrainCollider::paint_surface(const AABB& region,
                                    SurfaceMaterial material) {
    if (!region.valid()) return;
    paint_.push_back(SurfacePaint{region, material});
}

void TerrainCollider::clear_surface_paint() { paint_.clear(); }

SurfaceMaterial TerrainCollider::material(float x, float z) const {
    // Reverse order: the last paint laid down wins, so a small patch dropped
    // on top of a big one behaves the way anyone painting it would expect.
    for (std::size_t i = paint_.size(); i > 0u; --i) {
        const SurfacePaint& p = paint_[i - 1u];
        if (x >= p.region.min.x && x <= p.region.max.x && z >= p.region.min.z &&
            z <= p.region.max.z) {
            return p.material;
        }
    }
    return classify_surface(seed_, x, z);
}

float TerrainCollider::grip(float x, float z) const {
    return surface_grip(material(x, z), wetness_);
}

// --- probes ------------------------------------------------------------------

TerrainCollider::GroundHit TerrainCollider::probe_down(
    glm::vec3 origin, float max_distance) const {
    float surface_y = height(origin.x, origin.z);
    glm::vec3 surface_n = normal(origin.x, origin.z);
    SurfaceMaterial mat = material(origin.x, origin.z);
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
    SurfaceMaterial best_mat = SurfaceMaterial::kRock;

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
