#include "app/emergency_lighting.h"
#include "gfx/tiled_light_grid.h"
#include "test_assert.h"
#include <glm/gtc/matrix_transform.hpp>

using namespace apricot;
int main() {
    for (uint64_t step=0;step<240;++step) {
        const auto p=police_flash_power(step,true);
        REQUIRE(p[0]*p[1]==0);
        REQUIRE(p==police_flash_power(step+120,true));
        REQUIRE(police_flash_power(step,false)==(std::array<float,2>{0,0}));
    }
    REQUIRE(police_flash_power(0,true)[0]==1);
    REQUIRE(police_flash_power(60,true)[1]==1);
    REQUIRE(police_flash_power(18,true)[0]==0);
    Transform body; body.position={2,0,-10};
    body.rotation=glm::angleAxis(.6f,glm::vec3{0,1,0});
    body.scale={.8f,.9f,.9f};
    std::vector<TrafficSpotLight> lights;
    append_police_lights(lights,body,0,false);
    REQUIRE(lights.empty());
    append_police_lights(lights,body,0,true);
    REQUIRE(lights.size()==4);
    const auto expected=body.transform_point(
        police_lightbar_centres(PlayerCarId::MunicipalCruiser91C)[0]);
    for (const auto& light:lights) {
        REQUIRE(glm::length(glm::vec3{light.position_range}-expected)<.0001f);
        REQUIRE(std::fabs(glm::length(glm::vec3{light.direction_power})-1)<.0001f);
        REQUIRE(light.color_outer.r>light.color_outer.b*10);
        REQUIRE(light.direction_power.y<0);
    }
    lights.clear(); append_police_lights(lights,body,60,true);
    REQUIRE(lights.size()==4);
    REQUIRE(lights[0].color_outer.b>lights[0].color_outer.r*10);
    for (const auto model:{PlayerCarId::MunicipalCruiser91A,
                           PlayerCarId::MunicipalCruiser91B,
                           PlayerCarId::MunicipalCruiser91C,
                           PlayerCarId::MunicipalCruiser91D,
                           PlayerCarId::MunicipalCruiser91E,
                           PlayerCarId::LegacyCar5NextPolice}) {
        lights.clear();
        append_police_lights(lights,body,0,true,model);
        REQUIRE(lights.size()==4);
        const auto placed=body.transform_point(police_lightbar_centres(model)[0]);
        REQUIRE(glm::distance(glm::vec3{lights[0].position_range},placed)<.0001f);
    }
    TiledLightGrid grid;
    grid.build(lights,glm::perspective(glm::radians(60.f),16.f/9.f,.15f,1000.f),1280,720);
    REQUIRE(grid.visible_lights>0);
    return apricot_test::done("emergency_lighting_tests");
}
