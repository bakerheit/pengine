#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/rng.h"

namespace apricot {

// Burning ground. One molotov lands, one patch catches, and the fire walks
// outward across the street until it runs out of fuel and goes out.
//
// A GRID, NOT FREE PARTICLES, and that is the decision the rest of this file
// hangs off. Spreading fire has two properties that a particle cloud cannot
// give you and a grid gives you for nothing:
//
//   * It is BOUNDED. A cell either burns or it does not, a cell can only ever
//     catch once, and the whole field is a fixed-size array. There is no input
//     — not a stack of molotovs, not a fire lit inside another fire — that
//     makes this allocate, grow without limit, or take longer per step.
//   * It is REPRODUCIBLE. Whether a neighbour catches is hash_coord3() of that
//     neighbour's own grid coordinates, so the same bottle thrown at the same
//     place burns the same shape every run, and a replayed tape burns the fire
//     it recorded. Nothing here pulls from a stream, because a stream's value
//     depends on how many times it has been pulled and the spread order is
//     exactly what a capacity limit takes away from you.
//
// NO CLOCK. Every timing below is seconds of simulation time arriving as `dt`,
// like the rest of the sim. See docs/architecture.md.
//
// The flames themselves are not here: this owns where the fire IS and how hot
// each patch is, and app/molotov_visual.* turns that into geometry. That split is
// the same one blood_particles.h makes, and for the same reason — a headless
// test can burn a fire out over twenty seconds with no GL context anywhere.

// What the world is like under a candidate cell. Plain data, filled by the
// caller from whatever it already knows about the ground, so fire needs
// neither the physics module nor the terrain module.
struct FireGround {
    // False means nothing supports a fire here: off the edge of the world,
    // over water, or a drop the flames have no business crossing.
    bool supported = false;
    float height_m = 0.0f;
};

struct FireCell {
    int32_t gx = 0, gz = 0;      // grid coordinates, cell-size units
    // The cell the ignition started from. Carried rather than recomputed so
    // the spread radius stays anchored to the bottle even after the fire has
    // walked several cells away from it.
    int32_t origin_gx = 0, origin_gz = 0;
    uint64_t fire_id = 0;        // which ignition this cell belongs to
    float ground_y = 0.0f;
    float age = 0.0f;
    float lifetime = 0.0f;
    bool live = false;
    bool spread_done = false;
};

// How one burning cell should look right now. Position is the cell centre on
// the ground; `heat` is its own 0..1 burn curve, which rises fast and falls
// slowly, because a fire takes hold quickly and dies down for a long time.
struct FireCellDraw {
    glm::vec3 position{0.0f};
    float heat = 0.0f;
    float age = 0.0f;
    uint64_t variation = 0;   // stable per cell, for decorrelating flicker
    bool visible = false;
};

class FireField {
public:
    // Sixty-four cells at 1.1 m is roughly a nine-metre circle of burning
    // street, which is about one bottle's worth of fuel spread over pavement.
    // The cap is what makes the cost of a fire knowable: past it the fire
    // stops spreading rather than stops being bounded.
    static constexpr std::size_t kCapacity = 64;
    static constexpr float kCellSizeM = 1.1f;

    // How far from the bottle the flames may walk. Fuel runs out; a molotov
    // that eventually reaches the far side of Pinatty is a bug, not a feature.
    static constexpr float kSpreadRadiusM = 5.0f;

    // A cell has to be properly alight before it can set anything else going.
    // Without this delay the whole radius ignites on the first step and the
    // fire never reads as spreading — it reads as a decal that appeared.
    static constexpr float kSpreadDelayS = 0.85f;

    // Per-cell burn, picked from the cell's own hash inside this band.
    static constexpr float kLifetimeMinS = 7.5f;
    static constexpr float kLifetimeMaxS = 12.5f;

    // Take-hold and die-down. Both are fractions of the cell's own lifetime,
    // so a short-lived cell is not stuck in a long fade.
    static constexpr float kRiseFraction = 0.12f;
    static constexpr float kFallFraction = 0.45f;

    // The flames have to be able to reach. A neighbour whose ground sits more
    // than this above or below is a kerb the fire climbs or a wall it does
    // not; without the test a street fire walks up the side of a building.
    static constexpr float kSpreadStepM = 0.9f;

