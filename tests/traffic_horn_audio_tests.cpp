#include <algorithm>
#include <cstdio>
#include <vector>

#include "audio/player_car_assets.h"
#include "audio/traffic_horn_audio.h"
#include "audio_analysis.h"
#include "physics/vehicle.h"
#include "road_fixture.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {
struct Road {
    RoadGraph roads;
    LaneGraph lanes;
    LaneRef east=kInvalidLane;
    Road() {
        RoadSpine spine;
        spine.id=981; spine.cls=RoadClass::Street;
        spine.points={{-600,0},{600,0}};
        roads.build({spine},{},{}); lanes.build(roads,{});
        for (LaneRef lane=0;lane<lanes.lane_count();++lane)
            if (lanes.pose(lane,0).tangent.x>.9f) east=lane;
        REQUIRE(lanes.valid(east));
    }
    VehicleAgent car(uint32_t slot=17) const {
        VehicleAgent car;
        car.lane=east; car.lane_key=lanes.lane(east).key; car.slot=slot;
        car.dist_along_m=car.last_dist_m=580.f;
        car.mode=AgentMode::Integrating; car.cruise_mps=11.f;
        car.profile=make_driver_profile(DriverProfileKind::Normal);
        const auto pose=lanes.pose(east,car.dist_along_m);
        car.pos=pose.position; car.fwd=pose.tangent;
        return car;
    }
};

SfxBank recordings() {
    SfxBank bank;
    SfxOverridePaths paths;
    paths.traffic_horns=player_car_audio_overrides().traffic_horns;
    REQUIRE(override_bank_from_wavs(bank,paths)==kTrafficHornClipCount);
    return bank;
}

struct Rig {
    Road road;
    SfxBank bank=recordings();
    VoiceMixer mixer;
    TrafficHornAudio audio;
    CrowdTuning tuning;
    std::vector<VehicleAgent> cars{road.car()};
    std::vector<float> pcm=std::vector<float>(800);
    glm::vec3 listener=cars.front().pos+glm::vec3{0,1,3};
    uint64_t step=0;
    Rig() {
        mixer.prepare(48000); mixer.set_silent(false);
        mixer.set_listener({listener}); audio.start(mixer,bank);
    }
    bool tick(bool blocked=true) {
        for (auto& car:cars) car.delay_seconds=blocked ? car.delay_seconds+float(kSimDt) : 0.f;
        const bool fired=audio.update(step++,cars,road.lanes,tuning,listener);
        mixer.render(pcm.data(),400);
        REQUIRE(all_finite(pcm)); REQUIRE(mixer.dropped_commands()==0);
        REQUIRE(audio.voice_count()<=TrafficHornAudio::kVoiceBudget);
        return fired;
    }
    void ticks(int count,bool blocked=true) {for (int i=0;i<count;++i) tick(blocked);}
};

// PENG-51. The sim's one-shot fires the horn with no patience wait and no
// frustration at all — a rolling car that has just been cut off — and it is
// one horn, not one per step that the level stays high.
void player_cutoff_honks_with_no_wait() {
    Rig rig;
    rig.cars[0].speed_mps=3.f;
    REQUIRE(!rig.tick(false));
    rig.cars[0].honk_player=true; rig.cars[0].honk_player_fire=true;
    REQUIRE(rig.tick(false));
    REQUIRE(rig.audio.last_event().driver.slot==rig.cars[0].slot);
    rig.cars[0].honk_player_fire=false;
    rig.ticks(300,false);
    REQUIRE(rig.audio.play_count()==1);
    pass("a driver cut off by the player honks at once, once");
}

void player_honks_share_the_voice_budget() {
    Rig rig;
    for (uint32_t slot=18; slot<21; ++slot) rig.cars.push_back(rig.road.car(slot));
    for (auto& car:rig.cars) { car.speed_mps=3.f; car.honk_player=true; car.honk_player_fire=true; }
    REQUIRE(rig.tick(false));
    REQUIRE(rig.audio.play_count()==1);
    // The global 0.65 s spacing holds for player horns too.
    rig.ticks(70,false);
    REQUIRE(rig.audio.play_count()==1);
    rig.ticks(12,false);
    REQUIRE(rig.audio.play_count()==2);
    REQUIRE(rig.audio.voice_count()<=TrafficHornAudio::kVoiceBudget);
    pass("four drivers honking at the player share the voice budget and the global spacing");
}

