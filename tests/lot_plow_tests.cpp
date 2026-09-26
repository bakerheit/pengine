// Lot plow crews: planned on the real authored lots, driven through a real
// clearance field on the real map terrain, stopping for people.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app/plow_kit.h"
#include "app/vehicle_model_tuning.h"
#include "city/building_access.h"
#include "city/map.h"
#include "game/lot_plow.h"
#include "game/snow_clearance.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

// The crew's truck, from the same numbers the app hands it.
LotPlowTruckSpec grazer_spec() {
    const PlayerCarId id = PlayerCarId::RodeoGrazerPlow;
    const VehicleTuning tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
    return lot_plow_truck_spec(id, tuning);
}

std::vector<std::size_t> candidates(const std::vector<city::BuildingAccessLot>& lots) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < lots.size(); ++i)
        if (is_lot_plow_candidate(lots[i])) out.push_back(i);
    return out;
}

// Does a truck-and-blade corridor along a pass touch a solid part?
bool corridor_hits_solid(const city::BuildingAccessLot& lot, const LotPlowPass& p,
                         const LotPlowTruckSpec& spec) {
    const glm::vec2 dir = glm::normalize(p.end - p.start);
    const glm::vec2 right{-dir.y, dir.x};
    const float half = std::max(spec.half_width_m, spec.blade.half_width_m);
    const glm::vec2 tail = p.start - dir * (spec.blade.edge_forward_m + spec.rear_m);
    const float length = glm::length(p.end - tail);
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    for (const auto& part : lot.parts) {
        const float top = part.bottom_m + part.height_m;
        const bool struck = part.solid && part.height_m >= 0.12f && part.bottom_m <= 2.4f;
        const bool kerb = !part.solid && part.bottom_m <= 0.35f && top >= 0.15f &&
                          part.width_m * part.depth_m < 400.0f;
        if (!struck && !kerb) continue;
        if (part.name != nullptr && std::strcmp(part.name, lot.name) == 0) continue;
        // Sample the part's own rotated footprint in site space, to world.
        const float c = std::cos(part.yaw_deg * kDegToRad), s = std::sin(part.yaw_deg * kDegToRad);
        for (int i = 0; i <= 8; ++i) {
            for (int j = 0; j <= 8; ++j) {
                const float lx = (static_cast<float>(i) / 8.0f - 0.5f) * part.width_m;
                const float lz = (static_cast<float>(j) / 8.0f - 0.5f) * part.depth_m;
                const glm::vec2 site{part.centre.x + c * lx + s * lz, part.centre.z - s * lx + c * lz};
                const glm::vec2 w = lot_plow_detail::site_to_world(lot.site, site);
                const float along = glm::dot(w - tail, dir);
                const float side = glm::dot(w - tail, right);
                if (along >= 0.0f && along <= length && std::fabs(side) <= half) return true;
            }
        }
    }
    return false;
}

void passes_cross_real_lots_clear_of_everything_solid() {
    const auto lots = city::authored_building_access_lots();
    const auto spec = grazer_spec();
    std::size_t planned = 0;
    for (const std::size_t i : candidates(lots)) {
        const auto& lot = lots[i];
        const LotPlowPlan plan = plan_lot_plow(lot, i, spec, true);
        float length = 0.0f;
        for (const auto& p : plan.passes) {
            length += glm::length(p.end - p.start);
            REQUIRE_MSG(!corridor_hits_solid(lot, p, spec), "a planned pass runs through a solid part", lot.name);
            // Inside the pavement it was planned on.
            for (const glm::vec2 w : {p.start, p.end}) {
                const glm::vec2 d = w - glm::vec2{lot.site.origin.x, lot.site.origin.z};
                const glm::vec2 site{lot.site.cos_yaw * d.x - lot.site.sin_yaw * d.y,
                                     lot.site.sin_yaw * d.x + lot.site.cos_yaw * d.y};
                REQUIRE_MSG(std::fabs(site.x - lot.pavement.centre.x) <= 0.5f * lot.pavement.width_m &&
                            std::fabs(site.y - lot.pavement.centre.z) <= 0.5f * lot.pavement.depth_m,
                            "a pass leaves its lot", lot.name);
            }
        }
        std::printf("      %-18s %zu passes, %.0f m of blade run\n", lot.name, plan.passes.size(),
                    static_cast<double>(length));
        if (plan.passes.size() >= 2) ++planned;
        if (std::strcmp(lot.name, "gas lot") == 0) {
            // The pump islands are kerbs, not paint: no pass may cross one.
            for (const auto& part : lot.parts) {
                if (part.name == nullptr || std::strstr(part.name, "pump island") == nullptr) continue;
                REQUIRE(!part.solid);  // authored walkable, which is exactly the trap
            }
        }
    }
    REQUIRE(planned >= 4u);
    pass("lot passes run the real lots' pavement and never through a wall, island or pump");
}

