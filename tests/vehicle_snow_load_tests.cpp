// A vehicle's own snow: settles in the open, keeps under a canopy, sheds at
// speed down to a floor, melts. Stepped against the real Halloway canopy
// through the real SnowShelterField, at the fixed sim step.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "city/precipitation_cover.h"
#include "core/fixed_step.h"
#include "game/vehicle_snow_load.h"
#include "gfx/instance.h"
#include "physics/snow_shelter.h"
#include "scene/scene.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt = static_cast<float>(kSimDt);
constexpr int minutes(int m) { return static_cast<int>(m * 60 * kSimHz); }

struct Forecourt {
    SnowShelterField field;
    city::StartPart canopy{};
    glm::vec3 point(float x, float y, float z) const {
        const auto& s = city::kGasStationSite;
        return {s.origin.x + s.cos_yaw * x + s.sin_yaw * z, s.ground_m + y,
                s.origin.z - s.sin_yaw * x + s.cos_yaw * z};
    }
    // Roof exposure of a car whose ground point is at site-local (x, z).
    float roof(float x, float z) const {
        const glm::vec3 p = point(x, kVehicleSnowRoofAboveGroundM, z);
        return field.exposure(p.x, p.y, p.z);
    }
};

Forecourt forecourt() {
    Forecourt out;
    const auto& site = city::kGasStationSite;
    std::vector<StaticBox> covers;
    for (const auto& part : city::bake_building(city::kGasStationPlan)) {
        Transform t;
        t.position = out.point(part.centre.x, part.bottom_m + part.height_m * 0.5f, part.centre.z);
        t.rotation = glm::angleAxis(std::atan2(site.sin_yaw, site.cos_yaw), glm::vec3{0, 1, 0}) *
                     glm::quat(glm::radians(glm::vec3{part.pitch_deg, part.yaw_deg, part.roll_deg}));
        t.scale = {part.width_m, part.height_m, part.depth_m};
        city::append_precipitation_cover(part, t, covers);
        if (part.name && std::strcmp(part.name, "canopy roof") == 0) out.canopy = part;
    }
    out.field.build(covers);
    return out;
}

const VehicleSnowWeather kSnowing{1.0f, 0.8f, 0.0f};
const VehicleSnowWeather kStopped{1.0f, 0.0f, 0.0f};

void drive_under_the_canopy_keeps_the_load() {
    const Forecourt f = forecourt();
    REQUIRE(f.canopy.name != nullptr);
    const float centre_z = f.canopy.centre.z;
    const float south = centre_z + f.canopy.depth_m * 0.5f;
    // Parked in the open south of the forecourt through twenty minutes of
    // snowfall: a full blanket, capped at the ground's cover.
    float load = 0.0f;
    const float open = f.roof(-8.1f, south + 12.0f);
    REQUIRE(open == 1.0f);
    for (int i = 0; i < minutes(20); ++i)
        load = step_vehicle_snow_load(load, open, 0.0f, false, kSnowing, kDt);
    REQUIRE_NEAR(load, 1.0f, 1e-6);
    // Drive in at walking pace and park under the middle of the canopy. The
    // old shader went bare at the roof line; the car's own snow must not.
    float z = south + 12.0f;
    while (z > centre_z) {
        z = std::max(centre_z, z - 3.0f * kDt);
        load = step_vehicle_snow_load(load, f.roof(-8.1f, z), 3.0f, true, kSnowing, kDt);
    }
    const float arrived = load;
    REQUIRE(arrived > 0.97f);
    const float sheltered = f.roof(-8.1f, centre_z);
    REQUIRE(sheltered < 0.1f);
    // Ten minutes parked under cover, engine off, still snowing outside.
    for (int i = 0; i < minutes(10); ++i)
        load = step_vehicle_snow_load(load, sheltered, 0.0f, false, kSnowing, kDt);
    REQUIRE_NEAR(load, arrived, 1e-6);
    // Snow stops: only the slow cold melt, still clearly snowy ten minutes on.
    for (int i = 0; i < minutes(10); ++i)
        load = step_vehicle_snow_load(load, sheltered, 0.0f, false, kStopped, kDt);
    REQUIRE(load > 0.7f && load < arrived);
    std::printf("  arrived %.3f, after 10 min sheltered snowfall + 10 min dry %.3f\n",
                static_cast<double>(arrived), static_cast<double>(load));
    apricot_test::pass("a snowy car keeps its load under the Halloway canopy and only melts slowly");
}

