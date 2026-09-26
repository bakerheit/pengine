#include <cstdio>
#include <cstring>

#include "city/building_access.h"
#include "city/spines.h"
#include "app/road_fixture_layout.h"
#include "test_assert.h"

using namespace apricot;
namespace city = apricot::city;

namespace {

float mesh_top_at(const RoadMesh& mesh, glm::vec2 p) {
    float result = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        float y = 0;
        if (city::access_triangle_height(mesh.vertices[mesh.indices[i]].position,
                mesh.vertices[mesh.indices[i+1]].position,
                mesh.vertices[mesh.indices[i+2]].position, p, y)) result = std::max(result, y);
    }
    return result;
}

void inspect_mesh(const RoadMesh& mesh, bool grade_check, const char* label = "mesh") {
    REQUIRE(mesh.indices.size() % 3u == 0u);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = mesh.vertices[mesh.indices[i]].position;
        const auto b = mesh.vertices[mesh.indices[i + 1]].position;
        const auto c = mesh.vertices[mesh.indices[i + 2]].position;
        const auto n = glm::normalize(glm::cross(b - a, c - a));
        REQUIRE(std::isfinite(n.y) && n.y > 0.0f);
        if (grade_check) {
            const float grade = glm::length(glm::vec2{n.x, n.z}) / n.y;
            if (grade > city::kAccessMaxGrade + 0.002f)
                std::printf("  steep %s triangle %zu: grade %.9f; area %.9g; "
                            "A %.9f %.9f %.9f; B %.9f %.9f %.9f; C %.9f %.9f %.9f\n",
                            label, i / 3u, grade,
                            static_cast<double>((b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x)),
                            a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
            REQUIRE(grade <= city::kAccessMaxGrade + 0.002f);
        }
        REQUIRE(glm::dot(n, mesh.vertices[mesh.indices[i]].normal) > 0.9999f);
    }
}

