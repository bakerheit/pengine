#include <cstdio>
#include "app/traffic_signal_mesh.h"
#include "app/traffic_visual_layout.h"
#include "test_assert.h"
using namespace apricot;
namespace {
bool hit(const MeshData& mesh,glm::vec3 eye,glm::vec3 target) {
    const auto ray=target-eye;
    for(std::size_t i=0;i<mesh.indices.size();i+=3u) {
        const auto a=mesh.vertices[mesh.indices[i]].position;
        const auto b=mesh.vertices[mesh.indices[i+1u]].position;
        const auto c=mesh.vertices[mesh.indices[i+2u]].position;
        const auto e=b-a,f=c-a,p=glm::cross(ray,f);
        const float det=glm::dot(e,p);
        if(std::fabs(det)<1e-8f)continue;
        const auto s=eye-a;const float u=glm::dot(s,p)/det;
        if(u<0 || u>1)continue;
        const auto q=glm::cross(s,e);const float v=glm::dot(ray,q)/det;
        if(v<0 || u+v>1)continue;
        const float t=glm::dot(f,q)/det;
        if(t>0 && t<.99999f)return true;
    }
    return false;
}
void validate(const MeshData& mesh) {
    REQUIRE(!mesh.indices.empty());REQUIRE(mesh.indices.size()%3u==0u);
    for(auto index:mesh.indices)REQUIRE(index<mesh.vertices.size());
    for(const auto& v:mesh.vertices) {
        for(int axis=0;axis<3;++axis)REQUIRE(std::isfinite(v.position[axis]));
        REQUIRE_NEAR(glm::length(v.normal),1.0,1e-5);
        REQUIRE(v.uv.x>=0 && v.uv.x<=1 && v.uv.y>=0 && v.uv.y<=1);
    }
    for(std::size_t i=0;i<mesh.indices.size();i+=3u) {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1u]];
        const auto& c=mesh.vertices[mesh.indices[i+2u]];
        const auto cross=glm::cross(b.position-a.position,c.position-a.position);
        REQUIRE(glm::length(cross)>1e-8f);
        REQUIRE(glm::dot(cross,a.normal)>0);
    }
}
}
int main() {
    const auto shell=make_traffic_signal_housing();
    const auto border=make_traffic_signal_border();
    const auto lens=make_traffic_signal_lens();
    const auto pole=make_traffic_signal_pole();
    const auto arm=make_traffic_signal_arm();
    for(const auto* mesh:{&shell,&border,&lens,&pole,&arm})validate(*mesh);
    REQUIRE(shell.indices.size()/3u<500u);
    REQUIRE(lens.indices.size()/3u==20u);
    for(const auto& v:lens.vertices) {
        REQUIRE(v.normal.z>.9f);
        REQUIRE(glm::length(glm::vec2{v.position.x,v.position.y})<=.15601f);
    }
    for(const auto& v:pole.vertices) {
        REQUIRE(std::fabs(v.position.x)<=.10001f);
        REQUIRE(std::fabs(v.position.z)<=.10001f);
    }
    int views=0;
    // Test the actual opaque triangles between a driver's eye and all three
    // lens centres/quadrants, from both lanes and ordinary stopping distances.
    for(float distance:{8.f,16.f,30.f,60.f})for(float side:{-3.5f,0.f,3.5f}) {
        const glm::vec3 eye{side,-3.8f,distance};
        for(float height:{kSignalLensSpacing,0.f,-kSignalLensSpacing}) {
            for(glm::vec2 point:{glm::vec2{0,0},{-.08f,-.08f},{.08f,-.08f},{-.08f,.08f},{.08f,.08f}}) {
                const glm::vec3 target{point.x,height+point.y,kSignalLensZ};
                REQUIRE(!hit(shell,eye,target));REQUIRE(!hit(border,eye,target));++views;
            }
        }
    }
    // The deep visor actually shades overhead light rather than just looking
    // like trim, while its open lower half preserves the driver's sightline.
    REQUIRE(hit(shell,{0,3,1},{0,kSignalLensSpacing,kSignalLensZ}));
    std::printf("  %d driver-to-lens rays clear; housing %zu tris; lens %zu tris\n",
        views,shell.indices.size()/3u,lens.indices.size()/3u);
    apricot_test::pass("signal meshes have sound winding, round forward lenses, clear driver sightlines and unchanged pole footprint");
    return apricot_test::done("traffic_signal_mesh_tests");
}
