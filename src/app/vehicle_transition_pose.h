#pragma once

#include <utility>

#include "app/vehicle_driver_door.h"
#include "app/vehicle_driver_pose.h"
#include "game/vehicle_transition.h"

namespace apricot {

namespace transition_pose_detail {
inline glm::mat4 blend_joint(const glm::mat4& a, const glm::mat4& b, float t) {
    const auto rotation = glm::slerp(glm::normalize(glm::quat_cast(glm::mat3{a})),
                                    glm::normalize(glm::quat_cast(glm::mat3{b})), t);
    auto out = glm::mat4_cast(rotation);
    out[3] = glm::mix(a[3], b[3], t);
    return out;
}

// Contact-led exit: the outside foot crosses the sill and takes weight before
// the hips rise and the trailing foot leaves the cabin. Targets are in world
// space, so turning the pelvis never drags a planted foot with it.
inline void exit_pose(PlayerCarId car, const Skeleton& skeleton, const Transform& body,
        const VehicleDriverPose& seated, const VehicleDriverPose& standing,
        const VehicleTransitionSample& sample, VehicleDriverPose& out) {
    using namespace driver_pose_detail;
    car = player_car_body_id(car);
    const auto ramp = [](float t, float begin, float end) {
        return vehicle_transition_ease((t-begin)/(end-begin));
    };
    const float u = 1.f - std::clamp(sample.traverse, 0.f, 1.f);
    const auto& layout = vehicle_driver_layout(car);
    const auto door = vehicle_driver_door(car);
    const bool low_sports_cabin=car==PlayerCarId::VesperScythe || car==PlayerCarId::EmberGt;
    const int hips = skeleton.find_bone("mixamorig:Hips");
    const auto point = [&](const VehicleDriverPose& pose, int bone) {
        return pose.world.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(bone)][3]});
    };
    const auto seat_hip = point(seated, hips);
    const auto stand_hip = point(standing, hips);
    const auto outward = body.rotate({1,0,0});
    const float rise = ramp(u, .40f, .78f);
    out = seated;
    // The idle clip and bind rig use different forward axes. Rebase its root
    // before blending so the character turns once rather than twisting twice.
    const auto rebase = glm::mat4_cast(glm::inverse(seated.world.rotation)*standing.world.rotation);
    for (std::size_t i=0; i<out.local.size(); ++i) {
        const bool root = skeleton.bone(static_cast<int>(i)).parent < 0;
        const auto& name = skeleton.bone(static_cast<int>(i)).name;
        const bool arm = name.find("Arm")!=std::string::npos || name.find("Hand")!=std::string::npos;
        const auto target = root ? rebase*standing.local[i] : standing.local[i];
        out.local[i] = blend_joint(seated.local[i],target,
            root || static_cast<int>(i)==hips ? ramp(u,.05f,.70f) : arm ? ramp(u,.02f,.42f) : rise);
    }
    globals(skeleton, out);
    out.world.rotation = seated.world.rotation;
    out.world.scale = glm::mix(seated.world.scale, standing.world.scale, rise);
    glm::vec3 hip = glm::mix(seat_hip, stand_hip, ramp(u,.12f,.90f));
    // The compact's low cushion already puts the pelvis close to the floor.
    // Keep that pelvis height while leaning instead of adding the truck's dip.
    const float crouch=has_contact_driver_entry(car)?0.f:.10f;
    hip.y = glm::mix(seat_hip.y, stand_hip.y, rise) - crouch*ramp(u,.18f,.40f)*(1.f-rise);
    // Shift toward the planted foot while still crouched under the door frame.
    hip -= outward * (.06f * ramp(u,.22f,.43f) * (1.f-ramp(u,.60f,.9f)));
    if(low_sports_cabin) {
        // Rise over the exotic's wide sill while folding the torso through
        // the open doorway; its low seat cannot simply swivel at cushion height.
        hip.y+=.12f*ramp(u,.16f,.36f)*(1.f-rise);
        hip+=outward*(.08f*ramp(u,.20f,.40f)*(1.f-ramp(u,.65f,.90f)));
    }
    if(car==PlayerCarId::HalcyonSovereign)
        hip+=outward*(.10f*ramp(u,.20f,.40f)*(1.f-ramp(u,.65f,.90f)));
    out.world.position = hip - out.world.rotation *
        (out.world.scale * glm::vec3{out.joints[static_cast<std::size_t>(hips)][3]});
    if(low_sports_cabin) {
        const int spine=skeleton.find_bone("mixamorig:Spine");
        if(spine>=0) {
            const float lean=ramp(u,.08f,.25f)*(1.f-ramp(u,.62f,.90f));
            const auto axis=glm::inverse(out.world.rotation)*body.rotate({0,0,1});
            set_rotation(skeleton,out,spine,glm::angleAxis(glm::radians(-30.f)*lean,axis)*
                glm::quat_cast(glm::mat3{out.joints[static_cast<std::size_t>(spine)]}));
        }
    }
    const auto inverse = glm::inverse(out.world.matrix());
    const auto native = [&](glm::vec3 p) { return glm::vec3{inverse * glm::vec4{p,1}}; };
    const auto solve = [&](const char* side, const char* upper, const char* lower,
                           const char* end, glm::vec3 target, glm::vec3 pole, float weight,
                           float extension_margin=.001f) {
        const auto bone = [&](const char* name) {
            return skeleton.find_bone(std::string{"mixamorig:"}+side+name);
        };
        const int a=bone(upper), b=bone(lower), c=bone(end);
        const auto before_a=out.local[static_cast<std::size_t>(a)];
        const auto before_b=out.local[static_cast<std::size_t>(b)];
        limb(skeleton,out,a,b,c,native(target),native(pole),extension_margin);
        out.local[static_cast<std::size_t>(a)] = blend_joint(before_a,out.local[static_cast<std::size_t>(a)],weight);
        out.local[static_cast<std::size_t>(b)] = blend_joint(before_b,out.local[static_cast<std::size_t>(b)],weight);
        globals(skeleton,out);
    };
    for (int side=0; side<2; ++side) {
        const char* name = side==0 ? "Right" : "Left";
        const int foot = skeleton.find_bone(std::string{"mixamorig:"}+name+"Foot");
        const auto start = point(seated,foot);
        const auto finish = point(standing,foot);
        const float begin = side==1 ? .04f : .43f;
        const float crest = side==1 ? .25f : .62f;
        const float plant = side==1 ? .46f : .84f;
        const float foot_lift=car==PlayerCarId::EmberGt?.15f:low_sports_cabin?.13f:.11f;
        auto sill = body.transform_point({door.hinge.x+.16f,door.sill_y+foot_lift,
                                          layout.hip.z+(side==1?.16f:-.06f)});
        // Two eased arcs lift the sole above the sill before lowering it.
        const auto target = u < crest ? glm::mix(start,sill,ramp(u,begin,crest))
                                      : glm::mix(sill,finish,ramp(u,crest,plant));
        const int upper = skeleton.find_bone(std::string{"mixamorig:"}+name+"UpLeg");
        const auto thigh = point(out,upper);
        const auto hinge = body.rotate({0,0,-1});
        const auto bend = glm::cross(target-thigh,hinge);
        // The trailing foot stays in the footwell while the hips move
        // outward. Keep that knee forward/up until it clears the high sill;
        // an outward-only bend plane folds it down through the cab floor. The
        // compact also needs this stable pole on its leading knee through the
        // weight transfer; it prevents a bend-plane flip when entering.
        const bool protected_footwell=car==PlayerCarId::HarrowWorkman ||
            is_municipal_cruiser_91(car) || has_contact_driver_entry(car);
        const float cab_knee = has_contact_driver_entry(car) ? 1.f :
            protected_footwell && side==0 ? 1.f-ramp(u,.60f,.82f) : 0.f;
        auto pole = thigh + glm::mix(bend,body.rotate({.25f,.9f,1.f}),cab_knee);
        if (protected_footwell) {
            const int knee = skeleton.find_bone(std::string{"mixamorig:"}+name+"Leg");
            // Blend the bend direction, then solve the whole leg. Blending
            // solved joint rotations separately lets the ankle sag through
            // the floor even though both endpoint poses are above it.
            pole = glm::mix(point(seated,knee),pole,ramp(u,0.f,.15f));
        }
        // Patrol-sedan trailing feet start deep under the dashboard. A light
        // solve keeps that fold without crossing the analytic IK flip; the
        // outside foot still plants at full weight.
        const float solve_weight=protected_footwell
            ? (is_municipal_cruiser_91(car) && side==0 ? .10f : 1.f)
            : ramp(u,0.f,.15f);
        const float weight = solve_weight*(1.f-ramp(u,.90f,1.f));
        const float extension_margin=car==PlayerCarId::EmberGt ? .026f/out.world.scale.y : low_sports_cabin ||
            car==PlayerCarId::HalcyonSovereign || is_municipal_cruiser_91(car)
            ? .018f/out.world.scale.y : .001f;
        solve(name,"UpLeg","Leg","Foot",target,pole,weight,extension_margin);
        // Keep soles level as the knees fold over the sill.
        const auto rotation = glm::slerp(
            seated.world.rotation*glm::quat_cast(glm::mat3{seated.joints[static_cast<std::size_t>(foot)]}),
            standing.world.rotation*glm::quat_cast(glm::mat3{standing.joints[static_cast<std::size_t>(foot)]}),
            ramp(u,begin,plant));
        const auto original = out.local[static_cast<std::size_t>(foot)];
        set_rotation(skeleton,out,foot,glm::inverse(out.world.rotation)*rotation);
        out.local[static_cast<std::size_t>(foot)] = blend_joint(original,out.local[static_cast<std::size_t>(foot)],weight);
        globals(skeleton,out);
    }
    const float open = vehicle_transition_door_open(sample);
    const auto handle = vehicle_driver_door_transform(car,body,open).transform_point(door.handle);
    const float push = ramp(sample.progress,0.f,.14f)*(1.f-ramp(u,.06f,.32f));
    solve("Left","Arm","ForeArm","Hand",handle,
          body.transform_point(layout.handle_elbow),push);
    // The inside hand supports the rise, then the near hand draws the door shut.
    const float brace = ramp(u,.06f,.25f)*(1.f-ramp(u,.48f,.72f));
    solve("Right","Arm","ForeArm","Hand",body.transform_point(layout.hip+glm::vec3{-.18f,.08f,.22f}),
          hip+body.rotate({0,.15f,.6f}),brace);
    const float close = ramp(u,.80f,1.f)*(1.f-ramp(sample.settle,.40f,1.f));
    solve("Right","Arm","ForeArm","Hand",handle,
          hip+body.rotate({.35f,.15f,.55f}),close);
}

