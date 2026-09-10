// Halberd Field — is it a place, and can you get into it?
//
// The layout checks are cheap and they are the ones that catch an author's
// mistake: two buildings in the same cubic metre, a magazine outside the wire,
// a second hole in the perimeter that nobody meant to leave. None of them need
// the terrain.
//
// The last two do. "Gated, but you can drive in" is the whole design of the
// place, and the only way to know whether it is true is to put a REAL
// VehicleState on the REAL Yard Road and drive it through the gate onto the
// apron against a real TerrainCollider carrying the station's own ground. A
// consumer test with a hand-built collider would pass while the runway floated
// a metre over the plate.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/map.h"
#include "city/north_airbase.h"
#include "city/roads.h"
#include "city/spines.h"
#include "city/terrain_ops.h"
#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/road_graph.h"
#include "road/ribbon.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec2 world_of(float x, float z) {
    const auto& s = city::kHalberdFieldSite;
    return {s.origin.x + s.cos_yaw * x + s.sin_yaw * z,
            s.origin.z - s.sin_yaw * x + s.cos_yaw * z};
}

struct Footprint {
    const char* name;
    float x0, x1, z0, z1, y0, y1;
};

Footprint footprint_of(const city::StartPart& p) {
    return {p.name, p.centre.x - p.width_m * .5f, p.centre.x + p.width_m * .5f,
            p.centre.z - p.depth_m * .5f, p.centre.z + p.depth_m * .5f,
            p.bottom_m, p.bottom_m + p.height_m};
}

// --- Layout -----------------------------------------------------------------

void layout() {
    const auto parts = city::bake_halberd_field();
    REQUIRE(parts.size() > 500u);

    std::vector<Footprint> solids;
    for (const auto& p : parts) {
        // The wire is a line of posts that deliberately abuts nothing, and a
        // rotated piece has no axis-aligned footprint to compare.
        if (!p.solid || p.yaw_deg != 0.f) continue;
        if (std::strstr(p.name, "halberd fence")) continue;
        solids.push_back(footprint_of(p));
    }
    int overlaps = 0;
    for (std::size_t i = 0; i < solids.size(); ++i)
        for (std::size_t j = i + 1; j < solids.size(); ++j) {
            const auto& a = solids[i];
            const auto& b = solids[j];
            // A row of like parts - blast walls, shed pillars - may abut.
            if (std::strcmp(a.name, b.name) == 0) continue;
            const float ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
            const float oz = std::min(a.z1, b.z1) - std::max(a.z0, b.z0);
            const float oy = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
            if (ox > .5f && oz > .5f && oy > .3f) ++overlaps;
        }
    REQUIRE_MSG(overlaps == 0, "no two structures occupy the same space", "layout");

    // Everything is inside the wire. A magazine outside the perimeter is not a
    // magazine, it is a shed by the road.
    for (const auto& p : parts) {
        const auto f = footprint_of(p);
        REQUIRE_MSG(f.x0 >= -city::kHalberdFenceHalfX - 1.f &&
                        f.x1 <= city::kHalberdFenceHalfX + 1.f &&
                        f.z0 >= city::kHalberdFenceNorth - 1.f &&
                        f.z1 <= city::kHalberdFenceSouth + 1.f,
                    "every part of the station is inside the perimeter", p.name);
        REQUIRE(std::isfinite(p.bottom_m) && std::isfinite(p.height_m));
    }
    apricot_test::pass("the station is laid out without collisions and inside its own wire");
}

// One way in. The south fence must be continuous except for a single opening,
// and that opening must be where road 244 arrives.
void one_gate() {
    const auto parts = city::bake_halberd_field();
    const auto covered = [&](float x, float z, bool along_x) {
        for (const auto& p : parts) {
            if (std::strcmp(p.name, "halberd fence mesh") != 0) continue;
            const auto f = footprint_of(p);
            if (x >= f.x0 - .2f && x <= f.x1 + .2f && z >= f.z0 - .2f && z <= f.z1 + .2f)
                return true;
        }
        (void)along_x;
        return false;
    };
    // Walk all four sides. Only the south run may have a hole.
    int gaps_north = 0, gaps_east = 0, gaps_west = 0;
    for (float x = -city::kHalberdFenceHalfX + 2.f; x < city::kHalberdFenceHalfX - 2.f; x += 4.f) {
        if (!covered(x, city::kHalberdFenceNorth, true)) ++gaps_north;
    }
    for (float z = city::kHalberdFenceNorth + 2.f; z < city::kHalberdFenceSouth - 2.f; z += 4.f) {
        if (!covered(city::kHalberdFenceHalfX, z, false)) ++gaps_east;
        if (!covered(-city::kHalberdFenceHalfX, z, false)) ++gaps_west;
    }
    REQUIRE_MSG(gaps_north == 0 && gaps_east == 0 && gaps_west == 0,
                "three sides of the perimeter are unbroken", "perimeter");

    float first_gap = 0, last_gap = 0;
    int gap_samples = 0;
    for (float x = -city::kHalberdFenceHalfX + 2.f; x < city::kHalberdFenceHalfX - 2.f; x += 2.f) {
        if (covered(x, city::kHalberdFenceSouth, true)) continue;
        if (gap_samples == 0) first_gap = x;
        last_gap = x;
        ++gap_samples;
    }
    REQUIRE_MSG(gap_samples > 0, "the south fence has an opening", "gate");
    // One contiguous hole, wide enough to drive through and no wider than a
    // gate. Two separate holes would show up as a span far wider than the gate.
    REQUIRE_MSG(last_gap - first_gap < 42.f, "the opening is a single gate, not two holes", "gate");
    const float centre = (first_gap + last_gap) * .5f;
    REQUIRE_NEAR(centre, city::kHalberdGateX, 12.f);
    apricot_test::pass("the wire has exactly one opening and it is the main gate");
}

