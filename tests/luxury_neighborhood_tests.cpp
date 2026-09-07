#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/building_access.h"
#include "city/luxury_neighborhood.h"
#include "city/map.h"
#include "city/spines.h"
#include "physics/vehicle.h"
#include "road/ribbon.h"
#include "road/road_graph.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt=1.0f/120.0f;

const RoadSpine& spine(uint32_t id,const std::vector<RoadSpine>& spines) {
    const auto it=std::find_if(spines.begin(),spines.end(),[&](const auto& road) {
        return road.id==id;
    });
    REQUIRE(it!=spines.end());
    return *it;
}

glm::vec3 world(const city::StartSite& site,float x,float y,float z) {
    const auto p=city::luxury_world(site,{x,z});
    return {p.x,site.ground_m+y,p.y};
}

TerrainCollider estate_collider(std::size_t index,
                                const std::vector<city::StartPart>& parts,
                                const RoadCollision& roads) {
    TerrainCollider collider(city::kMapSeed);
    collider.set_road_collision(roads);
    const auto& site=city::kLuxuryEstates[index].site;
    const float site_yaw=std::atan2(site.sin_yaw,site.cos_yaw);
    for(const auto& part:parts) {
        const auto centre=world(site,part.centre.x,
            part.bottom_m+part.height_m*.5f,part.centre.z);
        const float yaw=site_yaw+glm::radians(part.yaw_deg);
        if(part.solid) collider.add_static_oriented_box(centre,
            {part.width_m*.5f,part.height_m*.5f,part.depth_m*.5f},yaw);
        if(city::luxury_ground_piece(part)) collider.add_static_ground_rect(
            {centre.x,centre.z},site.ground_m+part.bottom_m+part.height_m,
            {part.width_m*.5f,part.depth_m*.5f},yaw,Surface::Rock);
    }
    return collider;
}

void road_layout() {
    const auto spines=city::map_spines();
    const auto& approach=spine(240,spines);
    REQUIRE(approach.points.size()==7u);
    REQUIRE_NEAR(approach.points.front().x,-560.0f,.001f);
    REQUIRE_NEAR(approach.points.front().y,-340.0f,.001f);
    for(uint32_t id:{241u,242u,243u}) {
        const auto& court=spine(id,spines);
        REQUIRE(court.points.size()==15u);
        REQUIRE_NEAR(court.points[2].x,court.points.back().x,.001f);
        REQUIRE_NEAR(court.points[2].y,court.points.back().y,.001f);
    }

    const TerrainGround ground{city::kMapSeed};
    RoadGraph graph;
    graph.build(spines,{},ground.sampler());
    const auto entry=std::find_if(graph.nodes().begin(),graph.nodes().end(),[](const auto& node) {
        return glm::distance(node.pos,glm::vec2{-560.0f,-340.0f})<.01f;
    });
    REQUIRE(entry!=graph.nodes().end());
    REQUIRE(entry->kind==NodeKind::Junction);
    REQUIRE(entry->edges.size()>=3u);
    apricot_test::pass("Westmere branches from Marsh Road into three compact planted cul-de-sacs");
}

