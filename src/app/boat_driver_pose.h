#pragma once
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_transition_pose.h"
#include "game/boat_transition.h"

namespace apricot {
// Measured against the Marlin's cushion, helm rim and cockpit floor, metres.
inline const VehicleDriverLayout kMarlinSeatRig{
    {.51f,.62f,-.70f},
    {{.38f,.82f,-.245f},{.64f,.82f,-.245f}},
    {{.39f,.23f,-.20f},{.63f,.23f,-.20f}},
    {{.39f,.78f,-.20f},{.63f,.78f,-.20f}},
    {{.12f,.72f,-.65f},{.90f,.72f,-.65f}},{1.1f,.87f,-1.55f},2.6f};

inline bool make_boat_driver_pose(const Skeleton& skeleton,const AABB& bounds,
        const Transform& body,float steering,float time,VehicleDriverPose& out) {
    auto rig=kMarlinSeatRig;
    if(!std::isfinite(steering+time)) return false;
    const auto turn=glm::angleAxis(std::clamp(steering,-1.f,1.f)*.30f,glm::vec3{0,0,1});
    const glm::vec3 hub{.51f,.75f,-.245f};
    for(auto& wrist:rig.wrists) wrist=hub+turn*(wrist-hub);
    return make_seated_driver_pose(rig,skeleton,bounds,body,out,-10.f+std::sin(time*1.5f)*.5f);
}

inline bool make_boat_transition_keypose(const Skeleton& skeleton,const AABB& bounds,
        const Transform& body,const Transform& standing_world,
        const std::vector<glm::mat4>& standing_local,float fraction,VehicleDriverPose& out) {
    using namespace driver_pose_detail;
    VehicleDriverPose seated;
    if(!std::isfinite(fraction) || !make_boat_driver_pose(skeleton,bounds,body,0,0,seated) ||
        standing_local.size()!=seated.local.size() || !finite(standing_world.matrix())) return false;
    for(const auto& matrix:standing_local) if(!finite(matrix)) return false;
    const float t=std::clamp(fraction,0.f,1.f);
    if(t>=1) {out=std::move(seated);return true;}
    VehicleDriverPose standing;standing.local=standing_local;standing.world=standing_world;
    globals(skeleton,standing);
    const auto hips=static_cast<std::size_t>(skeleton.find_bone("mixamorig:Hips"));
    const auto start=standing_world.transform_point(glm::vec3{standing.joints[hips][3]});
    const auto local_start=glm::vec3{glm::inverse(body.matrix())*glm::vec4{start,1}};
    const float side=local_start.x<0?-1.f:1.f;
    const auto ease=[](float x){return vehicle_transition_ease(x);};
    const auto path=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,float u) {
        if(u<.35f) return glm::mix(a,b,ease(u/.35f));
        if(u<.65f) return glm::mix(b,c,ease((u-.35f)/.30f));
        return glm::mix(c,d,ease((u-.65f)/.35f));
    };
    const float fold=ease(t);
    out=standing;
    for(std::size_t i=0;i<out.local.size();++i)
        out.local[i]=transition_pose_detail::blend_joint(standing_local[i],seated.local[i],fold);
    globals(skeleton,out);
    const auto hip=path(start,body.transform_point({side*1.40f,1.60f,-1.55f}),
        body.transform_point({side*.65f,1.10f,-1.22f}),body.transform_point(kMarlinSeatRig.hip),t);
    out.world.rotation=glm::slerp(standing_world.rotation,seated.world.rotation,ease(t/.7f));
    out.world.scale=glm::mix(standing_world.scale,seated.world.scale,fold);
    out.world.position=hip-out.world.rotation*(out.world.scale*glm::vec3{out.joints[hips][3]});
    const auto native=[&](glm::vec3 p){return glm::vec3{glm::inverse(out.world.matrix())*glm::vec4{p,1}};};
    const char* sides[]={"Right","Left"};
    const float weight=ease(t/.16f)*(1.f-ease((t-.84f)/.16f));
    const auto base_local=out.local;
    for(int i=0;i<2;++i) {
        const auto bone=[&](const char* suffix){return skeleton.find_bone(std::string{"mixamorig:"}+sides[i]+suffix);};
        const int foot=bone("Foot"),hand=bone("Hand");
        const auto start_foot=standing_world.transform_point(glm::vec3{standing.joints[static_cast<std::size_t>(foot)][3]});
        const float delay=(i==(side>0?0:1))?0.f:.22f;
        const float leg_t=std::clamp((t-delay)/(1.f-delay),0.f,1.f);
        const auto ankle=path(start_foot,body.transform_point({side*1.42f,1.02f,-1.55f}),
            body.transform_point({side*.65f,.24f,-1.22f}),body.transform_point(kMarlinSeatRig.ankles[i]),leg_t);
        limb(skeleton,out,bone("UpLeg"),bone("Leg"),foot,native(ankle),
            native(body.transform_point({side*.8f,1.4f,-.50f})));
        set_rotation(skeleton,out,foot,glm::quat_cast(glm::mat3{
            glm::inverse(skeleton.bone(foot).inverse_bind)}));
        // Hands held ahead for balance; do not force a standing shoulder to
        // reach a waist-low gunwale by stretching the arm.
        const auto brace=body.transform_point({side*.9f,1.30f,-.9f+float(i)*.18f});
        const auto wrist=glm::mix(brace,body.transform_point(kMarlinSeatRig.wrists[i]),ease((t-.55f)/.40f));
        limb(skeleton,out,bone("Arm"),bone("ForeArm"),hand,native(wrist),
            native(body.transform_point({side*1.4f,1.15f,-1.8f})));
    }
    // Blend joint rotations, not only target positions: an IK pole can change
    // the knee/elbow even when its endpoint has zero weight.
    for(std::size_t i=0;i<out.local.size();++i)
        out.local[i]=transition_pose_detail::blend_joint(base_local[i],out.local[i],weight);
    globals(skeleton,out);
    for(const auto& matrix:out.skin) if(!finite(matrix)) return false;
    skin_matrices_to_dual_quaternions(out.skin,out.dual_real,out.dual_part);
    return true;
}
inline bool make_boat_transition_pose(const Skeleton& skeleton,const AABB& bounds,
        const Transform& body,const Transform& standing_world,
        const std::vector<glm::mat4>& standing_local,float fraction,VehicleDriverPose& out) {
    if(!std::isfinite(fraction)) return false;
    const float t=std::clamp(fraction,0.f,1.f);
    constexpr float knots[]={0,.18f,.35f,.50f,.65f,.82f,1};
    std::size_t interval=0;
    while(interval<5 && t>knots[interval+1]) ++interval;
    VehicleDriverPose a,b;
    if(!make_boat_transition_keypose(skeleton,bounds,body,standing_world,standing_local,knots[interval],a) ||
       !make_boat_transition_keypose(skeleton,bounds,body,standing_world,standing_local,knots[interval+1],b)) return false;
    const float blend=vehicle_transition_ease((t-knots[interval])/(knots[interval+1]-knots[interval]));
    out=a;
    for(std::size_t i=0;i<out.local.size();++i)
        out.local[i]=transition_pose_detail::blend_joint(a.local[i],b.local[i],blend);
    driver_pose_detail::globals(skeleton,out);
    const auto hips=static_cast<std::size_t>(skeleton.find_bone("mixamorig:Hips"));
    const auto hip=glm::mix(a.world.transform_point(glm::vec3{a.joints[hips][3]}),
        b.world.transform_point(glm::vec3{b.joints[hips][3]}),blend);
    out.world.rotation=glm::slerp(a.world.rotation,b.world.rotation,blend);
    out.world.scale=glm::mix(a.world.scale,b.world.scale,blend);
    out.world.position=hip-out.world.rotation*(out.world.scale*glm::vec3{out.joints[hips][3]});
    skin_matrices_to_dual_quaternions(out.skin,out.dual_real,out.dual_part);
    return true;
}
} // namespace apricot