void shipped_recordings_are_valid() {
    const auto bank=recordings();
    REQUIRE(bank.traffic_horns[0].sample_rate==44100);
    REQUIRE(bank.traffic_horns[1].sample_rate==48000);
    REQUIRE(bank.traffic_horns[2].sample_rate==44100);
    for (const auto& clip:bank.traffic_horns) {
        REQUIRE(clip.channels==1); REQUIRE(all_finite(clip.samples));
        REQUIRE(clip.duration_seconds()>.5f && clip.duration_seconds()<1.3f);
        REQUIRE(peak(clip.samples)<.81f); REQUIRE_NEAR(rms(clip.samples),.21,.002);
        REQUIRE(clip.samples.front()==0.f && clip.samples.back()==0.f);
    }
    SfxBank missing=synth_bank(48000);
    SfxOverridePaths paths; paths.traffic_horns[0]="/missing-traffic-horn.wav";
    REQUIRE(override_bank_from_wavs(missing,paths)==0);
    for (const auto& clip:missing.traffic_horns) REQUIRE(clip.empty());
    pass("three recorded horns keep native rates, matched levels, clean fades and no generated fallback");
}

void profiles_wait_and_repeat_without_spam() {
    uint64_t previous=0;
    for (auto kind:{DriverProfileKind::AggressiveLite,DriverProfileKind::Impatient,
                   DriverProfileKind::Normal,DriverProfileKind::Cautious}) {
        Rig rig; rig.cars[0].profile=make_driver_profile(kind);
        while (!rig.tick()) REQUIRE(rig.step<900);
        const uint64_t first=rig.audio.last_event().step;
        REQUIRE(first>previous); previous=first;
        REQUIRE(double(first)*kSimDt>=rig.cars[0].profile.honk_after);
        REQUIRE(double(first)*kSimDt<rig.cars[0].profile.honk_after+.67);
        while (!rig.tick()) REQUIRE(rig.step<2400);
        REQUIRE(double(rig.audio.last_event().step-first)*kSimDt>=4.5);
        const auto count=rig.audio.play_count();
        rig.cars[0].speed_mps=3.f; rig.ticks(1500,false);
        REQUIRE(rig.audio.play_count()==count);
        rig.cars[0].speed_mps=0.f; rig.tick(); rig.ticks(40);
        REQUIRE(rig.audio.play_count()==count);
        std::printf("      profile %u: first horn %.2fs\n",unsigned(kind),double(first)*kSimDt);
    }
    pass("driver patience controls onset; repeat cooldowns and renewed waits prevent horn spam");
}

void legal_controls_do_not_build_a_horn_queue() {
    RoadGraph roads; LaneGraph graph;
    roads.build(make_test_spines(),{},{}); graph.build(roads,{});
    CrowdTuning tuning;
    unsigned checked=0;
    for (LaneRef lane=0;lane<graph.lane_count();++lane) {
        VehicleAgent car; car.lane=lane; car.delay_seconds=60.f;
        const auto control=graph.approach_control(lane);
        if (control==JunctionControl::Stop) {
            // A stop sign farther down the road must not silence a driver
            // blocked by the player in the middle of the block.
            REQUIRE(traffic_horn_blocked(car,graph,0,tuning));
            car.stop_junction=graph.lane(lane).junction_to; car.stop_wait_steps=1;
            REQUIRE(!traffic_horn_blocked(car,graph,0,tuning));
            car.stop_completed=true;
            REQUIRE(traffic_horn_blocked(car,graph,0,tuning)); checked|=1u;
        }
        if (control!=JunctionControl::Signal) continue;
        const auto junction=graph.lane(lane).junction_to;
        Rig rig; rig.cars={car}; rig.listener=car.pos;
        bool saw_red=false;
        for (uint64_t step=0;step<12000;++step) {
            const auto phase=traffic_signal_phase(graph,junction,lane,int64_t(step),tuning);
            if (phase!=TrafficSignalPhase::Green) {
                REQUIRE(!traffic_horn_blocked(car,graph,int64_t(step),tuning));
                saw_red=true; checked|=phase==TrafficSignalPhase::Red ? 2u : 4u;
            }
            if (saw_red) {
                const bool fired=rig.audio.update(step,rig.cars,graph,tuning,rig.listener);
                rig.mixer.render(rig.pcm.data(),400);
                REQUIRE(!fired);
                if (phase==TrafficSignalPhase::Green) break; // No accumulated red-light burst.
            }
        }
    }
    REQUIRE(checked==7u);
    pass("red, yellow and mandatory stop dwell stay quiet; green starts a fresh wait");
}