// The runway carries ground collision and appears in no road table. Traffic
// that routes down a runway is a bug with a very good disguise.
void runway_is_not_a_road() {
    const auto parts = city::bake_halberd_field();
    int runway = 0, drivable = 0;
    for (const auto& p : parts) {
        if (std::strcmp(p.name, "halberd runway") == 0) {
            ++runway;
            REQUIRE_NEAR(p.width_m, city::kHalberdRunwayHalfLength * 2.f, 1.f);
            REQUIRE_NEAR(p.depth_m, city::kHalberdRunwayHalfWidth * 2.f, 1.f);
        }
        if (city::halberd_ground_piece(p)) ++drivable;
    }
    REQUIRE(runway == 1);
    REQUIRE(drivable >= 12);

    const auto centre = world_of(0, city::kHalberdRunwayZ);
    for (const auto& road : city::kRoads)
        for (int i = 0; i < road.count; ++i) {
            const float dx = road.path[i].x - centre.x;
            const float dz = road.path[i].z - centre.y;
            REQUIRE_MSG(std::sqrt(dx * dx + dz * dz) > 60.f,
                        "no authored road runs down the runway", road.name);
        }
    apricot_test::pass("the runway is paving with collision and is in no road table");
}

// The plate has to be flat at EVERY level of detail, not just the one the
// player is standing on. A runway that ripples at level 3 is a runway that
// ripples as soon as you look at it from the Kepler road.
void plate_is_flat() {
    const auto& s = city::kHalberdFieldSite;
    for (int lod = 0; lod < 4; ++lod) {
        float lo = 1e9f, hi = -1e9f;
        for (float x = -city::kHalberdFenceHalfX; x <= city::kHalberdFenceHalfX; x += 12.f)
            for (float z = city::kHalberdFenceNorth; z <= city::kHalberdFenceSouth; z += 12.f) {
                const auto p = world_of(x, z);
                const float y = mesh_height_at_lod(city::kMapSeed, p.x, p.y, lod);
                lo = std::min(lo, y);
                hi = std::max(hi, y);
            }
        REQUIRE_MSG(hi - lo < .25f, "the airfield plate is flat at this level of detail",
                    "plate");
        REQUIRE_NEAR(lo, s.ground_m, .25f);
    }
    // And no part of it is in the sea.
    for (float x = -city::kHalberdFenceHalfX; x <= city::kHalberdFenceHalfX; x += 20.f) {
        const auto p = world_of(x, city::kHalberdFenceNorth);
        REQUIRE_MSG(mesh_height_at(city::kMapSeed, p.x, p.y) > 4.f,
                    "the north fence stands on dry land", "shore");
    }
    apricot_test::pass("the airfield plate is flat at every level of detail and clear of the water");
}

// Two surfaces for one piece of ground is the failure this station shipped
// with: an internal road ran down the apron, and because a ribbon is draped and
// CROWNED it rides about 12 cm above the flat bed it was authored on, while
// paving is a plane. The pair swapped depending on where the camera stood and
// read in game as two road surfaces sliding over each other.
//
// The invariant is not "the heights are close". It is that no ribbon vertex
// lies inside a paving plane at all. Sharing an edge is fine and is how the
// gate works; sharing an area is not.
void no_ribbon_under_the_paving(const RibbonBake& ribbon) {
    const auto& site = city::kHalberdFieldSite;
    int inside = 0;
    const char* worst = "";
    for (const auto& p : city::bake_halberd_field()) {
        if (!city::halberd_ground_piece(p)) continue;
        const float x0 = p.centre.x - p.width_m * .5f + .25f;
        const float x1 = p.centre.x + p.width_m * .5f - .25f;
        const float z0 = p.centre.z - p.depth_m * .5f + .25f;
        const float z1 = p.centre.z + p.depth_m * .5f - .25f;
        for (const auto& mesh : ribbon.layers)
            for (const auto& v : mesh.vertices) {
                const float lx = v.position.x - site.origin.x;
                const float lz = v.position.z - site.origin.z;
                if (lx > x0 && lx < x1 && lz > z0 && lz < z1) { ++inside; worst = p.name; }
            }
    }
    REQUIRE_MSG(inside == 0, "no road ribbon runs underneath the station's paving",
                inside ? worst : "paving");
    apricot_test::pass("no road ribbon and no paving plane describe the same ground");
}

