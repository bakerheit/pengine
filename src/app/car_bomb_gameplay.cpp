#include "app/app.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/repair_shop.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

// Charcoal, not black. A body that is pure black reads as a paint job; one
// with a little warmth left in it reads as burnt.
constexpr PaintColor kCharredPaint{30, 27, 25};

// How hard the blast throws somebody at the lethal edge and at the reach. The
// dead at the car go furthest; somebody at the edge staggers.
constexpr float kThrowNearMps = 9.0f;
constexpr float kThrowFarMps = 2.5f;

float throw_at(float distance_m) {
    const float t = std::clamp(distance_m / kCarBombReachM, 0.0f, 1.0f);
    return kThrowNearMps + (kThrowFarMps - kThrowNearMps) * t;
}

}  // namespace

// The App's half of the car bomb. The rules are game/car_bomb.h, stepped here
// inside the fixed step; what lives here is what needs the world: which car is
// the rigged one and where it is, the blast, the fire, the bodies and the heat.

uint64_t App::parked_vehicle_identity(std::size_t index) const {
    if (index >= parked_vehicles_.size()) return 0;
    const auto& parked = parked_vehicles_[index];
    return vehicle_identity(parked.state.mechanical_key, parked.visual.active_car());
}

bool App::vehicle_burnt(uint64_t identity) const {
    return std::find(burnt_vehicles_.begin(), burnt_vehicles_.end(), identity) !=
           burnt_vehicles_.end();
}

const VehicleState* App::car_bomb_vehicle_state(uint64_t identity) const {
    if (identity == 0) return nullptr;
    if (identity == respray_vehicle_identity()) return &car_;
    for (std::size_t i = 0; i < parked_vehicles_.size(); ++i)
        if (parked_vehicle_identity(i) == identity) return &parked_vehicles_[i].state;
    return nullptr;
}

// K. Taken before the mapper, like R at the booth, so it never reaches the
// replay tape: the press enters the sim as a pending request on the next step,
// which is how the respray order and the pistol's trigger arrive too.
bool App::try_car_bomb_key(const SDL_Event& e) {
    const bool pressed = e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
        e.key.keysym.sym == SDLK_k && !(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI));
    if (!pressed) return false;
    if (!weapon_focus_ || ui_.screen() != UiScreen::Driving || dev_menu_.open() ||
        vehicle_transition_.active() || boat_transition_.active() ||
        !player_vitals_.alive() || paint_shop_.modal())
        return false;
    car_bomb_trigger_pending_ = true;
    return true;
}

void App::step_car_bomb_rules(int step_in_frame) {
    const bool driving = !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
                         !vehicle_transition_.active() && player_vitals_.alive();
    CarBombInput in;
    in.trigger = step_in_frame == 0 && car_bomb_trigger_pending_;
    if (step_in_frame == 0) car_bomb_trigger_pending_ = false;
    in.driven_vehicle = driving ? respray_vehicle_identity() : 0u;
    // The same gate the respray takes an order through: pulled in, stopped,
    // upright. A spray in progress has the bay.
    in.bay_ready = driving && respray_visit_.arrived && !respray_visit_.spraying() &&
                   repair_shop_ready(car_, tuning_);
    const VehicleState* rigged = car_bomb_.rigged ? car_bomb_vehicle_state(car_bomb_.vehicle)
                                                  : nullptr;
    in.rigged_exists = rigged != nullptr;
    in.rigged_on_lot = rigged != nullptr && on_repair_lot(*rigged);
    const CarBombResult result = step_car_bomb(car_bomb_, in);
    switch (result.event) {
    case CarBombEvent::None:
        return;
    case CarBombEvent::NoBomb:
        AP_INFO("car bomb: K with no bomb to fit or set off");
        return;
    case CarBombEvent::Lost:
        AP_INFO("car bomb: the rigged car left the world; the bomb went with it");
        return;
    case CarBombEvent::OnLot:
        vehicle_interaction_notice_ = "Not on Rook's lot - drive the rigged car off it first";
        vehicle_notice_until_ = step_index_ + 300;
        AP_INFO("car bomb: refused on Rook's lot");
        return;
    case CarBombEvent::Fitted: {
        ++car_bomb_fits_;
        car_bomb_feedback_title_ = "CAR BOMB FITTED";
        car_bomb_feedback_line_ = result.moved_from != 0
            ? "THE OLD ONE IS DISARMED - DRIVE OFF THE LOT, THEN K"
            : "DRIVE OFF THE LOT, THEN K TO SET IT OFF";
        car_bomb_feedback_s_ = 3.5f;
        VoiceParams click;
        click.category = Category::Impacts;
        click.spatial = true;
        click.position = car_.position;
        click.gain = 0.9f;
        audio_device_.mixer().play_oneshot(&weapon_reload_clip_, click);
        AP_INFO("car bomb: fitted to %s%s",
                player_car_definition(car_visual_.active_car()).model,
                result.moved_from != 0 ? " (the previous one disarmed)" : "");
        return;
    }
    case CarBombEvent::Detonated:
        detonate_car_bomb(result.vehicle);
        return;
    }
}

