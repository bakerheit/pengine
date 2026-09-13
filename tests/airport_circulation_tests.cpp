#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/airport.h"
#include "city/airport_paving.h"
#include "city/florangia_airport.h"
#include "city/roads.h"
#include "app/vehicle_model_tuning.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {

TerrainCollider supported_airport(const city::StartSite& site,
                                  const std::vector<city::StartPart>& parts) {
    TerrainCollider ground{city::kMapSeed};
    for (const auto& part:parts) {
        if (!city::airport_ground_piece(part)) continue;
        const glm::vec2 centre{
            site.origin.x+site.cos_yaw*part.centre.x+site.sin_yaw*part.centre.z,
            site.origin.z-site.sin_yaw*part.centre.x+site.cos_yaw*part.centre.z};
        ground.add_static_ground_rect(centre,site.ground_m+part.bottom_m+part.height_m,
            {part.width_m*.5f,part.depth_m*.5f},
            std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(part.yaw_deg));
    }
    return ground;
}

void paving_supports_tires_and_feet_at_the_visible_top() {
    const auto parts=city::bake_airport();
    const auto ground=supported_airport(city::kAirportSite,parts);
    constexpr float top=city::kAirportSite.ground_m+city::kAirportPavingTopM;
    const auto hit=ground.probe_down({60,7,2195},2);
    REQUIRE(hit.hit);REQUIRE_NEAR(hit.point.y,top,1e-5f);
    // Apron, both taxiways, runway and hangar apron use the slab's top.
    for (const auto p:{glm::vec2{60,2195},{465,2100},{-150,2100},{150,2046},{465,2199}}) {
        const auto paved=ground.probe_down({p.x,7,p.y},2);
        REQUIRE(paved.hit);REQUIRE_NEAR(paved.point.y,top,1e-5f);
    }
    for (float z:{2018.f,2074.f}) {
        const auto shoulder=ground.probe_down({150,7,z},2);
        REQUIRE(shoulder.hit);REQUIRE_NEAR(shoulder.point.y,6.10f,1e-5f);
    }
    // Recover the exact screenshot location, including a player initially
    // planted on the old terrain level beneath the 13 cm visible slab.
    auto player=spawn_character(TerrainCollider{city::kMapSeed},57,2195);
    player=step_character(player,CharacterTuning{},InputFrame{},ground,1.f/120.f);
    REQUIRE_NEAR(player.position.y,top,1e-4f);
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::EmberGt);
    auto car=spawn_vehicle(tuning,ground,60,2195,-glm::half_pi<float>());
    float worst_sink=0.f;
    for (int step=0;step<720;++step) {
        InputFrame input;input.throttle=step<240?0.f:.35f;
        car=step_vehicle(car,tuning,input,ground,1.f/120.f);
        if (step<120) continue;
        for (std::size_t wheel=0;wheel<car.wheels.size();++wheel) {
            REQUIRE(car.wheels[wheel].grounded);
            REQUIRE_NEAR(car.wheels[wheel].contact_point.y,top,.0001f);
            const glm::vec3 local{(wheel%2==0?-1.f:1.f)*tuning.half_track,
                -tuning.com_height_above_mount-car.wheels[wheel].suspension_length,
                (wheel<2?-1.f:1.f)*tuning.half_wheelbase};
            const auto centre=car.position+car.orientation*local;
            const float bottom=centre.y-tuning.wheel_radius;
            worst_sink=std::max(worst_sink,top-bottom);
            REQUIRE(bottom>=top-.015f);
        }
    }
    REQUIRE(car.position.x>65.f);
    std::printf("airport contact: visible top %.3f m; feet %.3f m; worst tire sink %.4f m\n",
                top,player.position.y,worst_sink);
    apricot_test::pass("Ember tires and player feet meet the visible apron while parked and driving");
}

