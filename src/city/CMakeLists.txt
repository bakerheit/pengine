# city -> apricot_sim. PINATTY, the map: district polygons, character
# parameters, landmarks and the terrain operators the height field evaluates.
#
# Compiled constexpr tables, not a data file. World generation is arithmetic
# and cannot fail; a loader would give every caller of height_at() a runtime
# failure mode it has no way to handle. See src/city/map.h for the full
# argument and docs/design/pinatty.md section 5.1 for the design's version.
#
# The dependency runs terrain -> city and never back: heightmap.cpp includes
# city/terrain_ops.h, and nothing in this module includes anything from
# terrain/. Most of the module is headers, because the tables have to be
# visible at compile time for the static_asserts and the bucket index to exist
# at all.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/map.cpp
)
