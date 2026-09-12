#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

#include "app/vehicle_driver_pose.h"
#include "app/vehicle_transition_pose.h"
#include "core/asset_root.h"
#include "test_assert.h"

using namespace apricot;

namespace {
glm::vec3 fitted_body_scale(PlayerCarId model) {
    const auto& d=player_car_definition(model);
    const float track=d.physical_half_track>0.f?d.physical_half_track:.78f;
    const float wheelbase=d.physical_half_wheelbase>0.f?2.f*d.physical_half_wheelbase:2.7f;
    const float length=wheelbase/(d.wheel_front_z+d.wheel_rear_z);
    return {track/d.wheel_x,length,length};
}
float vehicle_triangle_floor_clearance(PlayerCarId model,glm::vec3 a,glm::vec3 b,glm::vec3 c);

void cooked_pose_contracts() {
    for(const auto model:{PlayerCarId::AlderPip,PlayerCarId::VesperScythe,PlayerCarId::HalcyonSovereign}) {
        const auto& definition=player_car_definition(model);
        const auto folder=std::filesystem::path(definition.mesh_path).parent_path().string()+"/";
        const auto path=asset_path(folder+"driver_door.emesh");
        if(!std::filesystem::is_regular_file(path)) continue;
        StaticEmesh door,body;
        REQUIRE(read_static_emesh(path,door));
        REQUIRE(read_static_emesh(asset_path(folder+"body_open.emesh"),body));
        const auto contract=vehicle_driver_door(model);
        REQUIRE_NEAR(contract.sill_y,door.bounds.min.y,.001f);
        REQUIRE_NEAR(contract.sill_y+contract.panel_height,door.bounds.max.y,.001f);
        const float floor_y=model==PlayerCarId::VesperScythe?.24f:
            model==PlayerCarId::HalcyonSovereign?.35f:.29f;
        bool floor=false;
        for(std::size_t i=0;i<body.indices.size();i+=3) {
            const auto& a=body.vertices[body.indices[i]];
            const auto& b=body.vertices[body.indices[i+1]];
            const auto& c=body.vertices[body.indices[i+2]];
            if(std::fabs(a.py-floor_y)<.0001f && std::fabs(b.py-floor_y)<.0001f && std::fabs(c.py-floor_y)<.0001f &&
               std::min({a.px,b.px,c.px})<-.7f && std::max({a.px,b.px,c.px})>.7f) floor=true;
        }
        REQUIRE(floor);
        REQUIRE_NEAR(glm::length(fitted_body_scale(model)-glm::vec3{1}),0.f,.001f);
    }
    apricot_test::pass("new articulated cars use actual chassis fits, cooked floors and full door heights");
}

void workman_glass_openings() {
    const std::string root="models/vehicles/harrow_workman/";
    if (!std::filesystem::is_regular_file(asset_path(root+"body_open.emesh"))) return;
    StaticEmesh body,door;
    REQUIRE(read_static_emesh(asset_path(root+"body_open.emesh"),body));
    REQUIRE(read_static_emesh(asset_path(root+"driver_door.emesh"),door));
    const auto point=[](const auto& v) { return glm::vec3{v.px,v.py,v.pz}; };
    for (const char* name:{"windshield","rear_glass","passenger_glass","driver_glass"}) {
        StaticEmesh pane;
        REQUIRE(read_static_emesh(asset_path(root+name+".emesh"),pane));
        REQUIRE(pane.indices.size()==6u);
        glm::vec3 centre{0};
        for (auto index:pane.indices) centre+=point(pane.vertices[index])/6.f;
        const auto normal=glm::normalize(glm::cross(point(pane.vertices[1])-point(pane.vertices[0]),
                                                   point(pane.vertices[2])-point(pane.vertices[0])));
        // A short ray through the centre must not hit the old opaque cab shell.
        for (const auto* shell:{&body,&door}) for (std::size_t i=0;i<shell->indices.size();i+=3) {
            const auto a=point(shell->vertices[shell->indices[i]]);
            const auto e1=point(shell->vertices[shell->indices[i+1]])-a;
            const auto e2=point(shell->vertices[shell->indices[i+2]])-a;
            const auto h=glm::cross(normal,e2);
            const float determinant=glm::dot(e1,h);
            if (std::abs(determinant)<1e-7f) continue;
            const auto from=centre-normal*.10f-a;
            const float u=glm::dot(from,h)/determinant;
            const auto q=glm::cross(from,e1);
            const float v=glm::dot(normal,q)/determinant;
            const float distance=glm::dot(e2,q)/determinant;
            REQUIRE(!(u>=0.f && v>=0.f && u+v<=1.f && distance>=0.f && distance<=.20f));
        }
    }
    apricot_test::pass("four Workman panes cover real cab openings without opaque backing");
}

void hinged_door(PlayerCarId model) {
    const auto layout=vehicle_driver_door(model);
    for (int yaw=0;yaw<360;yaw+=90) {
        Transform body;
        body.position={12,4,-7};body.scale={.78f/.91f,2.7f/2.78f,2.7f/2.78f};
        body.set_euler_deg(6.f,float(yaw),-4.f);
        const auto hinge=body.transform_point(layout.hinge);
        const auto handle=body.transform_point(layout.handle);
        for (int i=0;i<=20;++i) {
            const auto door=vehicle_driver_door_transform(model,body,float(i)/20.f);
            REQUIRE(glm::distance(door.transform_point(layout.hinge),hinge)<.0001f);
            REQUIRE_NEAR(glm::distance(door.transform_point(layout.handle),hinge),
                         glm::distance(handle,hinge),.0001f);
            REQUIRE(glm::dot(door.transform_point(layout.handle)-handle,body.rotate({1,0,0}))>=-.0001f);
        }
    }
    apricot_test::pass("door swings outward on a fixed hinge without shearing its fitted panel");
}

void transition_clock() {
    REQUIRE(kVehicleTransitionTicks==300u);
    VehicleTransitionState idle;
    REQUIRE(!advance_vehicle_transition(idle));
    VehicleTransitionState enter{VehicleTransitionDirection::Enter,0};
    int completed=0;
    for (uint32_t tick=0;tick<=kVehicleTransitionTicks;++tick) {
        const auto a=sample_vehicle_transition(enter);
        const auto b=sample_vehicle_transition({VehicleTransitionDirection::Exit,tick});
        if (b.traverse>0.f && b.traverse<1.f) REQUIRE_NEAR(vehicle_transition_door_open(b),1.f,1e-6f);
        if (b.settle>0.f) REQUIRE_NEAR(b.traverse,0.f,1e-6f);
        if (b.approach<1.f) REQUIRE_NEAR(vehicle_transition_door_open(b),0.f,1e-6f);
        if (a.traverse>0.f && a.traverse<1.f) REQUIRE_NEAR(vehicle_transition_door_open(a),1.f,1e-6f);
        REQUIRE_NEAR(a.progress,float(tick)/float(kVehicleTransitionTicks),1e-6f);
        if (advance_vehicle_transition(enter)) ++completed;
    }
    REQUIRE(completed==1);
    REQUIRE(!advance_vehicle_transition(enter));
    REQUIRE_NEAR(sample_vehicle_transition(enter).settle,1.f,1e-6f);
    REQUIRE_NEAR(vehicle_transition_door_open(sample_vehicle_transition(enter)),0.f,1e-6f);
    const auto invalid=sample_vehicle_transition(enter,std::numeric_limits<float>::quiet_NaN());
    REQUIRE(std::isfinite(invalid.progress));
    apricot_test::pass("transition clock completes once; exit closes only after stepping clear");
}

void transition_motion(PlayerCarId model,const SkinnedEmesh& mesh,const Skeleton& skeleton) {
    Transform body;
    const auto& definition=player_car_definition(model);
    const auto& layout=vehicle_driver_layout(model);
    body.scale=fitted_body_scale(model);
    VehicleDriverPose seated;
    REQUIRE(make_vehicle_driver_pose(model,skeleton,mesh.bounds,body,seated));
    Animation idle;
    REQUIRE(idle.load(asset_path("models/characters/psx_pack/animations/idle.eanim"),skeleton));
    std::vector<glm::mat4> standing_local;
    idle.sample(0,skeleton,standing_local);
    strip_root_motion_xz(skeleton,standing_local);
    Transform standing;
    standing.scale=seated.world.scale;
    standing.rotation=seated.world.rotation;
    standing.position=body.transform_point({layout.approach_x,0,layout.hip.z})+glm::vec3{.55f,0,0};
    standing.position.y-=mesh.bounds.min.y*standing.scale.y;
    std::vector<glm::vec3> previous;
    float largest_step=0;uint32_t largest_tick=0;
    float floor_clearance=100.f;uint32_t floor_tick=0;
    std::vector<glm::mat4> standing_skin;
    skeleton.compute_skin_matrices(standing_local,standing_skin);
    const int hips=skeleton.find_bone("mixamorig:Hips");
    const auto standing_hips=standing.transform_point(glm::vec3{(standing_skin[static_cast<std::size_t>(hips)]*
        glm::inverse(skeleton.bone(hips).inverse_bind))[3]});
    for (uint32_t tick=0;tick<=kVehicleTransitionTicks;++tick) {
        VehicleDriverPose pose;
        const auto sample=sample_vehicle_transition({VehicleTransitionDirection::Enter,tick});
        REQUIRE(make_vehicle_transition_pose(model,skeleton,mesh.bounds,body,standing,standing_local,sample,pose));
        const auto hip=pose.world.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(hips)][3]});
        REQUIRE(hip.y<=standing_hips.y+.001f);
        REQUIRE(hip.y>=body.transform_point(layout.hip).y-.001f);
        for (int i=0;i<skeleton.bone_count();++i) {
            if (skeleton.bone(i).parent<0 || i==hips) continue;
            const float before=glm::length(glm::vec3{standing_local[static_cast<std::size_t>(i)][3]});
            const float after=glm::length(glm::vec3{pose.local[static_cast<std::size_t>(i)][3]});
            REQUIRE_NEAR(after,before,.001f);
        }
        for (const auto& matrix:pose.skin) {
            REQUIRE(driver_pose_detail::finite(matrix));
            REQUIRE_NEAR(glm::determinant(glm::mat3{matrix}),1.f,.003f);
        }
        std::vector<glm::vec3> vertices;
        for (const auto& vertex:mesh.vertices) {
            const auto point=pose.world.transform_point(skinned_vertex_position(vertex,pose.dual_real,pose.dual_part));
            REQUIRE(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
            if (model==PlayerCarId::HarrowWorkman) {
                const glm::vec3 in_car{glm::inverse(body.matrix())*glm::vec4{point,1}};
                if (std::fabs(in_car.x)<.86f && in_car.z>-.43f && in_car.z<.53f)
                    REQUIRE(in_car.y<1.93f);
            }
            // Car 5-NEXT's cabin is the tightest in the catalog: its imported
            // body is squashed to .664 of its authored height, so a rig seated
            // at the donor cruiser's hip puts its head through the headliner.
            // The lining sits .05 inside the 2.019 roof; stay under it. Both
            // halves of the fit are load bearing -- SEAT_DROP_M and
            // kCar5NextRecline -- and dropping further breaks the exit step.
            if(model==PlayerCarId::LegacyCar5Next) {
                const glm::vec3 in_car{glm::inverse(body.matrix())*glm::vec4{point,1}};
                if(std::fabs(in_car.x)<.85f && in_car.z>-.60f && in_car.z<1.00f)
                    REQUIRE(in_car.y<1.96f);
            }
            if(model==PlayerCarId::AlderPip) {
                const glm::vec3 in_car{glm::inverse(body.matrix())*glm::vec4{point,1}};
                if(std::fabs(in_car.x)<.69f && in_car.z>-1.58f && in_car.z<.18f)
                    REQUIRE(in_car.y<1.48f);
            }
            if (!previous.empty()) {
                const float distance=glm::distance(point,previous[vertices.size()]);
                if (distance>largest_step) {largest_step=distance;largest_tick=tick;}
            }
            if (tick==kVehicleTransitionTicks) {
                const auto end=seated.world.transform_point(skinned_vertex_position(vertex,seated.dual_real,seated.dual_part));
                REQUIRE(glm::distance(point,end)<.0001f);
            }
            vertices.push_back(point);
        }
        if(has_contact_driver_entry(model)) {
            const auto inverse=glm::inverse(body.matrix());
            for(std::size_t i=0;i<mesh.indices.size();i+=3) {
                const auto a=glm::vec3{inverse*glm::vec4{vertices[mesh.indices[i]],1}};
                const auto b=glm::vec3{inverse*glm::vec4{vertices[mesh.indices[i+1]],1}};
                const auto c=glm::vec3{inverse*glm::vec4{vertices[mesh.indices[i+2]],1}};
                const float clearance=vehicle_triangle_floor_clearance(model,a,b,c);
                if(clearance<floor_clearance) {floor_clearance=clearance;floor_tick=tick;}
            }
        }
        previous=std::move(vertices);
    }
    std::printf("      %s transition largest vertex step %.4f m at tick %u\n",definition.model,static_cast<double>(largest_step),largest_tick);
    REQUIRE(largest_step<.06f);
    if(has_contact_driver_entry(model)) {
        std::printf("      entering cabin clearance %.4f at tick %u\n",floor_clearance,floor_tick);
        REQUIRE(floor_clearance>=-.001f);
    }
    apricot_test::pass("actual supplied rig moves continuously and settles into the exact fitted driver pose");
}

