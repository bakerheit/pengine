#include "app/app.h"

#include <algorithm>

#include "core/log.h"

namespace apricot {

// Uses an existing patrol and the normal attributed-hit response API. Only
// the player is staged; all cruiser movement and officer actions are live.
InputFrame App::police_pursuit_check_input() {
    InputFrame input;
    input.handbrake = 1.0f;
    ++police_officer_check_tick_;
    if (police_officer_check_failed_ || police_officer_check_done_ ||
        police_officer_check_tick_ < 120u) return input;
    const auto fail = [&](const char* reason) {
        AP_ERROR("police pursuit check: %s (stage %d)", reason, police_officer_check_stage_);
        police_officer_check_failed_ = true;
        police_officer_check_capture_ = "failed";
    };
    const auto& cars = world_.traffic().vehicles();
    const auto& graph = world_.lanes();
    if (police_officer_check_stage_ == 0) {
        for (const auto& cop : cars) {
            if (!cop.police_unit || cop.police_pursuit || !graph.valid(cop.lane) ||
                cop.turn_from_lane != kInvalidLane || cop.committed_junction != 0xFFFFFFFFu ||
                cop.speed_mps < 1.0f || cop.speed_mps > 12.0f ||
                vehicle_engine_failed(cop.mechanical)) continue;
            const auto& lane = graph.lane(cop.lane);
            const LaneRef reverse = graph.opposing(cop.lane);
            const auto& links = graph.outgoing(cop.lane, true);
            const float remaining = lane.length_m - cop.dist_along_m;
            const float clear = traffic_junction_clearance(graph, lane.junction_to,
                                                           world_.traffic_tuning());
            if (cop.dist_along_m < 75.0f || remaining < clear + 40.0f || lane.width_m < 12.0f ||
                !graph.valid(reverse) || std::none_of(links.begin(), links.end(),
                    [&](const auto& turn) { return turn.to == reverse; })) continue;
            const auto behind = graph.pose(cop.lane, cop.dist_along_m - 50.0f);
            const auto target = graph.project_onto(reverse, {behind.position.x, behind.position.z});
            const auto pose = graph.pose(reverse, target.dist_along_m);
            if (glm::distance(cop.pos, car_.position) > 210.0f ||
                target.dist_along_m < clear + 20.0f) continue;
            if (std::any_of(cars.begin(), cars.end(), [&](const auto& other) {
                    return glm::distance(other.pos, pose.position) < 12.0f;
                })) continue;
            police_officer_check_unit_ = {cop.lane_key, cop.slot};
            police_pursuit_check_start_lane_ = cop.lane;
            teleport(pose.position, std::atan2(-pose.tangent.x, -pose.tangent.z));
            on_foot_ = false;
            wanted_.set_level(1);
            world_.set_police_context(1, car_.position, visible_police(car_.position));
            world_.report_police_vehicle_hit(police_officer_check_unit_);
            police_officer_check_stage_ = 1;
            police_officer_check_stage_tick_ = police_officer_check_tick_;
            police_officer_check_capture_ = "facing-away";
            AP_INFO("police pursuit check: unit %llu/%u, lane %u, player %.1fm behind, junction %u in %.1fm",
                static_cast<unsigned long long>(cop.lane_key), cop.slot, cop.lane,
                glm::distance(cop.pos, car_.position), lane.junction_to, remaining);
            break;
        }
        if (police_officer_check_tick_ > 2400u && police_officer_check_stage_ == 0)
            fail("no suitable away-facing patrol");
        return input;
    }
    const auto chosen = std::find_if(cars.begin(), cars.end(), [&](const auto& cop) {
        return VisiblePoliceIdentity{cop.lane_key, cop.slot} == police_officer_check_unit_;
    });
    if (chosen == cars.end()) { fail("selected patrol retired"); return input; }
    const auto& cop = *chosen;
    const float distance = glm::distance(cop.pos, car_.position);
    const uint64_t elapsed = police_officer_check_tick_ - police_officer_check_stage_tick_;
    for (const auto& driver : cars) {
        if (driver.police_unit || driver.emergency_yield == EmergencyYield::None) continue;
        if (driver.roadside_offset_m > .5f && !driver.maneuver.active() &&
            !police_pursuit_check_saw_yield_) {
            police_pursuit_check_saw_yield_ = true;
            police_pursuit_check_yield_unit_ = {driver.lane_key, driver.slot};
            police_pursuit_check_yield_position_ = driver.pos;
            police_officer_check_capture_ = "traffic-pulled-aside";
            AP_INFO("police pursuit check: civilian %llu/%u yielded, offset %.2fm, speed %.2f, cruiser range %.2fm",
                static_cast<unsigned long long>(driver.lane_key), driver.slot, driver.roadside_offset_m,
                driver.speed_mps, glm::distance(driver.pos, cop.pos));
        }
    }
    if (police_pursuit_check_saw_yield_ && !police_pursuit_check_saw_merge_) {
        const auto driver = std::find_if(cars.begin(), cars.end(), [&](const auto& car) {
            return VisiblePoliceIdentity{car.lane_key, car.slot} == police_pursuit_check_yield_unit_;
        });
        if (driver != cars.end() && driver->emergency_yield == EmergencyYield::None &&
            !driver->maneuver.active() && std::fabs(driver->roadside_offset_m) < .01f) {
            police_pursuit_check_saw_merge_ = true;
            police_pursuit_check_yield_position_ = driver->pos;
            police_officer_check_capture_ = "traffic-resumed";
            AP_INFO("police pursuit check: yielding civilian safely rejoined after %.2fs", double(elapsed)/120.0);
        }
    }
    if (!police_pursuit_check_saw_bypass_ && cop.maneuver.kind == TrafficManeuverKind::PoliceBypass &&
        cop.maneuver.progress_m > cop.maneuver.length_m * .35f) {
        police_pursuit_check_saw_bypass_ = true;
        police_officer_check_capture_ = "passing-traffic";
        AP_INFO("police pursuit check: cruiser passing traffic off its lane at %.2fs", double(elapsed)/120.0);
    }
    if (elapsed % 240u == 0u)
        AP_INFO("police pursuit check: %.1fs, lane %u, route %u/%zu, speed %.2f, distance %.2f, phase %u, turn %u",
            double(elapsed) / 120.0, cop.lane, cop.police_route_index, cop.police_route.size(),
            cop.speed_mps, distance, static_cast<unsigned>(cop.officer.phase),
            static_cast<unsigned>(cop.active_turn_kind));
    if (elapsed % 1200u == 0u && cop.speed_mps < .1f) {
        const auto& road = graph.lane(cop.lane);
        AP_INFO("police pursuit check: stopped at %.2f/%.2fm, width %.2f, local move %u, offset %.2f",
            cop.dist_along_m, road.length_m, road.width_m, unsigned(cop.maneuver.kind), cop.roadside_offset_m);
        for (const auto& other : cars) {
            if (&other == &cop || glm::distance(other.pos, cop.pos) > 25.f) continue;
            AP_INFO("police pursuit check: nearby %llu/%u lane %u, ahead %.2f, side %.2f, speed %.2f, yield %u, offset %.2f",
                static_cast<unsigned long long>(other.lane_key), other.slot, other.lane,
                glm::dot(other.pos-cop.pos,cop.fwd),
                glm::dot(other.pos-cop.pos,glm::vec3{-cop.fwd.z,0,cop.fwd.x}),
                other.speed_mps,unsigned(other.emergency_yield),other.roadside_offset_m);
        }
    }
    if (elapsed > 7200u) { fail("cruiser did not turn and pull over in 60 seconds"); return input; }
    if (police_officer_check_stage_ < 5 && !cop.police_pursuit) {
        fail("pursuit ended before approach"); return input;
    }
    const bool local_turn = cop.maneuver.kind == TrafficManeuverKind::PoliceTurnaround &&
        cop.maneuver.active() && cop.maneuver.progress_m > cop.maneuver.length_m * .35f;
    const bool junction_turn = cop.turn_from_lane == police_pursuit_check_start_lane_ &&
        cop.active_turn_kind == TurnKind::UTurn && cop.turn_progress_m > cop.turn_length_m * 0.35f;
    if (police_officer_check_stage_ == 1 && (local_turn || junction_turn)) {
        police_officer_check_stage_ = 2;
        police_officer_check_capture_ = "turning";
        AP_INFO("police pursuit check: live %s U-turn after %.2fs",
            local_turn ? "mid-block" : "junction", double(elapsed) / 120.0);
    }
    if (police_officer_check_stage_ == 2 && cop.turn_from_lane == kInvalidLane &&
        !cop.maneuver.active() && graph.lane(cop.lane).edge == graph.lane(police_pursuit_check_start_lane_).edge &&
        graph.lane(cop.lane).forward != graph.lane(police_pursuit_check_start_lane_).forward) {
        police_officer_check_stage_ = 3;
        police_officer_check_capture_ = "closing-in";
        AP_INFO("police pursuit check: reversed and closing, %.2fm from player", distance);
    }
    if (police_officer_check_stage_ == 3 && cop.officer.phase == PoliceOfficerPhase::Pursuing &&
        cop.speed_mps < 0.08f && distance <= kPoliceBlockedApproachRangeM) {
        police_officer_check_stage_ = 4;
        police_officer_check_capture_ = "pulled-over";
        AP_INFO("police pursuit check: stopped %.2fm away and officer exited in %.2fs",
            distance, double(elapsed) / 120.0);
    }
    if (police_officer_check_stage_ == 4 &&
        glm::distance(cop.officer.pos, car_.position) < 5.5f) {
        police_officer_check_stage_ = 5;
        police_officer_check_capture_ = "approached-player";
        AP_INFO("police pursuit check: officer reached player in %.2fs; standing down to check traffic recovery",
            double(elapsed) / 120.0);
        wanted_.set_level(0);
        world_.set_police_context(0, car_.position, {});
    }
    if (police_officer_check_stage_ == 5 && police_pursuit_check_saw_merge_) {
        police_officer_check_done_ = true;
        AP_INFO("police pursuit check: PASS; cruiser reversed, passed traffic, stopped and officer approached; civilian yielded and rejoined in %.2fs; bypass=%u",
            double(elapsed) / 120.0, unsigned(police_pursuit_check_saw_bypass_));
    }
    return input;
}

}  // namespace apricot
