#include <cstdio>
#include <cstring>
#include <limits>
#include <glm/gtc/quaternion.hpp>

#include "city/bank_vault_layout.h"
#include "core/transform.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;
namespace {
glm::vec3 world(glm::vec3 p) {
    const auto& s = city::kBankSite;
    return {s.origin.x + s.cos_yaw*p.x + s.sin_yaw*p.z,
            s.ground_m+p.y, s.origin.z-s.sin_yaw*p.x+s.cos_yaw*p.z};
}
AABB bounds(const city::StartPart& p) {
    Transform t;
    t.position = world({p.centre.x,p.bottom_m+p.height_m*0.5f,p.centre.z});
    t.rotation = glm::angleAxis(std::atan2(city::kBankSite.sin_yaw,city::kBankSite.cos_yaw),
                               glm::vec3{0,1,0}) *
                 glm::quat(glm::radians(glm::vec3{p.pitch_deg,p.yaw_deg,p.roll_deg}));
    t.scale = {p.width_m,p.height_m,p.depth_m};
    AABB unit;
    unit.expand(glm::vec3{-0.5f});
    unit.expand(glm::vec3{0.5f});
    return unit.transformed(t.matrix());
}
glm::vec3 centre(const city::StartPart& p) {
    return world({p.centre.x,p.bottom_m+p.height_m*0.5f,p.centre.z});
}
glm::vec3 half(const city::StartPart& p) {
    return glm::vec3{p.width_m,p.height_m,p.depth_m} * 0.5f;
}
float yaw(const city::StartPart& p) {
    return std::atan2(city::kBankSite.sin_yaw,city::kBankSite.cos_yaw) + glm::radians(p.yaw_deg);
}
void add_bank(TerrainCollider& collider) {
    for (const auto& p : city::bake_building(city::kBankPlan))
        if (p.solid) {
            REQUIRE(p.pitch_deg == 0.0f && p.roll_deg == 0.0f);
            collider.add_static_oriented_box(centre(p),half(p),yaw(p));
        }
}
void locked_and_code_flow() {
    BankVaultState s;
    REQUIRE(!bank_vault_toggle(s));
    REQUIRE(!bank_vault_submit(s, "0000"));
    REQUIRE(!bank_vault_submit(s, "749"));
    REQUIRE(!bank_vault_submit(s, "74910"));
    step_bank_vault(s, 10.0f, false);
    REQUIRE_NEAR(s.openness,0,1e-6);
    REQUIRE(bank_vault_submit(s,kBankVaultCode));
    step_bank_vault(s,1.1f,false);
    REQUIRE_NEAR(s.openness,0.5f,1e-5);
    step_bank_vault(s,1.1f,false);
    REQUIRE_NEAR(s.openness,1,1e-5);
    REQUIRE(bank_vault_toggle(s));
    step_bank_vault(s,2.2f,false);
    REQUIRE_NEAR(s.openness,0,1e-5);
    REQUIRE(s.unlocked);
    REQUIRE(bank_vault_toggle(s));
    apricot_test::pass("code validation, smooth opening, closing, and session unlock");
}
void proximity_and_safety() {
    REQUIRE(bank_target({-12.5f,0.2f,9.0f},true)==BankTarget::Note);
    REQUIRE(bank_target({-6.8f,0.2f,11.0f},true)==BankTarget::None);
    REQUIRE(bank_target({5.0f,0.2f,9.0f},true)==BankTarget::Vault);
    REQUIRE(bank_target({8.2f,0.2f,13.8f},true)==BankTarget::Vault);
    REQUIRE(bank_target({7.5f,0.2f,9.0f},true)==BankTarget::None);
    REQUIRE(bank_target({5.0f,0.2f,9.0f},false)==BankTarget::None);
    REQUIRE(bank_target({5.0f,5.0f,9.0f},true)==BankTarget::None);
    REQUIRE(bank_vault_swing_occupied({7.0f,0.2f,11.5f},0.32f));
    REQUIRE(!bank_vault_swing_occupied({5.0f,0.2f,9.0f},0.32f));
    REQUIRE(!bank_vault_swing_occupied({8.2f,0.2f,13.8f},0.32f));
    BankVaultState s;
    bank_vault_submit(s,kBankVaultCode);
    step_bank_vault(s,1.0f,true);
    REQUIRE(s.blocked);
    REQUIRE_NEAR(s.openness,0,1e-6);
    step_bank_vault(s,2.2f,false);
    bank_vault_toggle(s);
    step_bank_vault(s,1.0f,true);
    REQUIRE(s.blocked);
    REQUIRE_NEAR(s.openness,1,1e-6);
    step_bank_vault(s,2.2f,false);
    REQUIRE(!s.blocked);
    REQUIRE_NEAR(s.openness,0,1e-6);
    apricot_test::pass("range checks, interior control, and obstruction stop in both directions");
}
void real_collision_follows_door() {
    TerrainCollider collider{city::kMapSeed};
    add_bank(collider);
    const auto closed = city::bank_vault_leaf(0);
    const auto open = city::bank_vault_leaf(1);
    REQUIRE(closed.size() == open.size());
    REQUIRE(closed.size() >= 15u);
    const auto id = collider.add_kinematic_oriented_box(centre(closed[0]),half(closed[0]),yaw(closed[0]));
    const auto count = collider.static_boxes().size();
    const CharacterTuning tuning;
    const glm::vec3 doorway = world({7.0f,0.2f,11.5f});
    REQUIRE(!character_position_clear(collider,doorway,tuning));
    REQUIRE(collider.set_kinematic_oriented_box(id,centre(open[0]),half(open[0]),yaw(open[0])));
    REQUIRE(character_position_clear(collider,doorway,tuning));
    for (float x=5.7f;x<9.5f;x+=0.1f)
        REQUIRE(character_position_clear(collider,world({x,0.2f,11.5f}),tuning));
    REQUIRE(collider.set_kinematic_oriented_box(id,centre(closed[0]),half(closed[0]),yaw(closed[0])));
    REQUIRE(!character_position_clear(collider,doorway,tuning));
    REQUIRE(collider.static_boxes().size()==count);
    REQUIRE(!collider.set_kinematic_box(0,bounds(open[0])));
    collider.clear_static_boxes();
    REQUIRE(!collider.set_kinematic_box(id,bounds(open[0])));
    apricot_test::pass("actual character collision blocks closed vault and clears the open doorway");
}
void fixture_and_pose_contracts() {
    const auto parts=city::bake_building(city::kBankPlan);
    bool note=false,keypad=false;
    for(const auto& p:parts) {
        REQUIRE(std::strcmp(p.name,"bank vault open door")!=0);
        if(std::strcmp(p.name,"bank office access note")==0) {
            note=true;
            REQUIRE_NEAR(p.centre.x,kBankNotePosition.x,1e-5);
            REQUIRE_NEAR(p.centre.z,kBankNotePosition.y,1e-5);
            REQUIRE(p.bottom_m>1.1f);
        }
        keypad |= std::strcmp(p.name,"bank vault keypad face")==0;
    }
    REQUIRE(note && keypad);
    const glm::vec3 p{5,0.2f,9};
    REQUIRE(glm::distance(p,city::bank_local_position(world(p)))<0.0001f);
    BankVaultState a,b;
    bank_vault_submit(a,kBankVaultCode); bank_vault_submit(b,kBankVaultCode);
    for(int i=0;i<400;++i) {
        const bool occupied=i>100 && i<160;
        step_bank_vault(a,1.0f/120.0f,occupied);
        step_bank_vault(b,1.0f/120.0f,occupied);
        REQUIRE(a.openness==b.openness);
    }
    REQUIRE_NEAR(a.openness,1,1e-5);
    apricot_test::pass("note matches office clue, shared layout transforms and deterministic motion");
}

void bank_walkways_match_visible_edges() {
    TerrainCollider collider{city::kMapSeed};
    add_bank(collider);
    const auto door = city::bank_vault_leaf(1.0f)[0];
    collider.add_static_oriented_box(centre(door),half(door),yaw(door));
    const CharacterTuning tuning;
    for (float x = -9.0f; x <= 3.0f; x += 0.1f) {
        REQUIRE_MSG(character_position_clear(collider, world({x,0.2f,2.65f}), tuning),
                    "invisible collision in front of teller counter", "teller walkway");
    }
    for (float x = 8.0f; x <= 15.5f; x += 0.1f) {
        REQUIRE_MSG(character_position_clear(collider, world({x,0.2f,7.65f}), tuning),
                    "invisible collision behind open vault door", "vault south aisle");
    }
    // Drive the actual controller past the counter, through the open doorway,
    // around the leaf's tip and into the aisle behind it, then back out.
    PlayerCharacterState player;
    player.position = world({-9.0f,0.2f,2.65f});
    const glm::vec2 route[] = {{3,2.65f},{5.85f,2.65f},{5.85f,11.5f},
        {10.1f,11.5f},{10.1f,7.65f},{8,7.65f},{8,9.45f},{10.1f,9.45f},
        {10.1f,11.5f},{5.85f,11.5f},{5.85f,2.65f},{-9,2.65f}};
    constexpr float dt = 1.0f / 120.0f;
    for (const auto target : route) {
        const glm::vec3 destination = world({target.x,0.2f,target.y});
        for (int i = 0; i < 1200; ++i) {
            const glm::vec3 delta = destination - player.position;
            const float distance = glm::length(glm::vec2{delta.x,delta.z});
            if (distance < 0.002f) break;
            player.view_yaw = std::atan2(delta.x,-delta.z);
            InputFrame input;
            input.throttle = std::min(1.0f,distance / (tuning.walk_speed_mps * dt));
            player = step_character(player,tuning,input,collider,dt);
            REQUIRE_NEAR(player.position.y,city::kBankSite.ground_m+0.2f,0.005f);
        }
        REQUIRE_MSG(glm::distance(player.position,destination) < 0.02f,
                    "actual walking controller got stuck in bank aisle", "bank circuit");
    }
    REQUIRE(!character_position_clear(collider,world({-3,0.2f,3.7f}),tuning));
    REQUIRE(!character_position_clear(collider,world({12.5f,0.2f,10.5f}),tuning));
    apricot_test::pass("teller and vault aisles follow visible rotated edges");
}

void oriented_queries_and_moving_slots_agree() {
    TerrainCollider collider{city::kMapSeed};
    const auto parts = city::bake_building(city::kBankPlan);
    for (const auto& part : parts) {
        if (std::strcmp(part.name,"bank teller counter") != 0) continue;
        collider.add_static_oriented_box(centre(part),half(part),yaw(part));
    }
    REQUIRE(collider.static_boxes().size() == 1u);
    // The old broad box really covers this empty corner. Neither a support
    // probe nor the camera ray should mistake it for a surface now.
    const glm::vec3 empty_corner = world({3.0f,0.7f,2.65f});
    REQUIRE(collider.static_boxes()[0].bounds.contains(empty_corner));
    const auto support = collider.probe_down(empty_corner + glm::vec3{0,2,0},4);
    REQUIRE(!support.prop);
    const auto along_aisle = collider.raycast(world({-9,0.7f,2.65f}),
        glm::normalize(world({3,0.7f,2.65f})-world({-9,0.7f,2.65f})),12);
    REQUIRE(!along_aisle.hit);
    const auto into_counter = collider.raycast(world({-3,0.7f,2}),
        glm::normalize(world({-3,0.7f,3})-world({-3,0.7f,2})),2);
    REQUIRE(into_counter.hit && into_counter.prop);
    REQUIRE_NEAR(into_counter.distance,1.25f,0.001f);
    REQUIRE(glm::dot(into_counter.normal,
        glm::normalize(world({0,0,0})-world({0,0,1})))>0.999f);

    const auto leaf = city::bank_vault_leaf(0.5f)[0];
    const auto id = collider.add_kinematic_oriented_box(centre(leaf),half(leaf),yaw(leaf));
    const auto count = collider.static_boxes().size();
    const auto& moving_box = collider.static_boxes()[id];
    const glm::vec3 beside_leaf = centre(leaf) + moving_box.world_direction(
        {half(leaf).x + 0.4f,-half(leaf).y,0});
    REQUIRE(moving_box.bounds.contains(beside_leaf + glm::vec3{0,0.5f,0}));
    REQUIRE(character_position_clear(collider,beside_leaf,CharacterTuning{}));
    REQUIRE(!character_position_clear(collider,
        centre(leaf)-glm::vec3{0,half(leaf).y,0},CharacterTuning{}));
    REQUIRE(!collider.set_kinematic_oriented_box(0,centre(leaf),half(leaf),yaw(leaf)));
    REQUIRE(!collider.set_kinematic_oriented_box(id,centre(leaf),{-1,1,1},yaw(leaf)));
    REQUIRE(!collider.set_kinematic_oriented_box(id,centre(leaf),half(leaf),
        std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(collider.static_boxes().size() == count);
    REQUIRE(collider.set_kinematic_box(id,bounds(leaf)));
    REQUIRE(!collider.static_boxes()[id].oriented);
    REQUIRE(collider.set_kinematic_oriented_box(id,centre(leaf),half(leaf),yaw(leaf)));
    REQUIRE(collider.static_boxes()[id].oriented);
    collider.clear_static_boxes();
    REQUIRE(!collider.set_kinematic_oriented_box(id,centre(leaf),half(leaf),yaw(leaf)));
    apricot_test::pass("rotated support and camera rays avoid phantom corners; moving slots stay valid");
}
}
int main() {
    bank_walkways_match_visible_edges();
    oriented_queries_and_moving_slots_agree();
    locked_and_code_flow(); proximity_and_safety();
    real_collision_follows_door(); fixture_and_pose_contracts();
    std::puts("PASS bank_vault_tests");
}