struct Run {
    LotPlowCrew crew;
    SnowClearanceField field;
};

void run(Run& r, const TerrainCollider& ground, int steps, float depth,
         const std::vector<LotPlowObstacle>& obstacles = {}) {
    for (int s = 0; s < steps; ++s)
        r.crew.step(1.0f / 120.0f, depth, ground, obstacles, r.field);
}

void crews_clear_their_lots_and_sit_down() {
    const auto lots = city::authored_building_access_lots();
    const TerrainCollider ground(city::kMapSeed);
    Run r;
    r.crew.plan(lots, candidates(lots), 0xC0FFEEull, {grazer_spec()});
    REQUIRE(r.crew.trucks().size() == 3u);
    // No crews go out before there is snow worth pushing.
    run(r, ground, 240, 0.01f);
    REQUIRE(!r.crew.dispatched());
    const float depth = 0.12f;
    int steps = 0;
    for (; steps < 120 * 60 * 12; steps += 120) {
        run(r, ground, 120, depth);
        if (std::none_of(r.crew.trucks().begin(), r.crew.trucks().end(),
                         [](const LotPlowTruck& t) { return t.working(); })) break;
    }
    run(r, ground, 240, depth);  // time to drop the blade
    std::printf("      three lots done in %.0f s, %zu clearance strips\n",
                static_cast<double>(steps) / 120.0, r.field.strips().size());
    REQUIRE(steps < 120 * 60 * 12);
    // The city's field holds 128 strips for every plow in it; a square push
    // is one strip, so a lot costs about its pass count, not its turns.
    std::size_t passes = 0;
    for (const auto& truck : r.crew.trucks()) passes += truck.plan.passes.size();
    REQUIRE(r.field.strips().size() <= passes + passes / 2);
    for (const auto& truck : r.crew.trucks()) {
        REQUIRE(truck.phase == LotPlowTruck::Phase::Done && truck.pass + 1 == truck.plan.passes.size());
        // Every pass is cleared down its length, the whole blade width.
        for (const auto& p : truck.plan.passes) {
            const glm::vec2 dir = glm::normalize(p.end - p.start);
            const glm::vec2 right{-dir.y, dir.x};
            for (float t : {0.1f, 0.5f, 0.9f}) {
                for (float side : {-0.8f, 0.0f, 0.8f}) {
                    const glm::vec2 q = glm::mix(p.start, p.end, t) + right * (side * truck.spec.blade.half_width_m);
                    const float y = ground.height(q.x, q.y);
                    REQUIRE_MSG(r.field.depth_at(q.x, y, q.y, depth) < 0.01f, "a pass was left under snow",
                                truck.plan.lot);
                }
            }
        }
        // Parked with the blade down on the ground, not in mid-air.
        REQUIRE(truck.blade.scraping() && std::fabs(truck.speed_mps) < 0.01f);
    }
    pass("three crews push every pass of their lots clear, then drop their blades and sit");
}

