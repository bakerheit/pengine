#include <cmath>
#include <limits>
#include "app/snowplow_mesh.h"
#include "gfx/snow_clearance_visual.h"
#include "test_assert.h"

using namespace apricot;

namespace {
void refilling_cover_tracks_current_main_height() {
    // The old absolute-depth mapping was already opaque here despite seven
    // eighths of the snow still missing from the plowed road.
    REQUIRE_NEAR(visual_snow_cover_from_depth(.1), 1.f, .00001f);
    const float partial=snow_clearance_visual_cover(.1,.8);
    REQUIRE(partial>0.f && partial<.1f);
    REQUIRE_NEAR(snow_clearance_visual_cover(.4,.8), .5f, .00001f);
    REQUIRE(snow_clearance_visual_cover(.79,.8)<1.f);
    REQUIRE_NEAR(snow_clearance_visual_cover(.8,.8),1.f,.00001f);
    REQUIRE_NEAR(snow_clearance_visual_cover(1.2,.8),1.f,.00001f);

    // Follow changing main depth, not the pack recorded when the blade passed.
    REQUIRE(snow_clearance_visual_cover(.4,1.2)<
            snow_clearance_visual_cover(.4,.8));
    REQUIRE(snow_clearance_visual_cover(.4,.6)>
            snow_clearance_visual_cover(.4,.8));
    float previous=0.f;
    for (int i=0;i<=80;++i) {
        const float cover=snow_clearance_visual_cover(i*.01,.8);
        REQUIRE(cover>=previous && cover<=1.f);
        previous=cover;
    }

    // Thin snow still joins the exact surrounding material instead of turning
    // opaque at convergence, and a real residual dusting is not zeroed out.
    REQUIRE_NEAR(snow_clearance_visual_cover(.05,.05),
                 visual_snow_cover_from_depth(.05),.00001f);
    REQUIRE_NEAR(snow_clearance_visual_cover(.025,.05),.25f,.00001f);
    REQUIRE(snow_clearance_visual_cover(.008,.8)>0.f);
    REQUIRE_NEAR(snow_clearance_visual_cover(0.,.8),0.f,.00001f);
    REQUIRE_NEAR(snow_clearance_visual_cover(.1,0.),0.f,.00001f);

    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    for(double invalid:{nan,inf,-inf,-.1}) {
        REQUIRE_NEAR(snow_clearance_visual_cover(invalid,.8),1.f,.00001f);
        REQUIRE_NEAR(snow_clearance_visual_cover(invalid,.05),
                     visual_snow_cover_from_depth(.05),.00001f);
    }
    for(double invalid:{nan,inf,-inf})
        REQUIRE_NEAR(snow_clearance_visual_cover(.1,invalid),1.f,.00001f);
}
}

int main() {
    refilling_cover_tracks_current_main_height();
    const auto meshes=make_snowplow_meshes();
    AABB bounds;
    std::size_t triangles=0;
    for (const auto& mesh:meshes) {
        REQUIRE(mesh.bounds.valid());
        REQUIRE(!mesh.indices.empty());
        REQUIRE(mesh.indices.size()%3u==0u);
        for (const auto& vertex:mesh.vertices) {
            REQUIRE(std::isfinite(vertex.position.x));
            REQUIRE(std::isfinite(vertex.position.y));
            REQUIRE(std::isfinite(vertex.position.z));
            REQUIRE_NEAR(glm::length(vertex.normal),1.f,.0001f);
            bounds.expand(vertex.position);
        }
        for (std::size_t i=0;i<mesh.indices.size();i+=3) {
            REQUIRE(mesh.indices[i]<mesh.vertices.size());
            REQUIRE(mesh.indices[i+1]<mesh.vertices.size());
            REQUIRE(mesh.indices[i+2]<mesh.vertices.size());
            const auto& a=mesh.vertices[mesh.indices[i]];
            const auto& b=mesh.vertices[mesh.indices[i+1]];
            const auto& c=mesh.vertices[mesh.indices[i+2]];
            const auto cross=glm::cross(b.position-a.position,c.position-a.position);
            REQUIRE(glm::length(cross)>0.f);
            REQUIRE(glm::dot(cross,a.normal)>0.f);
            ++triangles;
        }
    }
    const auto footprint=traffic_vehicle_footprint(TrafficVehicleKind::Snowplow);
    REQUIRE(bounds.min.x>=-footprint.half_width_m-.001f);
    REQUIRE(bounds.max.x<=footprint.half_width_m+.001f);
    REQUIRE(bounds.min.z>=-footprint.half_length_m-.001f);
    REQUIRE(bounds.max.z<=footprint.half_length_m+.001f);
    REQUIRE(bounds.min.y>=0.f);
    REQUIRE_NEAR(bounds.size().x,kSnowplowBladeWidthM,.001f);
    REQUIRE(bounds.min.z<-kSnowplowBladeForwardM);
    REQUIRE(triangles>500u);
    const auto layout=make_snowplow_visual_layout(bounds);
    for (const auto& wheel:layout.wheel_centres) {
        REQUIRE_NEAR(wheel.y-layout.wheel_radius,0.f,.0001f);
        REQUIRE(std::abs(wheel.x)<footprint.half_width_m);
        REQUIRE(std::abs(wheel.z)+layout.wheel_radius<footprint.half_length_m);
    }
    for(std::size_t side=0;side<2;++side) {
        REQUIRE(snowplow_beacon_transform(side).position.y>bounds.max.y);
        int bright=0,dim=0;
        for(int64_t step=0;step<108;++step) {
            const float power=snowplow_beacon_power(step,side);
            REQUIRE_NEAR(power,snowplow_beacon_power(step+108,side),.0001f);
            if(power>.5f) ++bright; else ++dim;
        }
        REQUIRE(bright>0 && dim>0);
    }
    REQUIRE(snowplow_beacon_power(0,0)!=snowplow_beacon_power(0,1));
    return 0;
}
