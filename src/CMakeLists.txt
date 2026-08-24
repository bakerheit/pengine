# Module fan-out. Each subdirectory owns its own target_sources() call so that
# agents working on different modules never collide in the same CMake file.
# Sim-side modules (the link graph forbids GL/SDL/audio-device in any of them):
add_subdirectory(core)
add_subdirectory(scene)
add_subdirectory(terrain)
add_subdirectory(road)
add_subdirectory(city)     # Pinatty: map tables + terrain ops, and the AI decision layers
add_subdirectory(physics)
add_subdirectory(game)
add_subdirectory(audio)     # split: synth -> sim, device -> host

# Host-side modules (hardware lives here and only here):
add_subdirectory(platform)
add_subdirectory(gfx)

# The executable's entry point. src/app/ attaches the rest.
add_subdirectory(app)
target_sources(apricot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
