# Headless test suites.
#
# Every suite links apricot_sim and ONLY apricot_sim. That is not a
# convenience, it is the architecture assertion: if a test ever needs a window,
# a GL context or an audio device to run, the logic under test is in the wrong
# library. Keep it that way and `ctest` stays green on a headless runner.

# Register a suite in one line:
#     apricot_add_suite(rng_determinism_tests)
# Extra sources (rare — prefer putting logic in a sim module) go after the name:
#     apricot_add_suite(foo_tests helper.cpp)
function(apricot_add_suite name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_options(${name} PRIVATE ${APRICOT_STRICT_FLAGS})
    target_link_libraries(${name} PRIVATE apricot_sim)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

apricot_add_suite(rng_determinism_tests)
apricot_add_suite(fixed_step_tests)

# The engine's central claim: a recorded InputFrame tape replays bit for bit.
# Written against step_vehicle, TerrainCollider, InputFrame and FixedStep and
# NOTHING from game/, so the guarantee outlives whichever game is the sample.
# It already has: the rally that used to be the sample is gone and this suite
# did not move.
apricot_add_suite(sim_determinism_tests)

# game/ (PENG-23). What survived the rally: session conditions, which are a
# pure function of (seed, sim step) and feed VehicleTuning::grip_scale.
apricot_add_suite(conditions_tests)

# gfx (PENG-5). These reach into src/gfx/ headers that are deliberately GL-free
# and header-only -- sky_env.h, rain_field.h, glyph_atlas.h, primitives.h,
# instance.h -- so the real generators the renderer uploads get exercised here
# rather than a hand-written copy of them. Nothing below needs a context.
apricot_add_suite(sky_env_tests)
apricot_add_suite(rain_field_tests)
apricot_add_suite(glyph_atlas_tests)
apricot_add_suite(render_geometry_tests)
apricot_add_suite(render_batch_contract_tests)
apricot_add_suite(math_tests)
apricot_add_suite(input_latch_tests)
apricot_add_suite(rng_distribution_tests)
apricot_add_suite(terrain_determinism_tests)

# city (PENG-41). Pinatty's map: district polygons, character parameters,
# landmarks and the terrain operators height_at() evaluates. The structural
# invariants are static_asserts in src/city/ -- that is the point of the map
# being compiled data -- so this suite pins the things a compiler cannot see:
# that the bucket index changes no answer, that the operators are pure, and
# that the districts are measurably different places on the ground. It PRINTS
# its measurements, because a map that passes every assertion can still be
# boring and only the numbers will tell you.
apricot_add_suite(city_map_tests)
apricot_add_suite(terrain_mesh_tests)
# LOD (PENG-27). The coarse lattice is a strict subset of the fine one, so the
# same-level seam is untouched; the DIFFERENT-level crack is real, and this
# suite measures it against the skirt that has to cover it rather than
# asserting a skirt merely exists.
apricot_add_suite(terrain_lod_tests)
apricot_add_suite(scene_cull_tests)
apricot_add_suite(streamer_tests)
apricot_add_suite(terrain_collision_tests)
apricot_add_suite(vehicle_tests)

# road (PENG-32). The road graph, the ribbon bake and the lane graph, all
# sim-side: the bake emits plain vertex arrays and owns no GPU resource, which
# is exactly why these suites can run it headless. tests/road_fixture.h holds
# the hand-made spine set standing in for the map tables (PENG-41).
apricot_add_suite(road_graph_tests)
apricot_add_suite(road_ribbon_tests)
apricot_add_suite(lane_graph_tests)

# audio (PENG-8). All three link only apricot_sim: gain routing, PCM
# generation and the whole render loop are device-free by construction, so
# these measure real waveforms — spectra, zero crossings, seams — on a runner
# with no sound card. tests/audio_analysis.h holds the measurement tools.
apricot_add_suite(audio_mixer_tests)
apricot_add_suite(audio_synth_tests)
apricot_add_suite(audio_engine_tone_tests)

# city (PENG-29). The Pinatty decision layers lifted out of probablecause, with
# the tests that were pinning them there. They came across because a lifted
# algorithm with no test is a liability: these are the only thing that says the
# behaviour survived the move.
apricot_add_suite(ped_separation_tests)
apricot_add_suite(ped_reaction_tests)
apricot_add_suite(character_punch_tests)
apricot_add_suite(character_getup_tests)
apricot_add_suite(traffic_ai_tests)
apricot_add_suite(police_ai_tests)
apricot_add_suite(weather_tests)
apricot_add_suite(lightning_tests)
apricot_add_suite(objective_runtime_tests)
apricot_add_suite(road_author_tests)

# traffic (PENG-26). The ambient population: whether a city's worth of agents
# fits in a 120 Hz step, and whether sub-rate scheduling survives contact with
# a streamed world. traffic_determinism_tests carries the negative controls --
# the index-keyed sub-rate schedule is required to DIVERGE -- and
# traffic_bench measures. tests/traffic_harness.h is the rig both drive.
apricot_add_suite(traffic_determinism_tests)
apricot_add_suite(traffic_bench)
