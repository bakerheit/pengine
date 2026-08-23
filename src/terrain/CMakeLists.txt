# terrain -> apricot_sim. Procedural height field, surface materials, chunk
# meshing, prop scatter and the residency streamer. All of it a pure function
# of (seed, coordinate).
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/heightmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chunk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/streamer.cpp
)