// Doorless mount/dismount. One foot stays beside the bike while the outside
// leg follows a high arc over the seat; the pelvis rises too, so this reads as
// saddling instead of a car-entry slide.
inline void motorbike_saddle_pose(const Skeleton& skeleton,const Transform& body,
        const VehicleDriverPose& seated,const VehicleDriverPose& standing,
        float seat_fraction,VehicleDriverPose& out) {
    using namespace driver_pose_detail;
    const float t=vehicle_transition_ease(std::clamp(seat_fraction,0.f,1.f));
    if(t<=0.f) {out=standing;return;}
    if(t>=1.f) {out=seated;return;}
    const int hips=skeleton.find_bone("mixamorig:Hips");
    out=standing;
    const auto rebase=glm::mat4_cast(glm::inverse(standing.world.rotation)*seated.world.rotation);
    for(std::size_t i=0;i<out.local.size();++i) {
        const bool root=skeleton.bone(static_cast<int>(i)).parent<0;
        const auto target=root?rebase*seated.local[i]:seated.local[i];
        out.local[i]=blend_joint(standing.local[i],target,t);
    }
    globals(skeleton,out);
    const auto point=[&](const VehicleDriverPose& pose,int bone) {
        return pose.world.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(bone)][3]});
    };
    const auto standing_hip=point(standing,hips);
    const auto seated_hip=point(seated,hips);
    auto hip=glm::mix(standing_hip,seated_hip,t);
    hip.y+=.24f*std::sin(3.14159265359f*t);
    hip+=body.rotate({.10f*std::sin(3.14159265359f*t),0,0});
    out.world.rotation=glm::slerp(standing.world.rotation,seated.world.rotation,t);
    out.world.scale=glm::mix(standing.world.scale,seated.world.scale,t);
    out.world.position=hip-out.world.rotation*(out.world.scale*
        glm::vec3{out.joints[static_cast<std::size_t>(hips)][3]});

    // The +X leg is the one visibly thrown across from the staging side.
    const int upper=skeleton.find_bone("mixamorig:LeftUpLeg");
    const int lower=skeleton.find_bone("mixamorig:LeftLeg");
    const int foot=skeleton.find_bone("mixamorig:LeftFoot");
    const auto start=point(standing,foot),finish=point(seated,foot);
    const auto crest=body.transform_point({.28f,1.18f,-.10f});
    const auto first=glm::mix(start,crest,t),second=glm::mix(crest,finish,t);
    const auto target=glm::mix(first,second,t);
    const auto inverse=glm::inverse(out.world.matrix());
    const auto native=[&](glm::vec3 p){return glm::vec3{inverse*glm::vec4{p,1}};};
    limb(skeleton,out,upper,lower,foot,native(target),
         native(body.transform_point({.42f,1.05f,.18f})),.012f/out.world.scale.y);
}
}

