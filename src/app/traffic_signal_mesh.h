#pragma once

#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include "gfx/primitives.h"

namespace apricot {
inline constexpr float kSignalPoleHeight = 6.0f;
inline constexpr float kSignalHousingHeight = 1.43f;
inline constexpr float kSignalLensSpacing = .445f;
inline constexpr float kSignalLensZ = .164f;

namespace signal_mesh_detail {
inline void append(MeshData& out,const MeshData& part,glm::vec3 offset,
                   glm::mat3 rotation=glm::mat3{1.0f}) {
    const auto base=static_cast<uint32_t>(out.vertices.size());
    for(auto vertex:part.vertices) {
        vertex.position=offset+rotation*vertex.position;
        vertex.normal=rotation*vertex.normal;
        vertex.material_weights=glm::vec4{0.0f};
        out.bounds.expand(vertex.position);out.vertices.push_back(vertex);
    }
    for(auto index:part.indices)out.indices.push_back(base+index);
}
inline void box(MeshData& out,glm::vec3 position,glm::vec3 size) {
    append(out,make_box(size*.5f),position);
}
inline void quad(MeshData& out,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d) {
    const auto base=static_cast<uint32_t>(out.vertices.size());
    const glm::vec3 normal=glm::normalize(glm::cross(b-a,c-a));
    const std::array<glm::vec3,4> points{a,b,c,d};
    for(std::size_t i=0;i<points.size();++i) {
        out.vertices.push_back({points[i],normal,
            {i==1u || i==2u ? 1.f : 0.f,i>=2u ? 1.f : 0.f},glm::vec4{0.f}});
        out.bounds.expand(points[i]);
    }
    for(uint32_t index:{0u,1u,2u,0u,2u,3u})out.indices.push_back(base+index);
}
inline void visor(MeshData& out,float centre_y) {
    constexpr int segments=14;
    constexpr float inner=.174f,outer=.193f,rear=.142f,front=.345f;
    const auto point=[&](float angle,float radius,float z) {
        return glm::vec3{std::cos(angle)*radius,centre_y+std::sin(angle)*radius,z};
    };
    for(int i=0;i<segments;++i) {
        const float a=glm::radians(-20.f+220.f*static_cast<float>(i)/segments);
        const float b=glm::radians(-20.f+220.f*static_cast<float>(i+1)/segments);
        quad(out,point(a,outer,rear),point(b,outer,rear),point(b,outer,front),point(a,outer,front));
        quad(out,point(b,inner,rear),point(a,inner,rear),point(a,inner,front),point(b,inner,front));
        quad(out,point(a,inner,front),point(a,outer,front),point(b,outer,front),point(b,inner,front));
    }
    for(float angle:{-20.f,200.f}) {
        const float a=glm::radians(angle);
        if(angle<0)quad(out,point(a,inner,rear),point(a,outer,rear),point(a,outer,front),point(a,inner,front));
        else quad(out,point(a,outer,rear),point(a,inner,rear),point(a,inner,front),point(a,outer,front));
    }
}
}

// Local +Z faces incoming traffic. One shared black assembly combines the
// sectional housing, backplate, visors and hanging mount to keep batching cheap.
inline MeshData make_traffic_signal_housing() {
    using namespace signal_mesh_detail;
    MeshData out;
    box(out,{0,0,-.155f},{.63f,1.69f,.045f});
    for(float y:{kSignalLensSpacing,0.f,-kSignalLensSpacing}) {
        box(out,{0,y,0},{.405f,.427f,.28f});
        visor(out,y);
    }
    box(out,{0,.795f,-.075f},{.14f,.24f,.13f});
    box(out,{0,.87f,0},{.28f,.07f,.26f});
    // Raised rear service covers and hinge rails read from the cross street.
    box(out,{0,0,-.205f},{.31f,1.32f,.04f});
    box(out,{-.172f,0,-.19f},{.035f,1.24f,.07f});
    return out;
}
inline MeshData make_traffic_signal_border() {
    using namespace signal_mesh_detail;
    MeshData out;
    constexpr float width=.69f,height=1.75f,band=.03f,z=-.125f;
    box(out,{0,(height-band)*.5f,z},{width,band,.018f});
    box(out,{0,-(height-band)*.5f,z},{width,band,.018f});
    box(out,{(width-band)*.5f,0,z},{band,height-2*band,.018f});
    box(out,{-(width-band)*.5f,0,z},{band,height-2*band,.018f});
    return out;
}
inline MeshData make_traffic_signal_lens() {
    MeshData out;
    constexpr int sides=20;
    constexpr float radius=.156f;
    // Shallow convex front, not a glowing square or a light seen from behind.
    for(int i=0;i<sides;++i) {
        const float a=6.28318530718f*static_cast<float>(i)/sides;
        const float b=6.28318530718f*static_cast<float>(i+1)/sides;
        const glm::vec3 p{std::cos(a)*radius,std::sin(a)*radius,-.01f};
        const glm::vec3 q{std::cos(b)*radius,std::sin(b)*radius,-.01f};
        const glm::vec3 centre{0,0,.005f};
        const glm::vec3 normal=glm::normalize(glm::cross(p-centre,q-centre));
        const auto base=static_cast<uint32_t>(out.vertices.size());
        for(glm::vec3 v:{centre,p,q}) {
            out.vertices.push_back({v,normal,{.5f+v.x/(radius*2),.5f+v.y/(radius*2)},glm::vec4{0.f}});
            out.bounds.expand(v);
        }
        out.indices.insert(out.indices.end(),{base,base+1u,base+2u});
    }
    return out;
}
inline MeshData make_traffic_signal_pole() {
    using namespace signal_mesh_detail;
    MeshData out;
    append(out,make_cylinder(.10f,kSignalPoleHeight*.5f,10),{0,kSignalPoleHeight*.5f,0});
    // All foot details fit the existing 20-cm collision footprint.
    box(out,{0,.045f,0},{.20f,.09f,.20f});
    append(out,make_cylinder(.10f,.16f,10),{0,.20f,0});
    box(out,{0,.48f,.097f},{.09f,.18f,.006f});
    append(out,make_cylinder(.10f,.025f,10),{0,kSignalPoleHeight+.025f,0});
    return out;
}
inline MeshData make_traffic_signal_arm() {
    const auto y_to_x=glm::mat3(glm::rotate(glm::mat4{1.f},glm::radians(-90.f),glm::vec3{0,0,1}));
    MeshData out;
    signal_mesh_detail::append(out,make_cylinder(.075f,.5f,8),{},y_to_x);
    return out;
}
}