void voices_are_stable_bounded_and_cleaned_up() {
    Rig rig;
    auto snowplow=rig.cars[0]; snowplow.snowplow_unit=true;
    REQUIRE(traffic_horn_clip(snowplow)==2u);
    for (uint32_t slot=18;slot<32;++slot) rig.cars.push_back(rig.road.car(slot));
    for (auto& car:rig.cars) car.profile=make_driver_profile(DriverProfileKind::AggressiveLite);
    uint64_t previous=0;
    for (int i=0;i<1800;++i) {
        if (rig.tick()) {
            const auto& event=rig.audio.last_event();
            REQUIRE(!previous || event.step-previous>=78u); previous=event.step;
            const auto car=std::find_if(rig.cars.begin(),rig.cars.end(),[&](const auto& c){
                return c.lane_key==event.driver.lane_key && c.slot==event.driver.slot;
            });
            REQUIRE(car!=rig.cars.end()); REQUIRE(event.clip==traffic_horn_clip(*car));
        }
        if (i%19==0) std::reverse(rig.cars.begin(),rig.cars.end());
    }
    REQUIRE(rig.audio.play_count()>10);
    rig.cars.clear(); rig.ticks(180);
    REQUIRE(rig.audio.voice_count()==0); REQUIRE(rms(rig.pcm)<1e-8);
    rig.cars={rig.road.car()};
    while (!rig.tick()) REQUIRE(rig.step<3600);
    rig.audio.set_active(false); rig.ticks(180);
    REQUIRE(rms(rig.pcm)<1e-8);
    const auto count=rig.audio.play_count();
    rig.audio.set_active(true); rig.ticks(120); REQUIRE(rig.audio.play_count()==count);
    rig.listener+=glm::vec3{100,0,0}; rig.ticks(1200);
    REQUIRE(rig.audio.play_count()==count);
    SfxBank empty; rig.audio.start(rig.mixer,empty); rig.listener=rig.cars[0].pos;
    rig.ticks(1200); REQUIRE(rig.audio.play_count()==count);
    pass("reordered traffic retains its horn; global budget, retirement, pause, distance and missing clips are safe");
}

void real_crowd_frustration_triggers_playback() {
    Rig rig;
    rig.cars[0].speed_mps=3.f;
    rig.cars[0].profile=make_driver_profile(DriverProfileKind::Impatient);
    Crowd crowd; CrowdTuning tuning; tuning.max_peds=0;
    crowd.build(rig.road.lanes,905,{},tuning);
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles())=rig.cars;
    VehicleState player;
    player.position=rig.cars[0].pos+rig.cars[0].fwd*10.f;
    player.orientation=glm::angleAxis(std::atan2(-rig.cars[0].fwd.x,-rig.cars[0].fwd.z),glm::vec3{0,1,0});
    double level=0;
    for (uint64_t step=0;step<1500;++step) {
        crowd.rebuild_buckets(); crowd.step_vehicles(int64_t(step),&player);
        rig.audio.update(step,crowd.vehicles(),rig.road.lanes,tuning,rig.listener);
        rig.mixer.render(rig.pcm.data(),400); level=std::max(level,rms(rig.pcm));
    }
    REQUIRE(rig.audio.play_count()>0); REQUIRE(level>.005);
    pass("actual Crowd braking and frustration behind a stopped player produces audible mixed PCM");
}

std::vector<float> render_horn(std::size_t clip,glm::vec3 offset,float trim=1.f) {
    Rig rig;
    while (traffic_horn_clip(rig.cars[0])!=clip) ++rig.cars[0].slot;
    rig.listener=rig.cars[0].pos+offset;
    rig.mixer.set_listener({rig.listener}); rig.mixer.set_category(Category::World,trim);
    while (!rig.tick()) REQUIRE(rig.step<900);
    auto output=rig.pcm;
    for (int i=0;i<180;++i) {rig.tick(); output.insert(output.end(),rig.pcm.begin(),rig.pcm.end());}
    return output;
}

void spatial_mix_and_audition(const char* path) {
    std::vector<float> reel;
    for (std::size_t clip=0;clip<kTrafficHornClipCount;++clip) {
        const auto near=render_horn(clip,{0,1,3});
        const auto far=render_horn(clip,{0,1,45});
        const auto mute=render_horn(clip,{0,1,3},0);
        REQUIRE(rms(near)>.03); REQUIRE(rms(far)<rms(near)*.15);
        REQUIRE(rms(mute)<1e-8); REQUIRE(peak(near)<.81);
        reel.insert(reel.end(),near.begin(),near.end());
        reel.insert(reel.end(),48000,0.f);
    }
    const auto side=render_horn(0,{-4,0,0});
    double left=0,right=0;
    for (std::size_t i=0;i<side.size();i+=2) {left+=side[i]*side[i];right+=side[i+1]*side[i+1];}
    REQUIRE(right>left*10);
    if (path) {
        auto* file=std::fopen(path,"wb"); REQUIRE(file!=nullptr);
        const auto u16=[&](uint16_t n){std::fputc(n&255u,file);std::fputc(n>>8,file);};
        const auto u32=[&](uint32_t n){u16(uint16_t(n));u16(uint16_t(n>>16));};
        const uint32_t bytes=uint32_t(reel.size()*2);
        std::fwrite("RIFF",1,4,file);u32(36+bytes);std::fwrite("WAVEfmt ",1,8,file);
        u32(16);u16(1);u16(2);u32(48000);u32(192000);u16(4);u16(16);
        std::fwrite("data",1,4,file);u32(bytes);
        for (float sample:reel) u16(uint16_t(int16_t(std::lround(std::clamp(sample,-1.f,1.f)*32767.f))));
        REQUIRE(std::fclose(file)==0);
    }
    pass("all three recorded horns reach the stereo mixer with spatial falloff, panning and SFX mute");
}
} // namespace