inline bool make_vehicle_transition_pose(
        PlayerCarId car, const Skeleton& skeleton, const AABB& bounds, const Transform& body,
        const Transform& standing_world, const std::vector<glm::mat4>& standing_local,
        const VehicleTransitionSample& sample, VehicleDriverPose& out) {
    using namespace driver_pose_detail;
    using transition_pose_detail::blend_joint;
    car = player_car_body_id(car);
    VehicleDriverPose seated;
    if (!make_vehicle_driver_pose(car, skeleton, bounds, body, seated) ||
        standing_local.size() != seated.local.size() || !finite(standing_world.matrix()) ||
        glm::any(glm::lessThanEqual(standing_world.scale, glm::vec3{0})) ||
        !std::isfinite(sample.traverse) || !std::isfinite(sample.reach) ||
        !std::isfinite(sample.settle) || !std::isfinite(sample.progress)) return false;
    for (const auto& matrix : standing_local) if (!finite(matrix)) return false;
    const float t = std::clamp(sample.traverse, 0.f, 1.f);
    if (t >= 1.f && sample.direction != VehicleTransitionDirection::Exit) {
        out = std::move(seated); return true;
    }

    VehicleDriverPose standing;
    standing.local = standing_local;
    standing.world = standing_world;
    globals(skeleton, standing);
    if(is_motorbike(car)) {
        transition_pose_detail::motorbike_saddle_pose(
            skeleton,body,seated,standing,t,out);
        for(const auto& matrix:out.skin) if(!finite(matrix)) return false;
        skin_matrices_to_dual_quaternions(out.skin,out.dual_real,out.dual_part);
        return true;
    }
    if (sample.direction == VehicleTransitionDirection::Exit) {
        transition_pose_detail::exit_pose(car,skeleton,body,seated,standing,sample,out);
        for (const auto& matrix : out.skin) if (!finite(matrix)) return false;
        skin_matrices_to_dual_quaternions(out.skin,out.dual_real,out.dual_part);
        return true;
    }
    const auto hips = static_cast<std::size_t>(skeleton.find_bone("mixamorig:Hips"));
    const glm::vec3 start_hip = standing_world.transform_point(glm::vec3{standing.joints[hips][3]});
    const auto& layout = vehicle_driver_layout(car);
    const glm::vec3 seat_hip = body.transform_point(layout.hip);

    // Solve just two handle-reaching poses. Interpolating their local rotations
    // keeps the elbow on the same side and leaves leg articulation authored.
    const auto handle_pose = [&](float opening) {
        auto pose = standing;
        const int arm = skeleton.find_bone("mixamorig:LeftArm");
        const int elbow = skeleton.find_bone("mixamorig:LeftForeArm");
        const int hand = skeleton.find_bone("mixamorig:LeftHand");
        const auto inverse = glm::inverse(standing_world.matrix());
        const auto target = vehicle_driver_door_transform(car, body, opening)
            .transform_point(vehicle_driver_door(car).handle);
        const auto pole = body.transform_point(layout.handle_elbow);
        limb(skeleton, pose, arm, elbow, hand,
             glm::vec3{inverse * glm::vec4{target, 1}},
             glm::vec3{inverse * glm::vec4{pole, 1}});
        return pose;
    };
    const auto closed = handle_pose(0.f);
    // A full-size sedan door on a 68-degree hinge sweeps its handle as far as
    // a fleet cruiser's does, and pulling that inside the short reach window
    // moves the hand faster than the rest of the body can follow.
    const bool long_door = is_municipal_cruiser_91(car) || car==PlayerCarId::EmberGt ||
        car == PlayerCarId::LegacyCar5Next ||
        car == PlayerCarId::LegacyCar5NextPolice;
    // Release long truck/fleet-sedan doors before they reach the stop, keeping
    // the pulling arm in front of the shoulder instead of reaching behind it.
    const auto opened = handle_pose(car == PlayerCarId::HarrowWorkman ? .70f :
                                   car == PlayerCarId::HalcyonSovereign ? .65f :
                                   long_door ? .72f :
                                   has_contact_driver_entry(car) ? .90f : 1.f);
    const float early_reach=car==PlayerCarId::HalcyonSovereign
        ? .12f*vehicle_transition_ease((sample.approach-.85f)/.15f) : 0.f;
    const float reaching = vehicle_transition_ease((sample.reach+early_reach) /
        (car==PlayerCarId::HalcyonSovereign?.42f:long_door?.46f:.3f));
    const float opening = vehicle_transition_ease((sample.reach - .3f) / .7f);
    const float sit = vehicle_transition_ease(t);
    out = standing;
    for (int i = 0; i < skeleton.bone_count(); ++i) {
        const auto index = static_cast<std::size_t>(i);
        const auto& name = skeleton.bone(i).name;
        const bool left_arm = name.find("LeftArm") != std::string::npos ||
            name.find("LeftForeArm") != std::string::npos || name.find("LeftHand") != std::string::npos;
        const bool right_leg = name.find("RightUpLeg") != std::string::npos ||
            name.find("RightLeg") != std::string::npos || name.find("RightFoot") != std::string::npos ||
            name.find("RightToe") != std::string::npos;
        const bool left_leg = name.find("LeftUpLeg") != std::string::npos ||
            name.find("LeftLeg") != std::string::npos || name.find("LeftFoot") != std::string::npos ||
            name.find("LeftToe") != std::string::npos;
        auto from = standing_local[index];
        float blend = sit;
        if (left_arm) {
            from = blend_joint(from, blend_joint(closed.local[index], opened.local[index], opening), reaching);
            blend = vehicle_transition_ease(t / .65f);
        } else if (right_leg) blend = vehicle_transition_ease(t / .70f);
        else if (left_leg) blend = vehicle_transition_ease((t - .12f) / .88f);
        out.local[index] = blend_joint(from, seated.local[index], blend);
    }
    globals(skeleton, out);
    // Lower the hips into the seat. No upward vault, bone scaling or moving
    // leg IK poles: the seated pose supplies the normal knee bend throughout.
    glm::vec3 hip = glm::mix(start_hip, seat_hip, vehicle_transition_ease((t - .1f) / .9f));
    hip.y = glm::mix(start_hip.y, seat_hip.y, vehicle_transition_ease((t - .2f) / .8f));
    out.world.rotation = glm::slerp(standing_world.rotation, seated.world.rotation, sit);
    out.world.scale = glm::mix(standing_world.scale, seated.world.scale, sit);
    out.world.position = hip - out.world.rotation *
        (out.world.scale * glm::vec3{out.joints[hips][3]});
    if(has_contact_driver_entry(car)) {
        // Retrace the proven foot contacts when sitting down. Rotation-only
        // standing/seated blends sweep the compact's shins through its floor.
        // Retain the entry-specific handle reach, while feet, pelvis and torso
        // follow the reverse of the crouched, one-foot-at-a-time exit.
        VehicleDriverPose contact;
        auto contact_sample=sample;
        // Spend more of the shorter entry phase transferring weight through
        // the door. This monotonic remap keeps both endpoints and fixed timing.
        const float transfer_time=car==PlayerCarId::AlderPip?.09f:.05f;
        contact_sample.traverse=t+transfer_time*std::sin(6.28318530718f*t);
        transition_pose_detail::exit_pose(car,skeleton,body,seated,standing,contact_sample,contact);
        for(std::size_t i=0;i<out.local.size();++i) {
            const auto& name=skeleton.bone(static_cast<int>(i)).name;
            if(name.find("Arm")!=std::string::npos || name.find("Hand")!=std::string::npos)
                contact.local[i]=out.local[i];
        }
        out=std::move(contact);
        globals(skeleton,out);
    }
    for (const auto& matrix : out.skin) if (!finite(matrix)) return false;
    skin_matrices_to_dual_quaternions(out.skin, out.dual_real, out.dual_part);
    return true;
}

inline bool make_mistral_transition_pose(
        const Skeleton& skeleton, const AABB& bounds, const Transform& body,
        const Transform& standing_world, const std::vector<glm::mat4>& standing_local,
        const VehicleTransitionSample& sample, VehicleDriverPose& out) {
    return make_vehicle_transition_pose(PlayerCarId::VesperMistral, skeleton, bounds, body,
                                        standing_world, standing_local, sample, out);
}

} // namespace apricot
