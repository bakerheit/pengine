#pragma once
#include <array>
#include <string_view>
#include "app/vehicle_lamp_mesh.h"
#include "game/license_plate.h"

namespace apricot {
struct VehiclePlateMount {
    glm::vec3 centre{0};
    glm::vec3 normal{0,0,1};
    float width=.305f, height=.152f;
};
using VehiclePlateMounts = std::vector<VehiclePlateMount>;

// Native source coordinates: +Z nose, +Y up. Authored mounts win; legacy
// bodies are probed across the plate footprint so a bumper can't bury a corner.
inline VehiclePlateMounts vehicle_plate_mounts(const StaticEmesh& body, std::string_view path) {
    if (path.find("rodeo_grazer/")!=std::string_view::npos)
        return {{{0,.625f,2.446f},{0,0,1}},{{0,.560f,-2.646f},{0,0,-1}}};
    if (path.find("fang_venom_v2/")!=std::string_view::npos)
        return {{{0,.715f,-1.134f},{0,0,-1},.240f,.110f}};
    VehiclePlateMounts result;
    const bool bike=path.find("fang_venom")!=std::string_view::npos;
    for (const bool front:{true,false}) {
        if (bike && front) continue;
        VehiclePlateMount m;
        if (bike) { m.width=.23f; m.height=.115f; }
        m.centre={body.bounds.center().x,
            body.bounds.min.y+body.bounds.size().y*(bike?.36f:.24f),0};
        m.normal={0,0,front?1.f:-1.f};
        float z=0;
        bool found=false;
        // Require body behind the entire plate, not just its centre. Start
        // at the bumper and move up when the footprint crosses an open gap.
        const float preferred_y=m.centre.y;
        for (int attempt=0;attempt<32 && !found;++attempt) {
            const float y=preferred_y+static_cast<float>(attempt)*.025f;
            float upper=0,lower=0;
            if (!vehicle_body_surface_z(body,m.centre.x,y+m.height*.5f,front,upper) ||
                !vehicle_body_surface_z(body,m.centre.x,y-m.height*.5f,front,lower)) continue;
            const float slope=(upper-lower)/m.height;
            const float sign=front?1.f:-1.f;
            const glm::vec3 normal=glm::normalize(glm::vec3{0,-slope*sign,sign});
            if (std::fabs(normal.z)<.65f) continue; // A plate belongs on the fascia, not the hood.
            const glm::vec3 up{0,std::fabs(normal.z),-normal.y*sign};
            bool covered=true;
            float low=std::numeric_limits<float>::max(),high=-low;
            for (int ix=-2;ix<=2 && covered;++ix) for (int iy=-2;iy<=2;++iy) {
                const glm::vec3 offset=glm::vec3{static_cast<float>(ix)*m.width*.25f,0,0}+
                    up*(static_cast<float>(iy)*m.height*.25f);
                float sample=0;
                if (!vehicle_body_surface_z(body,m.centre.x+offset.x,y+offset.y,front,sample)) { covered=false;break; }
                low=std::min(low,sample-offset.z);high=std::max(high,sample-offset.z);
            }
            if (covered && high-low<.045f) {
                m.centre.y=y;m.normal=normal;z=front?high:low;found=true;
            }
        }
        if (!found) continue;
        m.centre.z=z+(front?.006f:-.006f);
        result.push_back(m);
    }
    return result;
}

inline MeshData make_vehicle_plate_mesh(const VehiclePlateMounts& mounts,
                                       const VehicleRegistration& plate) {
    MeshData mesh;
    if (!valid_registration(plate)) return mesh;
    const auto design=plate_design_index(plate.state,plate.series);
    const std::string serial=plate_serial(plate);
    for (const auto& m:mounts) {
        const glm::vec3 right=glm::normalize(glm::cross(glm::vec3{0,1,0},m.normal));
        const glm::vec3 up=glm::cross(m.normal,right);
        const auto quad=[&](float x0,float y0,float x1,float y1,
                            float tx,float ty,float tw,float th,float depth) {
            const auto start=static_cast<uint32_t>(mesh.vertices.size());
            const std::array<glm::vec2,4> corners{{{x0,y0},{x1,y0},{x1,y1},{x0,y1}}};
            const std::array<glm::vec2,4> uv{{{tx,ty+th},{tx+tw,ty+th},{tx+tw,ty},{tx,ty}}};
            for (std::size_t i=0;i<4;++i) {
                const glm::vec3 pos=m.centre+right*corners[i].x+up*corners[i].y+m.normal*depth;
                mesh.vertices.push_back({pos,m.normal,{uv[i].x/2048.f,1.f-uv[i].y/2048.f},{1,0,0,0}});
                mesh.bounds.expand(pos);
            }
            for (const uint32_t i:{0u,1u,2u,0u,2u,3u}) mesh.indices.push_back(start+i);
        };
        // Use a continuous grid: shared edge positions also stay watertight
        // when the PSX vertex snap moves them. No glyph overlays or T-junctions.
        std::vector<float> columns{0};
        for (std::size_t i=0;i<=serial.size();++i)
            columns.push_back(40.f+432.f*static_cast<float>(i)/static_cast<float>(serial.size()));
        columns.push_back(512);
        constexpr std::array<float,4> rows{0,84,180,256};
        for (std::size_t row=0;row<3;++row) for (std::size_t col=0;col+1<columns.size();++col) {
            const float x=columns[col],y=rows[row],w=columns[col+1]-x,h=rows[row+1]-y;
            const bool glyph=row==1u && col>0u && col<=serial.size();
            const float tx=glyph?static_cast<float>(kPlateGlyphs.find(serial[col-1]))*48.f:
                static_cast<float>(design%2)*512.f+x;
            const float ty=glyph?1040.f+static_cast<float>(design)*128.f:
                static_cast<float>(design/2)*256.f+y;
            quad(m.width*(x/512.f-.5f),m.height*(.5f-rows[row+1]/256.f),
                m.width*(columns[col+1]/512.f-.5f),m.height*(.5f-y/256.f),
                tx,ty,glyph?48.f:w,glyph?96.f:h,0);
        }
    }
    return mesh;
}
} // namespace apricot