    // Ignite at a world point. `fire_id` separates one bottle from the next —
    // it keys the spread hash, so two molotovs in the same street do not burn
    // identical shapes, and it identifies the cells the ignition owns.
    //
    // Returns false when the point is not finite or the field is full of
    // younger fire. A refused ignition changes nothing.
    bool ignite(glm::vec3 world, float ground_y, uint64_t fire_id) {
        if (!std::isfinite(world.x) || !std::isfinite(world.z) ||
            !std::isfinite(ground_y)) {
            return false;
        }
        const int32_t gx = cell_index(world.x);
        const int32_t gz = cell_index(world.z);
        // Lighting a fire inside a fire adds nothing and would double the
        // flame geometry standing in one place.
        if (find(gx, gz) != kNone) return false;
        const std::size_t slot = free_slot();
        if (slot == kNone) return false;
        light(slot, gx, gz, gx, gz, fire_id, ground_y);
        return true;
    }

    // Advance every burning cell and let the established ones spread.
    //
    // `ground(x, z, near_y)` returns a FireGround for a world point, with the
    // height of the burning cell doing the spreading so the caller can probe
    // from somewhere sensible rather than from the sky. It is called only for
    // candidate cells, at most four per established cell, once each. The
    // caller owns what "supported" means — see app/molotov_gameplay.cpp, which
    // refuses ground the collider will not stand a person on, and water.
    template <typename GroundFn>
    void step(float dt, GroundFn&& ground) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        for (auto& cell : cells_) {
            if (!cell.live) continue;
            cell.age += dt;
            if (cell.age >= cell.lifetime) cell.live = false;
        }
        // Spread is a SEPARATE PASS, after every live cell has aged. A cell
        // lit inside this pass starts at age zero and so fails the delay test
        // below however late in the array it lands — which is what stops a
        // fire from crossing its whole radius in a single step, and is why the
        // delay is a guard here rather than only a piece of pacing.
        for (std::size_t i = 0; i < kCapacity; ++i) {
            auto& cell = cells_[i];
            if (!cell.live || cell.spread_done || cell.age < kSpreadDelayS) continue;
            cell.spread_done = true;
            constexpr int32_t kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& step_xz : kSteps) {
                const int32_t nx = cell.gx + step_xz[0];
                const int32_t nz = cell.gz + step_xz[1];
                if (find(nx, nz) != kNone) continue;
                if (!within_radius(nx, nz, cell.origin_gx, cell.origin_gz)) continue;
                // One hash per candidate, keyed on the candidate's OWN
                // coordinates rather than on the cell doing the spreading, so
                // which patches catch is a property of the ground and not of
                // the order the flames happened to reach it.
                const uint64_t roll = hash_coord3(cell.fire_id ^ 0xF19E'5B0Cull,
                                                  nx, nz, 0);
                if (static_cast<float>(roll >> 40) / 16777215.0f > catch_chance(
                        nx, nz, cell.origin_gx, cell.origin_gz)) {
                    continue;
                }
                const std::size_t slot = free_slot();
                if (slot == kNone) return;  // full: the fire stops growing
                const FireGround under = ground(cell_centre(nx), cell_centre(nz),
                                                cell.ground_y);
                if (!under.supported) continue;
                if (std::fabs(under.height_m - cell.ground_y) > kSpreadStepM) continue;
                light(slot, nx, nz, cell.origin_gx, cell.origin_gz, cell.fire_id,
                      under.height_m);
            }
        }
    }

    void clear() { cells_ = {}; }

    bool burning() const {
        return std::any_of(cells_.begin(), cells_.end(),
                           [](const FireCell& c) { return c.live; });
    }

    std::size_t live_count() const {
        return static_cast<std::size_t>(std::count_if(cells_.begin(), cells_.end(),
            [](const FireCell& c) { return c.live; }));
    }

    const std::array<FireCell, kCapacity>& cells() const { return cells_; }

    FireCellDraw draw(std::size_t index) const {
        FireCellDraw out;
        if (index >= kCapacity) return out;
        const auto& cell = cells_[index];
        if (!cell.live) return out;
        out.position = {cell_centre(cell.gx), cell.ground_y, cell_centre(cell.gz)};
        out.heat = heat_of(cell);
        out.age = cell.age;
        out.variation = hash_coord3(cell.fire_id, cell.gx, cell.gz, 1);
        out.visible = true;
        return out;
    }

    // Where the fire sounds like it is, weighted by how hard each patch is
    // burning. A plain centroid drifts to the geometric middle of a fire that
    // has half burnt out, which is the quiet half.
    glm::vec3 centre() const {
        glm::vec3 sum{0.0f};
        float weight = 0.0f;
        for (const auto& cell : cells_) {
            if (!cell.live) continue;
            const float heat = heat_of(cell);
            sum += glm::vec3{cell_centre(cell.gx), cell.ground_y, cell_centre(cell.gz)} * heat;
            weight += heat;
        }
        return weight > 1e-4f ? sum / weight : glm::vec3{0.0f};
    }

    // Total burn, 0..1, for the loop's gain. Saturating rather than linear in
    // the cell count: a fire twice the size is not twice as loud, and a fire
    // that gets louder in proportion to its area drowns the street.
    float intensity() const {
        float heat = 0.0f;
        for (const auto& cell : cells_)
            if (cell.live) heat += heat_of(cell);
        return 1.0f - std::exp(-heat / 6.0f);
    }

    // How much fire is on a world point, 0..1, for anything standing in it.
    // Taken from the single hottest cell within reach rather than summed,
    // because standing between two patches is not twice as bad as standing in
    // one, and a summed field would kill the player for walking past a fire.
    float heat_at(glm::vec3 world) const {
        if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
            !std::isfinite(world.z)) {
            return 0.0f;
        }
        float best = 0.0f;
        for (const auto& cell : cells_) {
            if (!cell.live) continue;
            const glm::vec2 offset{world.x - cell_centre(cell.gx),
                                   world.z - cell_centre(cell.gz)};
            const float reach = kCellSizeM * 0.9f;
            const float distance = glm::length(offset);
            if (distance > reach) continue;
            // Flames are low. Standing on a balcony above a burning street is
            // not standing in it, and neither is being in a basement below.
            const float lift = world.y - cell.ground_y;
            if (lift < -0.6f || lift > 2.1f) continue;
            best = std::max(best, heat_of(cell) * (1.0f - distance / reach));
        }
        return best;
    }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    static int32_t cell_index(float world) {
        return static_cast<int32_t>(std::floor(world / kCellSizeM));
    }
    static float cell_centre(int32_t index) {
        return (static_cast<float>(index) + 0.5f) * kCellSizeM;
    }

    static float heat_of(const FireCell& cell) {
        if (cell.lifetime <= 0.0f) return 0.0f;
        const float life = std::clamp(cell.age / cell.lifetime, 0.0f, 1.0f);
        if (life < kRiseFraction) return life / kRiseFraction;
        if (life > 1.0f - kFallFraction) return (1.0f - life) / kFallFraction;
        return 1.0f;
    }

    // Thinner the further from the bottle, so the fire has a soft edge rather
    // than a hard disc of flame that stops dead at the radius.
    static float catch_chance(int32_t gx, int32_t gz,
                              int32_t origin_gx, int32_t origin_gz) {
        const float dx = static_cast<float>(gx - origin_gx) * kCellSizeM;
        const float dz = static_cast<float>(gz - origin_gz) * kCellSizeM;
        const float reach = std::sqrt(dx * dx + dz * dz) / kSpreadRadiusM;
        return std::clamp(0.95f - 0.75f * reach, 0.0f, 1.0f);
    }

    static bool within_radius(int32_t gx, int32_t gz,
                              int32_t origin_gx, int32_t origin_gz) {
        const float dx = static_cast<float>(gx - origin_gx) * kCellSizeM;
        const float dz = static_cast<float>(gz - origin_gz) * kCellSizeM;
        return dx * dx + dz * dz <= kSpreadRadiusM * kSpreadRadiusM;
    }

    std::size_t find(int32_t gx, int32_t gz) const {
        for (std::size_t i = 0; i < kCapacity; ++i)
            if (cells_[i].live && cells_[i].gx == gx && cells_[i].gz == gz) return i;
        return kNone;
    }

    // The first dead slot, never a live one. A fire that recycled a burning
    // cell to make room would put out a flame the player is looking at.
    std::size_t free_slot() const {
        for (std::size_t i = 0; i < kCapacity; ++i)
            if (!cells_[i].live) return i;
        return kNone;
    }

    void light(std::size_t slot, int32_t gx, int32_t gz, int32_t origin_gx,
               int32_t origin_gz, uint64_t fire_id, float ground_y) {
        auto& cell = cells_[slot];
        cell.gx = gx;
        cell.gz = gz;
        cell.origin_gx = origin_gx;
        cell.origin_gz = origin_gz;
        cell.fire_id = fire_id;
        cell.ground_y = ground_y;
        cell.age = 0.0f;
        const uint64_t roll = hash_coord3(fire_id ^ 0x0F17E'11ull, gx, gz, 2);
        cell.lifetime = kLifetimeMinS + (kLifetimeMaxS - kLifetimeMinS) *
            static_cast<float>(roll >> 40) / 16777215.0f;
        cell.live = true;
        cell.spread_done = false;
    }

    std::array<FireCell, kCapacity> cells_{};
};

}  // namespace apricot
