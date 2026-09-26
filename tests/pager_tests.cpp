// The pager (game/pager.h), Lou's page after the delivery (game/lou_page.h)
// and the payphones it sends the player to (game/payphones.h).
//
// The reach test walks the real character controller into every real payphone
// footprint, on the real terrain and roads, from four sides: a reach radius
// that only works on paper is the bug it is there to catch.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "audio/synth.h"
#include "city/authored_staff.h"
#include "city/map.h"
#include "city/spines.h"
#include "core/fixed_step.h"
#include "game/character.h"
#include "game/lou_page.h"
#include "game/pager.h"
#include "game/payphones.h"
#include "road/ribbon.h"
#include "road/road_graph.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt = static_cast<float>(kSimDt);

int steps_for(float seconds) { return static_cast<int>(std::ceil(seconds / kDt)); }

void a_page_shows_crawls_and_goes() {
    Pager pager;
    REQUIRE(!pager.showing() && pager.pages_shown() == 0 && pager.slide() == 0.0f);
    pager.receive("");
    REQUIRE(!pager.showing() && pager.queued() == 0);  // Nothing to crawl.

    const std::string text = kLouPageText;
    pager.receive(text);
    REQUIRE(pager.showing() && pager.text() == text && pager.last_page() == text);
    REQUIRE(pager.pages_shown() == 1 && pager.queued() == 0);
    REQUIRE(pager.crawl_cells() == 0.0f);  // Just off the right edge.

    const float pass = static_cast<float>(kPagerLcdCells) + static_cast<float>(text.size());
    const float crawl_s = pass * static_cast<float>(kPagerCrawlPasses) / kPagerCrawlCellsPerSecond;
    const float total_s = Pager::display_seconds(text.size());
    REQUIRE_NEAR(total_s, crawl_s + kPagerSlideSeconds, 1e-5);

    int steps = 0;
    float last_crawl = 0.0f;
    int wraps = 0;
    while (pager.showing()) {
        pager.step(kDt);
        ++steps;
        REQUIRE(pager.pages_shown() == 1);
        const float t = pager.shown_seconds();
        if (!pager.showing()) break;
        // Slides fully on, stays on for the crawl, slides back off.
        if (t >= kPagerSlideSeconds && t <= total_s - kPagerSlideSeconds)
            REQUIRE(pager.slide() == 1.0f);
        const float crawl = pager.crawl_cells();
        REQUIRE(crawl >= 0.0f && crawl <= pass);
        if (crawl < last_crawl) ++wraps;  // The second pass starts over.
        last_crawl = crawl;
        // The buzz is the beep pattern, from the moment the page arrives.
        REQUIRE(pager.buzzing() == pager_beep_sounding(t));
        // Past the last pass the LCD is empty while the pager slides away.
        if (t >= crawl_s) REQUIRE(crawl == pass);
    }
    REQUIRE(wraps == kPagerCrawlPasses - 1);
    REQUIRE(std::abs(steps - steps_for(total_s)) <= 1);
    REQUIRE(pager.text().empty() && pager.slide() == 0.0f && pager.crawl_cells() == 0.0f);
    REQUIRE(pager.last_page() == text && pager.pages_shown() == 1);
    // A page lasts the same time at any step size: it is sim time, not frames.
    Pager coarse;
    coarse.receive(text);
    int coarse_steps = 0;
    while (coarse.showing()) { coarse.step(2.0f * kDt); ++coarse_steps; }
    REQUIRE(std::abs(2 * coarse_steps - steps) <= 2);
    apricot_test::pass("a page slides on, buzzes, crawls twice across the LCD and slides off on time");
}