glm::vec3 joint_world(const Skeleton& s, const VehicleDriverPose& pose,
                       const std::string& name) {
    const int index = s.find_bone("mixamorig:" + name);
    REQUIRE(index >= 0);
    return pose.world.transform_point(glm::vec3{pose.joints[static_cast<std::size_t>(index)][3]});
}

// Clip each rendered triangle to the cab floor footprint. Vertex-only checks
// miss a shin triangle cutting the sill corner with all three vertices outside.
float triangle_footprint_clearance(glm::vec3 a,glm::vec3 b,glm::vec3 c,
    glm::vec4 edges,float top) {
    std::vector<glm::vec3> polygon{a,b,c};
    const auto clip = [&](int axis, float edge, float sign) {
        if (polygon.empty()) return;
        std::vector<glm::vec3> next;
        auto previous=polygon.back();
        float previous_distance=sign*(previous[axis]-edge);
        for (auto current:polygon) {
            const float distance=sign*(current[axis]-edge);
            if ((distance>=0)!=(previous_distance>=0))
                next.push_back(glm::mix(previous,current,previous_distance/(previous_distance-distance)));
            if (distance>=0) next.push_back(current);
            previous=current; previous_distance=distance;
        }
        polygon=std::move(next);
    };
    clip(0,edges.x,1);clip(0,edges.y,-1);
    clip(2,edges.z,1);clip(2,edges.w,-1);
    float clearance=100.f;
    for (auto point:polygon) clearance=std::min(clearance,point.y-top);
    return clearance;
}

