#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/vehicle_driver_pose.h"
#include "app/vehicle_headlight_profile.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_transition_pose.h"
#include "audio/vehicle_sound_profile.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec3 joint_world(const Skeleton& skeleton,const VehicleDriverPose& pose,
                      const char* name) {
    const int bone=skeleton.find_bone(std::string{"mixamorig:"}+name);
    REQUIRE(bone>=0);
    return pose.world.transform_point(
        glm::vec3{pose.joints[static_cast<std::size_t>(bone)][3]});
}

void asset_contract() {
    const auto& definition=player_car_definition(PlayerCarId::FangVenom);
    REQUIRE(std::string{definition.brand}=="FANG");
    REQUIRE(std::string{definition.model}=="VENOM");
    REQUIRE(is_motorbike(definition.id));
    const std::string body_path=definition.mesh_path;
    const std::string root=body_path.substr(0,body_path.find_last_of('/')+1);
    StaticEmesh body,front,rear,glass;
    REQUIRE(read_static_emesh(asset_path(definition.mesh_path),body));
    REQUIRE(read_static_emesh(asset_path(root+"front_wheel.emesh"),front));
    REQUIRE(read_static_emesh(asset_path(root+"rear_wheel.emesh"),rear));
    REQUIRE(read_static_emesh(asset_path(root+"windshield.emesh"),glass));
    REQUIRE(body.indices.size()/3>=1400u && body.indices.size()/3<=1900u);
    REQUIRE(front.indices.size()/3>=550u && front.indices.size()/3<=850u);
    REQUIRE(rear.indices.size()/3>=550u && rear.indices.size()/3<=850u);
    REQUIRE(glass.indices.size()/3==12u);
    REQUIRE(body.bounds.min.y>.18f); // neither tire is baked into the body
    REQUIRE_NEAR(body.bounds.size().x,.82f,.002f);
    REQUIRE_NEAR(body.bounds.size().y,.95523f,.002f);
    REQUIRE_NEAR(body.bounds.size().z,2.256f,.002f);
    REQUIRE_NEAR(std::max(front.bounds.size().y,front.bounds.size().z)*.5f,.345f,.002f);
    REQUIRE_NEAR(std::max(rear.bounds.size().y,rear.bounds.size().z)*.5f,.345f,.002f);
    REQUIRE_NEAR(front.bounds.size().x,.09988f,.002f);
    REQUIRE_NEAR(rear.bounds.size().x,.1372f,.002f);
    REQUIRE(rear.bounds.size().x>front.bounds.size().x);
    for(const auto* mesh:{&body,&front,&rear,&glass}) {
        REQUIRE(mesh->bounds.valid());
        for(const auto& vertex:mesh->vertices) {
            REQUIRE(std::isfinite(vertex.px) && std::isfinite(vertex.py) && std::isfinite(vertex.pz));
            REQUIRE(vertex.u>=0.f && vertex.u<=1.f && vertex.v>=0.f && vertex.v<=1.f);
        }
        for(const auto index:mesh->indices) REQUIRE(index<mesh->vertices.size());
    }
    REQUIRE(std::filesystem::file_size(asset_path(definition.texture_path))>1024u);
    const auto head=vehicle_headlight_profile(definition.mesh_path);
    const auto brake=vehicle_brakelight_profile(definition.mesh_path);
    REQUIRE(head.id==28 && head.exposed() && brake.id==28);
    glm::vec3 origin;
    REQUIRE(vehicle_headlight_origin(body,head,0,origin));
    REQUIRE(origin.y>.82f && origin.z>1.11f);
    apricot_test::pass("Fang Venom v2 has a detailed wheel-less body, unequal tire widths, separate glass and mapped lamps");
}

void handling_contract() {
    const auto& definition=player_car_definition(PlayerCarId::FangVenom);
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,
                                          PlayerCarId::FangVenom);
    REQUIRE_NEAR(tuning.half_wheelbase,.74f,1e-6f);
    REQUIRE_NEAR(tuning.half_track,.25f,1e-6f);
    REQUIRE_NEAR(tuning.wheel_radius,.345f,1e-6f);
    REQUIRE(tuning.car_collision_half_width<.45f);
    REQUIRE_NEAR(tuning.car_collision_half_length,1.13f,1e-6f);
    REQUIRE(tuning.mass_kg<400.f);
    REQUIRE_NEAR(tuning.half_track/definition.wheel_x,1.f,1e-6f);
    REQUIRE(vehicle_sound_profile(definition.mesh_path).model_key=="fang_venom_v2");
    apricot_test::pass("Fang Venom uses its measured wheelbase, narrow arcade-stable chassis and bike engine pitch");
}