// The same failure between the station's own planes. An airfield's surfaces do
// lie on top of each other - a taxiway runs out across an apron, a revetment's
// hardstanding is poured on the apron it opens onto - so the fix is not "never
// overlap", it is "declare which layer you are on". Two planes that share a
// layer and overlap are two quads fighting for the depth buffer.
//
// Like-named parts are exempt: the seven-segment runway numerals share corners
// by construction, and identical white on identical white cannot show a seam.
void paving_layers_do_not_fight() {
    std::vector<city::StartPart> planes;
    for (const auto& p : city::bake_halberd_field())
        if (p.height_m < .02f && !p.solid) planes.push_back(p);
    int fighting = 0;
    const char* first = "";
    const char* second = "";
    for (std::size_t i = 0; i < planes.size(); ++i)
        for (std::size_t j = i + 1; j < planes.size(); ++j) {
            const auto& a = planes[i];
            const auto& b = planes[j];
            if (std::fabs(a.bottom_m - b.bottom_m) > 1e-4f) continue;
            if (std::strcmp(a.name, b.name) == 0) continue;
            const float ox = std::min(a.centre.x + a.width_m * .5f, b.centre.x + b.width_m * .5f) -
                             std::max(a.centre.x - a.width_m * .5f, b.centre.x - b.width_m * .5f);
            const float oz = std::min(a.centre.z + a.depth_m * .5f, b.centre.z + b.depth_m * .5f) -
                             std::max(a.centre.z - a.depth_m * .5f, b.centre.z - b.depth_m * .5f);
            // A metre of tolerance: two painted lines may legitimately cross.
            if (ox > 1.f && oz > 1.f) { ++fighting; first = a.name; second = b.name; }
        }
    if (fighting) printf("    %s overlaps %s in the same layer\n", first, second);
    REQUIRE_MSG(fighting == 0, "no two paving planes share a layer and an area", "layers");
    apricot_test::pass("the station's paving is layered, and no layer fights itself");
}

// --- Driving ----------------------------------------------------------------

struct Fixture {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    RibbonBake ribbon;
    TerrainCollider collider{city::kMapSeed};
    Fixture() {
        roads.build(city::map_spines(), {}, ground.sampler());
        ribbon = bake_ribbons(roads, ground.sampler());
        collider.set_road_collision(build_road_collision(ribbon));
        // The same two rules world.cpp applies: solids become boxes, named
        // paving becomes supporting ground.
        const auto& site = city::kHalberdFieldSite;
        const float site_yaw = std::atan2(site.sin_yaw, site.cos_yaw);
        for (const auto& p : city::bake_halberd_field()) {
            const auto flat = world_of(p.centre.x, p.centre.z);
            const glm::vec3 centre{flat.x, site.ground_m + p.bottom_m + p.height_m * .5f, flat.y};
            const float yaw = site_yaw + glm::radians(p.yaw_deg);
            if (p.solid && p.pitch_deg == 0 && p.roll_deg == 0)
                collider.add_static_oriented_box(
                    centre, {p.width_m * .5f, p.height_m * .5f, p.depth_m * .5f}, yaw);
            if (city::halberd_ground_piece(p))
                collider.add_static_ground_rect({centre.x, centre.z},
                    site.ground_m + p.bottom_m + p.height_m,
                    {p.width_m * .5f, p.depth_m * .5f}, yaw, Surface::Rock);
        }
    }
};

struct Drive {
    bool arrived = false;
    float worst_below_ground_m = 0.0f;
    float driven_m = 0.0f;
};