float vehicle_triangle_floor_clearance(PlayerCarId model,glm::vec3 a,glm::vec3 b,glm::vec3 c) {
    if(!has_contact_driver_entry(model))
        return triangle_footprint_clearance(a,b,c,{-.96f,.96f,-.50f,.99f},.52f);
    const bool scythe=model==PlayerCarId::VesperScythe, limo=model==PlayerCarId::HalcyonSovereign;
    // Exact cooked floor, painted sill and roof underside, in source space.
    const glm::vec4 floor_box=scythe?glm::vec4{-.85f,.85f,-.76f,.78f}:
        limo?glm::vec4{-.86f,.86f,-2.20f,1.60f}:glm::vec4{-.73f,.73f,-.80f,.58f};
    const glm::vec4 sill_box=scythe?glm::vec4{.85f,.985f,-.54f,.78f}:
        limo?glm::vec4{.86f,1.014f,.18f,1.60f}:glm::vec4{.73f,.83f,-.55f,.58f};
    const glm::vec4 roof_box=scythe?glm::vec4{-.62f,.62f,-.63f,.12f}:
        limo?glm::vec4{-.86f,.86f,-2.26f,1.22f}:glm::vec4{-.69f,.69f,-1.58f,.18f};
    const float floor=triangle_footprint_clearance(a,b,c,floor_box,scythe?.24f:limo?.35f:.29f);
    const float sill=triangle_footprint_clearance(a,b,c,sill_box,scythe?.29f:limo?.37f:.32f);
    a.y=-a.y;b.y=-b.y;c.y=-c.y;
    const float roof=triangle_footprint_clearance(a,b,c,roof_box,scythe?-1.13f:limo?-1.64f:-1.48f);
    return std::min({floor,sill,roof});
}

