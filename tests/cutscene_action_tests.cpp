#include "../tools/cutscene_pose.h"
#include "core/asset_root.h"
#include "test_assert.h"
#include <cstdio>
using namespace apricot;
namespace cs=apricot::cutscene;
int main() {
    cs::Document doc;std::string error;
    if (!std::filesystem::exists(asset_path("models/characters/psx_pack/player_male_01/skin.emesh"))) {
        std::puts("SKIP private character rig checks (assets not staged)");return 0;
    }
    REQUIRE(cs::load(asset_path("cutscenes/johnny-opening.cutscene"),doc,error));
    REQUIRE(doc.actors.size()==3);REQUIRE(doc.cues.size()==19);
    const float action_shift=doc.cues[16].start-53.7f;
    REQUIRE_NEAR(doc.duration(),75.6f+action_shift,.001f);
    StaticEmesh parcel;REQUIRE(read_static_emesh(asset_path(doc.actors[2].mesh),parcel));
    REQUIRE(parcel.indices.size()==36);REQUIRE_NEAR(parcel.bounds.size().y,.10f,.001f);
    for (std::size_t index=0;index<2;++index) {
        const auto& actor=doc.actors[index];Skeleton skeleton;SkinnedEmesh mesh;Animation idle,walk;
        auto path=std::filesystem::path(asset_path(actor.mesh));path.replace_extension(".eskel");
        REQUIRE(skeleton.load(path.string()));REQUIRE(read_skinned_emesh(asset_path(actor.mesh),mesh));
        REQUIRE(idle.load(asset_path(actor.animation),skeleton));
        const bool walking=!actor.walk_animation.empty();
        if(walking) REQUIRE(walk.load(asset_path(actor.walk_animation),skeleton));
        const float scale=actor.height/mesh.bounds.size().y;
        const float plant=locomotion_plant_offset(mesh,skeleton,idle,scale);
        REQUIRE(!actor.gestures.empty());
        auto plain=actor;plain.gestures.clear();
        float gesture_hand_travel=0;
        for (const auto& gesture:actor.gestures) {
            const float at=gesture.start+gesture.duration*.4f;
            const auto model=cs::actor_transform(actor,mesh.bounds,plant,at);
            std::vector<glm::mat4> acted,neutral;
            REQUIRE(cs::sample_actor_pose(actor,at,skeleton,idle,walking?&walk:nullptr,model,acted));
            REQUIRE(cs::sample_actor_pose(plain,at,skeleton,idle,walking?&walk:nullptr,model,neutral));
            REQUIRE(acted!=neutral);
            VehicleDriverPose a,b;a.local=acted;b.local=neutral;
            driver_pose_detail::globals(skeleton,a);driver_pose_detail::globals(skeleton,b);
            for(const char* name:{"mixamorig:LeftFoot","mixamorig:RightFoot","mixamorig:Hips"}) {
                const auto bone=static_cast<std::size_t>(skeleton.find_bone(name));
                REQUIRE(glm::distance(glm::vec3{a.joints[bone][3]},glm::vec3{b.joints[bone][3]})*scale<.0001f);
            }
            for(const char* name:{"mixamorig:LeftHand","mixamorig:RightHand"}) {
                const auto bone=static_cast<std::size_t>(skeleton.find_bone(name));
                gesture_hand_travel=std::max(gesture_hand_travel,
                    glm::distance(glm::vec3{a.joints[bone][3]},glm::vec3{b.joints[bone][3]})*scale);
            }
            for (const auto& joint:acted) {
                REQUIRE(driver_pose_detail::finite(joint));
                REQUIRE_NEAR(glm::determinant(glm::mat3{joint}),1.f,.003f);
            }
        }
        std::printf("%s: gesture hand travel %.3f m\n",actor.name.c_str(),double(gesture_hand_travel));
        REQUIRE(gesture_hand_travel>.1f);
        auto protected_actor=actor;
        const float contact_time=63.1f+action_shift;
        protected_actor.gestures.push_back({contact_time-.5f,2,1,cs::GestureKind::Shrug});
        const auto contact_model=cs::actor_transform(actor,mesh.bounds,plant,contact_time);
        std::vector<glm::mat4> protected_pose,baseline_pose;
        REQUIRE(cs::sample_actor_pose(protected_actor,contact_time,skeleton,idle,walking?&walk:nullptr,contact_model,protected_pose));
        REQUIRE(cs::sample_actor_pose(plain,contact_time,skeleton,idle,walking?&walk:nullptr,contact_model,baseline_pose));
        REQUIRE(protected_pose==baseline_pose);
        float max_contact=0,max_time=0,min_torso_clearance=100;
        glm::vec3 worst_target{},worst_hand{},worst_shoulder{};
        for (int frame=1590;frame<=2268;++frame) {
            const float time=static_cast<float>(frame)/30+action_shift;
            const auto transform=cs::actor_transform(actor,mesh.bounds,plant,time);
            std::vector<glm::mat4> local;
            REQUIRE(cs::sample_actor_pose(actor,time,skeleton,idle,walking?&walk:nullptr,transform,local));
            for(const auto& joint:local) REQUIRE(driver_pose_detail::finite(joint));
            if (index==1&&time>=60+action_shift&&time<=64.1f+action_shift) {
                VehicleDriverPose body;body.local=local;driver_pose_detail::globals(skeleton,body);
                const auto box_model=cs::actor_transform(doc.actors[2],parcel.bounds,0,time);
                const auto inverse_box=glm::inverse(box_model.matrix());
                const auto bone_point=[&](const char* name) {
                    const int bone=skeleton.find_bone(name);REQUIRE(bone>=0);
                    return transform.transform_point(glm::vec3{body.joints[static_cast<std::size_t>(bone)][3]});
                };
                const auto hip=bone_point("mixamorig:Hips"),chest=bone_point("mixamorig:Spine2");
                // A 13 cm torso capsule protects the body while the parcel turns.
                // Wrist contact alone allowed the old path to pass through Lou.
                for (int step=0;step<=20;++step) {
                    const auto center=glm::mix(hip,chest,static_cast<float>(step)/20);
                    const auto box_local=glm::vec3{inverse_box*glm::vec4{center,1}};
                    const auto closest=glm::clamp(box_local,parcel.bounds.min,parcel.bounds.max);
                    min_torso_clearance=std::min(min_torso_clearance,
                        glm::distance(center,box_model.transform_point(closest)));
                }
            }
            const auto key=cs::actor_key(actor,time);
            if (key.reach>.999f) {
                VehicleDriverPose pose;pose.local=local;driver_pose_detail::globals(skeleton,pose);
                for(int side=0;side<2;++side) {
                    const auto bone=skeleton.find_bone(side?"mixamorig:RightHand":"mixamorig:LeftHand");
                    const auto hand=transform.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(bone)][3]});
                    const auto target=side?key.right_hand:key.left_hand;
                    if (glm::distance(hand,target)>max_contact) {
                        max_contact=glm::distance(hand,target);max_time=time;worst_target=target;worst_hand=hand;
                        const auto arm=skeleton.find_bone(side?"mixamorig:RightArm":"mixamorig:LeftArm");
                        worst_shoulder=transform.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(arm)][3]});
                    }
                }
            }
            // Lou routes around the checkout rather than crossing its solid footprint.
            const auto world=cs::actor_position(actor,time);const auto delta=world-glm::vec3{18,12,-12};
            const float x=.9945218954f*delta.x+.1045284633f*delta.z;
            const float z=-.1045284633f*delta.x+.9945218954f*delta.z;
            REQUIRE(!(x>-3.73f&&x<1.73f&&z>-10.83f&&z<-9.17f));
        }
        std::printf("%s: maximum full-grip wrist error %.4f m\n",actor.name.c_str(),static_cast<double>(max_contact));
        std::printf("time %.2f target %.3f %.3f %.3f hand %.3f %.3f %.3f shoulder %.3f %.3f %.3f\n",
            double(max_time),double(worst_target.x),double(worst_target.y),double(worst_target.z),
            double(worst_hand.x),double(worst_hand.y),double(worst_hand.z),
            double(worst_shoulder.x),double(worst_shoulder.y),double(worst_shoulder.z));
        REQUIRE(max_contact<.06f);
        if (index==1) {
            std::printf("Lou torso clearance: %.4f m\n",double(min_torso_clearance));
            REQUIRE(min_torso_clearance>.13f);
        }
        // Scrubbing backwards to a time must produce the same pose.
        const auto model=cs::actor_transform(actor,mesh.bounds,plant,63.2f+action_shift);
        std::vector<glm::mat4> first,second,scratch;
        REQUIRE(cs::sample_actor_pose(actor,63.2f+action_shift,skeleton,idle,walking?&walk:nullptr,model,first));
        REQUIRE(cs::sample_actor_pose(actor,70,skeleton,idle,walking?&walk:nullptr,model,scratch));
        REQUIRE(cs::sample_actor_pose(actor,63.2f+action_shift,skeleton,idle,walking?&walk:nullptr,model,second));
        REQUIRE(first==second);
    }
    return apricot_test::done("cutscene_action_tests");
}
