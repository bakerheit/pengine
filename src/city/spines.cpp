#include "city/spines.h"

#include "city/districts.h"
#include "city/roads.h"
#include "road/road_class.h"

namespace apricot {
namespace city {
namespace {

// ---------------------------------------------------------------------------
//  The two tables that must agree, and the assertions that make them
// ---------------------------------------------------------------------------
//
// roads.h speaks city::RoadClass and city::RoadStructure; src/road/ speaks
// apricot::RoadClass and apricot::RoadStructure. They are the same five
// classes and the same five structures, declared twice because the module
// dependency only runs one way (see spines.h).
//
// Two copies of one fact is two facts that can drift, so the drift is a BUILD
// FAILURE rather than a subtle seam. This is the same move city/terrain_ops.h
// makes for smoothstep, for the same reason.

constexpr apricot::RoadClass to_road_class(RoadClass c) {
    return static_cast<apricot::RoadClass>(static_cast<uint8_t>(c));
}
constexpr apricot::RoadStructure to_road_structure(RoadStructure s) {
    return static_cast<apricot::RoadStructure>(static_cast<uint8_t>(s));
}
constexpr apricot::BridgeDetailStyle to_bridge_detail_style(BridgeDetailStyle s) {
    return static_cast<apricot::BridgeDetailStyle>(static_cast<uint8_t>(s));
}

// The casts above are only sound if the enumerators line up.
static_assert(to_road_class(RoadClass::Freeway) == apricot::RoadClass::Freeway, "");
static_assert(to_road_class(RoadClass::Arterial) == apricot::RoadClass::Arterial, "");
static_assert(to_road_class(RoadClass::Street) == apricot::RoadClass::Street, "");
static_assert(to_road_class(RoadClass::Alley) == apricot::RoadClass::Alley, "");
static_assert(to_road_class(RoadClass::Dirt) == apricot::RoadClass::Dirt, "");

static_assert(to_road_structure(RoadStructure::Ground) ==
                  apricot::RoadStructure::Ground, "");
static_assert(to_road_structure(RoadStructure::Bridge) ==
                  apricot::RoadStructure::Bridge, "");
static_assert(to_road_structure(RoadStructure::Tunnel) ==
                  apricot::RoadStructure::Tunnel, "");
static_assert(to_road_structure(RoadStructure::Cut) == apricot::RoadStructure::Cut, "");
static_assert(to_road_structure(RoadStructure::Fill) ==
                  apricot::RoadStructure::Fill, "");
static_assert(to_bridge_detail_style(BridgeDetailStyle::None) ==
                  apricot::BridgeDetailStyle::None, "");
static_assert(to_bridge_detail_style(BridgeDetailStyle::Viaduct) ==
                  apricot::BridgeDetailStyle::Viaduct, "");
static_assert(to_bridge_detail_style(BridgeDetailStyle::Municipal) ==
                  apricot::BridgeDetailStyle::Municipal, "");

// The carriageway widths, and the sidewalk flags that go with them. THIS IS
// THE ONE THAT MATTERS. city's copy drives the Grade corridor half width -- how
// much ground is flattened under a road -- and road/'s copy drives the ribbon
// that is drawn on it. Let those two disagree and the map flattens a strip of
// one width while drawing a road of another, and the symptom is a kerb hanging
// off the side of a hill on one class of road only.
//
// (Neither number may be widened by 2 m again. See the capitals at the top of
// road/road_class.h: the +2 m founder pass is ALREADY IN both tables.)
constexpr bool the_two_width_tables_agree() {
    const RoadClass all[] = {RoadClass::Freeway, RoadClass::Arterial,
                             RoadClass::Street, RoadClass::Alley,
                             RoadClass::Dirt};
    for (const RoadClass c : all) {
        const apricot::RoadClassDef& d = apricot::road_class_def(to_road_class(c));
        if (d.carriageway_width_m != road_width_m(c)) return false;
        if (d.sidewalks != road_has_sidewalks(c)) return false;
    }
    return true;
}
static_assert(the_two_width_tables_agree(),
              "city/map.h's road_width_m() and road/road_class.h's "
              "kRoadClasses have drifted apart, so the ground is flattened to "
              "one width and the road is drawn at another");

static_assert(kWalkWidthM == apricot::kSidewalkWidthM,
              "city and road disagree about how wide a sidewalk is, so the "
              "corridor a road is graded into is not the corridor it draws");

// ---------------------------------------------------------------------------

// Population scalars for a road, from the district it belongs to. The
// countryside has no row in kDistricts by design, so it has its own pair.
struct Density {
    float traffic;
    float ped;
    float parked;
    float response_s;
};

Density density_for(DistrictId id) {
    if (id == DistrictId::Count) {
        return Density{kMeadowsTrafficDensity, kMeadowsPedDensity,
                       kMeadowsParkedDensity, kMeadowsResponseS};
    }
    const District& d = district(id);
    return Density{d.pop.traffic, d.pop.ped, d.pop.parked, d.heat.response_s};
}

}  // namespace

std::vector<RoadSpine> map_spines() {
    std::vector<RoadSpine> out;
    out.reserve(static_cast<std::size_t>(kRoadCount));

    for (int i = 0; i < kRoadCount; ++i) {
        const Road& r = kRoads[i];

        RoadSpine s;
        s.points.reserve(static_cast<std::size_t>(r.count));
        for (int p = 0; p < r.count; ++p) {
            s.points.push_back(glm::vec2{r.path[p].x, r.path[p].z});
            if (r.deck_profile) s.deck_heights.push_back(r.path[p].y);
        }

        s.cls = to_road_class(r.cls);
        s.structure = to_road_structure(r.structure);
        s.deck_y_m = r.deck_y_m;
        s.one_way = r.one_way;
        s.bridge_detail_style = r.deck_profile
            ? apricot::BridgeDetailStyle::Viaduct
            : to_bridge_detail_style(r.bridge_detail_style);
        s.curb_cut_tee = r.curb_cut_tee;

        // Passed through EXACTLY as authored, including zero. road_graph.h
        // reads "<= 0 means use the class table", and the class table is the
        // one this file just proved agrees with ours. Nothing here rounds an
        // override toward its class -- PCG-170 did and it turned every
        // hand-authored 8 m street into a 16 m one.
        s.width_m = r.width_m;
        s.width_start_m = r.width_start_m;
        s.width_end_m = r.width_end_m;
        s.lanes_start_per_dir = r.lanes_start_per_dir;
        s.lanes_end_per_dir = r.lanes_end_per_dir;
        s.lane_connect_start = r.lane_connect_start;
        s.lane_connect_end = r.lane_connect_end;

        s.block_quality = r.block_quality;
        s.speed_limit_mps = r.speed_limit_mps;

        const Density d = density_for(r.district);
        s.traffic_density = d.traffic;
        s.ped_density = d.ped;
        // .pop.parked was authored per district from the start and nothing
        // read it, so every kerb in Pinatty was equally empty. It is a
        // separate number from .traffic on purpose: Nickel Heights authors
        // 1.2 parked against 0.8 traffic and Marrow authors 0.05 against 0.15,
        // and a street reads as residential or industrial largely on that
        // ratio.
        s.parked_density = d.parked;
        // The district's authored response time rides the same path as its
        // densities, so a crime in Halloway Square gets a cruiser in three
        // seconds and one in the Meadows gets it in twenty-two (PENG-45).
        s.response_s = d.response_s;
        if (r.one_way) s.ped_density = 0.0f;

        // The AUTHORED id, never the loop counter. Everything downstream keys
        // entropy on RoadSpine::id, so handing it an index would re-roll every
        // lane choice and every roadblock site the moment somebody inserted a
        // road above this one in the table.
        s.id = r.id;

        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace city
}  // namespace apricot
