#include <cstdio>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include "gfx/tiled_light_grid.h"
#include "test_assert.h"

using namespace apricot;
namespace {
const glm::mat4 projection=glm::perspective(glm::radians(60.0f),16.0f/9.0f,0.15f,2000.0f);
bool has_light(const TiledLightGrid& grid,glm::vec3 p,uint32_t light,int width=1280,int height=720) {
    const auto clip=projection*glm::vec4{p,1};
    if(clip.w<kLightNear || clip.w>kLightFar) return true;
    const glm::vec2 ndc=glm::vec2{clip}/clip.w;
    if(std::fabs(ndc.x)>=1 || std::fabs(ndc.y)>=1) return true;
    const int x=std::clamp(static_cast<int>((ndc.x*.5f+.5f)*static_cast<float>(width))/64,0,grid.columns-1);
    const int y=std::clamp(static_cast<int>((ndc.y*.5f+.5f)*static_cast<float>(height))/64,0,grid.rows-1);
    const auto cell=grid.cells[static_cast<std::size_t>((light_depth_slice(clip.w)*grid.rows+y)*grid.columns+x)];
    for(uint32_t i=0;i<cell.y;++i) if(grid.indices[cell.x+i]==light) return true;
    return false;
}
void no_cap_and_depth_separation() {
    TiledLightGrid grid;
    std::vector<TrafficSpotLight> lights(200,{{0,0,-10,32},{0,0,-1,4.5f}});
    grid.build(lights,projection,1280,720);
    REQUIRE(grid.visible_lights==200u);
    REQUIRE(grid.max_cell_lights==200u);
    for(uint32_t i=0;i<200;++i) REQUIRE(has_light(grid,{0,0,-20},i));
    const auto near_cell=grid.cells[static_cast<std::size_t>((light_depth_slice(1)*grid.rows+5)*grid.columns+10)];
    REQUIRE(near_cell.y==0u);
    const auto first=grid.indices;
    grid.build(lights,projection,1280,720);
    REQUIRE(first==grid.indices);
    for(auto& light:lights) light.direction_power.w=0;
    grid.build(lights,projection,1280,720);
    REQUIRE(grid.visible_lights==0u && grid.indices.empty());
    lights={{{0,0,-10,32},{0,0,0,4}},
            {{0,0,-10,32},{0,0,-1,std::numeric_limits<float>::quiet_NaN()}},
            {{0,0,-10,-1},{0,0,-1,4}}};
    grid.build(lights,projection,1280,720);
    REQUIRE(grid.visible_lights==0u && grid.indices.empty());
    apricot_test::pass("200 overlapping beams retained, separated by depth, deterministic and off in daylight");
}
void clipped_cones_keep_every_visible_sample() {
    TiledLightGrid grid;
    std::vector<TrafficSpotLight> lights;
    for(int z=0;z<7;++z) for(int x=-4;x<=4;++x) {
        const glm::vec3 p{static_cast<float>(x)*9,2,8-static_cast<float>(z)*12};
        const glm::vec3 direction=glm::normalize(glm::vec3{-p.x*.04f,-.075f,-1});
        lights.push_back({glm::vec4{p,32},glm::vec4{direction,4.5f}});
    }
    // Lamp is outside the view, but its beam crosses the camera near plane.
    lights.push_back({{2,0,1,32},glm::vec4{glm::normalize(glm::vec3{-1,0,-1}),4.5f}});
    lights.push_back({{0,1,-25,32},glm::vec4{glm::normalize(glm::vec3{0,-.075f,1}),4.5f}});
    lights.push_back({{30,1,-30,32},glm::vec4{glm::normalize(glm::vec3{-1,-.1f,.1f}),4.5f}});
    lights.push_back({{0,1,-240,32},glm::vec4{glm::normalize(glm::vec3{.1f,-.1f,1}),4.5f}});
    for(const auto size:{glm::ivec2{1280,720},glm::ivec2{1311,733}}) {
        grid.build(lights,projection,size.x,size.y);
        for(std::size_t li=0;li<lights.size();++li) {
            const glm::vec3 p{lights[li].position_range},d{lights[li].direction_power};
            const auto right=glm::normalize(glm::cross(d,glm::vec3{0,1,0}));
            const auto up=glm::cross(right,d);
            for(int distance=1;distance<32;++distance) for(int ring=0;ring<8;++ring) {
                const float t=static_cast<float>(distance);
                const float angle=static_cast<float>(ring)*glm::radians(45.0f);
                const glm::vec3 sample=p+d*t+(right*std::cos(angle)+up*std::sin(angle))*t*.35f;
                REQUIRE(has_light(grid,sample,static_cast<uint32_t>(li),size.x,size.y));
            }
        }
    }
    apricot_test::pass("cone samples survive near-plane clipping, off-screen origins and resized tiles");
}
}
void colored_wide_cones() {
    const TrafficSpotLight standard;
    REQUIRE(standard.color_outer == glm::vec4(1,.86f,.66f,.94f));
    std::vector<TrafficSpotLight> lights{
        {{0,2,-12,24},{0,0,-1,4},{1,.015f,.005f,.55f}},
        {{0,2,-12,24},{0,0,-1,4},{.015f,.08f,1,.55f}}};
    TiledLightGrid grid;
    grid.build(lights,projection,1280,720);
    REQUIRE(grid.visible_lights==2);
    // A point outside a normal headlight cone must survive broad beacon culling.
    for (uint32_t id=0;id<2;++id) REQUIRE(has_light(grid,{7,2,-22},id));
    lights[0].color_outer.w=0;
    lights[1].color_outer.x=std::numeric_limits<float>::quiet_NaN();
    grid.build(lights,projection,1280,720);
    REQUIRE(grid.visible_lights==0);
    apricot_test::pass("colored wide cones retain road illumination and reject invalid data");
}
int main() {
    colored_wide_cones();
    no_cap_and_depth_separation(); clipped_cones_keep_every_visible_sample();
    return apricot_test::done("tiled_light_grid_tests");
}
