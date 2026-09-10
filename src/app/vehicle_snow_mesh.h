#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

#include "core/emesh_reader.h"
#include "gfx/primitives.h"

namespace apricot {

struct VehicleWindshieldProfile {
    std::string_view model;
    float half_width;
    float bottom_y, top_y;
    float bottom_z, top_z;
    float plane_tolerance = .035f;
    float normal_alignment = .93f;
};

// Source-space glass outlines from the actual vehicle generators/paint recipes.
// The seven *_surface cooks project their glass onto body receiver faces, so
// those profiles allow the measured receiver offset; UV atlas coordinates are
// intentionally absent because the surface bake repacks those charts.
// Coordinates are Apricot source XYZ (+Z forward), after Blender export.
inline constexpr std::array<VehicleWindshieldProfile,26> kVehicleWindshields{{
    {"alder_pip",.66f,.98f,1.38f,.572f,.258f},
    {"alder_ridge",.80f,1.36f,1.88f,.607f,.403f},
    {"alder_wayfarer",.81f,1.19f,1.65f,.7432f,.3074f},
    {"glr_lunge",.87f,.86f,1.325f,1.265f,.605f,.19f},
    {"glr_zip",.71f,.83f,1.245f,1.015f,.425f,.19f,.90f},
    {"halcyon_six",.79f,1.51f,2.02f,.611f,.611f,.035f},
    {"halcyon_sovereign",.80f,1.16f,1.55f,1.6242f,1.2958f},
    {"harrow_cityliner",1.06f,1.45f,2.49f,5.4f,5.4f},
    {"harrow_hauler",.74f,2.26f,3.07f,1.4f,1.0975f,.23f,.80f},
    {"harrow_parcel",.82f,1.47f,2.22f,1.3675f,.8338f},
    {"harrow_workman",.855f,1.30f,1.885f,.955f,.566f,.08f},
    {"car5",1.04f,1.50f,1.96f,1.23f,.575f,.13f},
    {"car8",1.20f,2.08f,2.94f,2.105f,1.853f,.055f},
    {"montrose_regent_eight",.86f,1.48f,2.34f,.511f,.511f,.035f},
    {"ambulance",.835f,1.325f,1.805f,1.305f,.945f},
    {"municipal_cruiser_91a",.90f,1.075f,1.515f,.815f,.415f},
    {"municipal_cruiser_91b",.82f,1.075f,1.47f,.785f,.35f},
    {"municipal_cruiser_91c",.89f,1.025f,1.475f,.855f,.315f},
    {"municipal_cruiser_91d",.89f,1.105f,1.625f,.695f,.295f},
    {"municipal_cruiser_91e",.93f,1.035f,1.455f,.75f,.305f},
    {"firetruck",.95f,1.52f,2.27f,2.82f,2.82f},
    {"orison_cinder",.74f,.872f,1.188f,.597f,.014f,.035f},
    {"spagatti_shu",.64f,.87f,1.16f,.80f,.225f},
    {"vesper_mistral",.756f,.95f,1.401f,.489f,.061f},
    {"vesper_scythe",.68f,.77f,1.065f,.959f,.2715f},
    {"vesper_vx91",.69f,1.15f,1.665f,.405f,-.265f,.16f}
}};

inline const VehicleWindshieldProfile* vehicle_windshield_profile(std::string_view path) {
    const auto slash=path.find_last_of('/');
    if(slash==std::string_view::npos) return nullptr;
    const auto parent=path.substr(0,slash);
    const auto start=parent.find_last_of('/');
    const auto model=parent.substr(start==std::string_view::npos ? 0u : start+1u);
    for(const auto& profile:kVehicleWindshields)
        if(model==profile.model) return &profile;
    return nullptr;
}

namespace vehicle_snow_detail {
inline glm::vec3 position(const EmeshVertex& vertex) {
    return {vertex.px,vertex.py,vertex.pz};
}
inline glm::vec2 pane_coords(glm::vec3 point,const VehicleWindshieldProfile& profile) {
    // Linear body-local coordinates interpolate without moving geometry or
    // its diffuse UVs. Values outside 0..1 remain outside: a large receiver
    // triangle may also own sheet metal below/alongside the painted pane.
    return {.5f+point.x/(profile.half_width*2.f),
            (point.y-profile.bottom_y)/(profile.top_y-profile.bottom_y)};
}
inline float aspect(const VehicleWindshieldProfile& profile) {
    return profile.half_width*2.f/std::hypot(profile.top_y-profile.bottom_y,
                                           profile.top_z-profile.bottom_z);
}
inline bool windshield_triangle(const std::array<glm::vec3,3>& points,
                                 const VehicleWindshieldProfile& profile) {
    const glm::vec3 up{0.f,profile.top_y-profile.bottom_y,profile.top_z-profile.bottom_z};
    const glm::vec3 pane_normal=glm::normalize(glm::cross(glm::vec3{1,0,0},up));
    const glm::vec3 cross=glm::cross(points[1]-points[0],points[2]-points[0]);
    const float area=glm::length(cross);
    if(!(area>1e-8f) || std::abs(glm::dot(cross/area,pane_normal))<profile.normal_alignment)
        return false;
    // Clip only for classification. A single bus/truck receiver can run from
    // bumper to roof; test the plane at its pane section, not its bumper.
    std::vector<glm::vec3> clipped(points.begin(),points.end());
    const auto clip=[&](int axis,float edge,bool above) {
        if(clipped.empty()) return;
        std::vector<glm::vec3> next;
        glm::vec3 previous=clipped.back();
        bool previous_inside=above ? previous[axis]>=edge : previous[axis]<=edge;
        for(const auto point:clipped) {
            const bool inside=above ? point[axis]>=edge : point[axis]<=edge;
            if(inside!=previous_inside) {
                const float t=(edge-previous[axis])/(point[axis]-previous[axis]);
                next.push_back(glm::mix(previous,point,t));
            }
            if(inside) next.push_back(point);
            previous=point; previous_inside=inside;
        }
        clipped=std::move(next);
    };
    clip(0,-profile.half_width,true); clip(0,profile.half_width,false);
    clip(1,profile.bottom_y,true); clip(1,profile.top_y,false);
    if(clipped.size()<3u) return false;
    for(const auto point:clipped)
        if(std::abs(glm::dot(point-glm::vec3{0,profile.bottom_y,profile.bottom_z},pane_normal))>
            profile.plane_tolerance) return false;
    return true;
}
inline MeshVertex vertex(const EmeshVertex& source,glm::vec4 metadata) {
    return {{source.px,source.py,source.pz},{source.nx,source.ny,source.nz},
            {source.u,source.v},metadata};
}
}

// Only metadata changes. Flattening triangle indices keeps a shared corner of
// windshield/frame/hood from smearing the pane flag onto unrelated surfaces.
inline MeshData make_vehicle_snow_mesh(const StaticEmesh& source,std::string_view path) {
    MeshData out;
    out.bounds=source.bounds;
    out.vertices.reserve(source.indices.size());
    out.indices.reserve(source.indices.size());
    const auto* profile=vehicle_windshield_profile(path);
    for(std::size_t i=0;i+2u<source.indices.size();i+=3u) {
        const std::array<glm::vec3,3> points{
            vehicle_snow_detail::position(source.vertices[source.indices[i]]),
            vehicle_snow_detail::position(source.vertices[source.indices[i+1]]),
            vehicle_snow_detail::position(source.vertices[source.indices[i+2]])};
        const bool pane=profile && vehicle_snow_detail::windshield_triangle(points,*profile);
        for(std::size_t j=0;j<3u;++j) {
            glm::vec4 metadata{1.f,0.f,0.f,0.f};
            if(pane) metadata={vehicle_snow_detail::pane_coords(points[j],*profile),
                               vehicle_snow_detail::aspect(*profile),-1.f};
            out.indices.push_back(static_cast<uint32_t>(out.vertices.size()));
            out.vertices.push_back(vehicle_snow_detail::vertex(source.vertices[source.indices[i+j]],metadata));
        }
    }
    return out;
}

// Named real-glass panes already have their exact outline in geometry. Stamp
// both sides and the thin caps; the caller chooses only the front windshield.
inline MeshData make_windshield_snow_mesh(const StaticEmesh& source) {
    MeshData out;
    out.bounds=source.bounds;
    out.indices=source.indices;
    out.vertices.reserve(source.vertices.size());
    const auto size=source.bounds.size();
    const bool valid=source.bounds.valid() && std::isfinite(size.x) &&
        std::isfinite(size.y) && std::isfinite(size.z) && size.x>1e-5f && size.y>1e-5f;
    const float ratio=valid ? size.x/std::hypot(size.y,size.z) : 1.f;
    for(const auto& vertex:source.vertices) {
        glm::vec4 metadata{1.f,0.f,0.f,0.f};
        if(valid) metadata={(vertex.px-source.bounds.min.x)/size.x,
                           (vertex.py-source.bounds.min.y)/size.y,ratio,-1.f};
        out.vertices.push_back(vehicle_snow_detail::vertex(vertex,metadata));
    }
    return out;
}

} // namespace apricot