void expanded_neighborhood_plots() {
    const auto original=city::authored_building_access_lots();
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(),{},ground.sampler());
    const auto ribbon=bake_ribbons(roads,ground.sampler());
    const auto expanded=city::expand_building_access_lots(original,roads);
    const auto bake=city::bake_building_access(roads,ribbon,ground.sampler(),original);
    std::size_t plots=0,edges=0,shifted=0;
    for(std::size_t i=0;i<original.size();++i) {
        const auto& old=original[i];const auto& lot=expanded[i];const auto& result=bake.lots[i];
        if(!lot.generate_frontage || !city::access_neighborhood_plot(lot.site,lot.pavement)) continue;
        ++plots;
        REQUIRE(lot.pavement.width_m>=old.pavement.width_m);
        REQUIRE(lot.pavement.depth_m>=old.pavement.depth_m);
        const bool restaurant=std::strcmp(old.site.name,"Cloggers")==0;
        if(restaurant) {
            REQUIRE(!lot.parking_tracks_frontage);
            REQUIRE_NEAR(glm::length(lot.parking_shift),0.f,.0001f);
        }
        REQUIRE(result.frontage.empty());
        REQUIRE(!result.replacement_pavement.empty());
        REQUIRE(city::building_access_replaces_pavement(old.site,old.pavement));
        const auto surface=city::access_surface_near(ribbon,lot);
        for(std::size_t side=0;side<4;++side) if(lot.sidewalk_edges[side]) {
            ++edges;
            constexpr std::array<glm::vec2,4> directions{{{1,0},{-1,0},{0,1},{0,-1}}};
            auto ray=lot;ray.outward_local=directions[side];
            const bool z=side>=2;
            const float centre=z?lot.pavement.centre.x:lot.pavement.centre.z;
            const float half=(z?lot.pavement.width_m:lot.pavement.depth_m)*.5f;
            for(float a=centre-half+4.f;a<centre+half-4.f;a+=2.f) {
                const auto hit=city::nearest_access_frontage(ray,roads,a);
                REQUIRE(hit.valid);
                REQUIRE_NEAR(hit.walk_distance_m,0.f,.03f);
                const auto inside=hit.sidewalk_outer-hit.outward*.03f;
                const auto walk=hit.sidewalk_outer+hit.outward*.03f;
                const float lot_y=mesh_top_at(result.replacement_pavement,inside);
                REQUIRE(std::isfinite(lot_y));
                REQUIRE_NEAR(lot_y,surface.at(walk,0),.008f);
                // Pavement is clipped out of the public sidewalk itself.
                REQUIRE(!std::isfinite(mesh_top_at(result.replacement_pavement,walk)));
            }
        }
        auto parts=old.parts;
        city::apply_building_access_layout(old.site,parts,bake);
        for(std::size_t p=0;p<parts.size();++p) {
            if(std::strcmp(parts[p].name,old.pavement.name)==0) {
                REQUIRE_NEAR(parts[p].width_m,lot.pavement.width_m,.001f);
                REQUIRE_NEAR(parts[p].depth_m,lot.pavement.depth_m,.001f);
            } else if(city::access_parking_marker(parts[p])) {
                REQUIRE_NEAR(parts[p].centre.x,lot.parts[p].centre.x,.001f);
                REQUIRE_NEAR(parts[p].centre.z,lot.parts[p].centre.z,.001f);
                // The flat parking row remains entirely on the final parcel.
                REQUIRE(std::fabs(parts[p].centre.x-lot.pavement.centre.x)+parts[p].width_m*.5f<lot.pavement.width_m*.5f);
                REQUIRE(std::fabs(parts[p].centre.z-lot.pavement.centre.z)+parts[p].depth_m*.5f<lot.pavement.depth_m*.5f);
                shifted+=glm::length(lot.parking_shift)>.001f;
            } else {
                REQUIRE(parts[p].centre.x==old.parts[p].centre.x);
                REQUIRE(parts[p].centre.z==old.parts[p].centre.z);
            }
        }
        std::printf("  expanded %s: %.1fx%.1f -> %.1fx%.1f m; parking shift %.2f m\n",
            lot.name,old.pavement.width_m,old.pavement.depth_m,lot.pavement.width_m,
            lot.pavement.depth_m,glm::length(lot.parking_shift));
    }
    REQUIRE(plots==10u);REQUIRE(edges>=32u);REQUIRE(shifted>=11u);
    // A closer neighboring parcel must win over a road farther along the ray.
    auto gas=original.front();auto neighbor=gas;
    const auto origin=city::access_world(gas.site,{29,0});
    neighbor.site.origin={origin.x,origin.y};
    neighbor.pavement.centre={0,0};neighbor.pavement.width_m=4;neighbor.pavement.depth_m=20;
    neighbor.pavement.name="neighbor test parcel";
    const auto guarded=city::expand_building_access_lots({gas,neighbor},roads);
    REQUIRE(!guarded.front().sidewalk_edges[0]);
    REQUIRE_NEAR(guarded.front().pavement.centre.x+guarded.front().pavement.width_m*.5f,
                 gas.pavement.centre.x+gas.pavement.width_m*.5f,.001f);
    apricot_test::pass("ten whole neighborhood plots meet sidewalks; restaurant bays stay at the door crossing while other parking follows the final parcel");
}

