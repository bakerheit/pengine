#pragma once

#include <vector>

#include "road/road_graph.h"

namespace apricot {
namespace city {

// PINATTY'S ROAD SPINES, as src/road/ wants them.
//
// This is the seam road/road_graph.h names in its own header:
//
//     RoadGraph::build(map_spines(), params, ground)
//
// The road module takes spines as a PARAMETER and knows nothing about where
// they came from; the tables are this module's, in roads.h. All this function
// does is translate one plain-data description into another.
//
// It is the ONLY file in src/city/ that includes anything from src/road/, and
// that is on purpose. roads.h is pulled into the height field's include graph
// (terrain_ops.h -> heightmap.cpp), so if it spoke src/road/'s types directly
// the road module would ride along into every terrain compile for no benefit.
// The cost of the separation is two enums and two width tables that have to
// agree, and spines.cpp turns "have to agree" into four static_asserts.
//
// Allocates, and therefore is NOT for the inner loop. Call it once at startup
// and hand the vector to RoadGraph::build; the graph, the ribbon bake and the
// lane graph are all built from it and none of them needs to call this again.
std::vector<RoadSpine> map_spines();

// Traffic and pedestrian density on a road in the countryside — the Meadows,
// which is deliberately not a district (see map.h) and therefore has no
// PopParams row to read.
//
// Lower than Marrow, which is the quietest district that does exist. A road
// between two districts should feel like the distance between them, and the
// design says so: "the countryside exists so the distance between districts is
// FELT".
inline constexpr float kMeadowsTrafficDensity = 0.20f;
inline constexpr float kMeadowsPedDensity = 0.03f;

}  // namespace city
}  // namespace apricot
