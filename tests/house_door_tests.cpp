#include <cstdio>
#include "city/residential_doors.h"
#include "city/landmarks.h"
#include "test_assert.h"

using namespace apricot;
namespace {
constexpr float dt=1.f/120.f;
const CharacterTuning tuning;
const auto& site=city::kResidentialHouses[city::kResidentialTargetHouse].site;
glm::vec3 world(glm::vec2 local,float y) {
    const auto p=city::residential_world(site,local);return {p.x,y,p.y};
}
struct Fixture {
    TerrainCollider collider{city::kMapSeed};
    GroundSampler ground;
    std::vector<city::ResidentialDoor> doors;
    std::vector<HouseDoorState> states;
    std::vector<std::size_t> ids;
    Fixture():ground{[](const void* p,float x,float z) {return static_cast<const TerrainCollider*>(p)->height(x,z);},&collider} {
        // Sample authored floors before registering raised support surfaces.
        doors=city::residential_doors(ground);
        const auto parts=city::bake_residential_house(city::kResidentialTargetHouse,ground);
        for(const auto& part:parts) {
            const auto c=world({part.centre.x,part.centre.z},site.ground_m+part.bottom_m+part.height_m*.5f);
            const float yaw=std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(part.yaw_deg);
            if(part.solid)collider.add_static_oriented_box(c,{part.width_m*.5f,part.height_m*.5f,part.depth_m*.5f},yaw);
            if(city::residential_ground_piece(part))collider.add_static_ground_rect({c.x,c.z},c.y+part.height_m*.5f,
                {part.width_m*.5f,part.depth_m*.5f},yaw,Surface::Rock);
        }
        states.resize(doors.size());
        for(const auto& door:doors) {
            const auto& d=door.physics;
            ids.push_back(collider.add_kinematic_oriented_box(house_door_centre(d,0),
                {d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw));
        }
    }
    void update(PlayerCharacterState& actor,const InputFrame& input) {
        const auto velocity=house_door_walk_velocity(actor,tuning,input);
        for(std::size_t i=0;i<doors.size();++i) {
            const auto& d=doors[i].physics;
            states[i]=step_house_door(d,states[i],&actor,tuning,velocity,dt);
            collider.set_kinematic_oriented_box(ids[i],house_door_centre(d,states[i].angle),
                {d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw+states[i].angle);
        }
        REQUIRE(character_position_clear(collider,actor.position,tuning));
        actor=step_character(actor,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,actor.position,tuning));
    }
};
void traversals() {
    // Each starts shut. Walk real character/controller through the furnished
    // house shell at its real rotation, from both sides of every doorway.
    for(std::size_t index=0;index<5;++index)for(float side:{-1.f,1.f}) {
        Fixture f;const auto& d=f.doors[index].physics;
        const auto t=house_door_tangent(d,0);const glm::vec2 n{-t.y,t.x};
        auto centre=house_door_centre(d,0);centre.y=d.hinge.y-.025f;
        PlayerCharacterState actor;actor.position=centre+glm::vec3{n.x,0,n.y}*side*1.05f;
        const auto target=centre-glm::vec3{n.x,0,n.y}*side*1.05f;
        REQUIRE(character_position_clear(f.collider,actor.position,tuning));
        float max_angle=0;
        for(int tick=0;tick<1800 && glm::distance(actor.position,target)>.04f;++tick) {
            const auto delta=target-actor.position;InputFrame input;
            input.look_dx=std::atan2(delta.x,-delta.z)-actor.view_yaw;
            input.throttle=std::min(1.f,glm::length(glm::vec2{delta.x,delta.z})/(tuning.walk_speed_mps*dt));
            f.update(actor,input);max_angle=std::max(max_angle,std::abs(f.states[index].angle));
        }
        std::printf("%s side %.0f: remaining %.3fm, swing %.1fdeg\n",f.doors[index].name,side,
            glm::distance(actor.position,target),glm::degrees(max_angle));
        REQUIRE(glm::distance(actor.position,target)<.05f);REQUIRE(max_angle>.5f);
        REQUIRE(f.states[index].angle*side>0); // opening moves away from approach
    }
    apricot_test::pass("real character crosses all five closed leaves in both directions");
}
void contact_and_safety() {
    HouseDoor d;d.width=1.5f;d.hinge={0,0,0};
    PlayerCharacterState actor;actor.position={.8f,0,.353f};
    HouseDoorState s;
    for(int i=0;i<600;++i)s=step_house_door(d,s,&actor,tuning,{},dt);
    REQUIRE(s.angle==0); // standing at actual contact must not auto-open
    actor.position.z=1;
    for(int i=0;i<600;++i)s=step_house_door(d,s,&actor,tuning,{0,0,-2.35f},dt);
    REQUIRE(s.angle==0); // facing it and walking in place out of reach isn't contact
    actor.position={-.8f,0,.353f};
    s=step_house_door(d,s,&actor,tuning,{0,0,-2.35f},dt);REQUIRE(s.angle==0);
    actor.position={.8f,4,.353f};
    s=step_house_door(d,s,&actor,tuning,{0,0,-2.35f},dt);REQUIRE(s.angle==0);
    actor.position={.8f,0,.353f};
    s=step_house_door(d,s,&actor,tuning,{0,0,-2.35f},dt);REQUIRE(s.angle>0);
    HouseDoorState a,b;
    for(int i=0;i<600;++i) {
        const glm::vec3 v{0,0,-2.35f};
        a=step_house_door(d,a,&actor,tuning,v,dt);b=step_house_door(d,b,&actor,tuning,v,dt);
        REQUIRE(a.angle==b.angle);REQUIRE(a.angular_velocity==b.angular_velocity);
        REQUIRE(std::abs(a.angle)<=d.max_angle);
    }
    // Occupy the closing arc and hold still. No panel sweep may enter capsule.
    s.angle=1.3f;s.angular_velocity=0;s.return_delay=0;
    actor.position={.8f,0,-.45f};
    REQUIRE(house_door_clearance(d,s.angle,actor.position,tuning.radius_m)>0);
    for(int i=0;i<2400;++i) {
        s=step_house_door(d,s,&actor,tuning,{},dt);
        REQUIRE(house_door_clearance(d,s.angle,actor.position,tuning.radius_m)>=0);
    }
    REQUIRE(s.angle>.5f);
    for(int i=0;i<7200;++i)s=step_house_door(d,s,nullptr,tuning,{},dt);
    REQUIRE(std::abs(s.angle)<.001f);
    apricot_test::pass("contact only, hinge/height separation, deterministic swing, occupied return and gentle close");
}
void end_contact_from_both_sides() {
    HouseDoor d;d.width=1.33f;
    for(float inset:{0.f,.09f})for(float side:{-1.f,1.f}) {
        d.pivot_inset=inset;
        HouseDoorState s;s.angle=side*1.2f;s.return_delay=1.5f;
        const auto t=house_door_tangent(d,s.angle);const glm::vec2 n{-t.y,t.x};
        const auto p=t*(d.width-d.pivot_inset+.25f)+n*(side*.24f);
        PlayerCharacterState actor;actor.position={p.x,0,p.y};
        const auto v=-t*2.3f+n*(side*.1f);
        const glm::vec3 velocity{v.x,0,v.y};
        REQUIRE(house_door_clearance(d,s.angle,actor.position,tuning.radius_m)>0);
        // This approaches the end but moves AWAY from the broad-face plane:
        // side*normal_speed is positive, the exact old deadlock condition.
        const auto before=s;
        s=step_house_door(d,s,&actor,tuning,velocity,dt);
        REQUIRE(std::abs(s.angle-before.angle)>.001f);
        REQUIRE(house_door_clearance(d,s.angle,actor.position,tuning.radius_m)>=0);
        const auto idle=step_house_door(d,before,&actor,tuning,{},dt);
        REQUIRE(idle.angle==before.angle); // no walking means no contact drive
    }
    apricot_test::pass("mirrored near-tangential free-end contacts drive the leaf without face-plane deadlock");
}
void render_collision_agree() {
    Fixture f;
    for(const auto& d:f.doors)for(float angle:{-1.5f,0.f,1.5f}) {
        const auto parts=city::residential_door_parts(d,angle);REQUIRE(parts.front().solid);
        const auto& leaf=parts.front();
        const auto centre=world({leaf.centre.x,leaf.centre.z},site.ground_m+leaf.bottom_m+leaf.height_m*.5f);
        REQUIRE(glm::distance(centre,house_door_centre(d.physics,angle))<.0003f);
        REQUIRE_NEAR(std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(leaf.yaw_deg),d.physics.closed_yaw+angle,.00001f);
        REQUIRE_NEAR(leaf.pitch_deg,0,0);REQUIRE_NEAR(leaf.width_m,d.physics.width,0);
        for(std::size_t i=1;i<parts.size();++i)REQUIRE(!parts[i].solid);
    }
    apricot_test::pass("authored visuals and rotating solid leaf share hinge, dimensions and yaw");
}
bool pieces_overlap(const city::StartPart& a,const city::StartPart& b) {
    if(a.bottom_m+a.height_m<=b.bottom_m || b.bottom_m+b.height_m<=a.bottom_m)return false;
    const float ay=glm::radians(a.yaw_deg),by=glm::radians(b.yaw_deg);
    const glm::vec2 at{std::cos(ay),-std::sin(ay)},an{-at.y,at.x};
    const glm::vec2 bt{std::cos(by),-std::sin(by)},bn{-bt.y,bt.x};
    const glm::vec2 delta{a.centre.x-b.centre.x,a.centre.z-b.centre.z};
    for(const auto axis:{at,an,bt,bn}) {
        const float ra=std::abs(glm::dot(axis,at))*a.width_m*.5f+std::abs(glm::dot(axis,an))*a.depth_m*.5f;
        const float rb=std::abs(glm::dot(axis,bt))*b.width_m*.5f+std::abs(glm::dot(axis,bn))*b.depth_m*.5f;
        if(std::abs(glm::dot(delta,axis))>=ra+rb)return false;
    }
    return true;
}
void jamb_clearance() {
    TerrainCollider terrain(city::kMapSeed);
    const GroundSampler ground{[](const void* p,float x,float z) {
        return static_cast<const TerrainCollider*>(p)->height(x,z);},&terrain};
    const auto shell=city::bake_residential_house(city::kResidentialTargetHouse,ground);
    const auto doors=city::residential_doors(ground);
    const char* frame_names[]={"house front entrance","house garden entrance","house study doorway",
        "house bath doorway","house bedroom doorway"};
    const float clear_width[]={1.65f-.22f,1.7f-.22f,1.6f-.22f,1.4f-.22f,1.6f-.22f};
    const float floor=city::residential_floor_top(city::kResidentialTargetHouse,ground);
    for(std::size_t i=0;i<doors.size();++i) {
        const auto& door=doors[i];
        REQUIRE_NEAR((clear_width[i]-door.physics.width)*.5f,.006f,.00001f);
        REQUIRE_NEAR(door.physics.pivot_inset,.09f,.00001f);
        const float opening_height=i<2?2.35f:2.3f;
        REQUIRE_NEAR(floor+opening_height-.11f-(door.physics.hinge.y-site.ground_m+door.physics.height),.006f,.00001f);
        std::size_t frames=0;
        for(const auto& frame:shell)if(std::strcmp(frame.name,frame_names[i])==0) {
            ++frames;
            if(frame.height_m>1.f) {
                const auto leaf=city::residential_door_parts(door,0).front();
                const glm::vec2 tangent{std::cos(door.local_yaw),-std::sin(door.local_yaw)};
                const glm::vec2 delta{frame.centre.x-leaf.centre.x,frame.centre.z-leaf.centre.z};
                REQUIRE_NEAR(std::abs(glm::dot(delta,tangent))-(frame.width_m+leaf.width_m)*.5f,.006f,.00001f);
            }
            for(int tenth=-950;tenth<=950;++tenth) {
                const auto parts=city::residential_door_parts(door,glm::radians(static_cast<float>(tenth)*.1f));
                for(const auto& part:parts)REQUIRE(!pieces_overlap(part,frame));
            }
        }
        REQUIRE(frames==3);
    }
    apricot_test::pass("all five doors fit each jamb within 6mm; slabs and hardware clear full -95 to +95 degree sweep");
}
void continuous_bedroom_route() {
    Fixture f;PlayerCharacterState actor;actor.position=world({3,1},f.doors[4].physics.hinge.y-.025f);
    // Return around the free end, not straight up the hinge line (x2.3),
    // which physically pushes an already-95deg leaf against its hard stop.
    const glm::vec2 route[]={{3,-1.7f},{2.3f,-3.8f},{2.8f,-2.7f},{2.8f,-1.7f},{3,1}};
    for(const auto waypoint:route) {
        const auto goal=world(waypoint,actor.position.y);
        for(int tick=0;tick<2400 && glm::distance(actor.position,goal)>.05f;++tick) {
            const auto delta=goal-actor.position;InputFrame input;
            input.look_dx=std::atan2(delta.x,-delta.z)-actor.view_yaw;
            input.throttle=std::min(1.f,glm::length(glm::vec2{delta.x,delta.z})/(tuning.walk_speed_mps*dt));
            f.update(actor,input);
        }
        const auto delta=actor.position-world({0,0},actor.position.y);
        const auto local=glm::vec2{site.cos_yaw*delta.x-site.sin_yaw*delta.z,site.sin_yaw*delta.x+site.cos_yaw*delta.z};
        std::printf("bedroom route target %.2f %.2f actual %.2f %.2f angle %.1f\n",waypoint.x,waypoint.y,local.x,local.y,glm::degrees(f.states[4].angle));
        REQUIRE(glm::distance(actor.position,goal)<.06f);
    }
    apricot_test::pass("continuous bedroom entry, bed aisle and return without resetting door state");
}
void continuous_house_route() {
    Fixture f;const auto start=world({0,8.7f},0);
    auto actor=spawn_character(f.collider,start.x,start.z);
    const glm::vec2 route[]={{0,5.1f},{0,1},{-3,1},{-3,-2.5f},{-3,-3.5f},{-3,1},
        {0,1},{0,-2.7f},{0,1},{3,1},{3,-1.7f},{2.3f,-3.8f},{2.8f,-2.7f},
        {2.8f,-1.7f},{3,1},{0,1},{0,7},{0,10},{0,7},{0,1},{-3,0},{-8,0},
        {-12,0},{-12,-10},{-12,0},{-8,0},{-3,0},{0,1},{0,8.7f}};
    unsigned touched=0;
    for(const auto waypoint:route) {
        const auto goal=world(waypoint,0);
        const auto remaining=[&] {return glm::length(glm::vec2{actor.position.x-goal.x,actor.position.z-goal.z});};
        for(int tick=0;tick<2400 && remaining()>.05f;++tick) {
            InputFrame input;input.look_dx=std::atan2(goal.x-actor.position.x,actor.position.z-goal.z)-actor.view_yaw;
            input.throttle=std::min(1.f,remaining()/(tuning.walk_speed_mps*dt));f.update(actor,input);
            for(std::size_t i=0;i<f.states.size();++i)if(std::abs(f.states[i].angle)>.25f)touched|=1u<<i;
        }
        if(remaining()>=.06f)std::printf("full route blocked toward %.2f %.2f, remaining %.3f\n",waypoint.x,waypoint.y,remaining());
        REQUIRE(remaining()<.06f);
    }
    REQUIRE(touched==31u);
    apricot_test::pass("continuous whole-house walk visits every room, exits/reenters front and garden, then leaves");
}
}
int main() {contact_and_safety();end_contact_from_both_sides();render_collision_agree();jamb_clearance();traversals();continuous_bedroom_route();continuous_house_route();return 0;}
