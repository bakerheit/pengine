#pragma once

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
    if (car == PlayerCarId::HarrowWorkman)
        return {kWorkmanDoorHinge, kWorkmanDoorHandle, kWorkmanDoorRearZ,
                kWorkmanDoorFrontZ, kWorkmanDoorSillY, kWorkmanDoorTopY - kWorkmanDoorSillY};
    return {kMistralDoorHinge, kMistralDoorHandle, kMistralDoorRearZ,
            kMistralDoorFrontZ, kMistralDoorSillY, .60f};
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
