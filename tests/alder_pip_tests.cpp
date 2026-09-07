#include <cmath>
#include <string>
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "test_assert.h"
using namespace apricot;
namespace {
bool projected_triangle(glm::vec2 p,glm::vec2 a,glm::vec2 b,glm::vec2 c) {
    auto cross=[](glm::vec2 x,glm::vec2 y){return x.x*y.y-x.y*y.x;};
    const float ab=cross(b-a,p-a),bc=cross(c-b,p-b),ca=cross(a-c,p-c);
    return (ab>1e-6f && bc>1e-6f && ca>1e-6f) || (ab<-1e-6f && bc<-1e-6f && ca<-1e-6f);
}
bool side_covers(const StaticEmesh& mesh,float z,float y,float side=1,float facing=0) {
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1]];
        const auto& c=mesh.vertices[mesh.indices[i+2]];
        if(side*a.px<.65f || side*b.px<.65f || side*c.px<.65f)continue;
        const float nx=(b.py-a.py)*(c.pz-a.pz)-(b.pz-a.pz)*(c.py-a.py);
        if(facing!=0 && nx*facing<=0)continue;
        if(projected_triangle({z,y},{a.pz,a.py},{b.pz,b.py},{c.pz,c.py}))return true;
    }
    return false;
}
}
int main() {
    StaticEmesh closed,open,door;
    REQUIRE(read_static_emesh(asset_path("models/vehicles/alder_pip/body.emesh"),closed));
    REQUIRE(read_static_emesh(asset_path("models/vehicles/alder_pip/body_open.emesh"),open));
    REQUIRE(read_static_emesh(asset_path("models/vehicles/alder_pip/driver_door.emesh"),door));
    REQUIRE(closed.indices.size()/3>=650u && closed.indices.size()/3<=1300u);
    REQUIRE(open.indices.size()/3<=1600u && door.indices.size()/3<=100u);
    REQUIRE_NEAR(closed.bounds.size().z,3.80f,1e-4f);
    REQUIRE_NEAR(closed.bounds.size().x,1.94f,1e-4f);
    REQUIRE_NEAR(closed.bounds.max.y,1.52f,1e-4f);
    REQUIRE_NEAR(door.bounds.min.y,.32f,1e-4f);
    REQUIRE_NEAR(door.bounds.min.z,-.55f,1e-4f);
    REQUIRE_NEAR(door.bounds.max.z,.58f,1e-4f);
    for(const auto* mesh:{&closed,&open,&door})for(const auto& v:mesh->vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1);
    }
    for(float z:{-.37f,-.11f,.23f})for(float y:{.48f,.68f}) {
        REQUIRE(!side_covers(open,z,y));
        REQUIRE(side_covers(door,z,y));
    }
    REQUIRE(side_covers(closed,-.11f,.68f));
    apricot_test::pass("Pip cooked entry door covers the real opening and leaves no stationary obstruction");
    // Both sides of the B pillar must survive back-face culling, including
    // the passenger frame viewed through the driver's window.
    for(float side:{-1.f,1.f})for(float facing:{-1.f,1.f})
        REQUIRE(side_covers(open,-.60f,1.19f,side,facing));
    for(float side:{-1.f,1.f})for(float z:{-1.1f,-.21f,.03f}) {
        REQUIRE(!side_covers(open,z,1.19f,side));
        REQUIRE(!side_covers(door,z,1.19f,side));
    }
    for(const char* name:{"windshield","rear_glass","passenger_glass","driver_glass",
                          "driver_rear_glass","passenger_rear_glass"}) {
        StaticEmesh pane;
        REQUIRE(read_static_emesh(asset_path(std::string("models/vehicles/alder_pip/")+name+".emesh"),pane));
        REQUIRE(pane.indices.size()==6u); // No duplicate backface/doubled tint.
        for(std::size_t i=0;i<pane.indices.size();i+=3) {
            const auto& a=pane.vertices[pane.indices[i]];
            const auto& b=pane.vertices[pane.indices[i+1]];
            const auto& c=pane.vertices[pane.indices[i+2]];
            const glm::vec3 n=glm::cross(glm::vec3{b.px-a.px,b.py-a.py,b.pz-a.pz},
                                       glm::vec3{c.px-a.px,c.py-a.py,c.pz-a.pz});
            REQUIRE(glm::length(n)>.01f);
        }
        if(std::string(name)=="passenger_glass") REQUIRE(side_covers(pane,-.21f,1.19f,-1));
        if(std::string(name)=="driver_glass") REQUIRE(side_covers(pane,-.21f,1.19f));
    }
    apricot_test::pass("Pip frames face inward and outward; six separate panes leave real glass apertures");
    return apricot_test::done("alder_pip_tests");
}