// A (lane_key, slot) pair is a recurring schedule slot: one car can retire and
// the next lap's car appear at the SAME pair with no absent update in between.
// This module keeps per-driver state across updates — a blocked-since stamp, a
// repeat cooldown, and an in-flight voice bound to a driver — and erases a
// driver only when its pair goes unseen for a step, which a seamless swap never
// does. Keyed on the pair alone, a brand-new car inherits the previous car's
// honk cooldown and the previous car's sounding horn slides onto it.
//
// This drives the real update()/mixer path, not the identity helper: the point
// is that THIS consumer resets, not that a predicate can tell two ids apart.
void a_fresh_departure_inherits_no_horn_state() {
    Rig rig;
    rig.cars[0].generation = 0;

    // Generation A: block it until it actually sounds a horn. That establishes
    // both halves of the state that must not transfer — a live voice and a
    // repeat cooldown.
    while (!rig.tick()) REQUIRE(rig.step < 900);
    const uint64_t a_honk = rig.audio.last_event().step;
    const uint64_t plays_after_a = rig.audio.play_count();
    REQUIRE(rig.audio.voice_count() == 1);
    REQUIRE(rig.audio.last_event().driver.lane_key == rig.cars[0].lane_key);

    // Swap to generation B at the SAME pair on the very next update. The pair
    // is present in `cars` on every step either side, so nothing observes a gap
    // and nothing is erased by the unseen-driver sweep.
    const uint64_t lane_key = rig.cars[0].lane_key;
    const uint32_t slot = rig.cars[0].slot;
    rig.cars[0] = rig.road.car(slot);
    rig.cars[0].generation = 1;
    rig.cars[0].delay_seconds = 0.0f;
    rig.cars[0].speed_mps = 0.0f;
    REQUIRE(rig.cars[0].lane_key == lane_key && rig.cars[0].slot == slot);

    rig.tick();
    // A's horn was mid-clip and well inside audible range, so the only thing
    // that can stop it is the driver it belongs to no longer existing. If it
    // followed B instead, this is 1 and B is audibly sounding a horn it never
    // earned.
    REQUIRE_MSG(rig.audio.voice_count() == 0,
                "an in-flight horn must not transfer to a new departure",
                "voice transfer");
    REQUIRE(rig.audio.play_count() == plays_after_a);

    // B now earns its own horn. Under the pair-only key it would still be
    // holding A's repeat cooldown, which is at least 4.5 s (540 steps) long.
    while (!rig.tick()) REQUIRE(rig.step < 2400);
    const uint64_t b_honk = rig.audio.last_event().step;
    const uint64_t inherited_cooldown_steps = 540;  // min repeat, 4.5 s at 120 Hz
    REQUIRE_MSG(b_honk - a_honk < inherited_cooldown_steps,
                "a new departure must not serve the old car's honk cooldown",
                "cooldown transfer");
    // ...and it waited its OWN patience rather than firing instantly on A's
    // accumulated blocked-since stamp.
    REQUIRE_MSG(static_cast<double>(b_honk - a_honk) * kSimDt >=
                    static_cast<double>(rig.cars[0].profile.honk_after) * 0.5,
                "a new departure must serve its own patience first",
                "blocked-since transfer");
    std::printf("      A honked at step %llu; B honked %llu steps later "
                "(inherited cooldown would be >= %llu)\n",
                static_cast<unsigned long long>(a_honk),
                static_cast<unsigned long long>(b_honk - a_honk),
                static_cast<unsigned long long>(inherited_cooldown_steps));
    pass("a fresh departure inherits no horn voice, cooldown or impatience");
}

int main(int argc,char** argv) {
    shipped_recordings_are_valid();
    player_cutoff_honks_with_no_wait();
    player_honks_share_the_voice_budget();
    profiles_wait_and_repeat_without_spam();
    legal_controls_do_not_build_a_horn_queue();
    voices_are_stable_bounded_and_cleaned_up();
    real_crowd_frustration_triggers_playback();
    a_fresh_departure_inherits_no_horn_state();
    spatial_mix_and_audition(argc>1 ? argv[1] : nullptr);
}
