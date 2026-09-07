#pragma once

#include "game/cutscene_document.h"
#include "app/vehicle_driver_pose.h"

namespace apricot::cutscene {
inline Transform actor_transform(const Actor& actor,const AABB& bounds,float plant,float time) {
    Transform result;result.scale=glm::vec3{actor.height/std::max(bounds.size().y,.01f)};
    result.rotation=glm::angleAxis(glm::radians(actor_yaw(actor,time)),glm::vec3{0,1,0});
    const glm::vec3 pivot{bounds.center().x,bounds.min.y,bounds.center().z};
    result.position=actor_position(actor,time)-result.rotation*(result.scale*pivot);
    result.position.y+=plant;return result;
}
inline glm::mat4 blend_pose_joint(const glm::mat4& a,const glm::mat4& b,float t) {
    auto result=glm::mat4_cast(glm::slerp(glm::normalize(glm::quat_cast(glm::mat3{a})),
        glm::normalize(glm::quat_cast(glm::mat3{b})),t));
    result[3]=glm::mix(a[3],b[3],t);return result;
}

inline void apply_conversation_gestures(const Actor& actor,float time,const Skeleton& skeleton,
        const Transform& model,std::vector<glm::mat4>& local) {
    // Contact poses and locomotion retain priority over conversational acting.
    if (actor_key(actor,time).reach>0||actor_walk_weight(actor,time)>0) return;
    const auto inverse=glm::inverse(model.matrix());
    const auto native=[&](glm::vec3 point){return glm::vec3{inverse*glm::vec4{point,1}};};
    const auto forward=model.rotate({0,0,1});
    for (const auto& gesture:actor.gestures) {
        const float weight=gesture_weight(gesture,time);
        if (weight<=0) continue;
        VehicleDriverPose pose;pose.local=local;driver_pose_detail::globals(skeleton,pose);
        const auto world_joint=[&](int index){return model.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(index)][3]});};
        const int left=skeleton.find_bone("mixamorig:LeftArm"),right=skeleton.find_bone("mixamorig:RightArm");
        if (left<0||right<0) continue;
        const auto across=glm::normalize(world_joint(right)-world_joint(left));
        const auto rotate=[&](const char* name,glm::vec3 world_axis,float degrees) {
            const int index=skeleton.find_bone(name);if (index<0) return;
            const auto axis=glm::normalize(glm::mat3{inverse}*world_axis);
            const auto current=glm::quat_cast(glm::mat3{pose.joints[static_cast<std::size_t>(index)]});
            driver_pose_detail::set_rotation(skeleton,pose,index,glm::angleAxis(glm::radians(degrees),axis)*current);
        };
        const float phase=(time-gesture.start)/gesture.duration;
        const bool nod=gesture.kind==GestureKind::Nod,glance=gesture.kind==GestureKind::Glance;
        if (nod) rotate("mixamorig:Head",across,8*std::sin(phase*3.14159265f));
        else if (glance) {
            rotate("mixamorig:Head",{0,1,0},12);
            rotate("mixamorig:Neck",forward,3);
        } else {
            // Upper-body shifts leave the pelvis and planted feet untouched.
            rotate("mixamorig:Spine1",{0,1,0},gesture.kind==GestureKind::Dismiss?-4.f:3.f);
            rotate("mixamorig:Spine2",across,gesture.kind==GestureKind::Self?-4.f:-2.f);
            rotate("mixamorig:Head",across,2);
            for (int side=0;side<2;++side) {
                if (side==0&&gesture.kind!=GestureKind::Shrug) continue;
                const std::string prefix=side?"mixamorig:Right":"mixamorig:Left";
                const int arm=side?right:left;
                const int elbow=skeleton.find_bone(prefix+"ForeArm"),hand=skeleton.find_bone(prefix+"Hand");
                if (elbow<0||hand<0) continue;
                const auto shoulder=world_joint(arm);
                const float sign=side?1.f:-1.f;
                auto target=shoulder+forward*.30f+across*(sign*.06f)+glm::vec3{0,-.22f,0};
                if (gesture.kind==GestureKind::Dismiss)
                    target+=across*(.10f*std::sin(phase*3.14159265f));
                if (gesture.kind==GestureKind::Shrug)
                    target=shoulder+forward*.24f+across*(sign*.14f)+glm::vec3{0,-.19f,0};
                if (gesture.kind==GestureKind::Self)
                    target=shoulder+forward*.13f-across*.17f+glm::vec3{0,-.17f,0};
                const auto pole=shoulder+across*(sign*.3f)+glm::vec3{0,-.4f,0};
                driver_pose_detail::limb(skeleton,pose,arm,elbow,hand,native(target),native(pole));
                // Keep fingers pointing outward rather than letting the wrist droop.
                const int finger=skeleton.find_bone(prefix+"HandIndex1");
                if (finger>=0) driver_pose_detail::aim_child(skeleton,pose,hand,finger,
                    native(target+forward*.08f+glm::vec3{0,.015f,0}));
            }
        }
        for (std::size_t i=0;i<local.size();++i)
            local[i]=blend_pose_joint(local[i],pose.local[i],weight);
    }
}

