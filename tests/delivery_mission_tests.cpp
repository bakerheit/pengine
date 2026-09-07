#include "game/delivery_mission.h"
#include "city/start_area.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    const auto& bay=city::kOpeningMissionCarLocal;
    const VehicleTuning vehicle;
    REQUIRE(bay.x-vehicle.car_collision_half_width>7.0f);
    REQUIRE(bay.x+vehicle.car_collision_half_width<11.0f);
    REQUIRE(bay.z-vehicle.car_collision_half_length>-5.94f);
    REQUIRE(bay.z+vehicle.car_collision_half_length<1.5f);
    const float entrance_x=city::kGasStoreWalls[0].a.x+
                           city::kGasStoreFrontOpenings[1].center_m;
    REQUIRE(glm::length(glm::vec2{bay.x,bay.z}-glm::vec2{entrance_x,-7.7f})<5.0f);
    const glm::vec2 roundtrip{
        city::kGasStationSite.cos_yaw*(city::kOpeningMissionCarPosition.x-city::kGasStationSite.origin.x)-
            city::kGasStationSite.sin_yaw*(city::kOpeningMissionCarPosition.z-city::kGasStationSite.origin.z),
        city::kGasStationSite.sin_yaw*(city::kOpeningMissionCarPosition.x-city::kGasStationSite.origin.x)+
            city::kGasStationSite.cos_yaw*(city::kOpeningMissionCarPosition.z-city::kGasStationSite.origin.z)};
    REQUIRE_NEAR(roundtrip.x,bay.x,1e-4);REQUIRE_NEAR(roundtrip.y,bay.z,1e-4);

    MissionStage car_stage=MissionStage::DeliveryNeedsCar;
    REQUIRE(mission_car_cue_visible(car_stage,true));
    REQUIRE(!mission_car_cue_visible(car_stage,false));
    REQUIRE(enter_mission_car(car_stage));
    REQUIRE(car_stage==MissionStage::DeliveryActive);
    REQUIRE(!mission_car_cue_visible(car_stage,true));
    REQUIRE(!enter_mission_car(car_stage));

    const glm::mat4 identity{1.0f};
    const auto centre=project_mission_cue({0,0,0},identity,{1280,720});
    REQUIRE(centre.visible);REQUIRE(centre.screen==glm::vec2(640,360));
    REQUIRE(!project_mission_cue({2,0,0},identity,{1280,720}).visible);
    REQUIRE(!project_mission_cue({0,0,0},identity,{0,720}).visible);

    const glm::vec3 devon=city::devon_position();
    MissionStage stage=MissionStage::DeliveryActive;
    REQUIRE(complete_delivery(stage,devon+glm::vec3{-1.7f,0,0},true));
    REQUIRE(stage==MissionStage::DeliveryComplete);
    REQUIRE(!complete_delivery(stage,devon,true));

    stage=MissionStage::DeliveryActive;
    REQUIRE(!complete_delivery(stage,devon,false));
    REQUIRE(stage==MissionStage::DeliveryActive);
    REQUIRE(!complete_delivery(stage,devon+glm::vec3{-2.1f,0,0},true));
    REQUIRE(!complete_delivery(stage,devon+glm::vec3{0,1.1f,0},true));
    REQUIRE(stage==MissionStage::DeliveryActive);

    stage=MissionStage::Opening;
    REQUIRE(!complete_delivery(stage,devon,true));
    REQUIRE(stage==MissionStage::Opening);
    return apricot_test::done("delivery_mission_tests");
}