void pages_wait_their_turn() {
    Pager pager;
    pager.receive("FIRST");
    pager.receive("SECOND");
    REQUIRE(pager.text() == "FIRST" && pager.queued() == 1 && pager.pages_shown() == 1);
    for (int i = 0; i < steps_for(Pager::display_seconds(5)) - 2; ++i) pager.step(kDt);
    REQUIRE(pager.text() == "FIRST" && pager.pages_shown() == 1);  // Not cut short.
    for (int i = 0; i < 4; ++i) pager.step(kDt);
    // The second starts the step the first ends, and beeps once of its own.
    REQUIRE(pager.showing() && pager.text() == "SECOND" && pager.pages_shown() == 2);
    REQUIRE(pager.queued() == 0 && pager.shown_seconds() < 4.0f * kDt);
    REQUIRE(pager.last_page() == "SECOND");
    pager.step(0.0f);
    pager.step(-1.0f);  // A zero or backwards step changes nothing.
    REQUIRE(pager.showing() && pager.shown_seconds() < 4.0f * kDt);
    apricot_test::pass("pages queue oldest first, each showing in full and beeping once");
}

void the_beep_sounds_the_pattern_the_pager_shakes_to() {
    for (const float t : {0.0f, 0.03f, 0.1f, 0.35f, 0.6f, 0.95f})
        REQUIRE_MSG(pager_beep_sounding(t), "silent where a beep is", std::to_string(t).c_str());
    for (const float t : {-0.1f, 0.06f, 0.099f, 0.45f, 0.59f, 0.96f, 1.0f, 5.0f})
        REQUIRE_MSG(!pager_beep_sounding(t), "a beep where it is silent", std::to_string(t).c_str());

    // The real clip, window by window: loud where the pattern beeps and silent
    // where it does not. A pattern changed on one side only fails here.
    const PcmClip clip = synth_pager_beep(kDefaultSampleRate);
    REQUIRE_NEAR(static_cast<double>(clip.duration_seconds()),
                 static_cast<double>(kPagerBuzzSeconds), 1e-3);
    // Each 2 ms window is judged at its centre, and skipped within 3 ms of a
    // beep's edge, where the clip's ramps live and a float boundary could go
    // either way.
    const std::size_t window = kDefaultSampleRate / 500;
    int loud = 0, quiet = 0;
    for (std::size_t start = 0; start + window <= clip.samples.size(); start += window) {
        const float centre = (static_cast<float>(start) + 0.5f * static_cast<float>(window)) /
                             static_cast<float>(kDefaultSampleRate);
        const bool sounding = pager_beep_sounding(centre);
        if (pager_beep_sounding(centre - 0.003f) != sounding ||
            pager_beep_sounding(centre + 0.003f) != sounding) continue;
        double sum = 0.0;
        for (std::size_t i = start; i < start + window; ++i)
            sum += static_cast<double>(clip.samples[i]) * static_cast<double>(clip.samples[i]);
        const double rms = std::sqrt(sum / static_cast<double>(window));
        if (sounding) {
            REQUIRE_MSG(rms > 0.2, "the pattern beeps and the clip is quiet", std::to_string(centre).c_str());
            ++loud;
        } else {
            REQUIRE_MSG(rms < 0.01, "the clip beeps and the pattern is quiet", std::to_string(centre).c_str());
            ++quiet;
        }
    }
    REQUIRE(loud >= 150 && quiet >= 200);
    apricot_test::pass("the synthesised beep sounds exactly the pattern the pager rattles to");
}

