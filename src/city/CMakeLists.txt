# city -> apricot_sim. Pinatty: the map, and the decision layers that run on it.
#
# Two things landed here from different directions and they belong together.
#
# THE MAP. District polygons, character parameters, landmarks, the road network
# and the terrain operators the height field evaluates. Compiled constexpr
# tables, not a data file: world generation is arithmetic and cannot fail, and a
# loader would give every caller of height_at() a runtime failure mode it has no
# way to handle.
# See map.h for the full argument and docs/design/pinatty.md section 5.1.
#
# The dependency runs terrain -> city and never back: heightmap.cpp includes
# city/terrain_ops.h, and nothing here includes anything from terrain/. Most of
# the module is headers, because the tables must be visible at compile time for
# the static_asserts and the bucket index to exist at all.
#
# spines.cpp is the ONE file here that includes from src/road/, and it is a .cpp
# rather than a header on purpose: roads.h rides into every terrain translation
# unit through terrain_ops.h, and dragging the road module along that path would
# buy nothing. It translates the city-side road table into src/road/'s RoadSpine
# and static_asserts that the two modules' width, sidewalk, class and structure
# tables agree.
#
# THE DECISION LAYERS. Traffic and police AI, pedestrian steering and reactions,
# the melee and get-up phase clocks, the weather and lightning schedulers, the
# objective runtime and the authoring road graph. Lifted from probablecause,
# which is why the split is what it is: every file taken was already free
# functions over plain-data view structs. What was not free of the engine
# stayed behind — see README.md for the list and the reasoning.
#
# Several files here are header-only and deliberately have no entry below.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/police_ai.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/traffic_ai.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/weather.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lightning.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/objective_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/road_author.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/spines.cpp
)
