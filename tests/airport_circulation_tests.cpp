#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/airport.h"
#include "city/roads.h"
#include "test_assert.h"

using namespace apricot;

namespace {

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
    parking_road_has_no_islands_or_raised_paving();
    each_terminal_door_has_a_continuous_clear_parking_walk();
    parking_markings_respect_walkways_and_paved_rows();
    hotel_walk_and_rental_forecourt_clear_roads_and_buildings();
    return apricot_test::done("airport_circulation_tests");
}
