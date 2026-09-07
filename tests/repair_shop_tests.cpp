#include "game/repair_shop.h"
#include "test_assert.h"
using namespace apricot;
int main() {
    TerrainCollider ground(city::kMapSeed);
    VehicleTuning tuning;
    const auto& site=city::kAutoRepairSite;
    const auto at=[&](float x,float z) {
        return spawn_vehicle(tuning,ground,site.origin.x+site.cos_yaw*x+site.sin_yaw*z,
            site.origin.z-site.sin_yaw*x+site.cos_yaw*z,std::atan2(site.sin_yaw,site.cos_yaw));
    };
    for(float bay:{-9.f,-1.f}) {
        auto car=at(bay,-5);
        car.health=14;car.body_damage.zones[kDamageFrontCenter]=1;
        car.mechanical.engine_failed=true;car.mechanical.oil_remaining=0;
        car.mechanical.fuel_remaining=.2f;car.mechanical.oil_lifetime_s=77;
        const auto position=car.position;const auto rotation=car.orientation;
        REQUIRE(service_repair_shop(car,tuning));
        REQUIRE(car.position==position && car.orientation==rotation);
        REQUIRE(car.health==100 && car.body_damage.zones[kDamageFrontCenter]==0);
        REQUIRE(!vehicle_engine_failed(car.mechanical));
        REQUIRE(car.mechanical.oil_remaining==1 && car.mechanical.fuel_remaining==1);
        car.health=30;car.velocity={0,0,2};
        REQUIRE(!service_repair_shop(car,tuning));REQUIRE(car.health==30);
    }
    auto outside=at(-9,8);outside.health=20;
    REQUIRE(!service_repair_shop(outside,tuning));REQUIRE(outside.health==20);
    REQUIRE(!repair_shop_ready(at(10,-5),tuning));
    auto waiting=at(-9,-5);waiting.health=20;
    RepairShopVisit visit;
    REQUIRE(!step_repair_shop(visit,waiting,tuning,1.f,true));
    REQUIRE(waiting.health==20);
    waiting.velocity={0,0,2};
    REQUIRE(!step_repair_shop(visit,waiting,tuning,1.f,true));
    waiting.velocity={0,0,0};
    REQUIRE(!step_repair_shop(visit,waiting,tuning,1.f,true));
    REQUIRE(step_repair_shop(visit,waiting,tuning,1.f,true));
    REQUIRE(waiting.health==100);
    waiting.health=80;
    REQUIRE(!step_repair_shop(visit,waiting,tuning,3.f,true));
    REQUIRE(waiting.health==80);
    REQUIRE(!step_repair_shop(visit,waiting,tuning,1.f,false));
    REQUIRE(step_repair_shop(visit,waiting,tuning,2.f,true));
    return apricot_test::done("repair_shop_tests");
}
