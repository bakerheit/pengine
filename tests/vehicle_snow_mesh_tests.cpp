#include <algorithm>
#include <cmath>
#include <string>

#include "app/player_car_catalog.h"
#include "app/vehicle_snow_mesh.h"
#include "app/vehicle_driver_pose.h"
#include "core/asset_root.h"
#include "test_assert.h"

using namespace apricot;

namespace {
void geometry_is_unchanged(const StaticEmesh& source,const MeshData& tagged) {
    REQUIRE(source.indices.size()==tagged.indices.size());
    REQUIRE(source.bounds.min==tagged.bounds.min);
    REQUIRE(source.bounds.max==tagged.bounds.max);
    for(std::size_t i=0;i<source.indices.size();++i) {
        const auto& a=source.vertices[source.indices[i]];
        const auto& b=tagged.vertices[tagged.indices[i]];
        REQUIRE(b.position==glm::vec3(a.px,a.py,a.pz));
        REQUIRE(b.normal==glm::vec3(a.nx,a.ny,a.nz));
        REQUIRE(b.uv==glm::vec2(a.u,a.v));
        REQUIRE(std::isfinite(b.material_weights.x));
        REQUIRE(std::isfinite(b.material_weights.y));
        REQUIRE(std::isfinite(b.material_weights.z));
    }
}

void every_enclosed_live_model_has_a_windshield_without_changing_geometry() {
    std::size_t models=0;
    for(const auto& car:kPlayerCars) {
        if(is_motorbike(car.id)) continue;
        StaticEmesh source;
        REQUIRE(read_static_emesh(asset_path(car.mesh_path),source));
        const auto* profile=vehicle_windshield_profile(car.mesh_path);
        REQUIRE(profile!=nullptr);
        const auto tagged=make_vehicle_snow_mesh(source,car.mesh_path);
        geometry_is_unchanged(source,tagged);
        std::size_t panes=0,other=0,protected_hood=0,protected_roof=0;
        for(std::size_t i=0;i<tagged.indices.size();i+=3u) {
            const auto& a=tagged.vertices[tagged.indices[i]];
            const auto& b=tagged.vertices[tagged.indices[i+1]];
            const auto& c=tagged.vertices[tagged.indices[i+2]];
            REQUIRE(a.material_weights.w==b.material_weights.w);
            REQUIRE(a.material_weights.w==c.material_weights.w);
            const auto center=(a.position+b.position+c.position)/3.f;
            if(a.material_weights.w<0.f) {
                ++panes;
                REQUIRE(a.material_weights.z>0.f);
                REQUIRE_NEAR(a.material_weights.z,b.material_weights.z,.000001f);
            } else {
                ++other;
                REQUIRE(a.material_weights==glm::vec4(1,0,0,0));
            }
            const auto cross=glm::cross(b.position-a.position,c.position-a.position);
            if(glm::length(cross)<1e-8f) continue;
            const auto normal=glm::normalize(cross);
            if(normal.y>.99f && center.y>profile->top_y) {
                REQUIRE(a.material_weights.w==0.f);
                ++protected_roof;
            }
            if(center.z>profile->bottom_z+.8f && normal.y>.85f) {
                REQUIRE(a.material_weights.w==0.f);
                ++protected_hood;
            }
        }
        REQUIRE_MSG(panes>0u,"live model has no tagged windshield",car.mesh_path);
        REQUIRE(other>panes);
        REQUIRE(protected_hood+protected_roof>0u);
        ++models;
    }
    REQUIRE(models>=26u);
}

void articulated_live_shells_keep_painted_windshields_tagged() {
    std::size_t opaque_panes=0,glass_shells=0;
    for(const auto& car:kPlayerCars) {
        if(!has_animated_driver(car.id) || is_motorbike(car.id)) continue;
        const std::string path=car.mesh_path;
        const std::string open_path=path.substr(0,path.find_last_of('/')+1)+(car.id==PlayerCarId::Bwc360 ? "body_drive.emesh" : "body_open.emesh");
        StaticEmesh source;
        REQUIRE(read_static_emesh(asset_path(open_path),source));
        const auto tagged=make_vehicle_snow_mesh(source,open_path);
        geometry_is_unchanged(source,tagged);
        const bool separate_glass=car.id==PlayerCarId::GlmMeridian || car.id==PlayerCarId::RodeoSwitchback || car.id==PlayerCarId::HarrowHookline ||
            car.id==PlayerCarId::Bwc360 || car.id==PlayerCarId::HarrowWorkman || car.id==PlayerCarId::EmberGt || car.id==PlayerCarId::RodeoGrazer ||
            car.id==PlayerCarId::AlderPip || car.id==PlayerCarId::LegacyCar5Next ||
            car.id==PlayerCarId::LegacyCar5NextPolice ||
            is_municipal_cruiser_91(car.id);
        if(separate_glass) {
            ++glass_shells;
            continue; // The separately loaded named pane owns snow and wiping.
        }
        const auto pane_vertices=std::count_if(tagged.vertices.begin(),tagged.vertices.end(),
            [](const auto& vertex) {return vertex.material_weights.w<0.f;});
        REQUIRE_MSG(pane_vertices>0,"articulated shell lost its painted windshield",open_path.c_str());
        ++opaque_panes;
    }
    // Mistral, Scythe and Sovereign paint their screens on the shell instead
    // of loading a named pane.
    REQUIRE(opaque_panes==3u);
    REQUIRE(glass_shells==15u);
}

void named_glass_uses_exact_pane_bounds_and_unknowns_fail_closed() {
    for(const auto* name:{"bwc_360","rodeo_grazer","ember_gt","alder_pip","car5_next","car5_next_police","harrow_workman","glm_meridian","rodeo_switchback","harrow_hookline","municipal_cruiser_91a",
            "municipal_cruiser_91b","municipal_cruiser_91c","municipal_cruiser_91d",
            "municipal_cruiser_91e"}) {
        StaticEmesh pane;
        REQUIRE(read_static_emesh(asset_path(std::string("models/vehicles/")+name+"/windshield.emesh"),pane));
        const auto tagged=make_windshield_snow_mesh(pane);
        geometry_is_unchanged(pane,tagged);
        const auto size=pane.bounds.size();
        for(const auto& vertex:tagged.vertices) {
            const auto metadata=vertex.material_weights;
            REQUIRE(metadata.w==-1.f);
            REQUIRE(metadata.x>=0.f && metadata.x<=1.f);
            REQUIRE(metadata.y>=0.f && metadata.y<=1.f);
            REQUIRE_NEAR(metadata.z,size.x/std::hypot(size.y,size.z),.000001f);
        }
        const auto unknown=make_vehicle_snow_mesh(pane,"models/buildings/unrelated/body.emesh");
        geometry_is_unchanged(pane,unknown);
        for(const auto& vertex:unknown.vertices)
            REQUIRE(vertex.material_weights==glm::vec4(1,0,0,0));
    }
    REQUIRE(vehicle_windshield_profile("models/vehicles/car5_lookalike/body.emesh")==nullptr);
}

void shared_corners_do_not_smear_pane_metadata_and_tall_receivers_work() {
    StaticEmesh source;
    source.vertices={
        {-.5f,1.5f,.611f,0,0,1,0,0,1,0,0,1},
        { .5f,1.5f,.611f,0,0,1,1,0,1,0,0,1},
        { .5f,2.0f,.611f,0,0,1,1,1,1,0,0,1},
        { .5f,2.0f,-.6f,1,0,0,0,1,1,0,0,1}};
    source.indices={0,1,2,1,3,2};
    for(const auto& vertex:source.vertices)source.bounds.expand({vertex.px,vertex.py,vertex.pz});
    const auto tagged=make_vehicle_snow_mesh(source,"models/vehicles/halcyon_six/body_surface.emesh");
    geometry_is_unchanged(source,tagged);
    for(int i=0;i<3;++i) REQUIRE(tagged.vertices[static_cast<std::size_t>(i)].material_weights.w==-1.f);
    for(int i=3;i<6;++i) REQUIRE(tagged.vertices[static_cast<std::size_t>(i)].material_weights.w==0.f);
    // The lower Hauler screen lives on a full-height front cab receiver. Its
    // bumper vertices lie far from the sloped upper-pane reference plane.
    const auto* profile=vehicle_windshield_profile("models/vehicles/harrow_hauler/body.emesh");
    REQUIRE(profile!=nullptr);
    REQUIRE(vehicle_snow_detail::windshield_triangle(
        {glm::vec3{-.9f,1.1f,1.4f},glm::vec3{.9f,1.1f,1.4f},glm::vec3{.9f,2.85f,1.4f}},*profile));
}
}

int main() {
    every_enclosed_live_model_has_a_windshield_without_changing_geometry();
    articulated_live_shells_keep_painted_windshields_tagged();
    named_glass_uses_exact_pane_bounds_and_unknowns_fail_closed();
    shared_corners_do_not_smear_pane_metadata_and_tall_receivers_work();
    return 0;
}