void lou_pages_once_the_delivery_is_done() {
    // Only a finished delivery leads to the page.
    for (const MissionStage other : {MissionStage::Opening, MissionStage::DeliveryActive,
                                     MissionStage::DeliveryNeedsCar, MissionStage::CallLou,
                                     MissionStage::LouCalled}) {
        MissionStage stage = other;
        float waited = 3.0f;
        for (int i = 0; i < steps_for(kLouPageDelaySeconds) * 2; ++i)
            REQUIRE(!step_lou_page(stage, waited, kDt));
        REQUIRE(stage == other && waited == 0.0f);
    }
    // The App's order: the rule, then the pager, on every step.
    MissionStage stage = MissionStage::DeliveryComplete;
    float waited = 0.0f;
    Pager pager;
    int fired_at = -1;
    for (int i = 1; i <= steps_for(kLouPageDelaySeconds) + 10; ++i) {
        if (step_lou_page(stage, waited, kDt)) {
            REQUIRE(fired_at < 0);  // Once.
            fired_at = i;
            pager.receive(kLouPageText);
        }
        pager.step(kDt);
    }
    REQUIRE(std::abs(fired_at - steps_for(kLouPageDelaySeconds)) <= 1);
    REQUIRE(stage == MissionStage::CallLou && waited == 0.0f);
    REQUIRE(pager.showing() && pager.text() == "Call me at store - Lou" && pager.pages_shown() == 1);
    // After the Mission Success card (6.25 s), never on top of it.
    REQUIRE(kLouPageDelaySeconds > 6.25f);

    // The wait restarts when the stage leaves the finished delivery, as the
    // delivery check's replay and every load do.
    stage = MissionStage::DeliveryComplete;
    waited = 0.0f;
    for (int i = 0; i < steps_for(kLouPageDelaySeconds) - 5; ++i)
        REQUIRE(!step_lou_page(stage, waited, kDt));
    stage = MissionStage::DeliveryActive;
    REQUIRE(!step_lou_page(stage, waited, kDt) && waited == 0.0f);
    stage = MissionStage::DeliveryComplete;
    for (int i = 0; i < 10; ++i) REQUIRE(!step_lou_page(stage, waited, kDt));
    REQUIRE(stage == MissionStage::DeliveryComplete);
    apricot_test::pass("Lou pages once, ten seconds after the delivery, and the wait restarts with the stage");
}

int payphone_named(const char* name) {
    for (std::size_t i = 0; i < kPayphoneCount; ++i)
        if (std::strcmp(payphone_site(i).name, name) == 0) return static_cast<int>(i);
    return -1;
}

void every_payphone_is_listed_once() {
    REQUIRE(kPayphoneCount == 12);
    for (std::size_t i = 0; i < kPayphoneCount; ++i) {
        const glm::vec3 p = payphone_position(i);
        REQUIRE_MSG(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
                    "payphone position is not finite", payphone_site(i).name);
        REQUIRE_MSG(std::fabs(p.x) < city::kWorldHalfMetres && std::fabs(p.z) < city::kWorldHalfMetres,
                    "payphone is off the map", payphone_site(i).name);
        REQUIRE_NEAR(p.y, payphone_site(i).ground_m, 1e-6);
        for (std::size_t j = 0; j < i; ++j)
            REQUIRE_MSG(glm::length(glm::vec2{p.x, p.z} - glm::vec2{payphone_position(j).x,
                                                                    payphone_position(j).z}) > 3.0f,
                        "two payphones share a spot", payphone_site(i).name);
        // Standing at the phone reaches that phone, not a neighbour.
        REQUIRE(payphone_in_reach(p, true) == static_cast<int>(i));
    }
    REQUIRE(payphone_named("Payphone: Ostend Docks") >= 0);
    REQUIRE(payphone_named("Payphone pedestal: Halloway Gas west") >= 0);
    REQUIRE(payphone_named("Wall phone: Halloway Gas west") >= 0);
    apricot_test::pass("all twelve payphones, the ten booths and Halloway Gas's two, one entry each");
}