void bare_car_under_cover_stays_nearly_bare() {
    const Forecourt f = forecourt();
    const float sheltered = f.roof(-8.1f, f.canopy.centre.z);
    const float edge = f.roof(-8.1f, f.canopy.centre.z + f.canopy.depth_m * 0.5f - 0.6f);
    REQUIRE(edge > sheltered);
    float middle = 0.0f, near_edge = 0.0f;
    for (int i = 0; i < minutes(30); ++i) {
        middle = step_vehicle_snow_load(middle, sheltered, 0.0f, false, kSnowing, kDt);
        near_edge = step_vehicle_snow_load(near_edge, edge, 0.0f, false, kSnowing, kDt);
    }
    REQUIRE_NEAR(middle, sheltered, 1e-5);   // a dusting, never a blanket
    REQUIRE_NEAR(near_edge, edge, 1e-5);     // drift reaches a car at the edge
    REQUIRE(middle < 0.1f && near_edge > middle);
    // Seeding agrees with the long-run state it stands in for.
    REQUIRE_NEAR(seed_vehicle_snow_load(sheltered, kSnowing), middle, 1e-5);
    REQUIRE_NEAR(seed_vehicle_snow_load(1.0f, kSnowing), 1.0f, 1e-6);
    apricot_test::pass("a bare car parked under cover collects only the drift that reaches its roof");
}

void speed_sheds_to_a_floor_and_heat_melts() {
    // Airflow alone (melt switched off) takes a full load down to the packed
    // floor within a minute at highway speed, and never below it.
    VehicleSnowLoadTuning airflow_only;
    airflow_only.melt_per_s = 0.0f;
    airflow_only.engine_melt_per_s = 0.0f;
    float shed = 1.0f;
    for (int i = 0; i < minutes(1); ++i)
        shed = step_vehicle_snow_load(shed, 1.0f, 30.0f, true, kStopped, kDt, airflow_only);
    REQUIRE(shed < airflow_only.shed_floor + 0.02f);
    for (int i = 0; i < minutes(30); ++i)
        shed = step_vehicle_snow_load(shed, 1.0f, 30.0f, true, kStopped, kDt, airflow_only);
    REQUIRE(shed >= airflow_only.shed_floor && shed < airflow_only.shed_floor + 1e-4f);
    // Driving the highway in falling snow keeps a real cap on the car.
    float load = 1.0f;
    for (int i = 0; i < minutes(10); ++i)
        load = step_vehicle_snow_load(load, 1.0f, 30.0f, true, kSnowing, kDt);
    REQUIRE(load > airflow_only.shed_floor && load < 0.9f);
    // Town speed blows nothing off: it loses exactly what an idling car does.
    float town = 1.0f, idling = 1.0f;
    for (int i = 0; i < minutes(1); ++i) {
        town = step_vehicle_snow_load(town, 1.0f, 8.0f, true, kStopped, kDt);
        idling = step_vehicle_snow_load(idling, 1.0f, 0.0f, true, kStopped, kDt);
    }
    REQUIRE(town == idling && town > 0.9f);
    float heat = 1.0f;
    const VehicleSnowWeather heatwave{0.0f, 0.0f, 1.0f};
    for (int i = 0; i < minutes(4); ++i)
        heat = step_vehicle_snow_load(heat, 0.0f, 0.0f, false, heatwave, kDt);
    REQUIRE(heat == 0.0f);
    // Bad input never produces a bad load.
    REQUIRE(step_vehicle_snow_load(std::nanf(""), 1.0f, 0.0f, false, kSnowing, kDt) >= 0.0f);
    REQUIRE(step_vehicle_snow_load(2.0f, 1.0f, 0.0f, false, kSnowing, kDt) <= 1.0f);
    REQUIRE(step_vehicle_snow_load(0.5f, 1.0f, 0.0f, false, kSnowing, -1.0f) == 0.5f);
    apricot_test::pass("highway airflow sheds to a packed floor, town speed does not, heat clears it");
}

