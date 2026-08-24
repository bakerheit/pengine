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
apricot_add_suite(sim_determinism_tests)
apricot_add_suite(rally_route_tests)
apricot_add_suite(rally_gate_tests)
apricot_add_suite(rally_replay_tests)
apricot_add_suite(rally_hud_tests)

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
apricot_add_suite(terrain_mesh_tests)
apricot_add_suite(scene_cull_tests)
apricot_add_suite(streamer_tests)
apricot_add_suite(terrain_collision_tests)
apricot_add_suite(vehicle_tests)

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