void authored_estates(GroundSampler ground) {
    for(std::size_t i=0;i<city::kLuxuryEstates.size();++i) {
        const auto& estate=city::kLuxuryEstates[i];
        const auto parts=city::bake_luxury_estate(i,ground);
        std::printf("  %s: %zu authored parts\n",estate.site.name,parts.size());
        REQUIRE_MSG(city::valid_start_parts(parts.data(),parts.size()),
                    "invalid estate geometry",estate.site.name);
        REQUIRE(parts.size()>220u && parts.size()<350u);
        const auto count=[&](const char* name) {
            return std::count_if(parts.begin(),parts.end(),[&](const auto& part) {
                return part.name && std::strcmp(part.name,name)==0;
            });
        };
        REQUIRE_MSG(count("house luxury driveway paving")==0,
                    "legacy stepped driveway slabs returned",estate.site.name);
        const auto driveway=city::bake_luxury_driveway(i,ground);
        REQUIRE_MSG(driveway.surface.vertices.size()==
                        city::kLuxuryDrivewaySectionCount*2u &&
                    driveway.surface.triangle_count()==
                        (city::kLuxuryDrivewaySectionCount-1u)*2u,
                    "continuous driveway ribbon drifted",estate.site.name);
        REQUIRE_MSG(!driveway.edge.empty(),
                    "continuous driveway edge is missing",estate.site.name);
        REQUIRE_MSG(count("house luxury front walk")==27,
                    "front walk is not terrain-following",estate.site.name);
        REQUIRE_MSG(count("house luxury entrance light lens")==2,
                    "missing paired entrance lamps",estate.site.name);
        REQUIRE_MSG(count("house luxury facade base course")==2,
                    "missing split facade base course",estate.site.name);
        REQUIRE_MSG(count("house luxury roof trim")==4,
                    "roof trim does not wrap the estate",estate.site.name);
        REQUIRE_MSG(count("house luxury address plaque")==1,
                    "missing non-text address plaque",estate.site.name);
        REQUIRE_MSG(count("house luxury address stud")==
                        1+2*static_cast<int>(i%3u),
                    "address stud pattern drifted",estate.site.name);
        REQUIRE_MSG(count("house luxury stucco pilaster")==
                        static_cast<int>(i%3u==0u)*2 &&
                        count("house luxury entry crown")==
                        static_cast<int>(i%3u==0u),
                    "stucco facade family drifted",estate.site.name);
        REQUIRE_MSG(count("house luxury brick string course")==
                        static_cast<int>(i%3u==1u)*2 &&
                        count("house luxury brick quoin")==
                        static_cast<int>(i%3u==1u)*6,
                    "brick facade family drifted",estate.site.name);
        REQUIRE_MSG(count("house luxury stepped parapet crown")==
                        static_cast<int>(i%3u==2u) &&
                        count("house luxury flat roof finial")==
                        static_cast<int>(i%3u==2u)*2,
                    "flat-roof facade family drifted",estate.site.name);
        REQUIRE_MSG(count("house luxury pool water")==static_cast<int>(estate.pool),
                    "pool variation drifted",estate.site.name);
        REQUIRE_MSG(count("house luxury pool fence post")==
                        static_cast<int>(estate.pool)*6 &&
                        count("house luxury pool fence rail")==
                        static_cast<int>(estate.pool)*10,
                    "private pool enclosure drifted",estate.site.name);
        const auto profile=city::luxury_driveway_profile(i,ground);
        for(std::size_t section=1;section<profile.size();++section)
            REQUIRE_MSG(std::fabs(profile[section]-profile[section-1u])<=.091f,
                        "private drive exceeds nine percent",estate.site.name);
        const float floor=city::luxury_floor_top(i,ground);
        for(float x=-21.5f;x<=23.5f;x+=1.0f)
            for(float z=-12.0f;z<=6.0f;z+=1.0f)
                REQUIRE_MSG(city::luxury_height(estate.site,ground,x,z)<floor,
                            "house floor clips the bluff",estate.site.name);
    }
    const auto common=city::bake_westmere_common(ground);
    const auto gate=city::bake_westmere_gate(ground);
    REQUIRE(city::valid_start_parts(common.data(),common.size()));
    REQUIRE(city::valid_start_parts(gate.data(),gate.size()));
    const auto common_count=[&](const char* name) {
        return std::count_if(common.begin(),common.end(),[&](const auto& part) {
            return part.name && std::strcmp(part.name,name)==0;
        });
    };
    REQUIRE(std::count_if(common.begin(),common.end(),[](const auto& part) {
        return part.name && std::strcmp(part.name,"luxury tree trunk")==0;
    })==10);
    REQUIRE(std::any_of(common.begin(),common.end(),[](const auto& part) {
        return part.name && std::strcmp(part.name,"westmere tennis court")==0;
    }));
    REQUIRE(std::any_of(gate.begin(),gate.end(),[](const auto& part) {
        return part.name && std::strcmp(part.name,"westmere gatehouse")==0;
    }));
    REQUIRE(common_count("westmere pool fence post")==6);
    REQUIRE(common_count("westmere pool fence rail")==10);
    REQUIRE(common_count("westmere pool coping")==4);
    REQUIRE(common_count("westmere tennis surface")==1);
    REQUIRE(common_count("westmere clubhouse rear wall")==1);
    REQUIRE(common_count("westmere clubhouse service hatch")==1);
    REQUIRE(common_count("westmere clubhouse counter base")==1);
    REQUIRE(common_count("westmere clubhouse counter cap")==1);
    REQUIRE(common_count("westmere clubhouse roof fascia")==4);
    REQUIRE(common_count("westmere clubhouse roof lantern panel")==4);
    REQUIRE(common_count("westmere clubhouse roof lantern cap")==1);
    for(std::size_t i=0;i<city::kWestmereCourtGreens.size();++i) {
        const auto green=city::bake_westmere_court_green(i,ground);
        REQUIRE(city::valid_start_parts(green.data(),green.size()));
        REQUIRE(std::count_if(green.begin(),green.end(),[](const auto& part) {
            return part.name && std::strcmp(part.name,"westmere court shrub")==0;
        })==12);
        REQUIRE(std::count_if(green.begin(),green.end(),[](const auto& part) {
            return part.name && std::strcmp(part.name,"luxury tree trunk")==0;
        })==4);
    }
    REQUIRE(city::wild_scatter_at(-620.0f,-555.0f)==0.0f);
    REQUIRE(city::wild_scatter_at(-655.0f,-438.0f)==0.0f);
    apricot_test::pass("nine estates, three planted court greens, community club and gate sit above the real bluff");
}

