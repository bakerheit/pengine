#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <vector>
#include "road/ribbon.h"

namespace apricot::detail {
inline constexpr uint32_t kJunctionSurface = std::numeric_limits<uint32_t>::max();
using WalkPolygon = std::vector<TerrainVertex>;

inline double walk_cross(glm::dvec2 a, glm::dvec2 b) {
    return a.x*b.y-a.y*b.x;
}
inline glm::dvec2 walk_xz(const TerrainVertex& v) { return {v.position.x,v.position.z}; }
inline TerrainVertex walk_lerp(const TerrainVertex& a,const TerrainVertex& b,double t) {
    const float f=static_cast<float>(t);
    return {glm::mix(a.position,b.position,f),glm::mix(a.normal,b.normal,f),
        glm::mix(a.uv,b.uv,f),glm::mix(a.material_weights,b.material_weights,f)};
}
inline WalkPolygon walk_half_plane(const WalkPolygon& poly,glm::dvec2 a,glm::dvec2 b,
                                   double orientation,bool inside) {
    WalkPolygon out;
    if(poly.empty())return out;
    auto distance=[&](const TerrainVertex& v) {
        // One millimetre of overlap between adjacent cutter triangles avoids
        // hairline remnants when world-space intersections round to float.
        const double d=orientation*walk_cross(b-a,walk_xz(v)-a)+.001*glm::length(b-a);
        return inside?d:-d;
    };
    auto previous=poly.back();double pd=distance(previous);
    for(const auto& current:poly) {
        const double cd=distance(current);
        if((pd>=0)!=(cd>=0))out.push_back(walk_lerp(previous,current,pd/(pd-cd)));
        if(cd>=0)out.push_back(current);
        previous=current;pd=cd;
    }
    return out;
}
struct WalkCutter {
    std::array<TerrainVertex,3> v;
    glm::dvec2 min,max;
    double orientation;
    uint32_t owner;
};

// Raised pavement may overlap a DIFFERENT road at a shallow merge even when
// both ribbons have sensible widths and junction trims. Subtract the actual
// driving footprint, retaining the sidewalk outside it. Mesh and collision
// both consume this result. Same-road kerb seams and grade-separated surfaces
// are deliberately retained.
inline void clip_walks_from_roads(RibbonBake& bake,const std::vector<uint32_t>& road_owners,
                                  const std::vector<uint32_t>& walk_owners,
                                  const std::vector<uint32_t>& kerb_owners,
                                  const GroundSampler& ground) {
    constexpr double cell=32.0;
    std::vector<WalkCutter> cutters;
    std::map<std::pair<int,int>,std::vector<std::size_t>> grid;
    auto cell_at=[](double v){return static_cast<int>(std::floor(v/cell));};
    for(auto layer:{RoadLayer::Carriageway,RoadLayer::Plate}) {
        const auto& m=bake.layer(layer);
        for(std::size_t i=0;i<m.indices.size();i+=3) {
            WalkCutter c{{m.vertices[m.indices[i]],m.vertices[m.indices[i+1]],m.vertices[m.indices[i+2]]},
                {},{},0,layer==RoadLayer::Carriageway?road_owners[i/3]:kJunctionSurface};
            c.min=c.max=walk_xz(c.v[0]);
            for(const auto& v:c.v){c.min=glm::min(c.min,walk_xz(v));c.max=glm::max(c.max,walk_xz(v));}
            const double area=walk_cross(walk_xz(c.v[1])-walk_xz(c.v[0]),walk_xz(c.v[2])-walk_xz(c.v[0]));
            if(std::fabs(area)<1e-9)continue;
            c.orientation=area>0?1:-1;
            const auto id=cutters.size();cutters.push_back(c);
            for(int x=cell_at(c.min.x);x<=cell_at(c.max.x);++x)
                for(int z=cell_at(c.min.y);z<=cell_at(c.max.y);++z)grid[{x,z}].push_back(id);
        }
    }
    for(auto layer:{RoadLayer::Walk,RoadLayer::Kerb}) {
        auto& m=bake.layer(layer);
        const auto& owners=layer==RoadLayer::Walk?walk_owners:kerb_owners;
        RoadMesh result;
        std::vector<uint32_t> retained(m.vertices.size(),kJunctionSurface);
        for(std::size_t i=0;i<m.indices.size();i+=3) {
            WalkPolygon triangle{m.vertices[m.indices[i]],m.vertices[m.indices[i+1]],m.vertices[m.indices[i+2]]};
            const bool draped=layer==RoadLayer::Walk && std::all_of(triangle.begin(),triangle.end(),
                [&](const auto& v){return std::fabs(v.position.y-ground.at(v.position.x,v.position.z)-
                    (kDrapeEpsM+kKerbHeightM))<.001f;});
            auto low=walk_xz(triangle[0]),high=low;
            for(const auto& v:triangle){low=glm::min(low,walk_xz(v));high=glm::max(high,walk_xz(v));}
            std::vector<std::size_t> candidates;
            for(int x=cell_at(low.x);x<=cell_at(high.x);++x)
                for(int z=cell_at(low.y);z<=cell_at(high.y);++z) {
                    const auto found=grid.find({x,z});
                    if(found!=grid.end())candidates.insert(candidates.end(),found->second.begin(),found->second.end());
                }
            std::sort(candidates.begin(),candidates.end());
            candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
            std::vector<WalkPolygon> pieces{triangle};
            const uint32_t owner=i/3<owners.size()?owners[i/3]:kJunctionSurface;
            for(const auto id:candidates) {
                const auto& c=cutters[id];
                if(owner!=kJunctionSurface && owner==c.owner)continue;
                if(c.max.x<low.x || c.min.x>high.x || c.max.y<low.y || c.min.y>high.y)continue;
                const auto normal=glm::cross(c.v[1].position-c.v[0].position,c.v[2].position-c.v[0].position);
                if(std::fabs(normal.y)<1e-8f)continue;
                std::vector<WalkPolygon> remaining;
                for(const auto& piece:pieces) {
                    glm::vec3 centre{0};for(const auto& v:piece)centre+=v.position;
                    centre/=static_cast<float>(piece.size());
                    const auto delta=centre-c.v[0].position;
                    const float above=delta.y+(normal.x*delta.x+normal.z*delta.z)/normal.y;
                    if(above<-.04f || above>.45f){remaining.push_back(piece);continue;}
                    auto inside=piece;
                    for(std::size_t e=0;e<3 && !inside.empty();++e) {
                        auto outside=walk_half_plane(inside,walk_xz(c.v[e]),walk_xz(c.v[(e+1)%3]),c.orientation,false);
                        if(outside.size()>=3)remaining.push_back(std::move(outside));
                        inside=walk_half_plane(inside,walk_xz(c.v[e]),walk_xz(c.v[(e+1)%3]),c.orientation,true);
                    }
                }
                pieces=std::move(remaining);
                if(pieces.empty())break;
            }
            if(pieces.size()==1 && pieces[0].size()==3 &&
               pieces[0][0].position==triangle[0].position &&
               pieces[0][1].position==triangle[1].position &&
               pieces[0][2].position==triangle[2].position) {
                for(std::size_t j=0;j<3;++j) {
                    const auto original=m.indices[i+j];
                    if(retained[original]==kJunctionSurface) {
                        retained[original]=static_cast<uint32_t>(result.vertices.size());
                        result.vertices.push_back(m.vertices[original]);
                    }
                    result.indices.push_back(retained[original]);
                }
                continue;
            }
            for(auto& piece:pieces) {
              if(draped)for(auto& v:piece)v.position.y=ground.at(v.position.x,v.position.z)+kDrapeEpsM+kKerbHeightM;
              if(layer==RoadLayer::Kerb) {
                // Reproject intersections onto the original vertical wall.
                // Otherwise float rounding can tilt extremely thin remnants.
                glm::dvec2 axis=walk_xz(triangle[1])-walk_xz(triangle[0]);
                const auto other=walk_xz(triangle[2])-walk_xz(triangle[0]);
                if(glm::dot(other,other)>glm::dot(axis,axis))axis=other;
                const double length2=glm::dot(axis,axis);
                if(length2>1e-12)for(auto& v:piece) {
                    const auto p=walk_xz(triangle[0])+axis*(glm::dot(walk_xz(v)-walk_xz(triangle[0]),axis)/length2);
                    v.position.x=static_cast<float>(p.x);v.position.z=static_cast<float>(p.y);
                }
              }
              for(std::size_t j=1;j+1<piece.size();++j) {
                const auto cross=glm::cross(piece[j].position-piece[0].position,piece[j+1].position-piece[0].position);
                if(glm::dot(cross,cross)<1e-12f)continue;
                if(layer==RoadLayer::Walk && std::fabs(cross.y)<1e-7f)continue;
                if(layer==RoadLayer::Kerb && std::fabs(cross.y)>.01f*glm::length(cross))continue;
                const auto base=static_cast<uint32_t>(result.vertices.size());
                result.vertices.insert(result.vertices.end(),{piece[0],piece[j],piece[j+1]});
                if(layer==RoadLayer::Walk && cross.y<0)
                    result.indices.insert(result.indices.end(),{base,base+2,base+1});
                else result.indices.insert(result.indices.end(),{base,base+1,base+2});
              }
            }
        }
        m=std::move(result);
    }
}
} // namespace apricot::detail