void airport_paving_roles_exclude_paint_and_leave_real_edges() {
    const auto pinatty=city::bake_airport();
    const auto florangia=city::bake_florangia_airport();
    for (const auto* parts:{&pinatty,&florangia}) {
        int surfaces=0;
        for (const auto& part:*parts) {
            if (city::airport_ground_piece(part)) {
                ++surfaces;
                REQUIRE(!part.solid);
                REQUIRE(part.bottom_m+part.height_m<=.31f);
            }
            if (part.finish==city::StartFinish::White || part.finish==city::StartFinish::Yellow)
                REQUIRE(!city::airport_ground_piece(part));
        }
        REQUIRE(surfaces>=10);
    }
    const auto ground=supported_airport(city::kAirportSite,pinatty);
    // The apron ends at local Z=63. No rectangular AABB or blanket lot lift
    // may leave a floating floor just beyond the real slab.
    const auto outside=ground.probe_down({60,7,2203.2f},2);
    REQUIRE(outside.hit);REQUIRE_NEAR(outside.point.y,ground.height(60,2203.2f),1e-5f);
    const auto regional=supported_airport(city::kFlorangiaAirportSite,florangia);
    const auto regional_hit=regional.probe_down({4730,8,4408},2);
    REQUIRE(regional_hit.hit);REQUIRE_NEAR(regional_hit.point.y,6.63f,1e-5f);
    apricot_test::pass("both airports support paved slabs without lifting paint, roofs or adjacent ground");
}

bool has(const city::StartPart& part, const char* text) {
    return std::strstr(part.name, text) != nullptr;
}

bool contains(const city::StartPart& part, glm::vec2 p, float margin = 0.0f) {
    const float angle = glm::radians(part.yaw_deg);
    const glm::vec2 d = p - glm::vec2{part.centre.x, part.centre.z};
    const glm::vec2 q{std::cos(angle) * d.x - std::sin(angle) * d.y,
                      std::sin(angle) * d.x + std::cos(angle) * d.y};
    return std::fabs(q.x) < part.width_m * 0.5f + margin &&
           std::fabs(q.y) < part.depth_m * 0.5f + margin;
}

const city::Road& road_named(const char* name) {
    for (const auto& road : city::kRoads)
        if (std::strcmp(road.name, name) == 0) return road;
    REQUIRE_MSG(false, "missing airport route", name);
    return city::kRoads[0];
}

glm::vec2 local(const city::RoadPoint& p) {
    return {p.x - city::kAirportSite.origin.x,
            p.z - city::kAirportSite.origin.z};
}

float distance_to_road(glm::vec2 p, const city::Road& road) {
    float nearest = 1e9f;
    for (int i = 1; i < road.count; ++i) {
        const glm::vec2 a = local(road.path[i - 1]);
        const glm::vec2 d = local(road.path[i]) - a;
        const float t = std::clamp(glm::dot(p - a, d) / glm::dot(d, d), 0.0f, 1.0f);
        nearest = std::min(nearest, glm::length(p - a - d * t));
    }
    return nearest;
}

void parking_road_has_no_islands_or_raised_paving() {
    const auto parts = city::bake_airport();
    const auto& road = road_named("Terminal Parking Access");
    for (int segment = 1; segment < road.count; ++segment) {
        const glm::vec2 a = local(road.path[segment - 1]);
        const glm::vec2 delta = local(road.path[segment]) - a;
        const glm::vec2 tangent = glm::normalize(delta);
        const glm::vec2 normal{-tangent.y, tangent.x};
        const int count = static_cast<int>(std::ceil(glm::length(delta) * 2.0f));
        for (int i = 0; i <= count; ++i) {
            for (const float lateral : {-3.9f, -2.0f, 0.0f, 2.0f, 3.9f}) {
                const glm::vec2 p = a + delta * (static_cast<float>(i) /
                                               static_cast<float>(count)) +
                                    normal * lateral;
                for (const auto& part : parts) {
                    if (part.bottom_m >= 2.0f) continue;
                    const bool raised_paving = has(part, "terminal parking") &&
                        (part.finish == city::StartFinish::Asphalt ||
                         part.finish == city::StartFinish::Concrete);
                    if (!part.solid && !raised_paving) continue;
                    REQUIRE_MSG(!contains(part, p),
                                "parking road is covered or obstructed", part.name);
                }
            }
        }
    }
    apricot_test::pass("both parking entrances and the full aisle stay clear");
}

