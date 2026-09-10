#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "physics/snow_shelter.h"

namespace apricot {

// Static world-space lookup for shelter shading. Every roof and every overlap
// survives: there is no nearest-roof budget that can make a far interior snow.
// The four texels preserve the CPU broad bounds, yaw footprint and underside.
struct SnowShelterGrid {
    float cell_size = 32.f;
    glm::vec2 origin{0.f};
    int columns = 0;
    int rows = 0;
    std::vector<glm::vec4> roofs;
    std::vector<glm::uvec2> cells;
    std::vector<uint32_t> indices;

    bool build(const SnowShelterField& field,
               std::size_t max_texels = 1u << 24) {
        roofs.clear(); cells.clear(); indices.clear();
        origin=glm::vec2{0.f}; columns=0; rows=0; cell_size=32.f;
        const auto& boxes=field.boxes();
        if (boxes.empty()) return true;
        if (boxes.size()>max_texels/4u || max_texels==0u) return false;
        AABB world;
        for (const auto& box:boxes) {
            world.expand(box.bounds.min); world.expand(box.bounds.max);
            const AABB& local=box.collision_bounds();
            roofs.push_back({local.min.x,local.min.z,local.max.x,local.max.z});
            roofs.push_back(box.oriented
                ? glm::vec4{box.axis_x,box.axis_z} : glm::vec4{1,0,0,1});
            roofs.push_back({box.oriented ? box.centre.x : 0.f,
                             box.oriented ? box.centre.z : 0.f,
                             box.bounds.min.y-SnowShelterField::kRoofClearanceM,0.f});
            roofs.push_back({box.bounds.min.x,box.bounds.min.z,
                             box.bounds.max.x,box.bounds.max.z});
        }
        // Usually 32 m. Extremely broad authored worlds can use coarser cells
        // to fit hardware capacity while retaining every roof and exact mask.
        for (int attempt=0;attempt<64;++attempt,cell_size*=2.f) {
            if (!std::isfinite(cell_size)) return false;
            origin=glm::floor(glm::vec2{world.min.x,world.min.z}/cell_size)*cell_size;
            const double cols=std::floor((static_cast<double>(world.max.x)-origin.x)/cell_size)+1.;
            const double lines=std::floor((static_cast<double>(world.max.z)-origin.y)/cell_size)+1.;
            if (!(cols>=1. && lines>=1.) || cols*lines>static_cast<double>(max_texels) ||
                cols>std::numeric_limits<int>::max() || lines>std::numeric_limits<int>::max()) continue;
            columns=static_cast<int>(cols); rows=static_cast<int>(lines);
            cells.assign(static_cast<std::size_t>(columns)*static_cast<std::size_t>(rows),{0u,0u});
            const auto cell_coord=[&](float value,float start,int count) {
                return std::clamp(static_cast<int>(std::floor(
                    (static_cast<double>(value)-start)/cell_size)),0,count-1);
            };
            const auto each_cell=[&](const StaticBox& box,auto visit) {
                const int x0=cell_coord(box.bounds.min.x,origin.x,columns);
                const int x1=cell_coord(box.bounds.max.x,origin.x,columns);
                const int z0=cell_coord(box.bounds.min.z,origin.y,rows);
                const int z1=cell_coord(box.bounds.max.z,origin.y,rows);
                for (int z=z0;z<=z1;++z) for(int x=x0;x<=x1;++x)
                    visit(static_cast<std::size_t>(z)*static_cast<std::size_t>(columns)+
                          static_cast<std::size_t>(x));
            };
            for (const auto& box:boxes) each_cell(box,[&](std::size_t cell) { ++cells[cell].y; });
            std::size_t total=0;
            for(auto& cell:cells) { cell.x=static_cast<uint32_t>(total); total+=cell.y; }
            if(total>max_texels || total>std::numeric_limits<uint32_t>::max()) continue;
            indices.resize(total);
            std::vector<uint32_t> cursor(cells.size(),0u);
            for(std::size_t i=0;i<boxes.size();++i)
                each_cell(boxes[i],[&](std::size_t cell) {
                    indices[cells[cell].x+cursor[cell]++]=static_cast<uint32_t>(i);
                });
            return true;
        }
        columns=0; rows=0; cells.clear(); indices.clear();
        return false;
    }

    // Evaluate the packed producer data with the same equations as lit.frag.
    // This pins packing, broad-phase inclusion and exact roof-edge behavior.
    bool covered(glm::vec3 point) const {
        if(columns==0 || rows==0 || !std::isfinite(point.x) ||
            !std::isfinite(point.y) || !std::isfinite(point.z)) return false;
        const glm::vec2 cell=glm::floor((glm::vec2{point.x,point.z}-origin)/cell_size);
        if(cell.x<0 || cell.y<0 || cell.x>=static_cast<float>(columns) ||
            cell.y>=static_cast<float>(rows)) return false;
        const auto span=cells[static_cast<std::size_t>(cell.y)*static_cast<std::size_t>(columns)+
                              static_cast<std::size_t>(cell.x)];
        for(uint32_t i=0;i<span.y;++i) {
            const std::size_t base=static_cast<std::size_t>(indices[span.x+i])*4u;
            const auto local=roofs[base]; const auto axes=roofs[base+1];
            const auto anchor=roofs[base+2]; const auto broad=roofs[base+3];
            if(point.y>=anchor.z || point.x<broad.x || point.z<broad.y ||
                point.x>broad.z || point.z>broad.w) continue;
            const glm::vec2 delta=glm::vec2{point.x,point.z}-glm::vec2{anchor};
            const glm::vec2 p{glm::dot(delta,glm::vec2{axes.x,axes.y}),
                              glm::dot(delta,glm::vec2{axes.z,axes.w})};
            if(p.x>=local.x && p.y>=local.y && p.x<=local.z && p.y<=local.w) return true;
        }
        return false;
    }
};

} // namespace apricot