void App::detonate_car_bomb(uint64_t identity) {
    const bool current = identity == respray_vehicle_identity();
    std::size_t parked_index = parked_vehicles_.size();
    if (!current) {
        for (std::size_t i = 0; i < parked_vehicles_.size(); ++i)
            if (parked_vehicle_identity(i) == identity) parked_index = i;
        if (parked_index == parked_vehicles_.size()) {
            AP_WARN("car bomb: detonated a car that is nowhere in the world");
            return;
        }
    }
    VehicleState& car = current ? car_ : parked_vehicles_[parked_index].state;
    const glm::vec3 origin = car.position + glm::vec3{0.0f, 0.9f, 0.0f};
    ++car_bomb_detonations_;
    // A fast player sets it off while "fitted" is still on screen.
    car_bomb_feedback_s_ = 0.0f;

    // Whoever is inside goes out of it first, the way the exit does, so every
    // system after this sees a player on foot beside a wreck and not a driver
    // sitting in one. The blast below is what kills them.
    const bool occupied = current && !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_;
    if (occupied) {
        drop_trailer();
        place_character_next_to_car(false);
        on_foot_ = true;
        police_emergency_enabled_ = false;
        vehicle_audio_.exit_vehicle();
        character_look_dx_pending_ = 0.0f;
        character_look_dy_pending_ = 0.0f;
        camera_obstruction_distance_ = -1.0f;
    }

    wreck_car_bomb_vehicle(car);
    burnt_vehicles_.push_back(identity);
    if (current) {
        // Only the current car is stepped while nobody drives it; a parked car
        // is a fixed obstacle and stays where it stood.
        launch_car_bomb_vehicle(car_, tuning_, identity);
        sync_current_vehicle_obstacle();
    } else {
        auto& parked = parked_vehicles_[parked_index];
        parked.visual.sync(scene_, parked.tuning, parked.state, parked.state, 0, 0, 0);
    }

    // Burnt paint. A car with no paint profile yet keeps its colour; the dents,
    // the dead lamps and the fire still say what happened to it.
    PlayerCarVisual& visual = current ? car_visual_ : parked_vehicles_[parked_index].visual;
    if (PaintMaterialPool::recolour_atlas(visual.worn_atlas())) {
        const MaterialId charred = current
            ? acquire_paint_material(kCharredPaint)
            : paint_pool_.acquire(visual.worn_atlas(), kCharredPaint, live_body_materials());
        if (charred != kInvalidId) visual.apply_respray(scene_, charred, kCharredPaint);
    }

    wreck_blast_.emit(origin, identity ^ step_index_);

    VoiceParams sound;
    sound.category = Category::Impacts;
    sound.spatial = true;
    sound.position = origin;
    sound.gain = 1.0f;
    // The recorded blast when there is one; the metal, the glass and the fuel
    // catching regardless. An empty clip is silence, not an error.
    audio_device_.mixer().play_oneshot(&car_bomb_blast_clip_, sound);
    vehicle_audio_.play_car_collision(30.0f, origin);
    audio_device_.mixer().play_oneshot(&molotov_glass_clip_, sound);
    audio_device_.mixer().play_oneshot(&molotov_whoosh_clip_, sound);

    // Everything below looks out from inside the car; its own box must not be
    // the first thing every ray hits.
    const std::size_t box = current ? current_vehicle_collider_
                                    : parked_vehicles_[parked_index].collider;
    const bool box_valid = box < collider_.static_boxes().size();
    const bool box_enabled = box_valid && collider_.static_boxes()[box].enabled;
    if (box_valid) collider_.set_kinematic_enabled(box, false);
    const auto sheltered = [this, origin](glm::vec3 target) {
        const glm::vec3 delta = target - origin;
        const float distance = glm::length(delta);
        if (!(distance > 0.05f)) return false;
        const auto wall = collider_.raycast(origin, delta / distance, distance);
        return wall.hit && wall.distance < distance - 0.1f;
    };

    const auto under = collider_.probe_down(origin, 30.0f, TerrainCollider::ProbeVehicles::Exclude);
    if (under.hit && under.point.y > kSeaLevelMetres + 0.15f)
        fire_.ignite(origin, under.point.y, identity ^ 0x424F4D42ull);

    // Heat for the bomb itself, not gated on a witness, for the reason
    // kArsonHeat gives. Each body is charged on its own below.
    wanted_.add_heat(kCarBombHeat, WantedSystem::Crime::Violent);

    const int64_t step = static_cast<int64_t>(step_index_);
    unsigned hurt = 0, killed = 0;
    for (const auto& target : world_.traffic().standing_within(origin, kCarBombReachM)) {
        if (sheltered(target.body)) continue;
        const float distance = glm::distance(target.body, origin);
        const PedShotHit body = world_.blast_ped(target, origin, car_bomb_damage(distance),
                                                 throw_at(distance), step);
        if (!body.hit) continue;
        ++hurt;
        if (body.killed) ++killed;
        charge_body_heat(body);
    }

    // The player: killed outright if they were sitting on it, otherwise the
    // same falloff as anybody else, and thrown the same way.
    if (occupied) {
        damage_player(kCarBombLethalDamage, "a car bomb");
    } else if (on_foot_ && player_vitals_.alive()) {
        const glm::vec3 chest = player_character_.position + glm::vec3{0.0f, 1.2f, 0.0f};
        const float distance = glm::distance(chest, origin);
        const float damage = car_bomb_damage(distance);
        if (damage > 0.0f && !sheltered(chest)) {
            glm::vec3 away = chest - origin;
            away.y = 0.0f;
            const float planar = glm::length(away);
            away = planar > 1e-3f ? away / planar : glm::vec3{0.0f, 0.0f, 1.0f};
            const float speed = throw_at(distance) * 0.6f;
            player_character_.velocity += away * speed + glm::vec3{0.0f, speed * 0.4f, 0.0f};
            player_character_.grounded = false;
            // The throw is part of the blast; charging the landing on top of
            // it would bill one event twice.
            player_landing_speed_mps_ = 0.0f;
            damage_player(damage, "a car bomb");
        }
    }
    if (box_valid) collider_.set_kinematic_enabled(box, box_enabled);
    if (current) sync_current_vehicle_obstacle();

    AP_INFO("car bomb: detonated %s at %.1f, %.1f; %u hit, %u killed; wanted %d",
            player_car_definition(visual.active_car()).model,
            static_cast<double>(origin.x), static_cast<double>(origin.z), hurt, killed,
            wanted_.level());
}

