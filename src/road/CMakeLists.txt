# road -> apricot_sim. The road network: a planar graph built from authored
# spines, a ribbon baker that emits plain vertex arrays, and the lane graph
# traffic and police drive on.
#
# NOTHING HERE OWNS A GPU RESOURCE, and the target above is why. The reference
# implementation's road network was a rendering object holding five uploaded
# meshes behind a render() method, which is exactly what made it unportable.
# The bake emits arrays; the host layer uploads them.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/road_graph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ribbon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lane_graph.cpp
)