void authored_inventory_and_slopes() {
    const auto lots = city::authored_building_access_lots();
    REQUIRE(lots.size() == 46u + city::kLuxuryEstates.size());
    const TerrainGround ground{city::kMapSeed};
    RoadGraph graph;
    graph.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
    RibbonBake ribbon = bake_ribbons(graph, ground.sampler());
    const auto access = city::bake_building_access(graph, ribbon, ground.sampler(), lots);
    REQUIRE(access.lots.size() == lots.size());
    std::size_t connected = 0;
    std::size_t extra_triangles = 0;
    for (std::size_t i = 0; i < access.lots.size(); ++i) {
        const auto& result = access.lots[i];
        std::printf("  %s: %s; frontage %zu tri; driveway %zu tri; replacement %zu tri; entry %.2f; road %llu; road XZ %.2f %.2f -> lot %.2f %.2f\n",
            result.name, result.connected ? "connected" : "BLOCKED",
            result.frontage.triangle_count(), result.driveway.triangle_count(),
            result.replacement_pavement.triangle_count(),
            static_cast<double>(result.entry_local),
            static_cast<unsigned long long>(result.entrance.road_key >> 32),
            result.road_endpoint.x, result.road_endpoint.z, result.lot_endpoint.x, result.lot_endpoint.z);
        if (std::strcmp(lots[i].site.name,"Cloggers")==0 ||
            std::strcmp(lots[i].site.name,"TacoMaco")==0) {
            REQUIRE(result.connected);
            // The authored vehicle path (2.8m including clearance) must fit
            // inside the actual opening even if junction clearance shifts it.
            REQUIRE(std::abs(result.entry_local-lots[i].preferred_entry_local)+1.4f <= result.width_m*.5f-.05f);
            if (!lots[i].generate_frontage) REQUIRE(result.frontage.empty());
        }
        if (!result.connected) continue;
        ++connected;
        REQUIRE(result.width_m >= (result.use == city::BuildingAccessUse::PedestrianPath ? 3.0f : 5.0f));
        REQUIRE_NEAR(result.lot_endpoint.y, city::access_lot_top(lots[i]), 1e-5);
        REQUIRE(result.entrance.road_key >> 32 != 151u);
        REQUIRE(city::access_clear_of_junctions(graph, result.entrance.curb, result.width_m * 0.5f));
        REQUIRE(!city::access_obstructed(lots[i], {result.lot_endpoint.x, result.lot_endpoint.z}, 0.4f));
        inspect_mesh(result.frontage, false, result.name);
        inspect_mesh(result.driveway, true, result.name);
        inspect_mesh(result.replacement_pavement, true, result.name);
        const glm::vec2 mouth{result.road_endpoint.x, result.road_endpoint.z};
        REQUIRE_NEAR(mesh_top_at(result.driveway, mouth), result.road_endpoint.y, .0001f);
        auto driveway_lot = lots[i];
        if (glm::length(driveway_lot.driveway_outward_local) > .5f)
            driveway_lot.outward_local = driveway_lot.driveway_outward_local;
        for (const auto& vertex : result.driveway.vertices) {
            const auto local = city::access_local(lots[i].site, {vertex.position.x, vertex.position.z});
            const float along = std::fabs(driveway_lot.outward_local.y) > .5f ? local.x : local.y;
            const auto edge = city::nearest_access_frontage(driveway_lot, graph, along);
            REQUIRE(edge.valid);
            REQUIRE(glm::dot(glm::vec2{vertex.position.x, vertex.position.z} - edge.curb, edge.outward) <= .002f);
        }
        extra_triangles += result.frontage.triangle_count() + result.driveway.triangle_count() +
                           result.replacement_pavement.triangle_count();
    }
    std::printf("  total: %zu/%zu connected, %zu added triangles\n", connected, lots.size(), extra_triangles);
    REQUIRE(connected == lots.size());
    for (const char* name : {
             "hospital public arrival Tenth entry curb cut",
             "hospital public arrival Tenth curb cut",
             "hospital ambulance Juniper entry curb cut",
             "hospital ambulance Juniper exit curb cut",
             "hospital service Sixth curb cut",
             "crown service drive",
             "Ocean Drive hotel service lane",
             "Calle Noche west service route",
             "Mirage south loading route to Coral Way",
             "mariposa Seabreeze driveway",
             "Palmera seven metre Seabreeze service lane",
             "rear truck lane",
         }) {
        const auto it = std::find_if(access.lots.begin(), access.lots.end(),
                                     [&](const auto& lot) {
                                         return std::strcmp(lot.name, name) == 0;
                                     });
        REQUIRE(it != access.lots.end());
        REQUIRE(it->connected);
        REQUIRE(!it->driveway.empty());
        REQUIRE(it->frontage.empty());
    }
    for (const auto& expected : {
             std::pair{"crown service drive", 233u},
             std::pair{"Ocean Drive hotel service lane", 234u},
             std::pair{"Calle Noche west service route", 227u},
             std::pair{"Mirage south loading route to Coral Way", 225u},
             std::pair{"mariposa Seabreeze driveway", 234u},
             std::pair{"Palmera seven metre Seabreeze service lane", 234u},
             std::pair{"rear truck lane", 225u},
         }) {
        const auto it = std::find_if(access.lots.begin(), access.lots.end(),
                                     [&](const auto& lot) {
                                         return std::strcmp(lot.name,
                                                            expected.first) == 0;
                                     });
        REQUIRE(it != access.lots.end());
        REQUIRE(it->use == city::BuildingAccessUse::LandsideService);
        REQUIRE((it->entrance.road_key >> 32) == expected.second);
    }
    LaneGraph lanes;
    lanes.build(graph, ground.sampler());
    const auto lamps = build_street_lamp_layouts(lanes);
    for (const auto& result : access.lots) {
        const auto& mesh = result.driveway;
        for (const auto& lamp : lamps) {
            for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
                float y = 0;
                REQUIRE(!city::access_triangle_height(mesh.vertices[mesh.indices[t]].position,
                    mesh.vertices[mesh.indices[t + 1]].position, mesh.vertices[mesh.indices[t + 2]].position,
                    {lamp.pole_ground.x, lamp.pole_ground.z}, y));
            }
        }
    }
    const auto original_road = ribbon.layer(RoadLayer::Carriageway);
    city::append_building_access(ribbon, access);
    const auto after = build_road_collision(ribbon);
    std::size_t drawn_collision_triangles = 0;
    for (auto layer : {RoadLayer::Carriageway, RoadLayer::Unpaved, RoadLayer::Walk, RoadLayer::Plate}) {
        const auto& mesh = ribbon.layer(layer);
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices[mesh.indices[i]].position;
            const auto b = mesh.vertices[mesh.indices[i+1]].position;
            const auto c = mesh.vertices[mesh.indices[i+2]].position;
            if (glm::length(glm::cross(b-a, c-a)) >= 1e-12f) ++drawn_collision_triangles;
        }
    }
    REQUIRE(after.triangles.size() == drawn_collision_triangles);
    // The original asphalt vertices/indices are untouched, not patched over.
    REQUIRE(std::equal(original_road.indices.begin(), original_road.indices.end(),
                       ribbon.layer(RoadLayer::Carriageway).indices.begin()));
    // Collision is built from the very same drawn corners, not flat boxes.
    for (const auto& result : access.lots) {
        const auto& mesh = result.driveway;
        if (!result.entrance.road_end) {
            const auto p = result.entrance.curb + result.entrance.outward * .25f;
            const float old_y = mesh_top_at(original_road, p);
            REQUIRE(std::isfinite(old_y));
            float new_y = -std::numeric_limits<float>::infinity();
            for (const auto& t : after.triangles) {
                float y = 0;
                if (city::access_triangle_height(t.geom.a, t.geom.b, t.geom.c, p, y)) new_y = std::max(new_y, y);
            }
            REQUIRE_NEAR(new_y, old_y, .0001f);
            const auto inside = result.entrance.curb - result.entrance.outward * .25f;
            const float ramp_y = mesh_top_at(mesh, inside);
            REQUIRE(std::isfinite(ramp_y));
            float collision_y = -std::numeric_limits<float>::infinity();
            for (const auto& t : after.triangles) {
                float y = 0;
                if (city::access_triangle_height(t.geom.a, t.geom.b, t.geom.c, inside, y))
                    collision_y = std::max(collision_y, y);
            }
            if(std::fabs(collision_y-ramp_y)>.0001f) std::printf("inlet mismatch: %s at %.3f %.3f\n",result.name,inside.x,inside.y);
            REQUIRE_NEAR(collision_y, ramp_y, .0001f);
        }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices[mesh.indices[i]].position;
            const auto b = mesh.vertices[mesh.indices[i + 1]].position;
            const auto c = mesh.vertices[mesh.indices[i + 2]].position;
            bool exact = false;
            for (const auto& t : after.triangles)
                if (t.geom.a == a && t.geom.b == b && t.geom.c == c) { exact = true; break; }
            REQUIRE(exact);
        }
    }
    apricot_test::pass("26 authored entrances: clear driveways, grade, upward winding, exact visible/collision triangle match");
}