// The respawn goes beside the current car, and after a car bomb the current
// car is the middle of a fire. Step out of it to the nearest clear spot on a
// ring round the wreck, rather than handing the player back to the flames.
void App::respawn_clear_of_fire() {
    if (fire_.heat_at(player_character_.position) <= 0.05f) return;
    const float ground = car_.position.y - tuning_.wheel_radius -
        static_suspension_length(tuning_) - tuning_.com_height_above_mount;
    const float yaw = player_character_.facing_yaw;
    for (const float radius : {7.0f, 9.0f, 11.5f, 14.0f}) {
        for (int k = 0; k < 12; ++k) {
            const float angle = static_cast<float>(k) * 0.5235988f;
            const PlayerCharacterState trial = spawn_character(collider_,
                car_.position.x + std::cos(angle) * radius,
                car_.position.z + std::sin(angle) * radius, yaw);
            if (trial.position.y > ground + 1.2f || trial.position.y < ground - 2.0f) continue;
            if (fire_.heat_at(trial.position) > 0.05f) continue;
            if (!character_position_clear(collider_, trial.position, character_tuning_)) continue;
            player_character_ = trial;
            prev_player_character_ = trial;
            return;
        }
    }
    AP_WARN("respawn: no clear ground outside the fire; respawning beside the car");
}

