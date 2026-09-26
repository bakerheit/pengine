#include "app/app.h"

#include <cmath>
#include <sstream>

namespace apricot {
void App::publish_playtest_state() {
    if (!playtest_.due()) return;
    const auto position = player_focus_position();
    const auto forward = on_foot_ ? character_forward(player_character_.view_yaw)
                                 : vehicle_forward(car_);
    const auto target = nearby_vehicle();
    const float speed = glm::length(on_foot_ ? player_character_.velocity : car_.velocity);
    const auto car_delta = car_.position - position;
    std::ostringstream state;
    state << std::boolalpha << "{\"version\":1,\"frame\":" << frames_rendered_
          << ",\"sim_step\":" << step_index_
          << ",\"accepted\":" << playtest_.accepted << ",\"completed\":" << playtest_.completed
          << ",\"action\":\"" << playtest_.current_action << "\",\"playing\":" << (ui_.screen() == UiScreen::Driving)
          << ",\"on_foot\":" << on_foot_ << ",\"transition\":" << vehicle_transition_.active()
          << ",\"alive\":" << player_vitals_.alive() << ",\"health\":" << player_vitals_.health
          << ",\"speed_mps\":" << speed << ",\"position\":[" << position.x << ',' << position.y << ',' << position.z << ']'
          << ",\"forward\":[" << forward.x << ',' << forward.z << ']'
          << ",\"car_position\":[" << car_.position.x << ',' << car_.position.y << ',' << car_.position.z << ']'
          << ",\"car_distance_m\":" << glm::length(glm::vec2{car_delta.x, car_delta.z})
          << ",\"can_enter\":" << (on_foot_ && target.kind != VehicleEntryTarget::Kind::None && !target.locked)
          << ",\"car_health\":" << car_.health << ",\"impacts\":" << car_.impact_count
          << ",\"distance_walked_m\":" << player_character_.distance_walked_m
          << ",\"fps\":" << fps_ << ",\"gl_errors\":" << gl_errors_;
    const auto hit = collider_.raycast(position + glm::vec3{0, .8f, 0}, forward, 8.f);
    state << ",\"obstacle_ahead_m\":" << (hit.hit ? hit.distance : 8.f);
    if (playtest_.capture) {
        const bool saved = save_screenshot((playtest_.directory / "snapshot.png").string());
        if (saved) playtest_capture_id_ = playtest_.completed;
        playtest_.capture = false;
    }
    state << ",\"capture_id\":" << playtest_capture_id_ << '}';
    playtest_.publish(state.str());
}
} // namespace apricot