// Steer a real car along a polyline with the same proportional controller
// city_roads_tests uses, and report how far under the ground it ever got.
Drive drive_route(const TerrainCollider& collider, const std::vector<glm::vec2>& line,
                  float target_speed_mps, int max_steps) {
    Drive d;
    VehicleTuning tuning;
    tuning.service_brake_grip_boost = 1.0f;
    const glm::vec2 d0 = glm::normalize(line[1] - line[0]);
    VehicleState car = spawn_vehicle(tuning, collider, line[0].x, line[0].y,
                                     std::atan2(-d0.x, -d0.y));
    std::size_t cursor = 0;
    glm::vec3 last = car.position;
    for (int step = 0; step < max_steps; ++step) {
        const glm::vec2 here{car.position.x, car.position.z};
        if (glm::length(line.back() - here) < 18.0f) { d.arrived = true; break; }
        // Waypoint to waypoint. The station's internal route turns tight
        // corners between hangars, so aiming at a point projected past the
        // corner - which is right on an open road - drives into the building.
        while (cursor + 2 < line.size() && glm::length(line[cursor + 1] - here) < 14.0f)
            ++cursor;
        const float speed = glm::length(glm::vec2{car.velocity.x, car.velocity.z});
        const glm::vec2 target = line[cursor + 1];
        const glm::vec3 fwd = vehicle_forward(car);
        const glm::vec2 f2 = glm::normalize(glm::vec2{fwd.x, fwd.z});
        glm::vec2 tt = target - here;
        tt = glm::length(tt) < 0.01f ? f2 : glm::normalize(tt);
        const float alpha = std::atan2(f2.x * tt.y - f2.y * tt.x, glm::dot(f2, tt));
        InputFrame in;
        in.steer = std::max(-1.0f, std::min(1.0f, alpha * 1.8f));
        const float want = target_speed_mps *
                           (1.0f - 0.6f * std::min(1.0f, std::fabs(alpha) * 1.7f));
        if (speed < want) in.throttle = 1.0f;
        else if (speed > want * 1.1f) in.brake = 0.5f;
        car = step_vehicle(car, tuning, in, collider, static_cast<float>(kSimDt));
        d.driven_m += glm::length(glm::vec2{car.position.x - last.x, car.position.z - last.z});
        last = car.position;
        d.worst_below_ground_m = std::max(
            d.worst_below_ground_m,
            collider.height(car.position.x, car.position.z) - car.position.y);
    }
    return d;
}

// The design in one test: you can drive in off the Yard Road, and once you are
// in you can use the place.
void drive_in_through_the_gate(Fixture& f) {
    // Off the Yard Road, through the gate, down to the station road, west past
    // the tower and up between two hangars onto the apron. Every leg is on
    // authored paving, which is the point of driving it rather than asserting
    // that the paving exists.
    std::vector<glm::vec2> route{{-500.f, -1918.f}};
    for (const auto local : {city::Vec2{190, 100}, city::Vec2{190, 68},
                             city::Vec2{0, 68}, city::Vec2{-175, 68},
                             city::Vec2{-175, 20}, city::Vec2{-320, 20}}) {
        const auto p = world_of(local.x, local.z);
        route.push_back({p.x, p.y});
    }
    const Drive d = drive_route(f.collider, route, 14.0f, 11000);
    printf("    gate run: %s, drove %.0f m, sank %.2f m\n",
           d.arrived ? "ARRIVED" : "STOPPED", static_cast<double>(d.driven_m),
           static_cast<double>(d.worst_below_ground_m));
    REQUIRE_MSG(d.arrived, "a real car drives off the Yard Road, through the gate and onto the apron",
                "gate run");
    REQUIRE_MSG(d.worst_below_ground_m < 0.35f, "the station's paving holds the car up",
                "gate run");
    apricot_test::pass("a real car drives in off the Yard Road and reaches the apron");
}

// The runway is the longest straight on the island and the reason to come here.
void drive_the_runway(Fixture& f) {
    std::vector<glm::vec2> route;
    for (float x = -430.f; x <= 430.f; x += 86.f) {
        const auto p = world_of(x, city::kHalberdRunwayZ);
        route.push_back({p.x, p.y});
    }
    const Drive d = drive_route(f.collider, route, 40.0f, 6000);
    printf("    runway run: %s, drove %.0f m, sank %.2f m\n",
           d.arrived ? "ARRIVED" : "STOPPED", static_cast<double>(d.driven_m),
           static_cast<double>(d.worst_below_ground_m));
    REQUIRE_MSG(d.arrived, "a real car drives the length of the runway", "runway run");
    REQUIRE_MSG(d.worst_below_ground_m < 0.35f, "the runway holds the car up", "runway run");
    REQUIRE(d.driven_m > 700.f);
    apricot_test::pass("a real car drives the full length of runway 09/27");
}

}  // namespace

int main() {
    layout();
    one_gate();
    runway_is_not_a_road();
    plate_is_flat();
    paving_layers_do_not_fight();
    Fixture f;
    no_ribbon_under_the_paving(f.ribbon);
    drive_in_through_the_gate(f);
    drive_the_runway(f);
    return apricot_test::done("north_airbase_tests");
}
