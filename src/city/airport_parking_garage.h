#pragma once

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/airport.h"
#include "road/ribbon.h"

namespace apricot::city {

// The stepped footprint is exactly the existing two public parking pads plus
// their internal aisle. The terminal drop-off and both access bends stay open.
inline constexpr float kAirportGarageFloor = .13f;
inline constexpr float kAirportGarageRise = 4.f;
inline constexpr float kAirportGarageSlab = .30f;
inline constexpr float kAirportGarageRampX0 = -95.f;
inline constexpr float kAirportGarageRampX1 = -45.f;
inline constexpr float kAirportGarageRampZ = 198.f;
inline constexpr float kAirportGarageRampWidth = 8.f;

struct AirportGarageQuad {
    // Airport-local, counterclockwise viewed from above. These are the actual
    // visible ramp surfaces, not a separate approximation for physics.
    std::array<glm::vec3,4> corners;
};
struct AirportGarageBake {
    std::vector<StartPart> parts;
    std::vector<AirportGarageQuad> ramp_quads;
    RoadCollision surfaces; // world coordinates, append to the road bake
};

inline glm::vec3 airport_garage_world(glm::vec3 local) {
    return local+glm::vec3{kAirportSite.origin.x,kAirportSite.ground_m,
                           kAirportSite.origin.z};
}
inline float airport_garage_ramp_height(float x, int floor) {
    // 6m parabolic transitions at both ends, 38m constant grade. Integrating
    // the grade gives exactly 4m / (50m - 6m) = 9.09% at the steepest point.
    const float d=glm::clamp(x-kAirportGarageRampX0,0.f,50.f);
    constexpr float grade=kAirportGarageRise/44.f;
    const float rise=d<6.f ? grade*d*d/12.f :
        (d>44.f ? kAirportGarageRise-grade*(50.f-d)*(50.f-d)/12.f : grade*(d-3.f));
    return kAirportGarageFloor+static_cast<float>(floor)*kAirportGarageRise+rise;
}
inline bool airport_garage_replaces(const StartPart& p) {
    return std::strstr(p.name,"terminal parking island")!=nullptr ||
           std::strcmp(p.name,"terminal parking stripe")==0;
}
inline void append_airport_garage_collision(RoadCollision& target,
                                           const AirportGarageBake& garage) {
    target.triangles.insert(target.triangles.end(),garage.surfaces.triangles.begin(),
                            garage.surfaces.triangles.end());
    target.bounds.expand(garage.surfaces.bounds);
}

inline AirportGarageBake bake_airport_parking_garage() {
    AirportGarageBake out;
    auto surface=[&](const AirportGarageQuad& q) {
        for(const std::array<std::size_t,3> ids :
            {std::array<std::size_t,3>{0,1,2},std::array<std::size_t,3>{0,2,3}}) {
            const auto a=airport_garage_world(q.corners[ids[0]]);
            const auto b=airport_garage_world(q.corners[ids[1]]);
            const auto c=airport_garage_world(q.corners[ids[2]]);
            out.surfaces.triangles.push_back({{a,b,c,glm::normalize(glm::cross(b-a,c-a))},
                                             RoadLayer::Carriageway,Surface::Rock});
            out.surfaces.bounds.expand(a);out.surfaces.bounds.expand(b);out.surfaces.bounds.expand(c);
        }
    };
    auto rect=[&](float x0,float x1,float z0,float z1,float y) {
        surface({{{{x0,y,z0},{x0,y,z1},{x1,y,z1},{x1,y,z0}}}});
    };
    auto part=[&](const char* name,float x,float z,float bottom,float width,
                  float height,float depth,StartFinish finish,bool solid) {
        out.parts.push_back({name,{x,z},bottom,width,height,depth,finish,solid});
    };
    // Preserve the ground road height. The old pads are still the ground-level
    // parking surface; their support is included here for consistent tests.
    rect(-200,130,189,211,kAirportGarageFloor);
    rect(-150,80,224,240,kAirportGarageFloor);
    // A 13m aisle connects the two old pads; its drawn road stays in place.
    rect(-140,70,211,224,DRAPE_EPS_M);

    // Slab cells avoid ramp/stair wells. A full slab under a ramp would let
    // wheel rays catch the wrong level and turn the first rise into a wall.
    const std::array<float,8> xs{{-200,-150,-120,-116,-95,-45,80,130}};
    const std::array<float,8> zs{{189,192,194,202,205,211,224,240}};
    for(int level=1;level<3;++level) {
        const float top=kAirportGarageFloor+static_cast<float>(level)*kAirportGarageRise;
        for(std::size_t ix=1;ix<xs.size();++ix) for(std::size_t iz=1;iz<zs.size();++iz) {
            const float x0=xs[ix-1],x1=xs[ix],z0=zs[iz-1],z1=zs[iz];
            if(x1<=x0 || (z0>=211.f && (x0<-150.f || x1>80.f))) continue;
            if(x0>=-95.f && x1<=-45.f && z0>=194.f && z1<=202.f) continue;
            if(x0>=-120.f && x1<=-116.f && z0>=192.f && z1<=205.f) continue;
            part("airport garage deck",(x0+x1)*.5f,(z0+z1)*.5f,top-kAirportGarageSlab,
                 x1-x0,kAirportGarageSlab,z1-z0,StartFinish::Concrete,true);
            rect(x0,x1,z0,z1,top);
        }
        // Continuous barriers around every exposed deck edge, including wings.
        for(const std::array<float,4> r : {std::array<float,4>{-35,189.2f,330,.4f},
              {-35,239.8f,230,.4f},{-199.8f,200,.4f,22},{129.8f,200,.4f,22},
              {-175,210.8f,50,.4f},{105,210.8f,50,.4f},
              {-149.8f,225.5f,.4f,29},{79.8f,225.5f,.4f,29}}) {
            part("airport garage parapet",r[0],r[1],top,r[2],1.05f,r[3],StartFinish::Concrete,true);
            part("airport garage teal edge",r[0],r[1],top+.77f,r[2],.18f,r[3]+.02f,StartFinish::TealDoor,false);
        }
    }
    // Same-direction stacked ramps are two-way, with wide flat approach/exit
    // aprons and an open return aisle south of the ramps. No tight hairpin.
    for(int floor=0;floor<2;++floor) {
        for(int i=0;i<50;++i) {
            const float x0=kAirportGarageRampX0+static_cast<float>(i),x1=x0+1.f;
            const float y0=airport_garage_ramp_height(x0,floor),y1=airport_garage_ramp_height(x1,floor);
            AirportGarageQuad q{{{{x0,y0,194},{x0,y0,202},{x1,y1,202},{x1,y1,194}}}};
            out.ramp_quads.push_back(q);surface(q);
            // Short rail segments follow the real surface; they are walls beside
            // the 8m clear road, never boxes laid across the ramp.
            for(const float z:{193.85f,202.15f})
                part("airport garage ramp barrier",(x0+x1)*.5f,z,std::min(y0,y1),1.f,
                     1.f+std::fabs(y1-y0),.30f,StartFinish::Concrete,true);
            if(i%4<2) {
                StartPart stripe{"airport garage ramp centre stripe",{(x0+x1)*.5f,198},
                                  (y0+y1)*.5f+.012f,1.01f,.012f,.12f,StartFinish::Yellow,false};
                stripe.roll_deg=glm::degrees(std::atan2(y1-y0,1.f));
                out.parts.push_back(stripe);
            }
        }
    }
    // Columns are only along perimeter parking noses, not the access road,
    // turning aprons, ramp approaches, or the three existing terminal walks.
    for(float x=-194.f;x<=124.f;x+=12.f) {
        bool walk=false;for(float axis:{-110.f,-35.f,40.f}) walk|=std::fabs(x-axis)<4.f;
        if(!walk) part("airport garage column",x,189.8f,.13f,.55f,8.f,.55f,StartFinish::Concrete,true);
        if(x>-147.f && x<77.f && !walk)
            part("airport garage column",x,239.2f,.13f,.55f,8.f,.55f,StartFinish::Concrete,true);
    }
    // Public stairs sit beside the west terminal crossing, not across it.
    // Each flight is 24 real 16.7cm risers with 50cm treads; 4m-wide landings
    // and the surrounding deck connect the two stacked flights.
    for(int floor=0;floor<2;++floor) {
        const float base=kAirportGarageFloor+static_cast<float>(floor)*kAirportGarageRise;
        for(int step=0;step<24;++step) {
            const float rise=static_cast<float>(step+1)*kAirportGarageRise/24.f;
            part("airport garage public stair tread",-118,192.25f+static_cast<float>(step)*.5f,
                 base+rise-.16f,4.f,.16f,.5f,StartFinish::Concrete,true);
        }
        part("airport garage stair landing",-118,204.5f,base+4.f-.30f,4.f,.30f,1.f,StartFinish::Concrete,true);
        rect(-120,-116,204,205,base+4.f);
        for(float x:{-120.15f,-115.85f}) {
            for(int step=0;step<24;++step)
                part("airport garage stair guard",x,192.25f+static_cast<float>(step)*.5f,
                     base+static_cast<float>(step)*4.f/24.f,.20f,1.f,.5f,StartFinish::Steel,true);
        }
    }
    // Parking paint keeps full bays away from pedestrian routes and ramp wells.
    for(int level=0;level<3;++level) {
        const float y=kAirportGarageFloor+static_cast<float>(level)*4.f;
        for(float z:{192.8f,235.5f}) for(float x=-194.f;x<=124.f;x+=3.f) {
            if(z>220.f && (x<-145.f || x>75.f)) continue;
            bool reserved=x>-124.f && x<-112.f;
            for(float axis:{-110.f,-35.f,40.f}) reserved|=std::fabs(x-axis)<4.5f;
            if(x>-100.f && x<-40.f && z<210.f) reserved=true;
            if(reserved) continue;
            part("airport garage parking stripe",x,z,y+.015f,.10f,.015f,5.f,StartFinish::White,false);
        }
        // Walk markings retain the door axes on every level; at ground the
        // existing concrete walks provide the real 18cm raised footway.
        if(level>0) for(float x:{-110.f,-35.f,40.f})
            part("airport garage pedestrian stripe",x,214.5f,y+.015f,.13f,.015f,49.f,StartFinish::Yellow,false);
    }
    // A clearly visible parking P, built as strokes rather than new text art.
    for(float x:{-177.f,105.f}) {
        part("airport garage parking sign board",x,188.88f,5.8f,5.f,3.f,.18f,StartFinish::TealDoor,false);
        part("airport garage parking P stem",x-.55f,188.77f,6.2f,.3f,2.15f,.035f,StartFinish::White,false);
        part("airport garage parking P top",x+.1f,188.77f,8.05f,1.3f,.30f,.035f,StartFinish::White,false);
        part("airport garage parking P middle",x+.1f,188.77f,7.1f,1.3f,.30f,.035f,StartFinish::White,false);
        part("airport garage parking P curve",x+.7f,188.77f,7.1f,.30f,1.25f,.035f,StartFinish::White,false);
    }
    return out;
}
} // namespace apricot::city