void the_marker_picks_the_nearest_phone_and_holds_it() {
    // From Devon's counter the nearest is the Ostend Docks booth, 23 m off.
    const glm::vec3 devon = city::devon_position();
    const int ostend = payphone_named("Payphone: Ostend Docks");
    REQUIRE(nearest_payphone({devon.x, devon.z}) == ostend);
    const glm::vec3 booth = payphone_position(static_cast<std::size_t>(ostend));
    REQUIRE(glm::length(glm::vec2{booth.x - devon.x, booth.z - devon.z}) < 30.0f);
    // From behind Halloway Gas's own counter, one of its two phones.
    const glm::vec3 clerk = city::authored_staff_position(city::kAuthoredStaff[0]);
    const int gas = nearest_payphone({clerk.x, clerk.z});
    REQUIRE(gas == payphone_named("Payphone pedestal: Halloway Gas west") ||
            gas == payphone_named("Wall phone: Halloway Gas west"));
    // An index that is not a phone is ignored, not trusted.
    REQUIRE(nearest_payphone({devon.x, devon.z}, 99) == ostend);
    REQUIRE(nearest_payphone({devon.x, devon.z}, -7) == ostend);

    // Walk from the Nickel Heights booth to The Strand's and back. Nothing else
    // is near that line, so the marker changes exactly once each way, and only
    // once the new phone is the margin nearer: no flicker at the halfway line.
    const int nickel = payphone_named("Payphone: Nickel Heights");
    const int strand = payphone_named("Payphone: The Strand");
    const glm::vec3 a3 = payphone_position(static_cast<std::size_t>(nickel));
    const glm::vec3 b3 = payphone_position(static_cast<std::size_t>(strand));
    const glm::vec2 a{a3.x, a3.z}, b{b3.x, b3.z};
    for (const bool outbound : {true, false}) {
        const glm::vec2 from = outbound ? a : b;
        const glm::vec2 to = outbound ? b : a;
        int target = nearest_payphone(from);
        REQUIRE(target == (outbound ? nickel : strand));
        int switches = 0;
        for (int i = 0; i <= 1000; ++i) {
            const glm::vec2 p = glm::mix(from, to, static_cast<float>(i) / 1000.0f);
            const int next = nearest_payphone(p, target);
            REQUIRE(next == nickel || next == strand);
            if (next != target) {
                ++switches;
                const float gain = glm::length(p - from) - glm::length(p - to);
                REQUIRE(gain > kPayphoneRetargetMarginM);
                REQUIRE(gain < kPayphoneRetargetMarginM + 2.0f);
            }
            target = next;
        }
        REQUIRE(switches == 1 && target == (outbound ? strand : nickel));
    }
    apricot_test::pass("the marker goes to the nearest payphone and does not flick at the halfway line");
}

void a_phone_is_in_reach_only_on_foot_beside_it() {
    for (std::size_t i = 0; i < kPayphoneCount; ++i) {
        const glm::vec3 p = payphone_position(i);
        const char* name = payphone_site(i).name;
        for (const glm::vec3 side : {glm::vec3{1, 0, 0}, glm::vec3{-1, 0, 0},
                                     glm::vec3{0, 0, 1}, glm::vec3{0, 0, -1}}) {
            REQUIRE_MSG(payphone_in_reach(p + side * 1.4f, true) == static_cast<int>(i),
                        "a phone 1.4 m away is out of reach", name);
            REQUIRE_MSG(payphone_in_reach(p + side * 1.4f, false) < 0,
                        "a phone is reachable from a car", name);
            REQUIRE_MSG(payphone_in_reach(p + side * 1.6f, true) < 0,
                        "a phone 1.6 m away is in reach", name);
        }
        REQUIRE_MSG(payphone_in_reach(p + glm::vec3{0, 1.1f, 0}, true) == static_cast<int>(i),
                    "a step up from the phone is out of reach", name);
        REQUIRE_MSG(payphone_in_reach(p + glm::vec3{0, 1.3f, 0}, true) < 0,
                    "a phone reachable from the storey above", name);
        REQUIRE_MSG(payphone_in_reach(p - glm::vec3{0, 1.3f, 0}, true) < 0,
                    "a phone reachable from below", name);
    }
    // The call needs the page and a phone, and moves the stage on once.
    const glm::vec3 phone = payphone_position(0);
    MissionStage stage = MissionStage::DeliveryComplete;
    REQUIRE(!call_lou(stage, phone, true) && stage == MissionStage::DeliveryComplete);
    stage = MissionStage::CallLou;
    REQUIRE(!call_lou(stage, phone + glm::vec3{5, 0, 0}, true));
    REQUIRE(!call_lou(stage, phone, false));
    REQUIRE(stage == MissionStage::CallLou);
    // Any phone, not just the marked one.
    REQUIRE(can_call_lou(stage, payphone_position(kPayphoneCount - 1), true));
    REQUIRE(call_lou(stage, phone, true) && stage == MissionStage::LouCalled);
    REQUIRE(!call_lou(stage, phone, true) && stage == MissionStage::LouCalled);
    apricot_test::pass("a phone is in reach on foot within 1.5 m and 1.2 m of height, and the call happens once");
}

