#pragma once
#include <algorithm>
#include <cmath>
#include "city/neighborhood_shops.h"
#include "physics/vehicle.h"
namespace apricot {
inline bool repair_shop_ready(const VehicleState& car,const VehicleTuning& tuning) {
    const auto& site=city::kAutoRepairSite;
    if(glm::length(car.velocity)>.5f || vehicle_up(car).y<.8f ||
        std::fabs(car.position.y-site.ground_m)>2.f) return false;
    const auto local=[&](glm::vec3 p) {
        return glm::vec2{site.cos_yaw*p.x-site.sin_yaw*p.z,
                         site.sin_yaw*p.x+site.cos_yaw*p.z};
    };
    const glm::vec2 center=local(car.position-glm::vec3{site.origin.x,0,site.origin.z});
    const glm::vec2 right=local(vehicle_right(car)),forward=local(vehicle_forward(car));
    const glm::vec2 extent=glm::abs(right)*tuning.car_collision_half_width+
        glm::abs(forward)*tuning.car_collision_half_length;
    if(center.y-extent.y < -14.5f || center.y+extent.y > 3.5f) return false;
    for(float bay:{-9.f,-1.f})
        if(std::fabs(center.x-bay)+extent.x<3.f) return true;
    return false;
}
inline bool service_repair_shop(VehicleState& car,const VehicleTuning& tuning) {
    if(!repair_shop_ready(car,tuning)) return false;
    repair_vehicle(car);
    return true;
}
struct RepairShopVisit {
    float stopped_s=0;
    bool serviced=false;
};
inline bool step_repair_shop(RepairShopVisit& visit,VehicleState& car,
                            const VehicleTuning& tuning,float dt,bool driving) {
    if(!driving || !repair_shop_ready(car,tuning)) {
        visit={};
        return false;
    }
    if(visit.serviced) return false;
    visit.stopped_s+=std::max(0.f,dt);
    if(visit.stopped_s<2.f) return false;
    visit.serviced=true;
    return service_repair_shop(car,tuning);
}
} // namespace apricot
