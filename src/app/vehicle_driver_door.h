#pragma once

#include "app/car5_next_door.h"
#include "app/mistral_door.h"
#include "app/workman_door.h"
#include "app/player_car_catalog.h"

namespace apricot {

struct VehicleDriverDoor {
    glm::vec3 hinge;
    glm::vec3 handle;
    float rear_z;
    float front_z;
    float sill_y;
    float panel_height;
    float open_radians = -1.1344640138f;
};

inline VehicleDriverDoor vehicle_driver_door(PlayerCarId car) {
    car=canonical_player_car_id(car);
    if(car==PlayerCarId::Bwc360) return {{.864f,.28f,.861f},{.881f,.835f,.07f},-.072f,.858f,.265f,1.07f,-1.082104136f};
    if(car==PlayerCarId::GlmMeridian) return {{.91f,.42f,1.50f},{.94f,.99f,.22f},.04f,1.50f,.33f,.81f};
    if(car==PlayerCarId::RodeoSwitchback) return {{.92f,.48f,.76f},{.95f,.99f,-.21f},-.35f,.76f,.41f,.73f};
    if(car==PlayerCarId::HarrowHookline) return {{1.02f,.55f,2.40f},{1.05f,1.20f,.67f},.47f,2.40f,.48f,.88f};
    if(car==PlayerCarId::RodeoGrazer) return {{.90f,.43f,.92f},{.916f,1.015f,-.19f},-.28f,.92f,.43f,1.31f};
    if(car==PlayerCarId::EmberGt) return {{.93f,.32f,.86f},{.953f,.757f,-.41f},-.60f,.86f,.24f,.96f};
    if(car==PlayerCarId::AlderPip) return {{.824f,.32f,.58f},{.824f,.78f,-.43f},-.55f,.58f,.32f,1.16f};
    if(car==PlayerCarId::VesperScythe) return {{.985f,.29f,.78f},{.97f,.68f,-.43f},-.54f,.78f,.29f,.84f};
    if(car==PlayerCarId::HalcyonSovereign) return {{1.014f,.37f,1.60f},{1.012f,.96f,.30f},.18f,1.60f,.37f,1.27f};
    if(car==PlayerCarId::MunicipalCruiser91A)
        return {{1.015f,.50f,.84f},{1.028f,1.00f,-.02f},-.22f,.84f,.50f,1.05f,
                -1.1868238914f};
    if(car==PlayerCarId::MunicipalCruiser91B)
        return {{1.012f,.40f,.82f},{1.018f,.88f,-.08f},-.30f,.82f,.38f,1.14f};
    if(car==PlayerCarId::MunicipalCruiser91C)
        return {{1.006f,.55f,.88f},{1.025f,1.00f,.04f},-.12f,.90f,.54f,.95f,
                -1.1868238914f};
    if(car==PlayerCarId::MunicipalCruiser91D)
        return {{.986f,.49f,.72f},{1.006f,1.03f,-.12f},-.32f,.72f,.49f,1.17f,
                -1.1519173063f};
    if(car==PlayerCarId::MunicipalCruiser91E)
        return {{1.045f,.47f,.78f},{1.065f,.98f,-.05f},-.24f,.78f,.47f,1.00f,
                -1.2217304764f};
    if (car == PlayerCarId::LegacyCar5Next ||
        car == PlayerCarId::LegacyCar5NextPolice)
        return {kCar5NextDoorHinge, kCar5NextDoorHandle, kCar5NextDoorRearZ,
                kCar5NextDoorFrontZ, kCar5NextDoorSillY,
                kCar5NextDoorTopY - kCar5NextDoorSillY, kCar5NextDoorOpenRadians};
    if (car == PlayerCarId::HarrowWorkman)
        return {kWorkmanDoorHinge, kWorkmanDoorHandle, kWorkmanDoorRearZ,
                kWorkmanDoorFrontZ, kWorkmanDoorSillY, kWorkmanDoorTopY - kWorkmanDoorSillY};
    return {kMistralDoorHinge, kMistralDoorHandle, kMistralDoorRearZ,
            kMistralDoorFrontZ, kMistralDoorSillY, .60f};
}

inline bool has_passenger_door(PlayerCarId car) {
    return car==PlayerCarId::Bwc360 || car==PlayerCarId::EmberGt || car==PlayerCarId::RodeoGrazer;
}

inline Transform vehicle_passenger_door_transform(PlayerCarId car,const Transform& body,float open) {
    const float fraction=std::isfinite(open)?std::clamp(open,0.f,1.f):0.f;
    if (!has_passenger_door(car) || fraction==0.f) return body;
    auto hinge=vehicle_driver_door(car).hinge;hinge.x=-hinge.x;
    Transform door=body;
    door.rotation=body.rotation*glm::angleAxis(
        -vehicle_driver_door(car).open_radians*fraction,glm::vec3{0,1,0});
    door.position=body.transform_point(hinge)-door.rotation*(body.scale*hinge);
    return door;
}

inline Transform vehicle_driver_door_transform(PlayerCarId car, const Transform& body, float open) {
    const float fraction=std::isfinite(open)?std::clamp(open,0.f,1.f):0.f;
    if(fraction==0.f) return body;
    const auto hinge=vehicle_driver_door(car).hinge;
    Transform door=body;
    door.rotation=body.rotation*glm::angleAxis(
        vehicle_driver_door(car).open_radians*fraction,glm::vec3{0,1,0});
    door.position=body.transform_point(hinge)-door.rotation*(body.scale*hinge);
    return door;
}

} // namespace apricot