void crews_are_deterministic() {
    const auto lots = city::authored_building_access_lots();
    const TerrainCollider ground(city::kMapSeed);
    Run a, b;
    a.crew.plan(lots, candidates(lots), 77u, {grazer_spec()});
    b.crew.plan(lots, candidates(lots), 77u, {grazer_spec()});
    run(a, ground, 120 * 40, 0.1f);
    run(b, ground, 120 * 40, 0.1f);
    REQUIRE(a.crew.trucks().size() == b.crew.trucks().size());
    for (std::size_t i = 0; i < a.crew.trucks().size(); ++i) {
        const auto& x = a.crew.trucks()[i];
        const auto& y = b.crew.trucks()[i];
        REQUIRE(x.plan.lot_index == y.plan.lot_index);
        REQUIRE(x.position == y.position && x.heading == y.heading && x.speed_mps == y.speed_mps);
        REQUIRE(x.pass == y.pass && x.phase == y.phase && x.blade.raised == y.blade.raised);
    }
    REQUIRE(a.field.strips().size() == b.field.strips().size());
    // And the seed is what picks the lots.
    std::size_t differs = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        Run c;
        c.crew.plan(lots, candidates(lots), seed, {grazer_spec()});
        if (c.crew.trucks().front().plan.lot_index != a.crew.trucks().front().plan.lot_index) ++differs;
    }
    REQUIRE(differs > 0u);
    pass("the same seed plans the same crews and steps them bit for bit; the seed picks the lots");
}

// A person standing in a lane: the truck stops short of them, never touches
// them, waits, and gives the pass up rather than push through.
void a_truck_stops_for_a_person_in_its_lane() {
    const auto lots = city::authored_building_access_lots();
    const TerrainCollider ground(city::kMapSeed);
    Run r;
    r.crew.plan(lots, candidates(lots), 0xC0FFEEull, {grazer_spec()});
    run(r, ground, 1, 0.12f);
    REQUIRE(r.crew.dispatched());
    const LotPlowTruck& truck = r.crew.trucks().front();
    const LotPlowPass first = truck.plan.passes.front();
    const glm::vec2 dir = glm::normalize(first.end - first.start);
    const LotPlowObstacle person{glm::mix(first.start, first.end, 0.5f), 0.35f};
    float closest = 1e9f;
    bool abandoned = false;
    for (int s = 0; s < 120 * 30; ++s) {
        run(r, ground, 1, 0.12f, {person});
        const glm::vec3 edge = truck.edge();
        closest = std::min(closest, glm::dot(person.xz - glm::vec2{edge.x, edge.z}, dir));
        if (truck.pass > 0) { abandoned = true; break; }
    }
    std::printf("      the blade stopped %.2f m short of the person\n", static_cast<double>(closest - person.radius_m));
    REQUIRE(closest - person.radius_m > 0.3f);
    REQUIRE(abandoned);
    pass("a truck stops short of a person in its lane and abandons the pass instead of pushing through");
}

void snow_going_sends_crews_home() {
    const auto lots = city::authored_building_access_lots();
    const TerrainCollider ground(city::kMapSeed);
    Run r;
    r.crew.plan(lots, candidates(lots), 5u, {grazer_spec()});
    run(r, ground, 120 * 10, 0.1f);
    REQUIRE(std::any_of(r.crew.trucks().begin(), r.crew.trucks().end(),
                        [](const LotPlowTruck& t) { return t.working(); }));
    run(r, ground, 120 * 3, 0.0f);
    for (const auto& truck : r.crew.trucks()) {
        REQUIRE(!truck.working());
        REQUIRE(std::fabs(truck.speed_mps) < 0.01f);
    }
    pass("when the snow is gone, crews stop and drop their blades");
}

}  // namespace

int main() {
    std::printf("lot_plow_tests\n");
    passes_cross_real_lots_clear_of_everything_solid();
    crews_clear_their_lots_and_sit_down();
    crews_are_deterministic();
    a_truck_stops_for_a_person_in_its_lane();
    snow_going_sends_crews_home();
    return apricot_test::done("lot_plow_tests");
}