void exit_motion(PlayerCarId model, const SkinnedEmesh& mesh, const Skeleton& skeleton) {
    const auto& definition=player_car_definition(model);
    const auto& layout=vehicle_driver_layout(model);
    Transform body;
    body.scale=fitted_body_scale(model);
    body.position={12,3,-7};
    body.set_euler_deg(0,73,0);
    VehicleDriverPose seated, standing;
    REQUIRE(make_vehicle_driver_pose(model,skeleton,mesh.bounds,body,seated));
    Animation idle;
    REQUIRE(idle.load(asset_path("models/characters/psx_pack/animations/idle.eanim"),skeleton));
    idle.sample(0,skeleton,standing.local);
    strip_root_motion_xz(skeleton,standing.local);
    standing.world.scale=seated.world.scale;
    standing.world.rotation=body.rotation*glm::angleAxis(1.57079632679f,glm::vec3{0,1,0});
    standing.world.position=body.transform_point({layout.approach_x,0,layout.hip.z})+body.rotate({.55f,0,0});
    standing.world.position.y-=mesh.bounds.min.y*standing.world.scale.y;
    driver_pose_detail::globals(skeleton,standing);
    skin_matrices_to_dual_quaternions(standing.skin,standing.dual_real,standing.dual_part);
    std::vector<glm::vec3> previous;
    float max_step=0, plant_error=0; uint32_t max_tick=0;
    float floor_clearance=100.f; uint32_t floor_tick=0;
    for (uint32_t tick=0;tick<=kVehicleTransitionTicks;++tick) {
        const auto sample=sample_vehicle_transition({VehicleTransitionDirection::Exit,tick});
        const float u=1.f-sample.traverse;
        VehicleDriverPose pose;
        REQUIRE(make_vehicle_transition_pose(model,skeleton,mesh.bounds,body,standing.world,standing.local,sample,pose));

        for (int i=0;i<skeleton.bone_count();++i) {
            REQUIRE(driver_pose_detail::finite(pose.skin[static_cast<std::size_t>(i)]));
            REQUIRE_NEAR(glm::determinant(glm::mat3{pose.skin[static_cast<std::size_t>(i)]}),1.f,.003f);
            if (skeleton.bone(i).parent>=0 && skeleton.bone(i).name!="mixamorig:Hips")
                REQUIRE_NEAR(glm::length(glm::vec3{pose.local[static_cast<std::size_t>(i)][3]}),
                             glm::length(glm::vec3{standing.local[static_cast<std::size_t>(i)][3]}),.001f);
        }
        if (u>=.46f && u<=.9f)
            plant_error=std::max(plant_error,glm::distance(joint_world(skeleton,pose,"LeftFoot"),joint_world(skeleton,standing,"LeftFoot")));
        if (u>=.46f && u<.49f) {
            REQUIRE(joint_world(skeleton,pose,"Hips").y<joint_world(skeleton,standing,"Hips").y-.12f);
            REQUIRE(joint_world(skeleton,pose,"RightFoot").y>joint_world(skeleton,pose,"LeftFoot").y+.15f);
        }
        std::vector<glm::vec3> vertices;
        for (const auto& vertex:mesh.vertices) {
            const auto point=pose.world.transform_point(skinned_vertex_position(vertex,pose.dual_real,pose.dual_part));
            if (!previous.empty() && glm::distance(point,previous[vertices.size()])>max_step) { max_step=glm::distance(point,previous[vertices.size()]); max_tick=tick; }
            const auto& endpoint=tick==0?seated:standing;
            if (tick==0 || tick==kVehicleTransitionTicks) {
                const auto expected=endpoint.world.transform_point(skinned_vertex_position(vertex,endpoint.dual_real,endpoint.dual_part));
                REQUIRE(glm::distance(point,expected)<.001f);
            }
            if (model==PlayerCarId::HarrowWorkman) {
                const glm::vec3 in_car{glm::inverse(body.matrix())*glm::vec4{point,1}};
                if (std::fabs(in_car.x)<.86f && in_car.z>-.43f && in_car.z<.53f) REQUIRE(in_car.y<1.93f);

            }
            vertices.push_back(point);
        }
        if (model==PlayerCarId::HarrowWorkman || has_contact_driver_entry(model)) {
            const auto inverse=glm::inverse(body.matrix());
            for (std::size_t i=0;i<mesh.indices.size();i+=3) {
                const auto a=glm::vec3{inverse*glm::vec4{vertices[mesh.indices[i]],1}};
                const auto b=glm::vec3{inverse*glm::vec4{vertices[mesh.indices[i+1]],1}};
                const auto c=glm::vec3{inverse*glm::vec4{vertices[mesh.indices[i+2]],1}};
                const float clearance=vehicle_triangle_floor_clearance(model,a,b,c);
                if (clearance<floor_clearance) { floor_clearance=clearance; floor_tick=tick; }
            }
        }
        previous=std::move(vertices);
    }
    std::printf("      %s exit: max vertex step %.4f m, planted foot error %.4f m\n",definition.model,
                static_cast<double>(max_step),static_cast<double>(plant_error));
    std::printf("      max tick %u\n",max_tick);
    if (model==PlayerCarId::HarrowWorkman || has_contact_driver_entry(model)) {
        std::printf("      cab clearance %.4f at tick %u\n",
            static_cast<double>(floor_clearance),floor_tick);
        REQUIRE(floor_clearance>=-.001f);
    }
    REQUIRE(max_step<.065f);
    REQUIRE(plant_error<.025f);
    apricot_test::pass("exit plants outside foot before rising, clears the roof and joins both endpoint poses");
}

