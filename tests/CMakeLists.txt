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
apricot_add_suite(terrain_determinism_tests)
apricot_add_suite(terrain_mesh_tests)
