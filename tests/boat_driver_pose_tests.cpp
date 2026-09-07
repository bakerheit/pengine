#include <cstdio>
#include <filesystem>
#include "app/boat_driver_pose.h"
#include "core/asset_root.h"
#include "test_assert.h"
using namespace apricot;
int main() {
    BoatTransitionState state{VehicleTransitionDirection::Enter,0};
    int commits=0;
    for(uint32_t i=0;i<=kBoatTransitionTicks;++i) {
        REQUIRE_NEAR(boat_transition_fraction(state),boat_transition_fraction(
            {VehicleTransitionDirection::Exit,kBoatTransitionTicks-i}),.00001f);
        if(advance_boat_transition(state)) ++commits;
    }
    REQUIRE(commits==1);REQUIRE(!advance_boat_transition(state));
    Skeleton skeleton;SkinnedEmesh mesh;
    const std::string path="models/characters/psx_pack/player_male_01/skin";
    if(!std::filesystem::is_regular_file(asset_path(path+".emesh"))) {
        std::puts("SKIP supplied boat pilot rig checks (private assets not staged)");
        return apricot_test::done("boat_driver_pose_tests");
    }
    REQUIRE(skeleton.load(asset_path(path+".eskel")));
    REQUIRE(read_skinned_emesh(asset_path(path+".emesh"),mesh));
    Animation idle;REQUIRE(idle.load(asset_path("models/characters/psx_pack/animations/idle.eanim"),skeleton));
    std::vector<glm::mat4> standing_local;idle.sample(0,skeleton,standing_local);
    strip_root_motion_xz(skeleton,standing_local);
    for(float yaw:{0.f,1.4f}) {
        Transform body;body.position={11,0,-7};body.set_euler_deg(3,yaw*57.29578f,4);
        VehicleDriverPose seated;
        REQUIRE(make_boat_driver_pose(skeleton,mesh.bounds,body,0,0,seated));
        const auto joint=[&](const char* name){const auto i=static_cast<std::size_t>(skeleton.find_bone(name));
            return seated.world.transform_point(glm::vec3{seated.joints[i][3]});};
        REQUIRE(glm::distance(joint("mixamorig:Hips"),body.transform_point(kMarlinSeatRig.hip))<.001f);
        for(int i=0;i<2;++i) {
            const auto prefix=i?"mixamorig:Left":"mixamorig:Right";
            const float wrist=glm::distance(joint((std::string(prefix)+"Hand").c_str()),body.transform_point(kMarlinSeatRig.wrists[i]));
            const float foot=glm::distance(joint((std::string(prefix)+"Foot").c_str()),body.transform_point(kMarlinSeatRig.ankles[i]));
            std::printf("  helm wrist %.4f ankle %.4f\n",double(wrist),double(foot));
            REQUIRE(wrist<.035f);REQUIRE(foot<.025f);
        }
        float low=100,high=-100;
        for(const auto& vertex:mesh.vertices) {
            const auto world=seated.world.transform_point(skinned_vertex_position(vertex,seated.dual_real,seated.dual_part));
            const auto p=glm::vec3{glm::inverse(body.matrix())*glm::vec4{world,1}};
            low=std::min(low,p.y);high=std::max(high,p.y);
        }
        std::printf("  pilot extent %.3f .. %.3f\n",double(low),double(high));
        REQUIRE(low>.10f);REQUIRE(high>1.2f && high<1.8f);
        for(float side:{-1.f,1.f}) {
            Transform shore;shore.scale=seated.world.scale;
            shore.rotation=body.rotation*glm::angleAxis(side>0?glm::pi<float>():0.f,glm::vec3{0,1,0});
            shore.position=body.transform_point({side*2.6f,.66f,-1.55f});
            shore.position.y-=mesh.bounds.min.y*shore.scale.y;
            std::vector<glm::vec3> last;
            float largest=0;uint32_t largest_tick=0;
            for(uint32_t i=0;i<=kBoatTransitionTicks;++i) {
                VehicleDriverPose pose;
                REQUIRE(make_boat_transition_pose(skeleton,mesh.bounds,body,shore,standing_local,
                    float(i)/float(kBoatTransitionTicks),pose));
                for(const auto& matrix:pose.skin) REQUIRE_NEAR(glm::determinant(glm::mat3{matrix}),1.f,.003f);
                for(std::size_t j=0;j<pose.local.size();++j) {
                    if(skeleton.bone(static_cast<int>(j)).parent<0 ||
                       skeleton.bone(static_cast<int>(j)).name=="mixamorig:Hips") continue;
                    REQUIRE_NEAR(glm::length(glm::vec3{pose.local[j][3]})*pose.world.scale.x,
                        glm::length(glm::vec3{standing_local[j][3]})*pose.world.scale.x,.0001f);
                }
                std::vector<glm::vec3> vertices;
                for(const auto& vertex:mesh.vertices) {
                    const auto world=pose.world.transform_point(skinned_vertex_position(vertex,pose.dual_real,pose.dual_part));
                    REQUIRE(std::isfinite(world.x+world.y+world.z));
                    if(!last.empty() && glm::distance(world,last[vertices.size()])>largest) {
                        largest=glm::distance(world,last[vertices.size()]);largest_tick=i;
                    }
                    if(i==kBoatTransitionTicks) REQUIRE(glm::distance(world,seated.world.transform_point(
                        skinned_vertex_position(vertex,seated.dual_real,seated.dual_part)))<.001f);
                    vertices.push_back(world);
                }
                last=std::move(vertices);
            }
            std::printf("  boarding max vertex step %.4f at %u\n",double(largest),largest_tick);
            REQUIRE(largest<.075f);
        }
    }
    apricot_test::pass("boat pilot seat fit, limb lengths, reversible continuous boarding and rotated hull attachment");
    return apricot_test::done("boat_driver_pose_tests");
}