void fitted_driver(PlayerCarId model, const SkinnedEmesh& mesh, const Skeleton& skeleton,
                    const Transform& car) {
    const auto& layout=vehicle_driver_layout(model);
    const bool workman=model==PlayerCarId::HarrowWorkman;
    const bool pip=model==PlayerCarId::AlderPip;
    const bool cruiser=is_municipal_cruiser_91(model);
    const bool scythe=model==PlayerCarId::VesperScythe,limo=model==PlayerCarId::HalcyonSovereign;
    VehicleDriverPose pose;
    REQUIRE(make_vehicle_driver_pose(model, skeleton, mesh.bounds, car, pose));
    REQUIRE(pose.skin.size() == static_cast<std::size_t>(skeleton.bone_count()));
    for (std::size_t i = 0; i < pose.skin.size(); ++i) {
        REQUIRE(driver_pose_detail::finite(pose.skin[i]));
        REQUIRE_NEAR(glm::determinant(glm::mat3{pose.skin[i]}), 1.f, .002f);
        REQUIRE_NEAR(glm::length(pose.dual_real[i]), 1.f, .0002f);
        REQUIRE(std::isfinite(glm::length(pose.dual_part[i])));
    }
    REQUIRE_NEAR(pose.world.scale.x * mesh.bounds.size().y, 1.76f, .0001f);
    REQUIRE_NEAR(pose.world.scale.x, pose.world.scale.y, .00001f);
    REQUIRE_NEAR(pose.world.scale.x, pose.world.scale.z, .00001f);
    const glm::vec3 hip = joint_world(skeleton, pose, "Hips");
    REQUIRE(glm::distance(hip, car.transform_point(layout.hip)) < .001f);
    const glm::vec3 centre = car.transform_point({0,layout.hip.y,layout.hip.z});
    const glm::vec3 forward = car.rotate({0,0,1});
    const glm::vec3 up = car.rotate({0,1,0});
    const glm::vec3 left = glm::normalize(glm::cross(up, forward));
    REQUIRE(glm::dot(hip-centre,left) > .25f);
    // Native +X is the face direction and must face the actual +Z car nose.
    REQUIRE(glm::dot(pose.world.rotate({1,0,0}),forward) > .999f);
    const glm::mat4 into_car = glm::inverse(car.matrix());
    for (int side = 0; side < 2; ++side) {
        const std::string prefix = side == 0 ? "Right" : "Left";
        const glm::vec3 hand = joint_world(skeleton,pose,prefix+"Hand");
        const glm::vec3 foot = joint_world(skeleton,pose,prefix+"Foot");
        const glm::vec3 knee = joint_world(skeleton,pose,prefix+"Leg");
        const glm::vec3 upper = joint_world(skeleton,pose,prefix+"UpLeg");
        const glm::vec3 knee_car{into_car*glm::vec4{knee,1}};
        const float wrist_error = glm::distance(hand, car.transform_point(layout.wrists[side]));
        const float foot_error = glm::distance(foot, car.transform_point(layout.ankles[side]));
        std::printf("      %s wrist %.4f m, ankle %.4f m, knee source %.3f %.3f %.3f\n",
            prefix.c_str(),static_cast<double>(wrist_error),static_cast<double>(foot_error),
            static_cast<double>(knee_car.x),static_cast<double>(knee_car.y),static_cast<double>(knee_car.z));
        REQUIRE(wrist_error < .025f);
        REQUIRE(foot_error < .025f);
        if(cruiser) {
            REQUIRE(knee_car.y>.50f && knee_car.y<1.20f);
            REQUIRE(knee_car.z>std::min(layout.hip.z,layout.ankles[side].z)-.05f &&
                    knee_car.z<std::max(layout.hip.z,layout.ankles[side].z)+.05f);
        } else if(scythe || limo) {
            REQUIRE(knee_car.y>layout.hip.y-.1f && knee_car.y<layout.hip.y+.5f);
            REQUIRE(knee_car.z>layout.hip.z+.1f && knee_car.z<layout.ankles[side].z-.02f);
        } else {
            REQUIRE(knee_car.y > (workman?.70f:.55f) && knee_car.y < (workman?1.22f:.95f));
            REQUIRE(knee_car.z > (workman?-.10f:-.35f) && knee_car.z < (workman?.52f:.20f));
        }
        // A standing/clipped figure fails both forward thighs and knee flexion.
        REQUIRE(glm::dot(knee-upper,forward) > .20f);
        const float straightness = glm::dot(glm::normalize(knee-upper),glm::normalize(foot-knee));
        REQUIRE(straightness < .85f);
    }
    float low = std::numeric_limits<float>::max(), high = -low;
    for (const auto& vertex : mesh.vertices) {
        const glm::vec3 local = skinned_vertex_position(vertex,pose.dual_real,pose.dual_part);
        const glm::vec3 world = pose.world.transform_point(local);
        const glm::vec3 point{into_car*glm::vec4{world,1}};
        REQUIRE(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
        low = std::min(low,point.y); high = std::max(high,point.y);
    }
    std::printf("      skinned source height %.3f .. %.3f\n",static_cast<double>(low),static_cast<double>(high));
    REQUIRE(low > (workman?.52f:pip?.29f:cruiser?.30f:scythe?.24f:limo?.35f:.40f));
    REQUIRE(high > (workman?1.50f:scythe?1.05f:1.20f) && high < (workman?1.93f:pip?1.48f:scythe?1.13f:limo?1.64f:1.70f));
}

}  // namespace

