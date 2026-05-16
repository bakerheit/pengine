#include "physics/world_collision.h"

#include <algorithm>
#include <cmath>

#include "world/heightmap.h"

namespace pengine {

void WorldCollision::add_cell(CellCoord coord, std::vector<AABB> aabbs) {
    std::lock_guard<std::mutex> lk(mu_);
    cells_[coord] = Cell{std::move(aabbs)};
}

void WorldCollision::remove_cell(CellCoord coord) {
    std::lock_guard<std::mutex> lk(mu_);
    cells_.erase(coord);
}

void WorldCollision::add_building(CellCoord coord, const AABB& b) {
    std::lock_guard<std::mutex> lk(mu_);
    cells_[coord].buildings.push_back(b);
}

bool WorldCollision::remove_building(CellCoord coord, const AABB& b) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = cells_.find(coord);
    if (it == cells_.end()) return false;
    auto& v = it->second.buildings;
    // Exact AABB equality. We keep the first match — duplicates of the
    // exact same AABB are not expected (every placement carries a unique
    // position even before scale/rot), and a sloppy match risks deleting
    // a neighbour by accident.
    for (auto bi = v.begin(); bi != v.end(); ++bi) {
        if (bi->min == b.min && bi->max == b.max) {
            v.erase(bi);
            return true;
        }
    }
    return false;
}

void WorldCollision::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    cells_.clear();
}

int WorldCollision::building_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    int n = 0;
    for (const auto& kv : cells_) n += static_cast<int>(kv.second.buildings.size());
    return n;
}

// ----------------------------------------------------------------------------
// Ray-AABB (slab method). Returns true on hit and writes t in [0, max_t] plus
// the outward face normal.
// ----------------------------------------------------------------------------
static bool ray_aabb(const glm::vec3& o, const glm::vec3& d, const AABB& box,
                     float max_t, float& out_t, glm::vec3& out_n) {
    float tmin = 0.f, tmax = max_t;
    int   axis = -1;
    float sign = 0.f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-8f) {
            if (o[i] < box.min[i] || o[i] > box.max[i]) return false;
            continue;
        }
        float inv = 1.f / d[i];
        float t1  = (box.min[i] - o[i]) * inv;
        float t2  = (box.max[i] - o[i]) * inv;
        float s   = -1.f;
        if (t1 > t2) { std::swap(t1, t2); s = 1.f; }
        if (t1 > tmin) { tmin = t1; axis = i; sign = s; }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (axis < 0) return false;
    out_t = tmin;
    out_n = glm::vec3{0.f};
    out_n[axis] = sign;
    return true;
}

// ----------------------------------------------------------------------------
// Heightmap raycast: march along the ray in fixed steps; on first sample below
// terrain, bisect for a refined hit. Cheap and good enough for player-scale.
// ----------------------------------------------------------------------------
static bool ray_terrain(const glm::vec3& origin, const glm::vec3& dir,
                        float max_dist, float& out_t, glm::vec3& out_n) {
    constexpr float STEP = 0.5f;
    float prev_t = 0.f;
    float prev_diff = origin.y - Heightmap::sample(origin.x, origin.z);
    if (prev_diff < 0.f) {
        // already below terrain — clamp
        out_t = 0.f;
        out_n = Heightmap::normal(origin.x, origin.z);
        return true;
    }
    for (float t = STEP; t <= max_dist; t += STEP) {
        glm::vec3 p = origin + dir * t;
        float diff = p.y - Heightmap::sample(p.x, p.z);
        if (diff <= 0.f) {
            // bisect between prev_t and t
            float lo = prev_t, hi = t;
            for (int i = 0; i < 8; ++i) {
                float mid = 0.5f * (lo + hi);
                glm::vec3 pm = origin + dir * mid;
                float dm = pm.y - Heightmap::sample(pm.x, pm.z);
                if (dm > 0.f) lo = mid; else hi = mid;
            }
            out_t = hi;
            glm::vec3 hp = origin + dir * out_t;
            out_n = Heightmap::normal(hp.x, hp.z);
            return true;
        }
        prev_t    = t;
        prev_diff = diff;
    }
    return false;
}

RayHit WorldCollision::raycast(const glm::vec3& origin, const glm::vec3& dir_in,
                                float max_dist) const {
    glm::vec3 dir = glm::normalize(dir_in);

    RayHit hit;
    float  best_t = max_dist;

    // Terrain.
    {
        float t; glm::vec3 n;
        if (ray_terrain(origin, dir, max_dist, t, n) && t < best_t) {
            best_t       = t;
            hit.hit      = true;
            hit.t        = t;
            hit.position = origin + dir * t;
            hit.normal   = n;
        }
    }

    // Buildings.
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& kv : cells_) {
            for (const AABB& b : kv.second.buildings) {
                float t; glm::vec3 n;
                if (ray_aabb(origin, dir, b, best_t, t, n) && t < best_t) {
                    best_t       = t;
                    hit.hit      = true;
                    hit.t        = t;
                    hit.position = origin + dir * t;
                    hit.normal   = n;
                }
            }
        }
    }

    return hit;
}

