#include "app/app.h"

#include <algorithm>
#include <cmath>

#include "app/vehicle_paint_catalog.h"
#include "core/log.h"
#include "game/ui_canvas.h"

namespace apricot {

// The App's half of a respray at Rook's Auto Repair. The rules are
// game/respray_shop.h (stepped here, inside the fixed step), the booth is
// app/paint_shop_interaction.h, the paint is app/vehicle_paint_materials.h.
// What lives here is the glue only App can do: it knows the live car, every
// parked car, the camera, the HUD and the wanted system.

uint64_t App::respray_vehicle_identity() const {
    // A theft, a parked car re-entered and a dev car swap all change this, and
    // a changed identity starts the visit over.
    return car_.mechanical_key ^
        ((static_cast<uint64_t>(car_visual_.active_car()) + 1u) * 0x9E3779B97F4A7C15ull);
}

std::vector<MaterialId> App::live_body_materials() const {
    std::vector<MaterialId> live;
    live.reserve(parked_vehicles_.size() + 2u);
    live.push_back(car_visual_.paint(scene_));
    if (const auto& colour = car_visual_.respray()) {
        const MaterialId owned = paint_pool_.find(car_visual_.worn_atlas(), *colour);
        if (owned != kInvalidId) live.push_back(owned);
    }
    for (const auto& parked : parked_vehicles_) {
        const MaterialId worn = parked.visual.paint(scene_);
        // The preview slot is repainted every time the picked colour changes,
        // so a parked car wearing it would change colour behind the player.
        if (paint_pool_.is_preview(worn))
            AP_ERROR("respray: a parked car is wearing the booth preview paint");
        live.push_back(worn);
    }
    return live;
}

MaterialId App::acquire_paint_material(PaintColor colour) {
    const char* worn = car_visual_.worn_atlas();
    if (!PaintMaterialPool::recolour_atlas(worn)) return kInvalidId;
    for (;;) {
        const std::vector<MaterialId> live = live_body_materials();
        const MaterialId id = paint_pool_.acquire(worn, colour, live);
        if (id != kInvalidId) return id;
        // Every owned slot is worn. Free one: the first parked painted car that
        // is the only body wearing its slot goes back to its factory paint.
        bool freed = false;
        for (auto& parked : parked_vehicles_) {
            const MaterialId slot = parked.visual.paint(scene_);
            if (!parked.visual.respray() || !paint_pool_.owns(slot) ||
                PaintMaterialPool::live_refs(slot, live) != 1u) continue;
            parked.visual.clear_respray(scene_);
            AP_WARN("respray: paint pool full; a parked %s went back to factory paint",
                    player_car_definition(parked.visual.active_car()).model);
            freed = true;
            break;
        }
        if (!freed) return kInvalidId;
    }
}

MaterialId App::committed_body_material() {
    const auto colour = car_visual_.respray();
    if (!colour) return car_visual_.factory_material();
    MaterialId owned = paint_pool_.find(car_visual_.worn_atlas(), *colour);
    if (owned == kInvalidId) owned = acquire_paint_material(*colour);
    if (owned != kInvalidId) return owned;
    AP_WARN("respray: no paint slot left for this car's colour; showing its factory paint");
    car_visual_.clear_respray(scene_);
    return car_visual_.factory_material();
}

void App::reset_respray_state() {
    if (paint_shop_.modal()) {
        paint_shop_ = PaintShopInteraction{};
        input_.set_ui_mode(ui_.modal());
    }
    respray_visit_ = {};
    respray_order_pending_.reset();
    respray_previous_.reset();
    respray_feedback_s_ = 0.0f;
    respray_camera_hold_s_ = 0.0f;
    respray_sight_valid_ = false;
    respray_reveal_step_ = -1;
    respray_sound_.stop_hiss(audio_device_.mixer());
    if (paint_pool_.is_preview(car_visual_.paint(scene_)))
        car_visual_.preview_body_material(scene_, committed_body_material());
}

bool App::route_paint_shop_event(const SDL_Event& e) {
    if (!paint_shop_.modal() && !paint_input_consumed_) return false;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_.sdl(), &w, &h);
    const UiCanvas canvas = UiCanvas::from_drawable(
        {static_cast<float>(window_.width()), static_cast<float>(window_.height())});
    paint_shop_.event(e, {static_cast<float>(w), static_cast<float>(h)}, canvas.size);
    // Once the booth has taken a frame's input, it keeps the rest of it: the
    // key that confirmed must not also reach the car behind the closed panel.
    paint_input_consumed_ = true;
    input_.handle_event(e);
    return true;
}

