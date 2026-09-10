#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "gfx/lighting.h"

namespace apricot {

inline constexpr int kLightTilePixels = 64;
inline constexpr int kLightDepthSlices = 24;
inline constexpr float kLightNear = 0.15f;
inline constexpr float kLightFar = 256.0f;
inline constexpr float kTrafficOuterCos = 0.94f;
inline constexpr float kTrafficInnerCos = 0.98f;

inline int light_depth_slice(float depth) {
    const float t = std::log(std::max(depth,kLightNear) / kLightNear) /
                    std::log(kLightFar / kLightNear);
    return std::clamp(static_cast<int>(t * kLightDepthSlices),0,kLightDepthSlices-1);
}

// Conservative clipped projection of the spotlight pyramid and radial range.
// No selection budget: every relevant lamp survives, including off-screen
// lamps whose cones reach the view. Lists have no per-tile truncation.
struct TiledLightGrid {
    int columns = 0, rows = 0;
    std::vector<glm::uvec2> cells;
    std::vector<uint32_t> indices;
    std::size_t visible_lights = 0;
    uint32_t max_cell_lights = 0;

    void build(const std::vector<TrafficSpotLight>& lights, const glm::mat4& vp,
               int width, int height) {
        columns = std::max(1,(width+kLightTilePixels-1)/kLightTilePixels);
        rows = std::max(1,(height+kLightTilePixels-1)/kLightTilePixels);
        cells.assign(static_cast<std::size_t>(columns*rows*kLightDepthSlices),glm::uvec2{0});
        indices.clear(); visible_lights = 0; max_cell_lights = 0;
        struct Span { int x0,x1,y0,y1,z0,z1; uint32_t light; };
        std::vector<Span> spans;
        spans.reserve(lights.size()*8);
        static const auto depth_edges=[] {
            std::array<float,kLightDepthSlices+1> edges{};
            for(int i=0;i<=kLightDepthSlices;++i) edges[static_cast<std::size_t>(i)]=
                kLightNear*std::pow(kLightFar/kLightNear,static_cast<float>(i)/kLightDepthSlices);
            return edges;
        }();
        for (std::size_t li=0; li<lights.size(); ++li) {
            const auto& light = lights[li];
            const float range = light.position_range.w;
            bool finite=true;
            for(int axis=0;axis<4;++axis)
                finite=finite && std::isfinite(light.position_range[axis]) &&
                    std::isfinite(light.direction_power[axis]) && std::isfinite(light.color_outer[axis]);
            if (!finite || !(range>0) || !(light.direction_power.w>0) ||
                !(light.color_outer.w > 0.0f && light.color_outer.w < 1.0f) ||
                glm::any(glm::lessThan(glm::vec3{light.color_outer}, glm::vec3{0})) ||
                glm::dot(glm::vec3{light.direction_power},glm::vec3{light.direction_power})<1e-8f) continue;
            const glm::vec3 p{light.position_range};
            const glm::vec3 d = glm::normalize(glm::vec3{light.direction_power});
            const glm::vec3 reference = std::fabs(d.y)<0.99f ? glm::vec3{0,1,0} : glm::vec3{1,0,0};
            const glm::vec3 right = glm::normalize(glm::cross(d,reference));
            const glm::vec3 up = glm::cross(right,d);
            const float outer = light.color_outer.w;
            const float radius = range * std::sqrt(1-outer*outer)/outer;
            std::array<glm::vec4,5> clip;
            clip[0] = vp * glm::vec4{p,1};
            int v=1;
            for (int y : {-1,1}) for (int x : {-1,1})
                clip[static_cast<std::size_t>(v++)] = vp * glm::vec4{p+d*range+
                    right*(radius*static_cast<float>(x))+up*(radius*static_cast<float>(y)),1};
            // The shader measures radial distance, not distance along the
            // cone axis. Wide ceiling cones otherwise get a huge pyramid
            // (a 7 m / .35 cosine lamp has an 18.7 m base radius). Bound the
            // spherical sector too, then intersect its projected footprint
            // with the pyramid in each depth band. Both bounds are conservative.
            const float sine=std::sqrt(1-outer*outer);
            const auto axial_max=[&](float component) {
                return component>=outer ? 1.f : component*outer+
                    std::sqrt(std::max(0.f,1-component*component))*sine;
            };
            glm::vec3 sector_lo,sector_hi;
            for(int axis=0;axis<3;++axis) {
                sector_lo[axis]=p[axis]-range*std::max(0.f,axial_max(-d[axis]))-.0001f;
                sector_hi[axis]=p[axis]+range*std::max(0.f,axial_max(d[axis]))+.0001f;
            }
            std::array<glm::vec4,8> sector_clip;
            v=0;
            for(int z:{0,1})for(int y:{0,1})for(int x:{0,1})
                sector_clip[static_cast<std::size_t>(v++)]=vp*glm::vec4{
                    x?sector_hi.x:sector_lo.x,y?sector_hi.y:sector_lo.y,
                    z?sector_hi.z:sector_lo.z,1};
            float depth_min=clip[0].w, depth_max=clip[0].w;
            for (const auto& c : clip) { depth_min=std::min(depth_min,c.w); depth_max=std::max(depth_max,c.w); }
            float sector_front=sector_clip[0].w,sector_back=sector_clip[0].w;
            for(const auto& c:sector_clip) {
                sector_front=std::min(sector_front,c.w);sector_back=std::max(sector_back,c.w);
            }
            depth_min=std::max(depth_min,sector_front);
            depth_max=std::min(depth_max,sector_back);
            if (depth_max<kLightNear || depth_min>kLightFar) continue;
            bool added=false;
            for(int slice=light_depth_slice(depth_min);slice<=light_depth_slice(depth_max);++slice) {
                const float front=std::max(kLightNear,depth_edges[static_cast<std::size_t>(slice)]-0.0001f);
                const float back=depth_edges[static_cast<std::size_t>(slice+1)]+0.0001f;
                glm::vec2 lo{1e10f}, hi{-1e10f};
                const auto projected_bounds = [&](const auto& vertices,glm::vec2& lower,glm::vec2& upper) {
                    const auto include = [&](glm::vec4 c) {
                        const glm::vec2 xy = glm::vec2{c}/c.w;
                        lower=glm::min(lower,xy); upper=glm::max(upper,xy);
                    };
                    for(const auto& c:vertices) if(c.w>=front && c.w<=back) include(c);
                    // Project only the cone section inside THIS depth band. A
                    // cone crossing the camera must not cover every far tile.
                    for(float plane:{front,back}) for(std::size_t a=0;a<vertices.size();++a)
                        for(std::size_t b=a+1;b<vertices.size();++b) {
                            if((vertices[a].w<plane)==(vertices[b].w<plane)) continue;
                            const float t=(plane-vertices[a].w)/(vertices[b].w-vertices[a].w);
                            include(glm::mix(vertices[a],vertices[b],t));
                        }
                };
                projected_bounds(clip,lo,hi);
                glm::vec2 sector_lower{1e10f},sector_upper{-1e10f};
                projected_bounds(sector_clip,sector_lower,sector_upper);
                lo=glm::max(lo,sector_lower);hi=glm::min(hi,sector_upper);
                if(lo.x>hi.x || lo.y>hi.y)continue;
                if(hi.x < -1 || hi.y < -1 || lo.x > 1 || lo.y > 1) continue;
                lo=glm::clamp(lo,glm::vec2{-1},glm::vec2{1});
                hi=glm::clamp(hi,glm::vec2{-1},glm::vec2{1});
                const auto tile=[](float n,int pixels,int count) {
                    return std::clamp(static_cast<int>((n*0.5f+0.5f)*static_cast<float>(pixels)/
                        static_cast<float>(kLightTilePixels)),0,count-1);
                };
                spans.push_back({tile(lo.x,width,columns),tile(hi.x,width,columns),
                    tile(lo.y,height,rows),tile(hi.y,height,rows),slice,slice,static_cast<uint32_t>(li)});
                added=true;
            }
            if(added) ++visible_lights;
        }
        const auto each_cell = [&](const Span& s,auto visit) {
            for(int z=s.z0;z<=s.z1;++z) for(int y=s.y0;y<=s.y1;++y) for(int x=s.x0;x<=s.x1;++x)
                visit(static_cast<std::size_t>((z*rows+y)*columns+x));
        };
        for(const auto& s:spans) each_cell(s,[&](std::size_t c){++cells[c].y;});
        uint32_t offset=0;
        for(auto& c:cells) { c.x=offset; offset+=c.y; max_cell_lights=std::max(max_cell_lights,c.y); }
        indices.resize(offset);
        std::vector<uint32_t> cursor(cells.size(),0);
        for(const auto& s:spans) each_cell(s,[&](std::size_t c){indices[cells[c].x+cursor[c]++]=s.light;});
    }
};
} // namespace apricot
