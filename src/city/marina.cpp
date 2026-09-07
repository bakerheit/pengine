#include "city/marina.h"
#include <cmath>
#include <cstring>

namespace apricot::city {
namespace {
glm::vec3 marina_world(glm::vec3 local) {
    return local+glm::vec3{kMarlinDockSite.origin.x,kMarlinDockSite.ground_m,
                           kMarlinDockSite.origin.z};
}
}

MarinaAccessBake bake_marina_access() {
    MarinaAccessBake out;
    using F=StartFinish;
    const auto part=[&](const char* name,float x,float z,float bottom,float width,
                        float height,float depth,F finish,bool solid=false) -> StartPart& {
        out.parts.push_back({name,{x,z},bottom,width,height,depth,finish,solid});
        return out.parts.back();
    };
    const auto surface=[&](const MarinaRampQuad& q) {
        for(const std::array<std::size_t,3> ids :
            {std::array<std::size_t,3>{0,1,2},std::array<std::size_t,3>{0,2,3}}) {
            const glm::vec3 a=marina_world(q.corners[ids[0]]);
            const glm::vec3 b=marina_world(q.corners[ids[1]]);
            const glm::vec3 c=marina_world(q.corners[ids[2]]);
            glm::vec3 normal=glm::normalize(glm::cross(b-a,c-a));
            if(normal.y<0) normal=-normal;
            out.surfaces.triangles.push_back({{a,b,c,normal},RoadLayer::Carriageway,Surface::Rock});
            out.surfaces.bounds.expand(a);out.surfaces.bounds.expand(b);out.surfaces.bounds.expand(c);
        }
    };

    part("marina parking lot",kMarinaParkingCentre.x,kMarinaParkingCentre.z,
         kMarinaParkingTop-.08f,kMarinaParkingWidth,.08f,kMarinaParkingDepth,F::Asphalt);
    // Three east-end bays are deliberately omitted from each bank. Their old
    // paint made the access road feel like it landed in a parking space; the
    // clear apron now reads as the lot entrance and turning area.
    for(float x=35.f;x<=kMarinaParkingLastStripeX;x+=3.f) for(float z:{-30.f,-10.f})
        part("marina parking stripe",x,z,kMarinaParkingTop+.012f,.10f,.012f,9.f,F::White);
    // Stops belong at the outer, kerb-facing ends of nose-in bays, not along
    // the centre aisle where they block the driver's approach.
    for(float x=36.5f;x<kMarinaParkingLastStripeX;x+=3.f)
        for(float z:{kMarinaNorthStopZ,kMarinaSouthStopZ})
            part("marina parking stop",x,z,kMarinaParkingTop,2.2f,.16f,.24f,F::Concrete,true);
    part("marina parking north curb",kMarinaParkingCentre.x,-34.88f,kMarinaParkingTop,
         kMarinaParkingWidth,.18f,.24f,F::Concrete,true);
    part("marina parking south curb",kMarinaParkingCentre.x,-5.12f,kMarinaParkingTop,
         kMarinaParkingWidth,.18f,.24f,F::Concrete,true);
    const float road_edge=kMarinaParkingCentre.x+kMarinaParkingWidth*.5f;
    for(float z:{-30.f,-10.f})
        part("marina parking east curb",road_edge-.12f,z,kMarinaParkingTop,
             .24f,.18f,10.f,F::Concrete,true);

    const float z0=kMarinaRampZ-kMarinaRampWidth*.5f;
    const float z1=kMarinaRampZ+kMarinaRampWidth*.5f;
    const std::array<float,3> xs{{kMarinaRampWestX,kMarinaRampMidX,kMarinaRampEastX}};
    for(std::size_t i=0;i+1<xs.size();++i) {
        const float x0=xs[i],x1=xs[i+1];
        const float y0=marina_ramp_height(x0),y1=marina_ramp_height(x1);
        MarinaRampQuad ramp{{{{x0,y0,z0},{x0,y0,z1},{x1,y1,z1},{x1,y1,z0}}}};
        out.ramp_quads.push_back(ramp);surface(ramp);
        const float run=x1-x0,rise=y1-y0;
        const float length=std::sqrt(run*run+rise*rise);
        const float roll=glm::degrees(std::atan2(rise,run));
        StartPart& slab=part("marina boat ramp",(x0+x1)*.5f,kMarinaRampZ,
            (y0+y1)*.5f-.11f,length,.22f,kMarinaRampWidth,F::Concrete);
        slab.roll_deg=roll;
        for(float side:{-1.f,1.f}) {
            StartPart& curb=part("marina boat ramp curb",slab.centre.x,
                kMarinaRampZ+side*(kMarinaRampWidth*.5f+.10f),
                (y0+y1)*.5f-.02f,length,.28f,.20f,F::Concrete,true);
            curb.roll_deg=roll;
        }
    }
    for(float x=kMarinaRampWestX+4.f;x<kMarinaRampEastX-2.f;x+=6.f) {
        const float y=marina_ramp_height(x);
        StartPart& stripe=part("marina boat ramp centre stripe",x,kMarinaRampZ,y+.015f,
            2.5f,.015f,.12f,F::Yellow);
        stripe.roll_deg=glm::degrees(std::atan2(marina_ramp_height(x+.1f)-y,.1f));
    }
    return out;
}

void append_marina_access_collision(RoadCollision& target,
                                     const MarinaAccessBake& access) {
    target.triangles.insert(target.triangles.end(),access.surfaces.triangles.begin(),
                            access.surfaces.triangles.end());
    target.bounds.expand(access.surfaces.bounds);
}

std::vector<StartPart> bake_marina() {
    using F=StartFinish;
    std::vector<StartPart> p;
    const auto add=[&](const char* name,float x,float z,float y,float w,float h,float d,F f,bool solid=false,
                       float pitch=0,float yaw=0,float roll=0) {
        p.push_back({name,{x,z},y,w,h,d,f,solid,pitch,yaw,roll});
    };
    // One broad structural collider per deck, individual boards above it.
    // Boards are inset with dark seams, never a coplanar decal over a slab.
    const auto deck=[&](float x,float z,float w,float d) {
        add("marina deck structure",x,z,.36f,w,.28f,d,F::DarkRoof,true);
        const int count=static_cast<int>(std::ceil(w/.36f));
        const float step=w/static_cast<float>(count);
        for(int i=0;i<count;++i)
            add("marina timber plank",x-w*.5f+(static_cast<float>(i)+.5f)*step,z,.64f,
                step-.015f,.02f,d-.035f,F::WarmWall);
        for(float side:{-1.f,1.f})
            add("marina timber fascia",x,z+side*d*.5f,.29f,w,.25f,.09f,F::WarmWall);
    };
    deck(-8,0,32,3);                  // shore to harbour head
    deck(-21,6.4f,1.8f,9.8f);         // two fingers, clear water between
    deck(-13,6.4f,1.8f,9.8f);
    deck(1,6.4f,12,9.8f);             // service platform joined at z=1.5
    // Heavy wooden piles reach below the actual seabed, with steel collars.
    const auto pile=[&](float x,float z) {
        add("marina timber piling",x,z,-12,.25f,13.15f,.25f,F::WarmWall,true);
        add("marina piling cap",x,z,1.15f,.30f,.065f,.30f,F::Steel);
        for(float y:{.25f,.90f})add("marina piling collar",x,z,y,.275f,.07f,.275f,F::Steel);
    };
    for(float x:{-23.5f,-17.f,-10.f,-3.f,6.5f}) {
        pile(x,-1.55f);pile(x,1.55f);
    }
    for(float x:{-21.f,-13.f})for(float z:{6.f,11.f})pile(x+.98f,z);
    for(float x:{-4.7f,6.7f})for(float z:{6.f,11.f})pile(x,z);
    const auto rail_x=[&](float a,float b,float z) {
        for(float x=a;x<=b+.01f;x+=1.5f)add("marina rail post",x,z,.66f,.07f,1.02f,.07f,F::Steel,true);
        for(float y:{1.16f,1.64f})add("marina guard rail",(a+b)*.5f,z,y,b-a,.055f,.055f,F::Steel,true);
    };
    // Guard exposed service edges, but do not rail off boarding faces.
    rail_x(-4.7f,6.7f,11.15f);
    rail_x(-4.7f,-.5f,1.53f); // leave an open approach to the shed
    for(float z:{-1.25f,0.f,1.25f})add("marina end post",-23.8f,z,.66f,.07f,1.02f,.07f,F::Steel,true);
    for(float y:{1.16f,1.64f})add("marina end rail",-23.8f,0,y,.055f,.055f,2.5f,F::Steel,true);
    // Cleats and coiled rope: actual holes, not dark discs.
    const auto coil=[&](float x,float z) {
        for(int ring=0;ring<3;++ring)for(int i=0;i<12;++i) {
            const float a=static_cast<float>(i)*.523598776f;
            const float r=.13f+static_cast<float>(ring)*.045f;
            add("marina rope coil",x+r*std::cos(a),z+r*std::sin(a),.668f,
                .026f,.026f,r*.54f,F::WarmWall,false,0,-a*57.29578f,0);
        }
    };
    for(float x:{-20.f,-14.f,-8.f,-2.f}) {
        add("marina cleat foot",x,-1.32f,.66f,.30f,.055f,.16f,F::Steel);
        add("marina cleat stem",x,-1.32f,.70f,.08f,.10f,.08f,F::Steel);
        add("marina cleat horns",x,-1.32f,.78f,.40f,.07f,.07f,F::Steel);
    }
    coil(-14,-1);coil(-2,-1);coil(-20.9f,9.2f);
    // Rubber fenders hang outside the edge, not in the walking corridor.
    for(float x:{-20.f,-14.f,-8.f,-2.f}) {
        add("marina fender cord",x,-1.64f,.18f,.025f,.53f,.025f,F::WarmWall);
        add("marina rubber fender",x,-1.66f,-.10f,.19f,.60f,.19f,F::DarkRoof);
        add("marina fender band",x,-1.66f,.12f,.205f,.06f,.205f,F::Steel);
    }
    // Safety ladders drop from deck into water; slim rails do not form walls.
    for(float x:{-18.f,-9.5f}) {
        for(float dx:{-.26f,.26f})
            add("marina ladder rail",x+dx,-1.68f,-.75f,.045f,1.7f,.045f,F::Steel);
        for(int i=0;i<6;++i)add("marina ladder rung",x,-1.70f,-.66f+static_cast<float>(i)*.25f,
            .54f,.045f,.065f,F::Steel);
    }
    const auto lifering=[&](float x,float z) {
        add("marina safety post",x,z,.66f,.08f,1.70f,.08f,F::White,true);
        for(int i=0;i<12;++i) {
            const float a=static_cast<float>(i)*.523598776f;
            add("marina life ring",x+.30f*std::cos(a),z-.09f,1.55f+.30f*std::sin(a)-.08f,
                .20f,.16f,.11f,(i/3)%2 ? F::White:F::RedTrim,false,0,0,a*57.29578f+90);
        }
    };
    lifering(-17,1.35f);lifering(5,1.8f);
    // Power/water pedestals and hose reel beside the berths.
    for(float x:{-21.f,-13.f}) {
        add("marina utility pedestal",x,7.2f,.66f,.32f,.80f,.28f,F::White,true);
        add("marina pedestal cap",x,7.2f,1.46f,.37f,.07f,.33f,F::TealDoor);
        add("marina socket face",x,7.045f,1.05f,.18f,.22f,.02f,F::DarkRoof);
    }
    // Open-front harbour service shed: proper doorway, framed windows,
    // pitched roof, bench and shelving inside. No solid cuboid interior.
    const BuildingOpening front[]={
        {"shed open door",OpeningKind::Door,2.6f,1.3f,0,2.2f,F::TealDoor,0,0,F::White,false},
        {"shed ticket window",OpeningKind::Window,.90f,1.05f,1.05f,.90f,F::Glass,1,0,F::White}};
    const BuildingOpening side[]={{"shed side window",OpeningKind::Window,2,1.5f,1.1f,.9f,F::Glass,1,0,F::White}};
    const BuildingWall walls[]={
        {"marina shed front",{-.6f,5.5f},{4.6f,5.5f},.66f,2.7f,.16f,F::TealDoor,front,2},
        {"marina shed back",{4.6f,10},{-.6f,10},.66f,2.7f,.16f,F::TealDoor},
        {"marina shed west",{-.6f,10},{-.6f,5.5f},.66f,2.7f,.16f,F::TealDoor,side,1},
        {"marina shed east",{4.6f,5.5f},{4.6f,10},.66f,2.7f,.16f,F::TealDoor,side,1}};
    const BuildingRoof roofs[]={{"marina shed roof",{2,7.75f},3.36f,5.2f,4.5f,.65f,.12f,.25f,0,
        RoofStyle::Gable,RidgeAxis::AlongX,F::DarkRoof,F::White}};
    BuildingPlan shed;
    shed.name="Ostend Bait & Tackle";shed.walls=walls;shed.wall_count=4;shed.roofs=roofs;shed.roof_count=1;
    auto parts=bake_building(shed);p.insert(p.end(),parts.begin(),parts.end());
    // Sit ahead of the roof overhang so its edge cannot cover the lettering.
    add("marina shed sign backing",2,5.14f,2.97f,4.5f,.62f,.10f,F::White);
    add("marina sign face",2,5.075f,2.99f,4.3f,.58f,.018f,F::White);
    add("marina service counter",.25f,8,.66f,.75f,.90f,2.9f,F::WarmWall,true);
    add("marina counter top",.25f,8,1.56f,.83f,.06f,3.05f,F::Steel);
    for(float y:{1.f,1.6f,2.2f})add("marina shelf",2.6f,9.65f,y,2.7f,.055f,.45f,F::WarmWall);
    for(float x:{1.55f,2.15f,3.25f})for(float y:{1.055f,1.655f})
        add("marina supply tin",x,9.64f,y,.22f,.28f,.20f,F::White);
    // Fishing stock makes the open shed read as the bait shop named by Lou,
    // even before the player is close enough to read the sign.
    for(float x:{3.35f,3.62f,3.89f,4.16f}) {
        add("marina fishing rod",x,9.67f,.86f,.035f,2.05f,.035f,F::WarmWall,
            false,0,0,-4);
        add("marina rod grip",x,9.66f,.82f,.075f,.34f,.075f,F::DarkRoof);
    }
    add("marina bait freezer",3.66f,8.95f,.66f,1.55f,.86f,.72f,F::White,true);
    add("marina bait freezer lid",3.66f,8.95f,1.52f,1.58f,.055f,.75f,F::TealDoor);
    // Spare rope/cargo, fuel drums and a workbench occupy service margins.
    for(int i=0;i<3;++i) {
        const float x=-3.8f+static_cast<float>(i)*.83f;
        add("marina cargo crate",x,9.6f,.66f,.70f,.70f,.70f,F::WarmWall,true);
        for(float dx:{-.28f,.28f})add("marina crate strap",x+dx,9.6f,.66f,.055f,.72f,.72f,F::Steel);
    }
    for(float z:{4.0f,5.1f}) {
        add("marina service drum",-3.8f,z,.66f,.64f,.92f,.64f,F::TealDoor,true);
        for(float y:{.78f,1.38f})add("marina drum band",-3.8f,z,y,.67f,.05f,.67f,F::Steel);
        add("marina drum cap",-3.8f,z,1.58f,.16f,.035f,.16f,F::Steel);
    }
    add("marina workbench",5.7f,8.0f,1.4f,1.5f,.12f,3.0f,F::WarmWall,true);
    for(float z:{6.7f,9.3f})for(float x:{5.1f,6.3f})add("marina bench leg",x,z,.66f,.08f,.74f,.08f,F::Steel,true);
    add("marina tool chest",5.7f,9,1.52f,.8f,.38f,.6f,F::RedTrim);
    // Shelter-side bench leaves a direct aisle from shore to the open door.
    add("marina waiting seat",2.4f,3.6f,1.03f,2.4f,.10f,.50f,F::WarmWall,true);
    add("marina waiting back",2.4f,3.88f,1.12f,2.4f,.48f,.07f,F::WarmWall,true);
    for(float x:{1.5f,3.3f})add("marina waiting leg",x,3.6f,.66f,.10f,.37f,.44f,F::Steel,true);
    for(const auto& at:kMarinaLampPositions) {
        // Mount poles at the pier edge, heads reach back over the deck.
        add("marina light pole",at.x,at.z+1.05f,.66f,.10f,3.5f,.10f,F::Steel,true);
        add("marina light arm",at.x,at.z+.55f,4.05f,.075f,.075f,1.05f,F::Steel);
        add("marina light shade",at.x,at.z,4.01f,.43f,.15f,.43f,F::DarkRoof);
        add("marina light emitter",at.x,at.z,3.99f,.33f,.02f,.33f,F::White);
    }
    auto access=bake_marina_access();
    p.insert(p.end(),access.parts.begin(),access.parts.end());
    return p;
}
std::vector<MarinaMapFootprint> marina_map_footprints() {
    std::vector<MarinaMapFootprint> footprints;
    bool ramp_added=false;
    for(const auto& part:bake_marina()) {
        const bool deck=std::strcmp(part.name,"marina deck structure")==0;
        const bool shed=std::strcmp(part.name,"marina shed roof")==0;
        const bool parking=std::strcmp(part.name,"marina parking lot")==0;
        const bool ramp=std::strcmp(part.name,"marina boat ramp")==0;
        if(!deck && !shed && !parking && !ramp) continue;
        if(ramp) {
            if(ramp_added) continue;
            ramp_added=true;
            footprints.push_back({"Ostend boat ramp",
                {(kMarinaRampWestX+kMarinaRampEastX)*.5f,kMarinaRampZ},
                kMarinaRampEastX-kMarinaRampWestX,kMarinaRampWidth,false,true});
            continue;
        }
        constexpr float radians=.01745329252f;
        footprints.push_back({deck ? "Ostend pier" : (shed ? "Ostend Bait & Tackle" :
            (parking ? "Ostend launch parking" : "Ostend boat ramp")),part.centre,
            part.width_m*std::cos(part.roll_deg*radians),
            part.depth_m*std::cos(part.pitch_deg*radians),parking,deck || ramp});
    }
    return footprints;
}
} // namespace apricot::city
