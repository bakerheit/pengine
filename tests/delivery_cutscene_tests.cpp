#include "app/cutscene_pose.h"
#include "game/delivery_mission.h"
#include "game/cutscene_playback.h"
#include "core/asset_root.h"
#include "test_assert.h"
#include <cstdio>
using namespace apricot;
namespace cs=apricot::cutscene;
int main() {
    cs::Document doc;std::string error;
    REQUIRE(cs::load(asset_path("cutscenes/mission1-delivery.cutscene"),doc,error));
    REQUIRE(doc.actors.size()==3);REQUIRE(doc.cues.size()==4);
    REQUIRE(doc.cues[1].speaker=="Devon");
    REQUIRE(doc.cues[1].text=="did anyone follow you?");
    for (std::size_t i=0;i<doc.cues.size();++i) {
        const auto& cue=doc.cues[i];REQUIRE(!cue.audio.empty());
        REQUIRE(std::filesystem::exists(asset_path(cue.audio)));
        REQUIRE(cue.start+cue.duration<=doc.duration());
        if(i) REQUIRE(cue.start>=doc.cues[i-1].start+doc.cues[i-1].duration);
    }
    const auto exit=cs::actor_position(doc.actors[0],doc.duration());
    REQUIRE(delivery_contact(exit,true));
    REQUIRE(glm::distance(cs::actor_position(doc.actors[1],0),city::devon_position())<.001f);
    for(bool skip:{false,true}) {
        cs::Playback playback;playback.start(doc.duration());
        MissionStage stage=MissionStage::DeliveryActive;
        playback.paused=true;REQUIRE(!playback.advance(50));REQUIRE(playback.time==0);
        REQUIRE(stage==MissionStage::DeliveryActive);
        playback.paused=false;
        if(skip) REQUIRE(playback.skip());else REQUIRE(playback.advance(doc.duration()+1));
        REQUIRE(complete_delivery(stage,exit,true));REQUIRE(stage==MissionStage::DeliveryComplete);
        REQUIRE(!complete_delivery(stage,exit,true));
    }
    if (!std::filesystem::exists(asset_path(doc.actors[0].mesh))) {
        std::puts("SKIP private rig checks (assets not staged)");
        return apricot_test::done("delivery_cutscene_tests");
    }
    StaticEmesh parcel;REQUIRE(read_static_emesh(asset_path(doc.actors[2].mesh),parcel));
    // Receiver secures the parcel before the courier lets go.
    const float receive=doc.actors[1].keys[2].time;
    REQUIRE(cs::actor_key(doc.actors[0],receive).reach==1);
    REQUIRE(cs::actor_key(doc.actors[0],receive+.5f).reach==1);
    for(std::size_t index=0;index<2;++index) {
        const auto& actor=doc.actors[index];Skeleton rig;SkinnedEmesh mesh;Animation idle,walk;
        auto path=std::filesystem::path(asset_path(actor.mesh));path.replace_extension(".eskel");
        REQUIRE(rig.load(path.string()));REQUIRE(read_skinned_emesh(asset_path(actor.mesh),mesh));
        REQUIRE(idle.load(asset_path(actor.animation),rig));
        const bool walking=!actor.walk_animation.empty();
        if(walking) REQUIRE(walk.load(asset_path(actor.walk_animation),rig));
        const float plant=locomotion_plant_offset(mesh,rig,idle,actor.height/mesh.bounds.size().y);
        float worst=0,clearance=100,worst_time=0;
        for(int frame=0;frame<=static_cast<int>(doc.duration()*30);++frame) {
            const float time=static_cast<float>(frame)/30;
            const auto model=cs::actor_transform(actor,mesh.bounds,plant,time);
            std::vector<glm::mat4> local;
            REQUIRE(cs::sample_actor_pose(actor,time,rig,idle,walking?&walk:nullptr,model,local));
            VehicleDriverPose pose;pose.local=local;driver_pose_detail::globals(rig,pose);
            const auto bone=[&](const char* name) {return model.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(rig.find_bone(name))][3]});};
            const auto key=cs::actor_key(actor,time);
            if(key.reach>.999f) {
                const float before=worst;
                worst=std::max(worst,glm::distance(bone("mixamorig:LeftHand"),key.left_hand));
                worst=std::max(worst,glm::distance(bone("mixamorig:RightHand"),key.right_hand));
                if(worst>before) worst_time=time;
            }
            const auto box=cs::actor_transform(doc.actors[2],parcel.bounds,0,time);
            const auto inv=glm::inverse(box.matrix());
            for(int j=0;j<=20;++j) {
                const auto center=glm::mix(bone("mixamorig:Hips"),bone("mixamorig:Spine2"),static_cast<float>(j)/20);
                const auto point=glm::vec3{inv*glm::vec4{center,1}};
                clearance=std::min(clearance,glm::distance(center,box.transform_point(glm::clamp(point,parcel.bounds.min,parcel.bounds.max))));
            }
            const auto p=cs::actor_position(actor,time);
            REQUIRE_NEAR(p.y,city::kMarinaDeckTop,.001f);
            // Entire cast stays in the open shop aisle, clear of counter and freezer.
            REQUIRE(p.x>-2040.f&&p.x<-2038.f);REQUIRE(p.z>-594.6f&&p.z<-591.2f);
            if(frame%30==0) {
                std::vector<glm::mat4> later,again;
                REQUIRE(cs::sample_actor_pose(actor,doc.duration(),rig,idle,walking?&walk:nullptr,model,later));
                REQUIRE(cs::sample_actor_pose(actor,time,rig,idle,walking?&walk:nullptr,model,again));
                REQUIRE(local==again);
            }
        }
        std::printf("%s: max wrist error %.4fm, torso clearance %.4fm at %.3fs\n",actor.name.c_str(),double(worst),double(clearance),double(worst_time));
        REQUIRE(worst<.06f);REQUIRE(clearance>.13f);
    }
    return apricot_test::done("delivery_cutscene_tests");
}