void each_terminal_door_has_a_continuous_clear_parking_walk() {
    const auto parts = city::bake_airport();
    const auto& parking = road_named("Terminal Parking Access");
    const auto& loop = road_named("the Terminal Loop");
    for (const float x : {-110.0f, -35.0f, 40.0f}) {
        for (float z = 150.0f; z < 240.0f; z += 0.25f) {
            for (const float offset : {-0.9f, 0.0f, 0.9f}) {
                const glm::vec2 p{x + offset, z};
                bool supported = distance_to_road(p, parking) <= 4.001f ||
                                 distance_to_road(p, loop) <= 9.001f;
                for (const auto& part : parts) {
                    if (part.bottom_m < 0.2f && part.height_m <= 0.35f &&
                        (part.finish == city::StartFinish::Concrete ||
                         part.finish == city::StartFinish::Asphalt) &&
                        contains(part, p, 0.001f)) supported = true;
                    if (part.solid && part.bottom_m < 2.0f &&
                        part.bottom_m + part.height_m > 0.45f) {
                        REQUIRE_MSG(!contains(part, p, 0.35f),
                                    "door-to-parking walk is blocked", part.name);
                    }
                }
                REQUIRE_MSG(supported, "walking route ends in grass", "terminal parking");
            }
        }
    }
    apricot_test::pass("all three doors connect to both parking rows without obstacles");
}

void parking_markings_respect_walkways_and_paved_rows() {
    const auto parts = city::bake_airport();
    int crossings = 0;
    for (const auto& part : parts) {
        if (has(part, "terminal parking crossing stripe")) {
            ++crossings;
            REQUIRE(!part.solid);
            REQUIRE_NEAR(part.bottom_m + part.height_m,
                         DRAPE_EPS_M + city::kAirportCrosswalkLiftM, 1e-6);
        }
        if (std::strcmp(part.name, "terminal parking stripe") != 0) continue;
        for (const float x : {-110.0f, -35.0f, 40.0f})
            REQUIRE(std::fabs(part.centre.x - x) >= 6.0f);
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec2 end{part.centre.x,
                               part.centre.z + part.depth_m * 0.5f * side};
            bool on_paving = false;
            for (const auto& pad : parts)
                if (has(pad, "terminal parking") &&
                    pad.finish == city::StartFinish::Asphalt && contains(pad, end))
                    on_paving = true;
            REQUIRE(on_paving);
        }
    }
    REQUIRE(crossings == 15);
    apricot_test::pass("parking bays leave pedestrian space and painted crossings sit on roads");
}

void hotel_walk_and_rental_forecourt_clear_roads_and_buildings() {
    const auto parts = city::bake_airport();
    const auto& rental_road = road_named("Rental Row");
    const auto& hotel_road = road_named("Camber Gateway");
    for (const auto& part : parts) {
        const bool hotel_walk = has(part, "airport hotel pedestrian approach") ||
            has(part, "airport frontage pedestrian connector");
        const bool rental_forecourt = std::strcmp(part.name, "rental forecourt") == 0;
        if (!hotel_walk && !rental_forecourt) continue;
        const auto& road = hotel_walk ? hotel_road : rental_road;
        for (float x = -0.5f; x <= 0.5f; x += 0.1f) {
            for (float z = -0.5f; z <= 0.5f; z += 0.1f) {
                const glm::vec2 p{part.centre.x + x * part.width_m,
                                  part.centre.z + z * part.depth_m};
                REQUIRE(distance_to_road(p, road) > road.width_m * 0.5f);
                for (const auto& obstacle : parts) {
                    if (obstacle.solid && obstacle.bottom_m < 2.0f &&
                        obstacle.bottom_m + obstacle.height_m > 0.5f)
                        REQUIRE_MSG(!contains(obstacle, p),
                                    "landside footpath crosses a building", obstacle.name);
                }
            }
        }
    }
    // The private approach and public verge connector meet at the parcel
    // line and still form one continuous route to the frontage sidewalk.
    for (float z = 189.0f; z <= 241.0f; z += 0.25f) {
        bool supported = false;
        for (const auto& part : parts) {
            if ((has(part, "airport hotel pedestrian approach") ||
                 has(part, "airport frontage pedestrian connector")) &&
                contains(part, {-324.0f, z}, 0.001f)) supported = true;
        }
        REQUIRE_MSG(supported, "hotel walk has a gap at the parcel edge", "frontage connector");
    }
    apricot_test::pass("hotel and rental pedestrian spaces clear driving routes");
}

} // namespace

int main() {
    paving_supports_tires_and_feet_at_the_visible_top();
    airport_paving_roles_exclude_paint_and_leave_real_edges();
    parking_road_has_no_islands_or_raised_paving();
    each_terminal_door_has_a_continuous_clear_parking_walk();
    parking_markings_respect_walkways_and_paved_rows();
    hotel_walk_and_rental_forecourt_clear_roads_and_buildings();
    return apricot_test::done("airport_circulation_tests");
}