// Sample from absolute scene time so scrubbing and replay reproduce the pass.
// The existing two-bone solver preserves arm lengths; only rotations change.
inline bool sample_actor_pose(const Actor& actor,float time,const Skeleton& skeleton,
        const Animation& idle,const Animation* walk,const Transform& model,
        std::vector<glm::mat4>& local) {
    idle.sample(std::max(0.f,time-actor.start),skeleton,local);
    if (walk) {
        const float weight=actor_walk_weight(actor,time);
        if (weight>0) {
            std::vector<glm::mat4> moving;walk->sample(time,skeleton,moving);
            for (std::size_t i=0;i<local.size();++i)
                local[i]=blend_pose_joint(local[i],moving[i],weight);
        }
    }
    strip_root_motion_xz(skeleton,local);
    apply_conversation_gestures(actor,time,skeleton,model,local);
    const auto key=actor_key(actor,time);
    if (actor.keys.empty()||key.reach<=0) return true;
    VehicleDriverPose pose;pose.local=local;pose.world=model;
    driver_pose_detail::globals(skeleton,pose);
    const int left=skeleton.find_bone("mixamorig:LeftArm");
    const int right=skeleton.find_bone("mixamorig:RightArm");
    if (left<0||right<0) return false;
    const auto inverse=glm::inverse(model.matrix());
    const auto point=[&](glm::vec3 p){return glm::vec3{inverse*glm::vec4{p,1}};};
    const auto shoulder=[&](int bone){return model.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(bone)][3]});};
    const glm::vec3 across=glm::normalize(shoulder(right)-shoulder(left));
    for (int side=0;side<2;++side) {
        const std::string prefix=side?"mixamorig:Right":"mixamorig:Left";
        const int arm=side?right:left;
        const int elbow=skeleton.find_bone(prefix+"ForeArm"),hand=skeleton.find_bone(prefix+"Hand");
        if (elbow<0||hand<0) return false;
        const auto target=side?key.right_hand:key.left_hand;
        const auto pole=shoulder(arm)+across*(side?.3f:-.3f)+glm::vec3{0,-.4f,0};
        driver_pose_detail::limb(skeleton,pose,arm,elbow,hand,point(target),point(pole));
        // A two-handed parcel grip curls inward under the edge. The idle
        // wrist rotation otherwise leaves the fingers dangling beside it.
        const int finger=skeleton.find_bone(prefix+"HandIndex1");
        const auto other=side?key.left_hand:key.right_hand;
        if(finger>=0 && glm::length(other-target)>.01f)
            driver_pose_detail::aim_child(skeleton,pose,hand,finger,
                point(target+glm::normalize(other-target)*.07f+glm::vec3{0,-.035f,0}));
    }
    for (std::size_t i=0;i<local.size();++i)
        local[i]=blend_pose_joint(local[i],pose.local[i],key.reach);
    return true;
}
} // namespace apricot::cutscene