void horizontal_inlet_cuts_do_not_leave_slits() {
    RoadMesh sidewalk,driveway;
    city::access_quad(sidewalk,{0,.18f,0},{10,.18f,0},{10,.18f,10},{0,.18f,10},1.f);
    city::access_quad(driveway,{4,.06f,0},{6,.06f,0},{6,.18f,5},{4,.18f,5},1.f);
    city::cut_access_opening(sidewalk,driveway);
    double area=0;
    for(std::size_t i=0;i<sidewalk.indices.size();i+=3) {
        const auto a=sidewalk.vertices[sidewalk.indices[i]].position;
        const auto b=sidewalk.vertices[sidewalk.indices[i+1]].position;
        const auto c=sidewalk.vertices[sidewalk.indices[i+2]].position;
        area+=std::fabs(city::access_cross(glm::dvec2{b.x-a.x,b.z-a.z},glm::dvec2{c.x-a.x,c.z-a.z}))*.5;
    }
    REQUIRE_NEAR(area,90.0,1e-5);
    apricot_test::pass("inlet removes its exact footprint without cutting slits in adjacent sidewalk");
}

void rotated_road_and_missing_access() {
    city::BuildingAccessLot lot;
    lot.name = "test lot";
    lot.site.origin = {90, -60};
    lot.site.cos_yaw = 0.8660254f;
    lot.site.sin_yaw = 0.5f;
    lot.site.ground_m = 0;
    lot.pavement = {"test lot", {0, 0}, 0, 24, 0.10f, 20, city::StartFinish::Asphalt};
    RoadSpine spine;
    spine.id = 4;
    spine.cls = RoadClass::Street;
    spine.points = {city::access_world(lot.site, {-100, 28}), city::access_world(lot.site, {100, 28})};
    RoadGraph graph;
    graph.build({spine}, RoadGraphParams{}, GroundSampler{});
    auto ribbon = bake_ribbons(graph, GroundSampler{});
    const auto first = city::bake_building_access(graph, ribbon, GroundSampler{}, {lot});
    REQUIRE(first.lots.front().connected);
    const auto& entry = first.lots.front();
    const auto local_curb = city::access_local(lot.site, entry.entrance.curb);
    const auto local_walk = city::access_local(lot.site, entry.entrance.sidewalk_outer);
    REQUIRE_NEAR(local_curb.y, 21, 2e-5);
    REQUIRE_NEAR(local_walk.y, 18, 2e-5);
    REQUIRE_NEAR(entry.road_endpoint.y, kDrapeEpsM, 1e-5);
    REQUIRE_NEAR(city::access_local(lot.site, {entry.road_endpoint.x, entry.road_endpoint.z}).y, 21, 2e-5);
    REQUIRE_NEAR(entry.lot_endpoint.y, 0.10f, 1e-5);
    inspect_mesh(entry.driveway, true);
    // Concrete uses the same one-metre UV density as the retained sidewalk,
    // not the asphalt's four/eight-metre repeat stretched across the inlet.
    for(const auto& v:entry.driveway.vertices) {
        REQUIRE_NEAR(v.uv.x,v.position.x/RibbonParams{}.slab_m,1e-5f);
        REQUIRE_NEAR(v.uv.y,v.position.z/RibbonParams{}.slab_m,1e-5f);
    }
    REQUIRE(!entry.driveway_kerb.empty());
    REQUIRE_NEAR(entry.driveway_kerb.bounds.min.y,kDrapeEpsM,1e-5f);
    REQUIRE(entry.driveway_kerb.bounds.max.y>=kDrapeEpsM+kKerbHeightM);
    for(const auto& v:entry.driveway_kerb.vertices) {
        REQUIRE(std::fabs(v.normal.y)<1e-5f);
        const auto local=city::access_local(lot.site,{v.position.x,v.position.z});
        REQUIRE_NEAR(std::fabs(local.x),entry.width_m*.5f,2e-5f);
        // No side wall across the mouth: cheeks remain at its outer edges.
        REQUIRE(local.y>=18.f-.001f && local.y<=21.f+.001f);
    }
    // A point in the formerly exposed vertical gap must now lie on a drawn
    // cheek triangle, for either side of the lowered mouth.
    for(float side:{-1.f,1.f}) {
        const auto xz=city::access_world(lot.site,{side*entry.width_m*.5f,20.75f});
        const glm::vec3 query{xz.x,.13f,xz.y};
        bool covered=false;
        for(std::size_t i=0;i<entry.driveway_kerb.indices.size();i+=3) {
            const auto a=entry.driveway_kerb.vertices[entry.driveway_kerb.indices[i]].position;
            const auto b=entry.driveway_kerb.vertices[entry.driveway_kerb.indices[i+1]].position;
            const auto c=entry.driveway_kerb.vertices[entry.driveway_kerb.indices[i+2]].position;
            const auto u=b-a,v=c-a,q=query-a;
            const float uu=glm::dot(u,u),uv=glm::dot(u,v),vv=glm::dot(v,v);
            const float det=uu*vv-uv*uv;
            if(det<1e-10f) continue;
            const float s=(glm::dot(q,u)*vv-glm::dot(q,v)*uv)/det;
            const float t=(glm::dot(q,v)*uu-glm::dot(q,u)*uv)/det;
            covered|=s>=-1e-4f && t>=-1e-4f && s+t<=1.0001f &&
                std::fabs(glm::dot(q,glm::normalize(glm::cross(u,v))))<1e-4f;
        }
        REQUIRE(covered);
    }
    const auto second = city::bake_building_access(graph, ribbon, GroundSampler{}, {lot});
    REQUIRE(second.lots.front().driveway.indices == entry.driveway.indices);
    for (std::size_t i = 0; i < entry.driveway.vertices.size(); ++i)
        REQUIRE(second.lots.front().driveway.vertices[i].position == entry.driveway.vertices[i].position);
    city::append_building_access(ribbon, first);
    // A real curb cut, not a ramp over an unmodified solid sidewalk.
    const auto mouth = city::access_world(lot.site, {0, 20.75f});
    const float ramp_y = kDrapeEpsM + (kKerbHeightM + city::kAccessSeamLiftM) * .25f / city::kAccessRampRunM;
    REQUIRE_NEAR(mesh_top_at(ribbon.layer(RoadLayer::Walk), mouth), ramp_y, .0001f);
    const auto beside = city::access_world(lot.site, {5, 20.75f});
    REQUIRE_NEAR(mesh_top_at(ribbon.layer(RoadLayer::Walk), beside), kDrapeEpsM + kKerbHeightM, .0001f);
    const auto& kerbs = ribbon.layer(RoadLayer::Kerb);
    for (std::size_t i = 0; i < kerbs.indices.size(); i += 3) {
        float min_x = 1e6f, max_x = -1e6f;
        bool on_curb = true;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto v = kerbs.vertices[kerbs.indices[i+k]].position;
            const auto p = city::access_local(lot.site, {v.x, v.z});
            on_curb &= std::fabs(p.y - 21.0f) < .002f;
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        }
        REQUIRE(!(on_curb && min_x < 0 && max_x > 0));
    }
    lot.allowed_spines = {151};
    REQUIRE(!city::bake_building_access(graph, ribbon, GroundSampler{}, {lot}).lots.front().connected);
    lot.allowed_spines.clear();
    lot.parts.push_back({"wall blocking whole frontage", {0, 10}, 0, 30, 3, 1,
                         city::StartFinish::Brick, true});
    REQUIRE(!city::bake_building_access(graph, ribbon, GroundSampler{}, {lot}).lots.front().connected);
    apricot_test::pass("rotated road-derived frontage, exact endpoints, repeatability, missing road and blocked frontage");
}