bool App::try_open_paint_shop(const SDL_Event& e, bool road_vehicle_controls) {
    const bool pressed =
        (e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_r &&
         !(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) ||
        (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_X);
    if (!pressed || !road_vehicle_controls || !player_vitals_.alive()) return false;
    if (!respray_visit_.arrived || respray_visit_.spraying() || respray_order_pending_ ||
        !repair_shop_ready(car_, tuning_) || player_car_paint_pending(car_visual_.active_car()))
        return false;
    if (!open_paint_shop()) return false;
    paint_input_consumed_ = true;
    return true;
}

bool App::open_paint_shop() {
    const char* worn = car_visual_.worn_atlas();
    const std::optional<PaintColor> factory = paint_pool_.factory_colour(worn);
    if (!factory) {
        AP_WARN("respray: %s has no paint profile", worn);
        return false;
    }
    PaintPickerOpen init;
    init.current_is_factory = !car_visual_.respray().has_value();
    if (car_visual_.respray()) init.current = *car_visual_.respray();
    init.factory = *factory;
    init.previous = respray_previous_;
    init.wanted_level = wanted_.level();
    init.seen = respray_visit_.seen;
    paint_shop_.open(init);
    paint_restore_mouse_ = input_.mouse_look();
    input_.set_ui_mode(true);
    choose_respray_sight();
    AP_INFO("respray: booth open on %s (wanted %d, %s)", worn, init.wanted_level,
            init.seen ? "a cop saw the pull-in" : "unseen");
    return true;
}

void App::choose_respray_sight() {
    const auto& site = city::kAutoRepairSite;
    const glm::vec2 local =
        repair_site_local(car_.position - glm::vec3{site.origin.x, 0.0f, site.origin.z});
    const float bay_x = std::fabs(local.x + 9.0f) < std::fabs(local.x + 1.0f) ? -9.0f : -1.0f;
    const UiCanvas canvas = UiCanvas::from_drawable(
        {static_cast<float>(window_.width()), static_cast<float>(window_.height())});
    const float free_fraction = PaintPickerLayout::from_canvas(canvas.size).free_fraction();
    // Scored as update_camera looks: through the car's own collision box.
    collider_.set_kinematic_enabled(current_vehicle_collider_, false);
    if (trailer_.attached) enable_trailer_collision(false);
    respray_sight_ = choose_respray_camera(collider_, car_, tuning_, bay_x, free_fraction,
                                           window_.aspect());
    if (trailer_.attached) enable_trailer_collision(true);
    sync_current_vehicle_obstacle();
    respray_sight_valid_ = true;
    if (!respray_sight_.clear)
        AP_WARN("respray: no bay camera cleared the garage; using the fallback shot");
}

bool App::process_paint_shop_input(float dt) {
    if (!paint_shop_.modal() && !paint_input_consumed_) return false;
    paint_shop_.update(dt, input_.ui_axis_x(), input_.ui_axis_y(), input_.frame().look_dx);
    // At most one composite per rendered frame, whatever the events did.
    if (paint_shop_.preview_changed()) {
        const PaintOrder preview = paint_shop_.preview();
        const MaterialId material = preview.factory
            ? car_visual_.factory_material()
            : paint_pool_.preview(car_visual_.worn_atlas(), preview.colour);
        if (material != kInvalidId) car_visual_.preview_body_material(scene_, material);
    }
    if (paint_shop_.take_cancel()) {
        car_visual_.preview_body_material(scene_, committed_body_material());
        AP_INFO("respray: booth cancelled");
    }
    if (const std::optional<PaintOrder> order = paint_shop_.take_order()) {
        // The order enters the sim on the next step. The car goes back to the
        // paint it wears, so the reveal starts from it.
        respray_order_pending_ = order;
        car_visual_.preview_body_material(scene_, committed_body_material());
        AP_INFO("respray: ordered %s", paint_order_name(*order));
    }
    if (paint_shop_.modal()) {
        input_.set_ui_mode(true);
    } else {
        input_.set_ui_mode(ui_.modal());
        if (paint_restore_mouse_) input_.set_mouse_look(true);
        paint_restore_mouse_ = false;
    }
    input_.consume_edges();
    clock_.reset();
    return true;
}

void App::step_respray_visit(int step_in_frame) {
    const bool driving = !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
                         !vehicle_transition_.active() && player_vitals_.alive();
    const bool first = step_in_frame == 0;
    const PaintOrder* request = first && respray_order_pending_ ? &*respray_order_pending_ : nullptr;
    const ResprayResult result = step_respray_shop(respray_visit_, car_, tuning_,
        static_cast<float>(kSimDt), driving, respray_vehicle_identity(), police_eyes_on_, request);
    if (first) respray_order_pending_.reset();
    // PREVIOUS belongs to the visit.
    if (!respray_visit_.in_bay) respray_previous_.reset();
    apply_respray_result(result);
}

void App::apply_respray_result(const ResprayResult& result) {
    switch (result.event) {
    case ResprayEvent::None:
        return;
    case ResprayEvent::Started:
        respray_reveal_step_ = -1;
        respray_sound_.start_hiss(audio_device_.mixer(), respray_clips_);
        AP_INFO("respray: spraying %s", paint_order_name(result.order));
        return;
    case ResprayEvent::Rejected:
    case ResprayEvent::Cancelled:
        car_visual_.preview_body_material(scene_, committed_body_material());
        respray_sound_.stop_hiss(audio_device_.mixer());
        respray_reveal_step_ = -1;
        if (result.event == ResprayEvent::Cancelled) {
            vehicle_interaction_notice_ = "Respray cancelled - hold still in the bay";
            vehicle_notice_until_ = step_index_ + 240;
            AP_INFO("respray: cancelled");
        } else {
            AP_INFO("respray: order refused");
        }
        return;
    case ResprayEvent::Completed:
        break;
    }

    // The wanted outcome is decided and applied before any paint work, so it
    // never depends on a decode or an upload.
    const ResprayOutcome outcome = respray_outcome(wanted_.level(), result.seen);
    if (outcome == ResprayOutcome::WantedCleared) {
        clear_heat_after_respray(wanted_, police_offenses_, police_arrest_);
        world_.set_police_context(0, player_focus_position());
        wanted_report_blink_ = false;
    }
    respray_previous_ = car_visual_.respray() ? PaintOrder{false, *car_visual_.respray()}
                                              : PaintOrder{true, {}};
    bool painted = true;
    if (result.order.factory) {
        car_visual_.clear_respray(scene_);
    } else if (const MaterialId owned = acquire_paint_material(result.order.colour);
               owned != kInvalidId) {
        car_visual_.apply_respray(scene_, owned, result.order.colour);
    } else {
        painted = false;
        car_visual_.preview_body_material(scene_, committed_body_material());
        vehicle_interaction_notice_ = "PAINT FAILED - TRY AGAIN";
        vehicle_notice_until_ = step_index_ + 300;
        AP_WARN("respray: the paint could not be applied");
    }
    respray_reveal_step_ = -1;
    respray_feedback_outcome_ = outcome;
    respray_feedback_order_ = result.order;
    respray_feedback_s_ = painted || outcome != ResprayOutcome::Painted ? 3.5f : 0.0f;
    respray_camera_hold_s_ = 0.6f;
    if (outcome == ResprayOutcome::WantedKept)
        respray_sound_.play_kept(audio_device_.mixer(), respray_clips_);
    else
        respray_sound_.play_done(audio_device_.mixer(), respray_clips_,
                                 outcome == ResprayOutcome::WantedCleared);
    ++respray_completions_;
    AP_INFO("respray: complete, %s, wanted %s", paint_order_name(result.order),
            outcome == ResprayOutcome::WantedCleared ? "cleared"
                : outcome == ResprayOutcome::WantedKept ? "kept (a cop saw the pull-in)" : "none");
}

void App::step_respray_reveal() {
    if (!respray_visit_.spraying()) {
        respray_reveal_step_ = -1;
        return;
    }
    const int step = std::clamp(
        static_cast<int>(std::floor(respray_visit_.spray_s / kRespraySeconds * 16.0f)), 0, 15);
    if (step == respray_reveal_step_) return;
    respray_reveal_step_ = step;
    const char* worn = car_visual_.worn_atlas();
    const std::optional<PaintColor> factory = paint_pool_.factory_colour(worn);
    if (!factory) return;
    const PaintColor from = car_visual_.respray().value_or(*factory);
    const PaintColor to = respray_visit_.order.factory ? *factory : respray_visit_.order.colour;
    const MaterialId material = paint_pool_.preview(
        worn, paint_mix_oklab(from, to, static_cast<float>(step + 1) / 16.0f));
    if (material == kInvalidId) return;
    car_visual_.preview_body_material(scene_, material);
    ++respray_reveal_count_;
}

bool App::respray_camera_active() const {
    return respray_sight_valid_ && !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
           !vehicle_transition_.active() &&
           (paint_shop_.modal() || respray_visit_.spraying() || respray_order_pending_.has_value() ||
            respray_camera_hold_s_ > 0.0f);
}

void App::respray_camera_pose(ChaseCameraPose& pose, float dt) {
    const RespraySight& sight = respray_sight_;
    const glm::quat spin = glm::angleAxis(paint_shop_.orbit_yaw(), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::vec3 eye = sight.pivot + spin * (sight.eye - sight.pivot);
    const glm::vec3 target = sight.pivot + spin * (sight.target - sight.pivot);
    respray_camera_blend_ = applied_ui_settings_.reduced_motion
        ? 1.0f : std::min(1.0f, respray_camera_blend_ + std::max(dt, 0.0f) / 0.40f);
    const float blend = vehicle_transition_ease(respray_camera_blend_);
    pose.target = glm::mix(transition_camera_.position + transition_camera_.forward() * 4.8f,
                           target, blend);
    pose.collision_pivot = sight.pivot;
    pose.desired_eye = glm::mix(transition_camera_.position, eye, blend);
    pose.fov_y = glm::mix(transition_camera_.fov_y, glm::radians(sight.fov_deg), blend);
}

void App::draw_respray_prompt(glm::vec2 vp) {
    if (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_ || paint_shop_.modal()) return;
    ResprayHintInput hint;
    hint.driving = !vehicle_transition_.active() && player_vitals_.alive();
    hint.on_lot = on_repair_lot(car_);
    hint.fits = repair_bay_fits(car_, tuning_);
    hint.ready = repair_shop_ready(car_, tuning_);
    hint.pending_car = player_car_paint_pending(car_visual_.active_car());
    hint.eyes_on = police_eyes_on_;
    hint.wanted_level = wanted_.level();
    hint.visit = &respray_visit_;
    const char* text = respray_hint_text(respray_hint(hint));
    if (*text) hud_.text_centered(text, vp.x * 0.5f, vp.y - 118.0f, 20.0f, {1.0f, 0.95f, 0.75f, 1.0f});
    if (respray_visit_.spraying()) {
        const float progress = std::clamp(respray_visit_.spray_s / kRespraySeconds, 0.0f, 1.0f);
        const float left = vp.x * 0.5f - 180.0f;
        hud_.text_centered("RESPRAYING", vp.x * 0.5f, vp.y - 184.0f, 22.0f, {1.0f, 0.95f, 0.8f, 1.0f});
        hud_.rect({left, vp.y - 150.0f}, {left + 360.0f, vp.y - 144.0f}, {0.05f, 0.06f, 0.06f, 0.9f});
        hud_.rect({left, vp.y - 150.0f}, {left + 360.0f * progress, vp.y - 144.0f}, {1.0f, 0.8f, 0.35f, 1.0f});
    }
}

void App::draw_respray_booth(glm::vec2 vp) {
    if (!paint_shop_.modal()) return;
    PaintShopView view;
    view.wanted_level = wanted_.level();
    view.flash = wanted_report_blink_;
    view.step = static_cast<int64_t>(step_index_);
    paint_shop_.draw(hud_, game_ui_, vp, view);
}

void App::draw_respray_card(glm::vec2 vp) {
    if (respray_feedback_s_ <= 0.0f || vp.x <= 0.0f || vp.y <= 0.0f) return;
    const char* title = "RESPRAYED";
    const char* line = paint_order_name(respray_feedback_order_);
    glm::vec4 ink{0.94f, 0.91f, 0.82f, 1.0f};
    if (respray_feedback_outcome_ == ResprayOutcome::WantedCleared) {
        title = "LOST THE COPS";
        line = "FRESH PAINT - WANTED LEVEL CLEARED";
        ink = {1.0f, 0.68f, 0.12f, 1.0f};
    } else if (respray_feedback_outcome_ == ResprayOutcome::WantedKept) {
        title = "THEY SAW YOU PULL IN";
        line = "NEW PAINT, SAME STARS - LOSE THEM, THEN COME BACK";
        ink = {1.0f, 0.34f, 0.26f, 1.0f};
    }
    const float alpha = glm::smoothstep(0.0f, 0.6f, respray_feedback_s_);
    const float centre = vp.x * 0.5f;
    const float top = vp.y * 0.29f;
    const float height = std::min(72.0f, vp.x * 0.09f);
    const float half = std::max(hud_.measure_title_text(title, height),
                                hud_.measure_text(line, 22.0f)) * 0.5f + 54.0f;
    const float bottom = top + height + (repair_shop_feedback_s_ > 0.0f ? 86.0f : 62.0f);
    hud_.quad({centre - half - 14.0f, top}, {centre + half, top}, {centre + half + 14.0f, bottom},
              {centre - half, bottom}, {0.012f, 0.025f, 0.04f, 0.84f * alpha});
    hud_.title_text_centered(title, centre + 4.0f, top + 15.0f, height, {0.0f, 0.0f, 0.0f, 0.9f * alpha});
    hud_.title_text_centered(title, centre, top + 11.0f, height, {ink.r, ink.g, ink.b, alpha});
    hud_.text_centered(line, centre, top + height + 24.0f, 22.0f, {1.0f, 0.95f, 0.8f, alpha});
    if (repair_shop_feedback_s_ > 0.0f)
        hud_.text_centered("CAR REPAIRED", centre, top + height + 54.0f, 18.0f, {0.65f, 1.0f, 0.65f, alpha});
}

}  // namespace apricot
