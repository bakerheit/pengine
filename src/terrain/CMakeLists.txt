# terrain -> apricot_sim. Procedural height field, chunk meshing and the
# residency streamer. All of it a pure function of (seed, coordinate).
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/heightmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chunk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/streamer.cpp
)