void section_specific_material_roles() {
    using Role=city::WestmereMaterialRole;
    const auto role=[](const char* name,city::StartFinish finish=city::StartFinish::White) {
        city::StartPart part{name,{0,0},0,1,1,1,finish,false};
        return city::westmere_material_role(part);
    };
    REQUIRE(role("house luxury front wall",city::StartFinish::WarmWall)==Role::PeachStucco);
    REQUIRE(role("house luxury front wall",city::StartFinish::White)==Role::CreamStucco);
    REQUIRE(role("house luxury front wall",city::StartFinish::Brick)==Role::Default);
    REQUIRE(role("house luxury main roof",city::StartFinish::DarkRoof)==Role::RoofTile);
    REQUIRE(role("house luxury boundary wall") == Role::Limestone);
    REQUIRE(role("westmere streetscape mailbox body") == Role::GreenMetal);
    REQUIRE(role("westmere pool coping") == Role::PoolTile);
    REQUIRE(role("westmere tennis surface") == Role::TennisSurface);
    REQUIRE(role("westmere pool fence rail") == Role::WhiteSlats);
    REQUIRE(role("luxury tree trunk") == Role::TreeBark);
    REQUIRE(role("westmere streetscape ornamental shrub") == Role::Foliage);
    REQUIRE(role("westmere court bench") == Role::BenchTimber);
    REQUIRE(role("westmere pool lounger") == Role::LoungerFabric);
    REQUIRE(role("westmere streetscape entry sign face") == Role::EntrySign);
    REQUIRE(role("house luxury entrance light lens",city::StartFinish::Yellow)==Role::Default);
    REQUIRE(role("westmere tennis baseline") == Role::Default);
    apricot_test::pass("Westmere imagegen textures map to specific model sections");
}

void connected_driveways(const RoadGraph& graph,RibbonBake ribbon,
                         GroundSampler ground) {
    const auto access=city::bake_building_access(graph,ribbon,ground);
    city::append_building_access(ribbon,access);
    city::append_luxury_driveways(ribbon,ground);
    const auto roads=build_road_collision(ribbon);
    for(std::size_t i=0;i<city::kLuxuryEstates.size();++i) {
        const auto& site=city::kLuxuryEstates[i].site;
        const auto it=std::find_if(access.lots.begin(),access.lots.end(),[&](const auto& lot) {
            return city::access_same_site(site,lot.site);
        });
        REQUIRE_MSG(it!=access.lots.end() && it->connected,
                    "estate has no working curb cut",site.name);
        REQUIRE((it->entrance.road_key>>32)==city::kLuxuryEstates[i].road_id);
        REQUIRE(it->frontage.empty() && it->replacement_pavement.empty());

        const auto parts=city::bake_luxury_estate(i,ground);
        const auto collider=estate_collider(i,parts,roads);
        const VehicleTuning tuning;
        auto car=spawn_vehicle(tuning,collider,it->road_endpoint.x,
                               it->road_endpoint.z,
                               std::atan2(site.sin_yaw,site.cos_yaw));
        for(int tick=0;tick<2400;++tick) {
            const auto q=city::access_local(site,{car.position.x,car.position.z});
            if(q.y<15.0f) break;
            car.velocity.x=-site.sin_yaw*3.0f;
            car.velocity.z=-site.cos_yaw*3.0f;
            car=step_vehicle(car,tuning,{},collider,kDt);
            REQUIRE_MSG(car.impact_count==0u,"vehicle hit estate entrance",site.name);
            REQUIRE(glm::dot(vehicle_up(car),glm::vec3{0,1,0})>.95f);
        }
        const auto parked=city::access_local(site,{car.position.x,car.position.z});
        std::printf("  %s drive ended at local %.2f %.2f with %u impacts\n",
                    site.name,parked.x,parked.y,car.impact_count);
        REQUIRE_MSG(parked.y<15.0f,"vehicle could not reach garage apron",site.name);
        REQUIRE_NEAR(parked.x,it->entry_local,.65f);
    }
    apricot_test::pass("a production vehicle crosses all nine real curb cuts and reaches every garage apron");
}

}  // namespace

int main() {
    road_layout();
    const TerrainGround ground{city::kMapSeed};
    authored_estates(ground.sampler());
    section_specific_material_roles();
    RoadGraph graph;
    graph.build(city::map_spines(),{},ground.sampler());
    connected_driveways(graph,bake_ribbons(graph,ground.sampler()),ground.sampler());
    return apricot_test::done("luxury_neighborhood_tests");
}
