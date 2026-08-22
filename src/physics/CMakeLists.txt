# physics -> apricot_sim. Terrain queries and vehicle dynamics. Every step
# function is pure in (state, input, collider, dt) — no wall-clock reads.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/terrain_collider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vehicle.cpp
)
