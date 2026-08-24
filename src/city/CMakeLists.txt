# city -> apricot_sim. Pinatty: the decision layers of the city simulation.
# Traffic and police AI, pedestrian steering and reactions, the melee/get-up
# phase clocks, the weather and lightning schedulers, the objective runtime and
# the authoring road graph.
#
# Lifted from probablecause, which is why the split is what it is: every file
# here was already free functions over plain-data view structs. What was NOT
# free of the engine stayed behind. See README.md for the list and why.
#
# Several files here are header-only and deliberately have no entry below.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/police_ai.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/traffic_ai.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/weather.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lightning.cpp
)