// ----------------------------------------------------------------------------
// Cylinder vs AABB resolution (XZ plane only). Pushes the cylinder centre out
// along the shortest axis through the building face it overlaps.
// ----------------------------------------------------------------------------
glm::vec2 WorldCollision::resolve_cylinder_xz(glm::vec2 pos_xz, float feet_y,
                                               float height, float radius) const {
    std::lock_guard<std::mutex> lk(mu_);
    const float head_y = feet_y + height;

    // Two passes catches most corner cases without needing a full simplex.
    for (int pass = 0; pass < 2; ++pass) {
        bool any = false;
        for (const auto& kv : cells_) {
            for (const AABB& b : kv.second.buildings) {
                // Vertical overlap test.
                if (head_y < b.min.y || feet_y > b.max.y) continue;

                // Closest point on AABB-XZ rect to the cylinder centre.
                float cx = std::max(b.min.x, std::min(pos_xz.x, b.max.x));
                float cz = std::max(b.min.z, std::min(pos_xz.y, b.max.z));
                float dx = pos_xz.x - cx;
                float dz = pos_xz.y - cz;
                float d2 = dx * dx + dz * dz;

                if (d2 < radius * radius) {
                    if (d2 > 1e-12f) {
                        float d   = std::sqrt(d2);
                        float push = (radius - d);
                        pos_xz.x += dx / d * push;
                        pos_xz.y += dz / d * push;
                    } else {
                        // Centre is *inside* the box; push out along the shortest face.
                        float dxmin = pos_xz.x - b.min.x;
                        float dxmax = b.max.x  - pos_xz.x;
                        float dzmin = pos_xz.y - b.min.z;
                        float dzmax = b.max.z  - pos_xz.y;
                        float m = std::min({dxmin, dxmax, dzmin, dzmax});
                        if      (m == dxmin) pos_xz.x  = b.min.x - radius;
                        else if (m == dxmax) pos_xz.x  = b.max.x + radius;
                        else if (m == dzmin) pos_xz.y  = b.min.z - radius;
                        else                 pos_xz.y  = b.max.z + radius;
                    }
                    any = true;
                }
            }
        }
        if (!any) break;
    }
    return pos_xz;
}

// ----------------------------------------------------------------------------
// OBB-vs-AABB resolution (XZ plane only). SAT against four candidate axes
// (the OBB's two axes and the AABB's world X/Z), push along the smallest
// overlap. Iterates a few times for corner stacks.
// ----------------------------------------------------------------------------
glm::vec2 WorldCollision::resolve_obb_xz(glm::vec2 center,
                                          glm::vec2 ax_x, glm::vec2 ax_z,
                                          glm::vec2 half_ext,
                                          float feet_y, float height) const {
    std::lock_guard<std::mutex> lk(mu_);
    const float head_y = feet_y + height;

    constexpr glm::vec2 AAX{1.f, 0.f};
    constexpr glm::vec2 AAZ{0.f, 1.f};
    const glm::vec2 axes[4] = {ax_x, ax_z, AAX, AAZ};

    for (int pass = 0; pass < 3; ++pass) {
        bool any = false;
        for (const auto& kv : cells_) {
            for (const AABB& b : kv.second.buildings) {
                if (head_y < b.min.y || feet_y > b.max.y) continue;

                glm::vec2 b_center{(b.min.x + b.max.x) * 0.5f,
                                    (b.min.z + b.max.z) * 0.5f};
                glm::vec2 b_half  {(b.max.x - b.min.x) * 0.5f,
                                    (b.max.z - b.min.z) * 0.5f};
                glm::vec2 d = center - b_center;

                float min_overlap = 1e9f;
                glm::vec2 mtv{1.f, 0.f};
                bool separated = false;
                for (int i = 0; i < 4; ++i) {
                    const glm::vec2& n = axes[i];
                    float r_obb  = std::abs(glm::dot(ax_x * half_ext.x, n))
                                 + std::abs(glm::dot(ax_z * half_ext.y, n));
                    float r_aabb = b_half.x * std::abs(n.x)
                                 + b_half.y * std::abs(n.y);
                    float dist   = std::abs(glm::dot(d, n));
                    float overlap = r_obb + r_aabb - dist;
                    if (overlap <= 0.f) { separated = true; break; }
                    if (overlap < min_overlap) {
                        min_overlap = overlap;
                        mtv = n;
                    }
                }
                if (separated) continue;
                if (glm::dot(d, mtv) < 0.f) mtv = -mtv;
                center += mtv * min_overlap;
                any = true;
            }
        }
        if (!any) break;
    }
    return center;
}

} // namespace pengine