// The real character, walked straight at each phone from four sides, on the
// real terrain and roads, with that phone's real cooked collision box. It
// must be stopped by the phone and be able to call from where it stops.
PlayerCharacterState walk_toward(PlayerCharacterState s, const TerrainCollider& collider,
                                 glm::vec3 goal) {
    const CharacterTuning tuning;
    for (int i = 0; i < 900; ++i) {
        const glm::vec2 d{goal.x - s.position.x, goal.z - s.position.z};
        if (glm::length(d) < 0.02f) break;
        InputFrame input;
        input.look_dx = std::atan2(d.x, -d.y) - s.view_yaw;
        input.throttle = 1.0f;
        s = step_character(s, tuning, input, collider, kDt);
    }
    return s;
}

void the_real_character_can_call_from_every_phone(bool require_assets) {
    city::KyjhiPropAsset booth, pedestal, wall;
    const bool have = city::load_kyjhi_prop_asset(booth, city::kKyjhiPhoneboothAssetRoot) &&
                      city::load_kyjhi_prop_asset(pedestal, city::kKyjhiPayphoneAssetRoot) &&
                      city::load_kyjhi_prop_asset(wall, city::kKyjhiWallPhoneAssetRoot);
    if (!have && !require_assets) {
        std::puts("SKIP private payphone geometry; cook the kyjhi payphones to walk up to them");
        return;
    }
    REQUIRE(have);
    // The widest footprint is inside the reach with a character pressed to it.
    const CharacterTuning tuning;
    for (const city::KyjhiPropAsset* asset : {&booth, &pedestal, &wall}) {
        const float corner = glm::length(glm::vec2{asset->box.half.x, asset->box.half.z});
        REQUIRE(corner + tuning.radius_m < kPayphoneReachM);
    }

    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    const auto ribbon = bake_ribbons(roads, ground.sampler());
    TerrainCollider collider{city::kMapSeed};
    collider.set_road_collision(build_road_collision(ribbon));
    for (const auto& site : city::kKyjhiPhoneboothSites) city::add_kyjhi_prop_collision(collider, booth, site);
    for (const auto& site : city::kKyjhiPayphoneSites) city::add_kyjhi_prop_collision(collider, pedestal, site);
    for (const auto& site : city::kKyjhiWallPhoneSites) city::add_kyjhi_prop_collision(collider, wall, site);

    for (std::size_t i = 0; i < kPayphoneCount; ++i) {
        const glm::vec3 phone = payphone_position(i);
        const char* name = payphone_site(i).name;
        for (const glm::vec2 side : {glm::vec2{1, 0}, glm::vec2{-1, 0}, glm::vec2{0, 1}, glm::vec2{0, -1}}) {
            const glm::vec2 start = glm::vec2{phone.x, phone.z} + side * 3.0f;
            auto state = spawn_character(collider, start.x, start.y);
            state = walk_toward(state, collider, phone);
            const float from_phone = glm::length(glm::vec2{state.position.x - phone.x,
                                                           state.position.z - phone.z});
            std::printf("  %s from (%+.0f,%+.0f): stopped %.2f m out, feet %+.2f m\n", name,
                        static_cast<double>(side.x), static_cast<double>(side.y),
                        static_cast<double>(from_phone),
                        static_cast<double>(state.position.y - phone.y));
            REQUIRE_MSG(from_phone > 0.3f, "the character walked into the phone", name);
            REQUIRE_MSG(payphone_in_reach(state.position, true) == static_cast<int>(i),
                        "the character cannot call from where the phone stops it", name);
        }
    }
    apricot_test::pass("the real character, stopped by each real phone from four sides, can call from there");
}

}  // namespace

int main(int argc, char** argv) {
    const bool require_assets = argc > 1 && std::strcmp(argv[1], "--require-assets") == 0;
    a_page_shows_crawls_and_goes();
    pages_wait_their_turn();
    the_beep_sounds_the_pattern_the_pager_shakes_to();
    lou_pages_once_the_delivery_is_done();
    every_payphone_is_listed_once();
    the_marker_picks_the_nearest_phone_and_holds_it();
    a_phone_is_in_reach_only_on_foot_beside_it();
    the_real_character_can_call_from_every_phone(require_assets);
    return apricot_test::done("pager_tests");
}