void rig_saddles(const char* rig) {
    const std::string root=std::string{"models/characters/psx_pack/"}+rig+"/skin";
    Skeleton skeleton; SkinnedEmesh mesh;
    REQUIRE(skeleton.load(asset_path(root+".eskel")));
    REQUIRE(read_skinned_emesh(asset_path(root+".emesh"),mesh));
    REQUIRE(skeleton.accepts(mesh));
    Transform body;
    const auto& layout=vehicle_driver_layout(PlayerCarId::FangVenom);
    VehicleDriverPose seated;
    REQUIRE(make_vehicle_driver_pose(PlayerCarId::FangVenom,skeleton,mesh.bounds,body,seated));
    REQUIRE(glm::distance(joint_world(skeleton,seated,"Hips"),layout.hip)<.002f);
    std::printf("      %s fitted errors: RH %.3f LH %.3f RF %.3f LF %.3f\n",rig,
        static_cast<double>(glm::distance(joint_world(skeleton,seated,"RightHand"),layout.wrists[0])),
        static_cast<double>(glm::distance(joint_world(skeleton,seated,"LeftHand"),layout.wrists[1])),
        static_cast<double>(glm::distance(joint_world(skeleton,seated,"RightFoot"),layout.ankles[0])),
        static_cast<double>(glm::distance(joint_world(skeleton,seated,"LeftFoot"),layout.ankles[1])));
    REQUIRE(glm::distance(joint_world(skeleton,seated,"RightHand"),layout.wrists[0])<.03f);
    REQUIRE(glm::distance(joint_world(skeleton,seated,"LeftHand"),layout.wrists[1])<.03f);
    REQUIRE(glm::distance(joint_world(skeleton,seated,"RightFoot"),layout.ankles[0])<.03f);
    REQUIRE(glm::distance(joint_world(skeleton,seated,"LeftFoot"),layout.ankles[1])<.03f);

    Animation idle;
    REQUIRE(idle.load(asset_path("models/characters/psx_pack/animations/idle.eanim"),skeleton));
    std::vector<glm::mat4> standing_local;
    idle.sample(0,skeleton,standing_local); strip_root_motion_xz(skeleton,standing_local);
    Transform standing;
    standing.scale=seated.world.scale; standing.rotation=seated.world.rotation;
    standing.position=body.transform_point({layout.approach_x+.20f,0,layout.hip.z});
    standing.position.y-=mesh.bounds.min.y*standing.scale.y;
    VehicleDriverPose standing_pose;
    standing_pose.local=standing_local;standing_pose.world=standing;
    driver_pose_detail::globals(skeleton,standing_pose);

    VehicleTransitionSample middle;
    middle.phase=VehicleTransitionPhase::Traverse;middle.approach=1;middle.reach=1;
    middle.traverse=.5f;middle.progress=.5f;middle.direction=VehicleTransitionDirection::Enter;
    VehicleDriverPose saddle;
    REQUIRE(make_vehicle_transition_pose(PlayerCarId::FangVenom,skeleton,mesh.bounds,
        body,standing,standing_local,middle,saddle));
    const auto start_hip=joint_world(skeleton,standing_pose,"Hips");
    const auto seat_hip=joint_world(skeleton,seated,"Hips");
    const auto saddle_hip=joint_world(skeleton,saddle,"Hips");
    REQUIRE(saddle_hip.y>glm::mix(start_hip,seat_hip,.5f).y+.18f);
    const auto start_foot=joint_world(skeleton,standing_pose,"LeftFoot");
    const auto seat_foot=joint_world(skeleton,seated,"LeftFoot");
    REQUIRE(joint_world(skeleton,saddle,"LeftFoot").y>
            glm::mix(start_foot,seat_foot,.5f).y+.12f);
    for(const auto& matrix:saddle.skin) REQUIRE(driver_pose_detail::finite(matrix));

    middle.direction=VehicleTransitionDirection::Exit;
    VehicleDriverPose dismount;
    REQUIRE(make_vehicle_transition_pose(PlayerCarId::FangVenom,skeleton,mesh.bounds,
        body,standing,standing_local,middle,dismount));
    REQUIRE(glm::distance(joint_world(skeleton,dismount,"Hips"),saddle_hip)<.002f);
    std::printf("      %s rider: seated hands/feet fitted; saddle hip %.3f m\n",
                rig,static_cast<double>(saddle_hip.y));
}

} // namespace

int main() {
    asset_contract();
    handling_contract();
    rig_saddles("player_male_01");
    rig_saddles("civilian_male_03");
    apricot_test::pass("player and NPC rigs share the seated, mount and dismount bike animation path");
    return apricot_test::done("fang_venom_tests");
}