int main() {
    cooked_pose_contracts();
    workman_glass_openings();
    transition_clock();
    hinged_door(PlayerCarId::VesperMistral);
    hinged_door(PlayerCarId::HarrowWorkman);
    hinged_door(PlayerCarId::AlderPip);
    hinged_door(PlayerCarId::VesperScythe);
    hinged_door(PlayerCarId::HalcyonSovereign);
    hinged_door(PlayerCarId::MunicipalCruiser91A);
    hinged_door(PlayerCarId::MunicipalCruiser91B);
    hinged_door(PlayerCarId::MunicipalCruiser91C);
    hinged_door(PlayerCarId::MunicipalCruiser91D);
    hinged_door(PlayerCarId::MunicipalCruiser91E);
    hinged_door(PlayerCarId::LegacyCar5Next);
    hinged_door(PlayerCarId::LegacyCar5NextPolice);
    for (std::size_t i = 0; i < kPlayerCarCount; ++i) {
        const auto car = static_cast<PlayerCarId>(i);
        REQUIRE(!shows_vehicle_driver(car,false));
        REQUIRE(shows_vehicle_driver(car,true) == has_animated_driver(car));
    }
    VehicleDriverPose rejected;
    REQUIRE(!make_mistral_driver_pose(Skeleton{},AABB{},Transform{},rejected));
    apricot_test::pass("driver visibility follows supported articulated cars and occupancy");
    const std::string root = "models/characters/psx_pack/player_male_01/skin";
    if (!std::filesystem::is_regular_file(asset_path(root+".emesh"))) {
        std::printf("SKIP supplied driver rig checks (private assets not staged)\n");
        return apricot_test::done("vehicle_driver_pose_tests");
    }
    Skeleton skeleton; SkinnedEmesh mesh;
    REQUIRE(skeleton.load(asset_path(root+".eskel")));
    REQUIRE(read_skinned_emesh(asset_path(root+".emesh"),mesh));
    REQUIRE(skeleton.accepts(mesh));
    transition_motion(PlayerCarId::VesperMistral,mesh,skeleton);
    transition_motion(PlayerCarId::HarrowWorkman,mesh,skeleton);
    transition_motion(PlayerCarId::AlderPip,mesh,skeleton);
    transition_motion(PlayerCarId::VesperScythe,mesh,skeleton);
    transition_motion(PlayerCarId::HalcyonSovereign,mesh,skeleton);
    transition_motion(PlayerCarId::MunicipalCruiser91A,mesh,skeleton);
    transition_motion(PlayerCarId::MunicipalCruiser91B,mesh,skeleton);
    transition_motion(PlayerCarId::MunicipalCruiser91C,mesh,skeleton);
    transition_motion(PlayerCarId::MunicipalCruiser91D,mesh,skeleton);
    transition_motion(PlayerCarId::MunicipalCruiser91E,mesh,skeleton);
    transition_motion(PlayerCarId::LegacyCar5Next,mesh,skeleton);
    transition_motion(PlayerCarId::LegacyCar5NextPolice,mesh,skeleton);
    exit_motion(PlayerCarId::VesperMistral,mesh,skeleton);
    exit_motion(PlayerCarId::HarrowWorkman,mesh,skeleton);
    exit_motion(PlayerCarId::AlderPip,mesh,skeleton);
    exit_motion(PlayerCarId::VesperScythe,mesh,skeleton);
    exit_motion(PlayerCarId::HalcyonSovereign,mesh,skeleton);
    exit_motion(PlayerCarId::MunicipalCruiser91A,mesh,skeleton);
    exit_motion(PlayerCarId::MunicipalCruiser91B,mesh,skeleton);
    exit_motion(PlayerCarId::MunicipalCruiser91C,mesh,skeleton);
    exit_motion(PlayerCarId::MunicipalCruiser91D,mesh,skeleton);
    exit_motion(PlayerCarId::MunicipalCruiser91E,mesh,skeleton);
    exit_motion(PlayerCarId::LegacyCar5Next,mesh,skeleton);
    exit_motion(PlayerCarId::LegacyCar5NextPolice,mesh,skeleton);
    Transform bad;
    bad.rotation={0.f,0.f,0.f,0.f};
    REQUIRE(!make_mistral_driver_pose(skeleton,mesh.bounds,bad,rejected));
    bad=Transform{};bad.position.x=std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!make_mistral_driver_pose(skeleton,mesh.bounds,bad,rejected));
    const std::string car_path=asset_path("models/vehicles/vesper_mistral/body.emesh");
    if (std::filesystem::is_regular_file(car_path)) {
        StaticEmesh car;
        REQUIRE(read_static_emesh(car_path,car));
        int left_rim=0,right_rim=0;
        for (const auto& v:car.vertices) {
            if (v.py>.865f && v.py<1.135f && v.pz>.119f && v.pz<.151f) {
                if (v.px>.295f && v.px<.565f) ++left_rim;
                if (v.px<-.295f && v.px>-.565f) ++right_rim;
            }
        }
        REQUIRE(left_rim>=64);
        REQUIRE(right_rim==0);
        apricot_test::pass("cooked steering rim is on +X, the left side facing the +Z nose");
    }
    for (auto model:{PlayerCarId::VesperMistral,PlayerCarId::HarrowWorkman,
                    PlayerCarId::AlderPip,PlayerCarId::VesperScythe,
                    PlayerCarId::HalcyonSovereign,
                    PlayerCarId::MunicipalCruiser91A,
                    PlayerCarId::MunicipalCruiser91B,
                    PlayerCarId::MunicipalCruiser91C,
                    PlayerCarId::MunicipalCruiser91D,
                    PlayerCarId::MunicipalCruiser91E})
      for (int i = 0; i < 3; ++i) {
        Transform car;
        car.scale=fitted_body_scale(model);
        car.position = {17.f*static_cast<float>(i),4.f,-9.f};
        car.set_euler_deg(i == 2 ? 13.f : 0.f,static_cast<float>(i)*91.f,i == 2 ? -9.f : 0.f);
        fitted_driver(model,mesh,skeleton,car);
    }
    apricot_test::pass("actual supplied player has bent legs, wheel-reaching arms and rigid finite skinning");
    apricot_test::pass("hip remains in left seat across fitted, yawed, pitched and rolled cars");
    return apricot_test::done("vehicle_driver_pose_tests");
}