void traffic_table_seeds_steps_and_forgets() {
    const Forecourt f = forecourt();
    VehicleSnowLoads loads;
    std::vector<VehicleSnowSample> samples(3);
    samples[0] = {10, 0, 1, 1.0f, 0.0f, true};
    samples[1] = {10, 1, 1, f.roof(-8.1f, f.canopy.centre.z), 0.0f, true};
    samples[2] = {4, 2, 7, 1.0f, 12.0f, true};  // out of order on purpose
    const VehicleSnowWeather light{0.6f, 0.5f, 0.0f};
    loads.step(samples, light, kDt);
    REQUIRE(loads.size() == 3u);
    REQUIRE_NEAR(loads.load(10, 0, 1), 0.6f, 1e-6);
    REQUIRE(loads.load(10, 1, 1) < 0.06f);
    REQUIRE_NEAR(loads.load(4, 2, 7), 0.6f, 1e-6);
    REQUIRE(loads.load(4, 2, 8) < 0.0f);  // a different departure is unknown
    // Car (10,0) drives under cover: its load is stepped, not reseeded.
    samples[0].roof_exposure = samples[1].roof_exposure;
    loads.step(samples, light, kDt);
    REQUIRE(loads.load(10, 0, 1) > 0.59f);
    // (4,2) retires and a new departure takes its slot under cover: reseed.
    samples[2] = {4, 2, 8, samples[1].roof_exposure, 0.0f, true};
    loads.step(samples, light, kDt);
    REQUIRE(loads.load(4, 2, 7) < 0.0f);
    REQUIRE(loads.load(4, 2, 8) < 0.06f);
    samples.pop_back();
    loads.step(samples, light, kDt);
    REQUIRE(loads.size() == 2u);
    REQUIRE(loads.load(4, 2, 8) < 0.0f);
    // Two tables fed the same steps agree exactly: no hidden state.
    VehicleSnowLoads a, b;
    for (int i = 0; i < 600; ++i) {
        samples[0].speed_mps = static_cast<float>(i % 40);
        a.step(samples, kSnowing, kDt);
        b.step(samples, kSnowing, kDt);
    }
    REQUIRE(a.load(10, 0, 1) == b.load(10, 0, 1));
    apricot_test::pass("traffic loads seed on appearance, step while resident, reseed a new departure");
}

void load_rides_the_instance_and_props_keep_the_world_path() {
    Renderable prop;
    REQUIRE(prop.snow_load < 0.0f);
    const InstanceData world = make_instance(glm::mat4{1.0f}, glm::vec4{1.0f}, glm::vec2{1.0f});
    REQUIRE(world.normal_c0.w < 0.0f);
    REQUIRE(world.normal_c1.w == 0.0f && world.normal_c2.w == 0.0f);
    const InstanceData car = make_instance(glm::mat4{1.0f}, glm::vec4{1.0f}, glm::vec2{1.0f},
                                           glm::vec4{0.0f}, glm::vec4{0.0f}, glm::vec4{0.0f},
                                           0.625f);
    REQUIRE(car.normal_c0.w == 0.625f);
    REQUIRE(glm::vec3(car.normal_c0) == glm::vec3(world.normal_c0));
    apricot_test::pass("snow load rides the spare normal-column W; world props default to the ground path");
}

}  // namespace

int main() {
    drive_under_the_canopy_keeps_the_load();
    bare_car_under_cover_stays_nearly_bare();
    speed_sheds_to_a_floor_and_heat_melts();
    traffic_table_seeds_steps_and_forgets();
    load_rides_the_instance_and_props_keep_the_world_path();
    return apricot_test::done("vehicle_snow_load");
}