void clipped_lot_keeps_paving_and_exposes_the_road() {
    city::BuildingAccessLot lot;
    lot.site.origin = {350, 2350};
    lot.site.cos_yaw = 0.8660254f;
    lot.site.sin_yaw = 0.5f;
    lot.site.ground_m = 0;
    lot.pavement = {"clipped test lot", {0, 0}, 0, 24, 0.13f, 40,
                    city::StartFinish::Asphalt};
    RoadSpine road;
    road.cls = RoadClass::Street;
    road.points = {city::access_world(lot.site, {-100, 0}), city::access_world(lot.site, {100, 0})};
    RoadGraph graph;
    graph.build({road}, RoadGraphParams{}, GroundSampler{});
    const auto ribbon = bake_ribbons(graph, GroundSampler{});
    const auto surface = city::access_surface_near(ribbon, lot);
    const auto replacement = city::access_clipped_airport_pavement(lot, surface);
    inspect_mesh(replacement, true);
    double area = 0;
    for (std::size_t i = 0; i < replacement.indices.size(); i += 3) {
        const auto a = replacement.vertices[replacement.indices[i]].position;
        const auto b = replacement.vertices[replacement.indices[i + 1]].position;
        const auto c = replacement.vertices[replacement.indices[i + 2]].position;
        area += 0.5 * std::fabs((static_cast<double>(b.x) - a.x) * (static_cast<double>(c.z) - a.z) -
                               (static_cast<double>(b.z) - a.z) * (static_cast<double>(c.x) - a.x));
        const auto midpoint = (a + b + c) / 3.0f;
        const auto local = city::access_local(lot.site, {midpoint.x, midpoint.z});
        // All retained triangles lie outside the complete 14 m road + two
        // 3 m sidewalks. The original slab would have hidden that corridor.
        if (std::fabs(local.y) < 9.999f)
            std::printf("  clipping interior vertex: local %.8f %.8f; triangle %.8f %.8f / %.8f %.8f / %.8f %.8f\n",
                local.x, local.y, a.x, a.z, b.x, b.z, c.x, c.z);
        REQUIRE(std::fabs(local.y) >= 9.999f);
        REQUIRE_NEAR(a.y, 0.13f, 1e-6);
    }
    REQUIRE_NEAR(area, 24.0 * (40.0 - 20.0), 0.03);
    apricot_test::pass("rotated airport-scale slab clipping: exact retained area, no paving over the road or walk");
}

} // namespace

int main() {
    expanded_neighborhood_plots();
    horizontal_inlet_cuts_do_not_leave_slits();
    rotated_road_and_missing_access();
    clipped_lot_keeps_paving_and_exposes_the_road();
    authored_inventory_and_slopes();
    return apricot_test::done("building_access_tests");
}