void App::draw_car_bomb_prompt(glm::vec2 vp) {
    if (paint_shop_.modal() || respray_visit_.spraying() || !player_vitals_.alive()) return;
    const bool driving = !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
                         !vehicle_transition_.active();
    const VehicleState* rigged = car_bomb_.rigged ? car_bomb_vehicle_state(car_bomb_.vehicle)
                                                  : nullptr;
    CarBombHintInput hint;
    hint.driving = driving;
    hint.bay_ready = driving && respray_visit_.arrived && !respray_visit_.spraying() &&
                     repair_shop_ready(car_, tuning_);
    hint.in_rigged_car = driving && car_bomb_.rigged &&
                         car_bomb_.vehicle == respray_vehicle_identity();
    hint.rigged = rigged != nullptr;
    hint.rigged_on_lot = rigged != nullptr && on_repair_lot(*rigged);
    const CarBombHint h = car_bomb_hint(hint);
    const char* text = car_bomb_hint_text(h);
    if (!*text) return;
    const glm::vec4 ink = h == CarBombHint::Armed ? glm::vec4{1.0f, 0.46f, 0.30f, 1.0f}
                                                  : glm::vec4{1.0f, 0.95f, 0.75f, 1.0f};
    // Above the respray line (vp.y - 118) and the interaction prompt (- 90).
    hud_.text_centered(text, vp.x * 0.5f, vp.y - 146.0f, 20.0f, ink);
}

void App::draw_car_bomb_card(glm::vec2 vp) {
    if (car_bomb_feedback_s_ <= 0.0f || vp.x <= 0.0f || vp.y <= 0.0f) return;
    const float alpha = glm::smoothstep(0.0f, 0.6f, car_bomb_feedback_s_);
    const float centre = vp.x * 0.5f;
    const float top = vp.y * 0.29f;
    const float height = std::min(72.0f, vp.x * 0.09f);
    const float half = std::max(hud_.measure_title_text(car_bomb_feedback_title_, height),
                                hud_.measure_text(car_bomb_feedback_line_, 22.0f)) * 0.5f + 54.0f;
    const float bottom = top + height + 62.0f;
    hud_.quad({centre - half - 14.0f, top}, {centre + half, top}, {centre + half + 14.0f, bottom},
              {centre - half, bottom}, {0.012f, 0.025f, 0.04f, 0.84f * alpha});
    hud_.title_text_centered(car_bomb_feedback_title_, centre + 4.0f, top + 15.0f, height,
                             {0.0f, 0.0f, 0.0f, 0.9f * alpha});
    hud_.title_text_centered(car_bomb_feedback_title_, centre, top + 11.0f, height,
                             {1.0f, 0.46f, 0.30f, alpha});
    hud_.text_centered(car_bomb_feedback_line_, centre, top + height + 24.0f, 22.0f,
                       {1.0f, 0.95f, 0.8f, alpha});
}

}  // namespace apricot
