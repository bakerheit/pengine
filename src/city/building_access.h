#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "city/airport.h"
#include "city/neighborhood_shops.h"
#include "city/pawn_shop.h"
#include "city/gun_store.h"
#include "city/emergency_stations.h"
#include "city/east_arm_plaza.h"
#include "city/hospital_campus.h"
#include "city/neighborhood_bar.h"
#include "city/loom_cultural.h"
#include "city/burgerpiz.h"
#include "city/luxury_neighborhood.h"
#include "city/north_pinatty_gas_station.h"
#include "city/miandi_bayfront.h"
#include "city/miandi_calle_noche.h"
#include "city/miandi_mariposa_motel.h"
#include "city/miandi_ocean_drive.h"
#include "city/miandi_port_sol.h"
#include "city/miandi_prism_works.h"
#include "city/miandi_sunwave_hotel.h"
#include "city/residential_neighborhood.h"
#include "city/tacomaco.h"
#include "road/ribbon.h"

namespace apricot::city {

// Host integration: bake roads first, call bake_building_access(), then
// append_building_access() BEFORE both build_road_collision() and upload().
// Ramps start flush at the carriageway edge and rise BEHIND the curb.
// Cut the existing walk/kerb out of their footprint before merging ramps, so
// neither concrete nor its raised collision surface spills into a traffic lane.
// Never register these as flat StaticGroundRects or solid bounding boxes.
inline constexpr float kAccessRampRunM = 2.0f;
inline constexpr float kAccessSeamLiftM = 0.003f;
inline constexpr float kAccessMaxGrade = 0.12f;
inline constexpr float kAccessFixtureMarginM = 0.45f;

enum class BuildingAccessUse { PublicParking, LandsideService, PedestrianPath };

struct BuildingAccessLot {
    const char* name = nullptr;
    StartSite site{};
    StartPart pavement{};
    std::vector<StartPart> parts;
    // Semantic front of the property, in site space. Road coordinates, gap
    // widths and elevations are measured, never authored as patch rectangles.
    glm::vec2 outward_local{0, 1};
    glm::vec2 driveway_outward_local{0}; // optional side entrance (motel pool court)
    float preferred_entry_local = 0.0f; // X for +/-Z frontage, Z for +/-X
    float driveway_width_m = 6.0f;
    bool generate_frontage = true; // additional curb cuts share the first frontage
    bool blocks_parcel_expansion = true; // false for non-parcel access envelopes
    // Most street-facing layouts move their loose parking paint to the final
    // parcel edge. Restaurant bays instead stay tied to their door crossing.
    bool parking_tracks_frontage = true;
    BuildingAccessUse use = BuildingAccessUse::PublicParking;
    std::vector<uint32_t> allowed_spines; // explicit landside-only airport list
    std::array<bool,4> sidewalk_edges{}; // +X, -X, +Z, -Z; expanded plot perimeter
    glm::vec2 parking_shift{0};
};

inline glm::vec2 access_world(const StartSite& s, glm::vec2 p) {
    return {s.origin.x + s.cos_yaw * p.x + s.sin_yaw * p.y,
            s.origin.z - s.sin_yaw * p.x + s.cos_yaw * p.y};
}
inline glm::vec2 access_local(const StartSite& s, glm::vec2 p) {
    p -= glm::vec2{s.origin.x, s.origin.z};
    return {s.cos_yaw * p.x - s.sin_yaw * p.y,
            s.sin_yaw * p.x + s.cos_yaw * p.y};
}

// Explicit inventory: neighborhood businesses, civic stations, hospital
// arrivals, three public airport parking lots, and the landside cargo yard.
// Runway, aircraft apron, hangar apron, taxiways and Secure Apron Access (151)
// are deliberately absent.
inline std::vector<BuildingAccessLot> authored_building_access_lots(GroundSampler ground) {
    std::vector<BuildingAccessLot> out;
    auto add = [&](const StartSite& site, std::vector<StartPart> parts,
                   const char* pavement, glm::vec2 front, float preferred,
                   std::vector<uint32_t> roads = {},
                   BuildingAccessUse use = BuildingAccessUse::PublicParking) {
        const auto it = std::find_if(parts.begin(), parts.end(), [&](const StartPart& p) {
            return p.name && std::strcmp(p.name, pavement) == 0;
        });
        if (it == parts.end()) return; // missing inventory is asserted in tests
        BuildingAccessLot lot;
        lot.name = pavement;
        lot.site = site;
        lot.pavement = *it;
        lot.parts = std::move(parts);
        lot.outward_local = front;
        lot.preferred_entry_local = preferred;
        lot.allowed_spines = std::move(roads);
        lot.use = use;
        out.push_back(std::move(lot));
    };
    add(kGasStationSite, bake_building(kGasStationPlan), "gas lot", {0, 1}, 0);
    add(kMotelSite, bake_building(kMotelPlan), "motel lot", {0, 1}, 12);
    out.back().driveway_outward_local = {1, 0};
    out.back().preferred_entry_local = -9;
    add(kApartmentSite, bake_building(kApartmentPlan), "apartment lot", {0, -1}, 27);
    add(kFastFoodSite, bake_building(kFastFoodPlan), "restaurant lot", {0, -1}, 28);
    out.back().parking_tracks_frontage = false;
    add(kTacomacoSite,bake_burgerpiz_lot(),"imported restaurant parking lot",{0,1},-24,{33});
    out.back().driveway_width_m=6.0f;
    out.back().parking_tracks_frontage=false;
    add(kCarWashSite, bake_building(kCarWashPlan), "wash lot", {0, 1}, 0);
    out.back().driveway_width_m = 5.0f;
    add(kBankSite, bake_building(kBankPlan), "bank parking lot", {0, -1}, 24);
    add(kAutoRepairSite, bake_auto_repair(), "repair lot", {0, 1}, -9);
    add(kLaundromatSite, bake_laundromat(), "laundry lot", {0, 1}, 0);
    add(kPawnShopSite,bake_pawn_shop(),"pawn lot",{0,1},-30);
    // Keep the compact lot fixed; frontage fills the gap to Sixth's sidewalk.
    add(kGunStoreSite,bake_gun_store(),"gun store lot",{0,1},13,{36});
    // The Galleria opens south onto Halloway Square's East Arm. Keep the
    // public arrival off the Plaza Ring ramp system.
    add(kEastArmPlazaSite, bake_east_arm_plaza(), "east arm plaza parking lot",
        {0, 1}, -46.0f, {51});
    out.back().driveway_width_m = 9.0f;
    out.back().parking_tracks_frontage = false;
    auto galleria_east_entrance = out.back();
    galleria_east_entrance.name = "East Arm Galleria east parking entrance";
    galleria_east_entrance.preferred_entry_local = 46.0f;
    galleria_east_entrance.generate_frontage = false;
    out.push_back(galleria_east_entrance);
    // The bar's customer lot sits to its left as seen from Halloway Street.
    // Its side inlet belongs on Mercer Avenue, not across the front pavement.
    add(kNeighborhoodBarSite,bake_neighborhood_bar(),"bar lot",{-1,0},2,{21});
    add(kFreakyFranksSite,bake_burgerpiz_lot(),"imported restaurant parking lot",{0,1},-24,{36});
    out.back().driveway_width_m=6.0f;
    out.back().parking_tracks_frontage=false;
    add(kBurgerPizSite,bake_burgerpiz_lot(),"imported restaurant parking lot",{0,1},-24,{39});
    out.back().driveway_width_m=6.0f;
    out.back().parking_tracks_frontage=false;
    add(kLoomMuseumSite,bake_loom_museum(),"museum lot",{0,1},0,{209},BuildingAccessUse::PedestrianPath);
    out.back().driveway_width_m=5.0f;
    out.back().parking_tracks_frontage=false;
    add(kLoomParkSite,bake_loom_park(),"garden entrance walk",{0,-1},0,{209},BuildingAccessUse::PedestrianPath);
    out.back().driveway_width_m=3.2f;
    out.back().parking_tracks_frontage=false;

    add(kFireStationSite,bake_emergency_station(true),"fire station lot",{0,1},-13.4f);
    out.back().driveway_width_m=27.5f;
    add(kPoliceStationSite,bake_emergency_station(false),"police station lot",{0,1},22.f);

    // The hospital's internal drives already carry their own paving and paint.
    // These synthetic pavement envelopes exist only so the road baker cuts
    // matching, collision-bearing openings through each perimeter sidewalk and
    // kerb. They are never appended as building parts.
    const auto add_hospital_access = [&](const char* name, Vec2 centre,
                                         float width, float depth,
                                         glm::vec2 outward, float preferred,
                                         float driveway_width,
                                         uint32_t road) {
        BuildingAccessLot lot;
        lot.name = name;
        lot.site = kHospitalSite;
        lot.pavement = {name, centre, 0.10f, width, 0.10f, depth,
                        StartFinish::Asphalt, false};
        lot.outward_local = outward;
        lot.preferred_entry_local = preferred;
        lot.driveway_width_m = driveway_width;
        lot.generate_frontage = false;
        lot.blocks_parcel_expansion = false;
        lot.parking_tracks_frontage = false;
        lot.allowed_spines = {road};
        out.push_back(std::move(lot));
    };
    add_hospital_access("hospital public arrival Tenth entry curb cut",
                        {0.5f, -14.7f}, 83.0f, 12.0f, {0, -1}, -35.0f,
                        8.0f, 40);
    add_hospital_access("hospital public arrival Tenth curb cut",
                        {0.5f, -14.7f}, 83.0f, 12.0f, {0, -1}, 39.0f,
                        7.0f, 40);
    add_hospital_access("hospital ambulance Juniper entry curb cut",
                        {212.0f, 83.0f}, 16.0f, 94.0f, {1, 0}, 42.0f,
                        10.0f, 25);
    add_hospital_access("hospital ambulance Juniper exit curb cut",
                        {212.0f, 83.0f}, 16.0f, 94.0f, {1, 0}, 124.0f,
                        10.0f, 25);
    add_hospital_access("hospital service Sixth curb cut",
                        {174.0f, 175.0f}, 90.0f, 62.0f, {0, 1}, 184.0f,
                        8.0f, 36);

    const auto airport = bake_airport();
    // The south edge of public parking faces the public frontage road. The
    // internal roads remain intact; apron access is never a nearest-road fallback.
    add(kAirportSite, airport, "terminal parking", {0, 1}, -170, {150, 158});
    add(kAirportSite, airport, "airport hotel lot", {0, 1}, -350, {150, 153});
    add(kAirportSite, airport, "rental car lot", {0, 1}, 230, {150, 154});
    add(kAirportSite, airport, "air cargo yard", {1, 0}, 170, {155},
        BuildingAccessUse::LandsideService);

    add(kMiandiGasStationSite,bake_miandi_gas_station_lot(),
        "miandi gas parking lot",{0,1},-2.5f,{230});
    out.back().driveway_width_m=10.0f;
    out.back().parking_tracks_frontage=false;

    add(kNorthPinattyGasStationSite,bake_miandi_gas_station_lot(),
        "miandi gas parking lot",{0,1},-2.5f,{90});
    out.back().driveway_width_m=10.0f;
    out.back().parking_tracks_frontage=false;

    // Miandi's public walks terminate at the generated sidewalk edge in their
    // authored site bakes. These three records are vehicle/service curb cuts:
    // the tower and hotels use quiet side streets, while Port Sol gets a full
    // truck-width mouth on Coral Way. No fake frontage slab is generated.
    add(kMiandiBayfrontSite, bake_miandi_bayfront(), "crown service drive",
        {1, 0}, -58.0f, {233}, BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 8.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;
    add(kMiandiOceanDriveSite, bake_miandi_ocean_drive(),
        "Ocean Drive hotel service lane", {-1, 0}, 0.0f, {234},
        BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 7.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;
    add(kMiandiPortSolSite, bake_miandi_port_sol(), "rear truck lane",
        {0, -1}, 44.5f, {225}, BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 9.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;
    add(kMiandiCalleNocheSite, bake_miandi_calle_noche(),
        "Calle Noche west service route", {-1, 0}, -29.0f, {227},
        BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 6.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;
    add(kMiandiPrismWorksSite, bake_miandi_prism_works(),
        "Mirage south loading route to Coral Way", {0, 1}, 66.0f, {225},
        BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 10.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;
    add(kMiandiMariposaMotelSite, bake_miandi_mariposa_motel(),
        "mariposa Seabreeze driveway", {1, 0}, 55.0f, {234},
        BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 7.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;
    add(kMiandiSunwaveHotelSite, bake_miandi_sunwave_hotel(),
        "Palmera seven metre Seabreeze service lane", {-1, 0}, 12.0f,
        {234}, BuildingAccessUse::LandsideService);
    out.back().driveway_width_m = 7.0f;
    out.back().generate_frontage = false;
    out.back().parking_tracks_frontage = false;

    // Westmere's long drives use a continuous terrain-following triangle
    // ribbon. This small analytic anchor gives the curb-cut baker the ribbon's
    // exact road-end seam without putting a visible horizontal box there.
    for(std::size_t i=0;i<kLuxuryEstates.size();++i) {
        BuildingAccessLot lot;
        lot.name=kLuxuryEstates[i].site.name;
        lot.site=kLuxuryEstates[i].site;
        lot.parts=bake_luxury_estate(i,ground);
        const auto profile=luxury_driveway_profile(i,ground);
        const float seam_top=(profile[profile.size()-2u]+profile.back())*.5f;
        lot.pavement={"house luxury driveway access anchor",
            {kLuxuryDrivewayCentreX,
             kLuxuryDrivewayStartZ+
                 (static_cast<float>(kLuxuryDrivewaySectionCount)-1.5f)*
                     kLuxuryDrivewayStepM},
            seam_top-.10f,7.4f,.10f,kLuxuryDrivewayStepM,
            StartFinish::Concrete};
        lot.outward_local={0,1};lot.preferred_entry_local=17.0f;
        lot.driveway_width_m=6.5f;lot.generate_frontage=false;
        lot.allowed_spines={kLuxuryEstates[i].road_id};
        out.push_back(std::move(lot));
    }
    // Private drives only: no full-width frontage fill and no replacement lot.
    // Keep this group last: residential runtime tests deliberately address the
    // six suburban access records as the inventory tail.
    for(std::size_t i=0;i<kResidentialHouses.size();++i) {
        BuildingAccessLot lot;
        lot.name=kResidentialHouses[i].site.name;
        lot.site=kResidentialHouses[i].site;
        lot.parts=bake_residential_house(i,ground);
        const auto p=std::find_if(lot.parts.rbegin(),lot.parts.rend(),[](const StartPart& part) {
            return std::strcmp(part.name,"house driveway paving")==0;
        });
        lot.pavement=*p;
        lot.pavement.width_m=6.2f; // room for a 5m opening and its fixture margin
        lot.outward_local={0,1};lot.preferred_entry_local=11;
        lot.driveway_width_m=5;lot.generate_frontage=false;lot.allowed_spines={123};
        out.push_back(std::move(lot));
    }
    return out;
}

inline std::vector<BuildingAccessLot> authored_building_access_lots() {
    const TerrainGround ground{kMapSeed};return authored_building_access_lots(ground.sampler());
}

struct AccessFrontageHit {
    bool valid = false;
    glm::vec2 lot_edge{0};
    glm::vec2 outward{0};
    glm::vec2 curb{0};
    glm::vec2 sidewalk_outer{0};
    float curb_distance_m = 0;
    float walk_distance_m = 0;
    uint64_t road_key = 0;
    bool sidewalk = false;
    bool road_end = false;
};

inline float access_cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }
inline double access_cross(glm::dvec2 a, glm::dvec2 b) { return a.x * b.y - a.y * b.x; }

inline glm::vec2 access_front_point(const BuildingAccessLot& lot, float along) {
    const auto& p = lot.pavement;
    if (std::fabs(lot.outward_local.y) > 0.5f)
        return {along, p.centre.z + lot.outward_local.y * p.depth_m * 0.5f};
    return {p.centre.x + lot.outward_local.x * p.width_m * 0.5f, along};
}

inline AccessFrontageHit nearest_access_frontage(const BuildingAccessLot& lot,
                                                 const RoadGraph& roads, float along) {
    AccessFrontageHit best;
    best.lot_edge = access_world(lot.site, access_front_point(lot, along));
    best.outward = access_world(lot.site, lot.outward_local) - access_world(lot.site, {0, 0});
    float distance = std::numeric_limits<float>::max();
    for (const RoadEdge& e : roads.edges()) {
        if (e.cls == apricot::RoadClass::Freeway || !road_is_paved(e.cls) ||
            road_structure_is_decked(e.structure)) continue;
        if (!lot.allowed_spines.empty() &&
            std::find(lot.allowed_spines.begin(), lot.allowed_spines.end(), e.spine_id) ==
                lot.allowed_spines.end()) continue;
        for (std::size_t i = 1; i < e.points.size(); ++i) {
            const glm::vec2 segment = e.points[i] - e.points[i - 1];
            const float len = glm::length(segment);
            if (len < 0.01f) continue;
            const glm::vec2 tangent = segment / len;
            const float crossing = std::fabs(access_cross(best.outward, tangent));
            if (crossing < 0.90f) {
                // A service spur may END at the lot rather than run beside
                // it. Accept only its true dead end and only within its width.
                for (uint32_t node_id : {e.node_a, e.node_b}) {
                    const auto& node = roads.node(node_id);
                    if (node.kind != NodeKind::DeadEnd) continue;
                    const glm::vec2 into_road = node_id == e.node_a ? tangent : -tangent;
                    if (glm::dot(into_road, best.outward) < 0.95f) continue;
                    const auto delta = node.pos - best.lot_edge;
                    const float d = glm::dot(delta, best.outward);
                    if (d < -0.1f || d > 20.0f ||
                        std::fabs(access_cross(delta, best.outward)) > e.half_width_m() - 0.25f ||
                        d >= distance) continue;
                    distance = d;
                    best.valid = true;
                    best.curb_distance_m = best.walk_distance_m = d;
                    best.curb = best.sidewalk_outer = best.lot_edge + best.outward * d;
                    best.road_key = e.key();
                    best.sidewalk = false;
                    best.road_end = true;
                }
                continue;
            }
            const float denominator = access_cross(best.outward, segment);
            const glm::vec2 delta = e.points[i - 1] - best.lot_edge;
            const float d = access_cross(delta, segment) / denominator;
            const float t = access_cross(delta, best.outward) / denominator;
            // Existing airport slabs sometimes overlap the road. Their lot
            // surface is clipped below; permit the frontage to fall inside it.
            const float min_d = lot.allowed_spines.empty() ? e.half_width_m() : -4.0f;
            if (t < -1e-4f || t > 1.0001f || d < min_d || d > 55.0f) continue;
            if (std::fabs(d) > distance + 1e-4f ||
                (std::fabs(std::fabs(d) - distance) <= 1e-4f && e.key() >= best.road_key)) continue;
            const float walk = e.sidewalks() ? kSidewalkWidthM : 0.0f;
            const float curb_d = d - e.half_width_m() / crossing;
            const float walk_d = d - (e.half_width_m() + walk) / crossing;
            if (walk_d > 35.0f || walk_d < -18.0f) continue;
            distance = std::fabs(d);
            best.valid = true;
            best.curb_distance_m = curb_d;
            best.walk_distance_m = walk_d;
            best.curb = best.lot_edge + best.outward * curb_d;
            best.sidewalk_outer = best.lot_edge + best.outward * walk_d;
            best.road_key = e.key();
            best.sidewalk = e.sidewalks();
            best.road_end = false;
        }
    }
    return best;
}

inline bool access_same_site(const StartSite& a, const StartSite& b) {
    return a.origin.x == b.origin.x && a.origin.z == b.origin.z;
}

inline bool access_parking_marker(const StartPart& p) {
    return p.name && (std::strstr(p.name,"parking stripe") ||
                      std::strstr(p.name,"parking stop"));
}

inline bool access_neighborhood_plot(const StartSite& site,const StartPart& part) {
    if(!part.name) return false;
    const std::array<std::pair<const StartSite*,const char*>,10> plots{{
        {&kGasStationSite,"gas lot"},{&kMotelSite,"motel lot"},
        {&kApartmentSite,"apartment lot"},{&kFastFoodSite,"restaurant lot"},
        {&kCarWashSite,"wash lot"},{&kBankSite,"bank parking lot"},
        {&kAutoRepairSite,"repair lot"},{&kLaundromatSite,"laundry lot"},
        {&kPawnShopSite,"pawn lot"},{&kNeighborhoodBarSite,"bar lot"}}};
    for(const auto& [where,name]:plots)
        if(access_same_site(site,*where) && std::strcmp(part.name,name)==0) return true;
    return false;
}

// Only consume land directly between an authored parcel and a nearby parallel
// sidewalk. A neighboring parcel blocks expansion, even if a road lies beyond
// it. Shared lots keep their boundary; airport circulation is handled separately.
inline std::vector<BuildingAccessLot> expand_building_access_lots(
    const std::vector<BuildingAccessLot>& source, const RoadGraph& roads) {
    auto out=source;
    constexpr std::array<glm::vec2,4> directions{{{1,0},{-1,0},{0,1},{0,-1}}};
    for(auto& lot:out) {
        if(!lot.generate_frontage || !access_neighborhood_plot(lot.site,lot.pavement)) continue;
        for(std::size_t side=0;side<directions.size();++side) {
            auto ray=lot; ray.outward_local=directions[side];
            const bool z=side>=2;
            const float centre=z?lot.pavement.centre.x:lot.pavement.centre.z;
            const float half=(z?lot.pavement.width_m:lot.pavement.depth_m)*.5f;
            float distance=12.f, farthest=0.f;
            bool valid=true;
            for(float along:{centre-half,centre,centre+half}) {
                const auto hit=nearest_access_frontage(ray,roads,along);
                if(!hit.valid || !hit.sidewalk || hit.road_end || hit.walk_distance_m<-.01f ||
                   hit.walk_distance_m>12.f) { valid=false;break; }
                distance=std::min(distance,std::max(0.f,hit.walk_distance_m));
                farthest=std::max(farthest,hit.walk_distance_m);
            }
            // A rectangular parcel must not bridge an angled road or junction.
            if(!valid || farthest-distance>.03f) continue;
            auto candidate=lot.pavement;
            candidate.centre.x+=directions[side].x*distance*.5f;
            candidate.centre.z+=directions[side].y*distance*.5f;
            (z?candidate.depth_m:candidate.width_m)+=distance;
            for(const auto& other:out) {
                if(access_same_site(lot.site,other.site)) continue;
                if(!other.blocks_parcel_expansion) continue;
                glm::vec2 lo{std::numeric_limits<float>::max()},hi{-std::numeric_limits<float>::max()};
                for(float x:{-1.f,1.f}) for(float zz:{-1.f,1.f}) {
                    const auto p=access_local(lot.site,access_world(other.site,
                        {other.pavement.centre.x+x*other.pavement.width_m*.5f,
                         other.pavement.centre.z+zz*other.pavement.depth_m*.5f}));
                    lo=glm::min(lo,p);hi=glm::max(hi,p);
                }
                if(candidate.centre.x+candidate.width_m*.5f>lo.x+.01f &&
                   candidate.centre.x-candidate.width_m*.5f<hi.x-.01f &&
                   candidate.centre.z+candidate.depth_m*.5f>lo.y+.01f &&
                   candidate.centre.z-candidate.depth_m*.5f<hi.y-.01f) valid=false;
            }
            if(valid) { lot.pavement=candidate;lot.sidewalk_edges[side]=true; }
        }
        // Street-facing parking follows the final parcel edge, leaving a 2 m
        // flat margin before the sidewalk transition. Restaurant bays are
        // building-anchored: their marks and wheel stops must remain beside
        // the door crossing as the shared lot expands toward the street.
        if(lot.parking_tracks_frontage) {
            const bool front_z=std::fabs(lot.outward_local.y)>.5f;
            float marker_front=-std::numeric_limits<float>::infinity();
            for(const auto& p:lot.parts) if(access_parking_marker(p))
                marker_front=std::max(marker_front,p.centre.x*lot.outward_local.x+
                    p.centre.z*lot.outward_local.y+(front_z?p.depth_m:p.width_m)*.5f);
            const float edge=lot.pavement.centre.x*lot.outward_local.x+
                lot.pavement.centre.z*lot.outward_local.y+
                (front_z?lot.pavement.depth_m:lot.pavement.width_m)*.5f;
            const float move=std::isfinite(marker_front)?std::max(0.f,edge-2.05f-marker_front):0.f;
            lot.parking_shift=lot.outward_local*move;
            for(auto& p:lot.parts) if(access_parking_marker(p)) {
                p.centre.x+=lot.parking_shift.x;p.centre.z+=lot.parking_shift.y;
            }
        }
    }
    // Additional curb cuts must use the very same expanded parcel geometry.
    for(auto& lot:out) if(!lot.generate_frontage)
        for(const auto& primary:out) if(primary.generate_frontage && access_same_site(primary.site,lot.site)) {
            lot.pavement=primary.pavement;lot.parts=primary.parts;
            lot.sidewalk_edges=primary.sidewalk_edges;break;
        }
    return out;
}

// Barycentric sample of the actual baked top, not a fresh terrain-height
// approximation. Used for ramp seams and for the integration's collision QA.
inline bool access_triangle_height(const glm::vec3& a, const glm::vec3& b,
                                    const glm::vec3& c, glm::vec2 p, float& y) {
    const glm::vec2 aa{a.x, a.z}, bb{b.x, b.z}, cc{c.x, c.z};
    const float d = access_cross(bb - aa, cc - aa);
    if (std::fabs(d) < 1e-7f) return false;
    const float u = access_cross(p - aa, cc - aa) / d;
    const float v = access_cross(bb - aa, p - aa) / d;
    if (u < -1e-4f || v < -1e-4f || u + v > 1.0001f) return false;
    y = a.y + u * (b.y - a.y) + v * (c.y - a.y);
    return true;
}

struct AccessSurface {
    std::vector<CollisionTri> tops;
    std::vector<CollisionTri> road_tops;
    float at(glm::vec2 p, float fallback, bool road_only = false) const {
        bool found = false;
        float result = fallback;
        for (const auto& t : road_only ? road_tops : tops) {
            float y = 0;
            if (access_triangle_height(t.a, t.b, t.c, p, y)) {
                result = found ? std::max(result, y) : y;
                found = true;
            }
        }
        return result;
    }
};

inline AccessSurface access_surface_near(const RibbonBake& bake, const BuildingAccessLot& lot) {
    AccessSurface out;
    AABB region;
    const auto& p = lot.pavement;
    for (float x : {-1.0f, 1.0f}) for (float z : {-1.0f, 1.0f}) {
        const auto w = access_world(lot.site, {p.centre.x + x * (p.width_m * 0.5f + 58),
                                              p.centre.z + z * (p.depth_m * 0.5f + 58)});
        region.expand({w.x, 0, w.y});
    }
    for (RoadLayer layer : {RoadLayer::Carriageway, RoadLayer::Walk, RoadLayer::Plate}) {
        const RoadMesh& mesh = bake.layer(layer);
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
            const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
            const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
            if (std::max({a.x, b.x, c.x}) < region.min.x ||
                std::min({a.x, b.x, c.x}) > region.max.x ||
                std::max({a.z, b.z, c.z}) < region.min.z ||
                std::min({a.z, b.z, c.z}) > region.max.z) continue;
            out.tops.push_back({a, b, c, {0, 1, 0}});
            if (layer != RoadLayer::Walk) out.road_tops.push_back(out.tops.back());
        }
    }
    return out;
}

inline void access_triangle(RoadMesh& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                             float tile_m = 4.0f) {
    // Clipping a long road triangle can leave collinear float vertices. Test
    // projected area in double before normalizing their cross product. Use a
    // scale-aware projected altitude too: a micron-wide clipping sliver can
    // have enough absolute area to pass while float Y rounding invents a steep
    // normal on an otherwise flat surface.
    const double area = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.z) - a.z) -
                        (static_cast<double>(b.z) - a.z) * (static_cast<double>(c.x) - a.x);
    const auto projected_length = [](glm::vec3 p, glm::vec3 q) {
        return std::hypot(static_cast<double>(q.x) - p.x,
                          static_cast<double>(q.z) - p.z);
    };
    const double longest_edge = std::max({projected_length(a, b), projected_length(b, c),
                                          projected_length(c, a)});
    if (std::fabs(area) < 1e-5 || longest_edge < 1e-7 ||
        std::fabs(area) / longest_edge < 1e-4) return;
    glm::vec3 n = glm::cross(b - a, c - a);
    if (glm::length(n) < 1e-7f) return;
    if (n.y < 0) { std::swap(b, c); n = -n; }
    n = glm::normalize(n);
    for (glm::vec3 p : {a, b, c}) {
        mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back({p, n, {p.x / tile_m, p.z / tile_m}, {1, 0, 0, 0}});
        mesh.bounds.expand(p);
    }
}
inline void access_quad(RoadMesh& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                         float tile_m = 4.0f) {
    access_triangle(mesh, a, b, c, tile_m);
    access_triangle(mesh, a, c, d, tile_m);
}

// Exposed cheek beside a lowered driveway ramp. It closes the vertical space
// to the retained sidewalk; it is a kerb face, never an extra drivable top.
inline void access_kerb_cheek(RoadMesh& mesh, glm::vec3 a, glm::vec3 b,
                               float adjacent_a_y, float adjacent_b_y,
                               glm::vec2 facing) {
    const glm::vec3 c{b.x,std::max(b.y,adjacent_b_y),b.z},
                    d{a.x,std::max(a.y,adjacent_a_y),a.z};
    a.y=std::min(a.y,adjacent_a_y);
    b.y=std::min(b.y,adjacent_b_y);
    const float run=glm::length(glm::vec2{b.x-a.x,b.z-a.z});
    const float tile=RibbonParams{}.slab_m;
    const glm::vec3 face{facing.x,0.f,facing.y};
    const auto tri=[&](glm::vec3 p0,glm::vec3 p1,glm::vec3 p2,
                       glm::vec2 uv0,glm::vec2 uv1,glm::vec2 uv2) {
        auto normal=glm::cross(p1-p0,p2-p0);
        if(glm::length(normal)<1e-7f) return;
        if(glm::dot(normal,face)<0.f) { std::swap(p1,p2);std::swap(uv1,uv2);normal=-normal; }
        normal=glm::normalize(normal);
        const uint32_t base=static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({p0,normal,uv0,{1,0,0,0}});
        mesh.vertices.push_back({p1,normal,uv1,{1,0,0,0}});
        mesh.vertices.push_back({p2,normal,uv2,{1,0,0,0}});
        for(uint32_t i=0;i<3;++i) { mesh.indices.push_back(base+i);mesh.bounds.expand(mesh.vertices[base+i].position); }
    };
    const glm::vec2 ua{0.f,a.y/tile},ub{run/tile,b.y/tile},
                    uc{run/tile,c.y/tile},ud{0.f,d.y/tile};
    tri(a,b,c,ua,ub,uc);tri(a,c,d,ua,uc,ud);
}

// Pitched/rolled detail at head height is not a driveway obstacle. Low raised
// non-solid objects (pool coping, islands, stoops) ARE, just like solid walls.
inline bool access_obstructed(const BuildingAccessLot& lot, glm::vec2 world, float margin) {
    const glm::vec2 local = access_local(lot.site, world);
    for (const StartPart& p : lot.parts) {
        if (p.name && std::strcmp(p.name, lot.pavement.name) == 0) continue;
        if (p.bottom_m > 2.5f || p.bottom_m + p.height_m <= 0.20f) continue;
        if (!p.solid && p.height_m < 0.15f && p.finish != StartFinish::PoolWater) continue;
        const float yaw = p.yaw_deg * 0.017453292519943295f;
        const glm::vec2 delta = local - glm::vec2{p.centre.x, p.centre.z};
        const glm::vec2 q{std::cos(yaw) * delta.x - std::sin(yaw) * delta.y,
                          std::sin(yaw) * delta.x + std::cos(yaw) * delta.y};
        if (std::fabs(q.x) < p.width_m * 0.5f + margin &&
            std::fabs(q.y) < p.depth_m * 0.5f + margin) return true;
    }
    return false;
}

inline bool access_clear_of_junctions(const RoadGraph& roads, glm::vec2 p, float half_width) {
    for (uint32_t j : roads.junctions())
        if (glm::length(p - roads.node(j).pos) <
            roads.junction_trim_m(j, 0.6f) + kSidewalkWidthM + half_width + 2.0f) return false;
    return true;
}

struct BuildingAccessResult {
    const char* name = nullptr;
    BuildingAccessUse use = BuildingAccessUse::PublicParking;
    bool connected = false;
    float entry_local = 0;
    float width_m = 0;
    AccessFrontageHit entrance{};
    glm::vec3 road_endpoint{0};
    glm::vec3 lot_endpoint{0};
    RoadMesh frontage;
    RoadMesh driveway;
    RoadMesh driveway_kerb;
    RoadMesh replacement_pavement; // original slab must be suppressed
    StartSite site{};
    StartPart plot{};
    glm::vec2 parking_shift{0};
};
struct BuildingAccessBake { std::vector<BuildingAccessResult> lots; };

// Apply before constructing render nodes AND their collisions. Matching the
// original site keeps material selection and authored fixture identities intact.
inline void apply_building_access_layout(const StartSite& site,
    std::vector<StartPart>& parts,const BuildingAccessBake& bake) {
    for(const auto& lot:bake.lots) {
        if(!access_same_site(site,lot.site) || !lot.name || !lot.plot.name ||
           std::strcmp(lot.name,lot.plot.name)!=0) continue;
        for(auto& part:parts) {
            if(part.name && std::strcmp(part.name,lot.plot.name)==0) part=lot.plot;
            else if(access_parking_marker(part)) {
                part.centre.x+=lot.parking_shift.x;part.centre.z+=lot.parking_shift.y;
            }
        }
        return;
    }
}

inline float access_lot_top(const BuildingAccessLot& lot) {
    return lot.site.ground_m + lot.pavement.bottom_m + lot.pavement.height_m;
}

inline std::array<glm::vec3, 4> access_profile(const BuildingAccessLot& lot,
                                              const AccessFrontageHit& hit,
                                              const AccessSurface& surface,
                                              const GroundSampler& ground) {
    const bool expanded=std::any_of(lot.sidewalk_edges.begin(),lot.sidewalk_edges.end(),[](bool b){return b;});
    const float inset=expanded?2.25f:1.5f;
    const float finish_d = std::min(-inset, hit.walk_distance_m - inset);
    const float ramp_run = std::min(kAccessRampRunM,
        std::max(0.0f, hit.curb_distance_m - hit.walk_distance_m));
    const std::array<float, 4> distances{
        hit.curb_distance_m, hit.curb_distance_m - ramp_run,
        hit.walk_distance_m, finish_d};
    std::array<glm::vec3, 4> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto p = hit.lot_edge + hit.outward * distances[i];
        float y = access_lot_top(lot);
        if (i < 3u) {
            // At the mouth sample asphalt only. Triangle-edge tolerance can
            // otherwise select the raised walk even a few mm into the road.
            y = surface.at(p, ground.at(p.x, p.y) + kDrapeEpsM +
                (i > 0u && hit.sidewalk ? kKerbHeightM : 0.0f), i == 0u);
            if (i > 0u && ramp_run > 0.0f) y += kAccessSeamLiftM;
        }
        result[i] = {p.x, y, p.y};
    }
    return result;
}

using AccessPolygon = std::vector<glm::dvec2>;
inline double access_polygon_area(const AccessPolygon& p) {
    double area = 0;
    if (p.empty()) return area;
    for (std::size_t i = 0; i < p.size(); ++i)
        area += access_cross(p[i] - p.front(), p[(i + 1) % p.size()] - p.front());
    return std::fabs(area) * 0.5f;
}
inline AccessPolygon access_clip_half_plane(const AccessPolygon& p, glm::dvec2 a,
                                             glm::dvec2 b, double sign) {
    AccessPolygon out;
    if (p.empty()) return out;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto from = p[i], to = p[(i + 1) % p.size()];
        const double d0 = sign * access_cross(b - a, from - a);
        const double d1 = sign * access_cross(b - a, to - a);
        if (d0 >= 0) out.push_back(from);
        if ((d0 >= 0) != (d1 >= 0)) out.push_back(from + (to - from) * (d0 / (d0 - d1)));
    }
    return out;
}

// Subtract the DRAWN roadway and sidewalk triangles from a complete lot.
// In particular, Rental Row and the public parking aisle cross their slabs;
// an uncut flat slab would override the ramp/road collision and reintroduce
// the very step being fixed. Neighborhood perimeter bands meet the sidewalk;
// interiors and airport polygons keep the original lot height.
inline RoadMesh access_clipped_airport_pavement(const BuildingAccessLot& lot,
                                                const AccessSurface& surface) {
    const auto& p = lot.pavement;
    const glm::vec2 lo{p.centre.x-p.width_m*.5f,p.centre.z-p.depth_m*.5f},
                    hi{p.centre.x+p.width_m*.5f,p.centre.z+p.depth_m*.5f};
    std::vector<float> xs{lo.x,hi.x},zs{lo.y,hi.y};
    constexpr float rim=2.f;
    if(lot.sidewalk_edges[0]) xs.push_back(hi.x-rim);
    if(lot.sidewalk_edges[1]) xs.push_back(lo.x+rim);
    if(lot.sidewalk_edges[2]) zs.push_back(hi.y-rim);
    if(lot.sidewalk_edges[3]) zs.push_back(lo.y+rim);
    std::sort(xs.begin(),xs.end());std::sort(zs.begin(),zs.end());
    const auto height=[&](glm::vec2 world) {
        const auto q=access_local(lot.site,world);
        const std::array<float,4> distances{hi.x-q.x,q.x-lo.x,hi.y-q.y,q.y-lo.y};
        const std::array<glm::vec2,4> edges{{{hi.x,q.y},{lo.x,q.y},{q.x,hi.y},{q.x,lo.y}}};
        float closest=rim, y=access_lot_top(lot);
        for(std::size_t side=0;side<4;++side) if(lot.sidewalk_edges[side] && distances[side]<closest) {
            closest=std::max(0.f,distances[side]);
            constexpr std::array<glm::vec2,4> outward{{{1,0},{-1,0},{0,1},{0,-1}}};
            // Sample just inside the retained walk, avoiding float-sized misses
            // at the exact outer edge of a long rotated ribbon triangle.
            const auto edge=access_world(lot.site,edges[side]+outward[side]*.02f);
            const float walk=surface.at(edge,y);
            y=glm::mix(access_lot_top(lot),walk,1.f-closest/rim);
        }
        return y;
    };
    struct Patch { AccessPolygon polygon; glm::dvec3 plane; };
    std::vector<Patch> pieces;
    for(std::size_t x=1;x<xs.size();++x) for(std::size_t z=1;z<zs.size();++z) {
        AccessPolygon cell;
        for(glm::vec2 corner:{glm::vec2{xs[x-1],zs[z-1]},glm::vec2{xs[x],zs[z-1]},
                             glm::vec2{xs[x],zs[z]},glm::vec2{xs[x-1],zs[z]}})
            cell.push_back(access_world(lot.site,corner));
        for(std::size_t i=2;i<cell.size();++i) {
            const auto vertex=[&](std::size_t j) { return glm::dvec3{cell[j].x,height(glm::vec2{cell[j]}),cell[j].y}; };
            const auto a=vertex(0),b=vertex(i-1),c=vertex(i);
            const auto n=glm::cross(b-a,c-a);
            const glm::dvec3 plane{-n.x/n.y,-n.z/n.y,glm::dot(n,a)/n.y};
            pieces.push_back({{cell[0],cell[i-1],cell[i]},plane});
        }
    }
    for (const auto& tri : surface.tops) {
        const std::array<glm::dvec2, 3> cutter{
            glm::dvec2{tri.a.x, tri.a.z}, glm::dvec2{tri.b.x, tri.b.z}, glm::dvec2{tri.c.x, tri.c.z}};
        const double cross = access_cross(cutter[1] - cutter[0], cutter[2] - cutter[0]);
        if (std::fabs(cross) < 1e-6f) continue;
        const float sign = cross > 0 ? 1.0f : -1.0f;
        const auto cut_lo = glm::min(cutter[0], glm::min(cutter[1], cutter[2]));
        const auto cut_hi = glm::max(cutter[0], glm::max(cutter[1], cutter[2]));
        std::vector<Patch> next;
        for (auto& patch : pieces) {
            auto& polygon=patch.polygon;
            glm::dvec2 bounds_lo = polygon.front(), bounds_hi = polygon.front();
            for (glm::dvec2 v : polygon) { bounds_lo = glm::min(bounds_lo, v); bounds_hi = glm::max(bounds_hi, v); }
            if (bounds_hi.x <= cut_lo.x || bounds_lo.x >= cut_hi.x ||
                bounds_hi.y <= cut_lo.y || bounds_lo.y >= cut_hi.y) {
                next.push_back(std::move(patch));
                continue;
            }
            auto remaining = polygon;
            for (std::size_t edge = 0; edge < 3 && !remaining.empty(); ++edge) {
                auto outside = access_clip_half_plane(remaining, cutter[edge], cutter[(edge + 1) % 3], -sign);
                if (outside.size() >= 3 && access_polygon_area(outside) > 1e-4f)
                    next.push_back({std::move(outside),patch.plane});
                remaining = access_clip_half_plane(remaining, cutter[edge], cutter[(edge + 1) % 3], sign);
            }
        }
        pieces = std::move(next);
    }
    RoadMesh out;
    for (const auto& patch : pieces) {
        const auto& polygon=patch.polygon;
        for (std::size_t i = 2; i < polygon.size(); ++i) {
            const auto vertex = [&](std::size_t j) {
                const glm::vec2 w{polygon[j]};
                return glm::vec3{w.x,static_cast<float>(patch.plane.x*polygon[j].x+patch.plane.y*polygon[j].y+patch.plane.z),w.y};
            };
            access_triangle(out, vertex(0), vertex(i - 1), vertex(i));
        }
    }
    return out;
}

// In add_start_site(), skip replaced lot slabs BEFORE creating either
// render nodes or static ground rectangles. Their replacements are in the
// merged road bake, including collision. All islands, markings and buildings
// stay in the authored site bake. This predicate also works with site copies.
inline bool building_access_replaces_pavement(const StartSite& site, const StartPart& part) {
    if(access_neighborhood_plot(site,part)) return true;
    if (site.origin.x != kAirportSite.origin.x || site.origin.z != kAirportSite.origin.z || !part.name)
        return false;
    for (const char* name : {"terminal parking", "airport hotel lot", "rental car lot", "air cargo yard"})
        if (std::strcmp(part.name, name) == 0) return true;
    return false;
}

inline BuildingAccessBake bake_building_access(
    const RoadGraph& roads, const RibbonBake& ribbon, const GroundSampler& ground,
    const std::vector<BuildingAccessLot>& lots) {
    BuildingAccessBake out;
    const auto expanded=expand_building_access_lots(lots,roads);
    for (const BuildingAccessLot& lot : expanded) {
        BuildingAccessResult result;
        result.name = lot.name;
        result.site=lot.site;result.plot=lot.pavement;result.parking_shift=lot.parking_shift;
        result.use = lot.use;
        result.width_m = lot.driveway_width_m;
        const bool front_z = std::fabs(lot.outward_local.y) > 0.5f;
        const float centre = front_z ? lot.pavement.centre.x : lot.pavement.centre.z;
        const float extent = (front_z ? lot.pavement.width_m : lot.pavement.depth_m) * 0.5f;
        const float lo = centre - extent;
        const float hi = centre + extent;
        const AccessSurface surface = access_surface_near(ribbon, lot);
        if (lot.generate_frontage && building_access_replaces_pavement(lot.site, lot.pavement))
            result.replacement_pavement = access_clipped_airport_pavement(lot, surface);
        // Full street-facing frontage, sampled every metre so curved or
        // angled roads determine the outer edge independently of the lot.
        for (float a = lo; lot.generate_frontage && a < hi - 0.01f; a += 1.0f) {
            const auto left = nearest_access_frontage(lot, roads, a);
            const auto right = nearest_access_frontage(lot, roads, std::min(a + 1.0f, hi));
            if (!left.valid || !right.valid || left.walk_distance_m <= 0.01f ||
                right.walk_distance_m <= 0.01f) continue;
            const auto lp = access_profile(lot, left, surface, ground);
            const auto rp = access_profile(lot, right, surface, ground);
            const float y = access_lot_top(lot);
            access_quad(result.frontage, lp[2], rp[2],
                {right.lot_edge.x, y, right.lot_edge.y}, {left.lot_edge.x, y, left.lot_edge.y});
        }
        // Search closest to the authored bay/aisle first, while keeping the
        // whole car corridor clear. No fallback through a building or junction.
        BuildingAccessLot driveway_lot = lot;
        if (glm::length(lot.driveway_outward_local) > 0.5f)
            driveway_lot.outward_local = lot.driveway_outward_local;
        const bool drive_z = std::fabs(driveway_lot.outward_local.y) > 0.5f;
        const float drive_centre = drive_z ? lot.pavement.centre.x : lot.pavement.centre.z;
        const float drive_extent = (drive_z ? lot.pavement.width_m : lot.pavement.depth_m) * 0.5f;
        const float drive_lo = drive_centre - drive_extent;
        const float drive_hi = drive_centre + drive_extent;
        const float half = result.width_m * 0.5f;
        std::vector<float> candidates;
        const float preferred = std::clamp(lot.preferred_entry_local, drive_lo + half + 0.5f, drive_hi - half - 0.5f);
        candidates.push_back(preferred);
        for (float a = drive_lo + half + 0.5f; a <= drive_hi - half - 0.5f; a += 0.5f)
            candidates.push_back(a);
        std::stable_sort(candidates.begin(), candidates.end(), [&](float a, float b) {
            return std::fabs(a - preferred) < std::fabs(b - preferred);
        });
        for (float a : candidates) {
            bool valid = true;
            const auto centre_hit=nearest_access_frontage(driveway_lot,roads,a);
            if(!centre_hit.valid || !access_clear_of_junctions(roads,centre_hit.curb,half)) continue;
            std::vector<std::array<glm::vec3, 4>> profiles;
            for (float x = -half; x <= half + 0.01f; x += 0.5f) {
                const auto hit = nearest_access_frontage(driveway_lot, roads, a + x);
                // Retain the conservative narrow-entrance junction buffer.
                // Wide apparatus aprons have their full width checked at the
                // centre; their edges need only clear the junction itself.
                if (!hit.valid || !access_clear_of_junctions(roads,hit.curb,
                        result.width_m>12.f?0.f:half)) { valid = false; break; }
                const auto profile = access_profile(lot, hit, surface, ground);
                for (std::size_t k = 1; k < profile.size(); ++k) {
                    const auto delta = profile[k] - profile[k - 1];
                    const float run = glm::length(glm::vec2{delta.x, delta.z});
                    if (run > 0.001f && std::fabs(delta.y) / run > kAccessMaxGrade) valid = false;
                }
                for (float d = std::min(-4.0f, hit.walk_distance_m - 2.0f);
                     d <= hit.curb_distance_m + 0.1f; d += 0.4f)
                    if (access_obstructed(lot, hit.lot_edge + hit.outward * d,
                                          kAccessFixtureMarginM)) valid = false;
                if (!valid) break;
                profiles.push_back(profile);
            }
            if (!valid || profiles.size() < 2u) continue;
            for (std::size_t i = 1; i < profiles.size(); ++i)
                for (std::size_t k = 1; k < 4u; ++k)
                    access_quad(result.driveway, profiles[i - 1][k - 1], profiles[i][k - 1],
                                profiles[i][k], profiles[i - 1][k], RibbonParams{}.slab_m);
            // The cut removes the old curb but also exposes the side of the
            // neighboring sidewalk. Close both cheeks using its actual baked
            // height, so looking across an inlet cannot reveal grass beneath.
            if (nearest_access_frontage(driveway_lot, roads, a).sidewalk) {
                for (const bool right_side : {false,true}) {
                    const auto& edge=right_side ? profiles.back():profiles.front();
                    const auto& inner=right_side ? profiles[profiles.size()-2]:profiles[1];
                    const glm::vec2 inward=glm::normalize(glm::vec2{inner[0].x-edge[0].x,inner[0].z-edge[0].z});
                    for(std::size_t k=1;k<3;++k) {
                        const auto adjacent=[&](glm::vec3 p) {
                            const glm::vec2 beside=glm::vec2{p.x,p.z}-inward*.02f;
                            return surface.at(beside,ground.at(beside.x,beside.y)+kDrapeEpsM+kKerbHeightM);
                        };
                        access_kerb_cheek(result.driveway_kerb,edge[k-1],edge[k],
                            adjacent(edge[k-1]),adjacent(edge[k]),inward);
                    }
                }
            }
            result.connected = true;
            result.entry_local = a;
            result.entrance = nearest_access_frontage(driveway_lot, roads, a);
            const auto mid = access_profile(lot, result.entrance, surface, ground);
            result.road_endpoint = mid.front();
            result.lot_endpoint = mid.back();
            break;
        }
        out.lots.push_back(std::move(result));
    }
    return out;
}

inline BuildingAccessBake bake_building_access(
    const RoadGraph& roads,const RibbonBake& ribbon,const GroundSampler& ground) {
    return bake_building_access(roads,ribbon,ground,authored_building_access_lots(ground));
}

inline void append_access_mesh(RoadMesh& target, const RoadMesh& source) {
    const auto base = static_cast<uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (uint32_t i : source.indices) target.indices.push_back(base + i);
    target.bounds.expand(source.bounds);
}

// Subtract the actual driveway footprint, including vertical kerb faces.
// Clip in XZ but interpolate full vertices: heights, UVs and shading survive.
// The roadway and its markings are never passed to this function.
inline void cut_access_opening(RoadMesh& mesh, const RoadMesh& driveway) {
    if (mesh.empty() || driveway.empty()) return;
    using Polygon = std::vector<TerrainVertex>;
    struct Cutter {
        std::array<glm::dvec2, 3> corners;
        glm::dvec2 lo, hi;
        double sign;
    };
    std::vector<Cutter> cutters;
    for (std::size_t i = 0; i < driveway.indices.size(); i += 3) {
        Cutter c;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto p = driveway.vertices[driveway.indices[i + k]].position;
            c.corners[k] = {p.x, p.z};
        }
        const double area = access_cross(c.corners[1] - c.corners[0], c.corners[2] - c.corners[0]);
        if (std::fabs(area) < 1e-7) continue;
        c.sign = area > 0 ? 1 : -1;
        c.lo = glm::min(c.corners[0], glm::min(c.corners[1], c.corners[2]));
        c.hi = glm::max(c.corners[0], glm::max(c.corners[1], c.corners[2]));
        cutters.push_back(c);
    }
    const auto split = [](const Polygon& polygon, glm::dvec2 a, glm::dvec2 b, double sign, double tolerance_m) {
        std::array<Polygon, 2> halves; // inside, outside
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto& from = polygon[i];
            const auto& to = polygon[(i + 1) % polygon.size()];
            const auto distance = [&](const TerrainVertex& v) {
                // Only vertical curb faces need boundary tolerance. Expanding
                // a cut in a horizontal top leaves a slit beyond the ramp.
                return sign * access_cross(b - a, glm::dvec2{v.position.x, v.position.z} - a) +
                       tolerance_m * glm::length(b - a);
            };
            const double d0 = distance(from), d1 = distance(to);
            halves[d0 >= 0 ? 0u : 1u].push_back(from);
            if ((d0 >= 0) != (d1 >= 0)) {
                const float t = static_cast<float>(d0 / (d0 - d1));
                TerrainVertex crossing{
                    glm::mix(from.position, to.position, t),
                    glm::normalize(glm::mix(from.normal, to.normal, t)),
                    glm::mix(from.uv, to.uv, t),
                    glm::mix(from.material_weights, to.material_weights, t)};
                halves[0].push_back(crossing);
                halves[1].push_back(crossing);
            }
        }
        return halves;
    };
    RoadMesh out;
    out.vertices = mesh.vertices;
    out.bounds = mesh.bounds;
    out.indices.reserve(mesh.indices.size());
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        std::array<TerrainVertex, 3> triangle;
        glm::dvec2 lo{std::numeric_limits<double>::max()}, hi{-std::numeric_limits<double>::max()};
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& v = mesh.vertices[mesh.indices[i + k]];
            triangle[k] = v;
            lo = glm::min(lo, glm::dvec2{v.position.x, v.position.z});
            hi = glm::max(hi, glm::dvec2{v.position.x, v.position.z});
        }
        // Nearly every road triangle is nowhere near this six-metre opening.
        // Preserve its indices without allocating polygon scratch storage.
        if (hi.x < driveway.bounds.min.x - .001 || lo.x > driveway.bounds.max.x + .001 ||
            hi.y < driveway.bounds.min.z - .001 || lo.y > driveway.bounds.max.z + .001) {
            out.indices.insert(out.indices.end(), mesh.indices.begin() + static_cast<std::ptrdiff_t>(i),
                               mesh.indices.begin() + static_cast<std::ptrdiff_t>(i + 3));
            continue;
        }
        const auto& pa=triangle[0].position;
        const auto& pb=triangle[1].position;
        const auto& pc=triangle[2].position;
        const double projected_area=access_cross(glm::dvec2{pb.x-pa.x,pb.z-pa.z},
                                                  glm::dvec2{pc.x-pa.x,pc.z-pa.z});
        const double tolerance=std::fabs(projected_area)<1e-8 ? .001:0.0;
        bool touched = false;
        std::vector<Polygon> pieces{Polygon(triangle.begin(), triangle.end())};
        for (const auto& c : cutters) {
            if (hi.x < c.lo.x - .001 || lo.x > c.hi.x + .001 ||
                hi.y < c.lo.y - .001 || lo.y > c.hi.y + .001) continue;
            touched = true;
            std::vector<Polygon> next;
            for (const auto& piece : pieces) {
                auto remaining = piece;
                for (std::size_t edge = 0; edge < 3 && remaining.size() >= 3; ++edge) {
                    auto halves = split(remaining, c.corners[edge], c.corners[(edge + 1) % 3], c.sign, tolerance);
                    if (halves[1].size() >= 3) next.push_back(std::move(halves[1]));
                    remaining = std::move(halves[0]);
                }
            }
            pieces = std::move(next);
        }
        if (!touched) {
            out.indices.insert(out.indices.end(), mesh.indices.begin() + static_cast<std::ptrdiff_t>(i),
                               mesh.indices.begin() + static_cast<std::ptrdiff_t>(i + 3));
            continue;
        }
        for (const auto& polygon : pieces) for (std::size_t k = 2; k < polygon.size(); ++k) {
            // Float vertex storage can leave a sub-millimetre sliver on an
            // INTERNAL boundary between adjacent cutter triangles, especially
            // far from the origin. Its centroid is still inside the union we
            // removed. Reject it without expanding the outer curb opening.
            if(tolerance==0.0) {
                const auto xz=[](const TerrainVertex& v) {return glm::dvec2{v.position.x,v.position.z};};
                const auto centre=(xz(polygon[0])+xz(polygon[k-1])+xz(polygon[k]))/3.0;
                bool inside=false;
                for(const auto& cut:cutters) {
                    bool contained=true;
                    for(std::size_t edge=0;edge<3;++edge)
                        contained &= cut.sign*access_cross(cut.corners[(edge+1)%3]-cut.corners[edge],centre-cut.corners[edge])>=0.0;
                    if(contained) {inside=true;break;}
                }
                if(inside) continue;
            }
            if (glm::length(glm::cross(polygon[k-1].position - polygon[0].position,
                                      polygon[k].position - polygon[0].position)) < 1e-6f) continue;
            for (const auto& v : {polygon[0], polygon[k-1], polygon[k]}) {
                out.indices.push_back(static_cast<uint32_t>(out.vertices.size()));
                out.vertices.push_back(v);
            }
        }
    }
    mesh = std::move(out);
}

inline void append_building_access(RibbonBake& roads, const BuildingAccessBake& access) {
    for (const auto& lot : access.lots) {
        cut_access_opening(roads.layer(RoadLayer::Walk), lot.driveway);
        cut_access_opening(roads.layer(RoadLayer::Kerb), lot.driveway);
        auto frontage = lot.frontage;
        auto pavement = lot.replacement_pavement;
        // A shared frontage may have separate entry and exit curb cuts.
        // Cut every mouth before adding it, so a later opening cannot remain
        // covered by the first entrance's full-width frontage sheet.
        for (const auto& entrance:access.lots) {
            cut_access_opening(frontage, entrance.driveway);
            cut_access_opening(pavement, entrance.driveway);
        }
        append_access_mesh(roads.layer(RoadLayer::Carriageway), frontage);
        append_access_mesh(roads.layer(RoadLayer::Carriageway), pavement);
        append_access_mesh(roads.layer(RoadLayer::Walk), lot.driveway);
    }
    for(const auto& lot:access.lots)
        append_access_mesh(roads.layer(RoadLayer::Kerb),lot.driveway_kerb);
}

} // namespace apricot::city
