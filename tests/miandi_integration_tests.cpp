#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/miandi_bayfront.h"
#include "city/miandi_calle_noche.h"
#include "city/miandi_calle_ocho.h"
#include "city/miandi_context.h"
#include "city/miandi_layout.h"
#include "city/miandi_mariposa_motel.h"
#include "city/miandi_night_lighting.h"
#include "city/miandi_ocean_drive.h"
#include "city/miandi_port_sol.h"
#include "city/miandi_prism_works.h"
#include "city/miandi_resort_frontage.h"
#include "city/miandi_sunwave_hotel.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

struct Bounds {
    float x0;
    float x1;
    float z0;
    float z1;
};

Bounds site_bounds(const city::StartSite& site) {
    REQUIRE_NEAR(site.cos_yaw, 1.0f, 1e-5f);
    REQUIRE_NEAR(site.sin_yaw, 0.0f, 1e-5f);
    return {site.origin.x + site.lot_centre.x - site.lot_width_m * .5f,
            site.origin.x + site.lot_centre.x + site.lot_width_m * .5f,
            site.origin.z + site.lot_centre.z - site.lot_depth_m * .5f,
            site.origin.z + site.lot_centre.z + site.lot_depth_m * .5f};
}

Bounds local_bounds(const city::BuildingPiece& part) {
    const float yaw = part.yaw_deg * 0.01745329251994329577f;
    const float ex = std::fabs(std::cos(yaw)) * part.width_m * .5f +
                     std::fabs(std::sin(yaw)) * part.depth_m * .5f;
    const float ez = std::fabs(std::sin(yaw)) * part.width_m * .5f +
                     std::fabs(std::cos(yaw)) * part.depth_m * .5f;
    return {part.centre.x - ex, part.centre.x + ex,
            part.centre.z - ez, part.centre.z + ez};
}

bool overlaps(const Bounds& a, const Bounds& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.z0 < b.z1 && b.z0 < a.z1;
}

const city::BuildingPiece& named(const std::vector<city::BuildingPiece>& parts,
                                 const char* name) {
    const auto it = std::find_if(parts.begin(), parts.end(), [&](const auto& part) {
        return part.name && std::strcmp(part.name, name) == 0;
    });
    REQUIRE_MSG(it != parts.end(), "missing Miandi integration piece", name);
    return *it;
}

void finished_parcels_are_separate_and_clear_the_grid() {
    const std::array<const city::StartSite*, 9> sites{
        &city::kMiandiCalleOchoSite, &city::kMiandiBayfrontSite,
        &city::kMiandiOceanDriveSite, &city::kMiandiNorthPromenadeSite,
        &city::kMiandiPortSolSite, &city::kMiandiCalleNocheSite,
        &city::kMiandiPrismWorksSite, &city::kMiandiMariposaMotelSite,
        &city::kMiandiSunwaveHotelSite};
    std::array<Bounds, sites.size()> bounds{};
    for (std::size_t i = 0; i < sites.size(); ++i) {
        bounds[i] = site_bounds(*sites[i]);
        REQUIRE(city::miandi_city_contains(bounds[i].x0, bounds[i].z0));
        REQUIRE(city::miandi_city_contains(bounds[i].x1, bounds[i].z1));
        REQUIRE_NEAR(height_at(city::kMapSeed, sites[i]->origin.x,
                               sites[i]->origin.z),
                     city::kMiandiGroundM, .001f);
        for (std::size_t j = 0; j < i; ++j)
            REQUIRE_MSG(!overlaps(bounds[i], bounds[j]),
                        "finished Miandi parcel overlap", sites[i]->name);
    }

    // Hotel terraces meet Ocean Drive's outer sidewalk seam; other parcels
    // retain their verges. No parcel enters a road/sidewalk ribbon.
    REQUIRE_NEAR(bounds[0].z0, 8225.0f, 1e-5f);
    REQUIRE_NEAR(bounds[1].z1, 8375.0f, 1e-5f);
    REQUIRE_NEAR(bounds[2].x1, 8086.0f, 1e-5f);
    REQUIRE_NEAR(bounds[3].x0, 8114.0f, 1e-5f);
    REQUIRE_NEAR(bounds[4].z0, 8625.0f, 1e-5f);
    apricot_test::pass("finished Miandi parcels are separate, supported, and clear of road ribbons");
}

void solid_geometry_stays_inside_owned_parcels() {
    const auto calle = city::bake_miandi_calle_ocho();
    const auto bayfront = city::bake_miandi_bayfront();
    auto hotels = city::bake_miandi_ocean_drive();
    const auto frontage = city::bake_miandi_resort_frontage();
    hotels.insert(hotels.end(), frontage.begin(), frontage.end());
    const auto port = city::bake_miandi_port_sol();
    const auto noche = city::bake_miandi_calle_noche();
    const auto prism = city::bake_miandi_prism_works();
    const auto mariposa = city::bake_miandi_mariposa_motel();
    const auto palmera = city::bake_miandi_sunwave_hotel();
    const std::array<std::pair<const city::StartSite*,
                               const std::vector<city::BuildingPiece>*>, 8>
        packages{{{&city::kMiandiCalleOchoSite, &calle},
                  {&city::kMiandiBayfrontSite, &bayfront},
                  {&city::kMiandiOceanDriveSite, &hotels},
                  {&city::kMiandiPortSolSite, &port},
                  {&city::kMiandiCalleNocheSite, &noche},
                  {&city::kMiandiPrismWorksSite, &prism},
                  {&city::kMiandiMariposaMotelSite, &mariposa},
                  {&city::kMiandiSunwaveHotelSite, &palmera}}};

    std::size_t solids = 0;
    for (const auto& [site, parts] : packages) {
        for (const auto& part : *parts) {
            if (!part.solid) continue;
            ++solids;
            const Bounds p = local_bounds(part);
            REQUIRE_MSG(p.x0 >= site->lot_centre.x - site->lot_width_m * .5f - .01f,
                        "solid leaves parcel west", part.name);
            REQUIRE_MSG(p.x1 <= site->lot_centre.x + site->lot_width_m * .5f + .01f,
                        "solid leaves parcel east", part.name);
            REQUIRE_MSG(p.z0 >= site->lot_centre.z - site->lot_depth_m * .5f - .01f,
                        "solid leaves parcel north", part.name);
            REQUIRE_MSG(p.z1 <= site->lot_centre.z + site->lot_depth_m * .5f + .01f,
                        "solid leaves parcel south", part.name);
        }
    }
    REQUIRE(solids > 80u);
    apricot_test::pass("all collision solids stay inside their authored Miandi parcels");
}

void public_and_service_routes_meet_sidewalk_edges_without_overlap() {
    const auto calle = city::bake_miandi_calle_ocho();
    const auto bayfront = city::bake_miandi_bayfront();
    const auto hotels = city::bake_miandi_ocean_drive();
    const auto promenade = city::bake_miandi_north_promenade();
    const auto port = city::bake_miandi_port_sol();
    const auto noche = city::bake_miandi_calle_noche();
    const auto prism = city::bake_miandi_prism_works();
    const auto mariposa = city::bake_miandi_mariposa_motel();
    const auto palmera = city::bake_miandi_sunwave_hotel();

    const auto& calle_walk = named(calle, "Calle Ocho continuous public pavement");
    const auto& crown_walk = named(bayfront, "crown forecourt walk");
    const auto& crown_drive = named(bayfront, "crown service drive");
    const auto& coral_walk = named(hotels, "Bellmar lobby walk");
    const auto& hotel_service = named(hotels, "Ocean Drive hotel service lane");
    const auto& beach_link = named(promenade, "north promenade Ocean Drive connector");
    const auto& market_walk = named(port, "public market walk");
    const auto& truck_lane = named(port, "rear truck lane");
    const auto& noche_walk = named(noche, "Calle Noche north public walk");
    const auto& noche_service = named(noche, "Calle Noche west service route");
    const auto& prism_walk = named(prism, "Mirage north public walk to Bayfront");
    const auto& prism_club_walk = named(prism, "Mirage club public walk to Bayfront");
    const auto& prism_loading = named(prism, "Mirage south loading route to Coral Way");
    const auto& mariposa_walk = named(mariposa, "mariposa Bayfront lobby walk");
    const auto& mariposa_drive = named(mariposa, "mariposa Seabreeze driveway");
    const auto& sunwave_walk = named(palmera, "Palmera public route to Ocean Drive");
    const auto& sunwave_club_walk = named(palmera, "Palmera club route to Ocean Drive");
    const auto& sunwave_service = named(palmera, "Palmera seven metre Seabreeze service lane");

    for (const auto* route : {&calle_walk, &crown_walk, &crown_drive,
                              &coral_walk, &hotel_service, &beach_link,
                              &market_walk, &truck_lane, &noche_walk,
                              &noche_service, &prism_walk, &prism_club_walk,
                              &prism_loading, &mariposa_walk, &mariposa_drive,
                              &sunwave_walk, &sunwave_club_walk,
                              &sunwave_service})
        REQUIRE(!route->solid);

    REQUIRE_NEAR(city::kMiandiCalleOchoSite.origin.z + local_bounds(calle_walk).z0,
                 8210.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiBayfrontSite.origin.z + local_bounds(crown_walk).z1,
                 8386.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiBayfrontSite.origin.x + local_bounds(crown_drive).x1,
                 7690.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.origin.x + local_bounds(coral_walk).x1,
                 8086.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.origin.x + local_bounds(hotel_service).x0,
                 7910.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiNorthPromenadeSite.origin.x + local_bounds(beach_link).x0,
                 8114.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiPortSolSite.origin.z + local_bounds(market_walk).z0,
                 8610.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiPortSolSite.origin.z + local_bounds(truck_lane).z0,
                 8610.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiCalleNocheSite.origin.z + local_bounds(noche_walk).z0,
                 8414.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiCalleNocheSite.origin.x + local_bounds(noche_service).x0,
                 6910.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiPrismWorksSite.origin.z + local_bounds(prism_walk).z0,
                 8414.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiPrismWorksSite.origin.z + local_bounds(prism_club_walk).z0,
                 8414.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiPrismWorksSite.origin.z + local_bounds(prism_loading).z1,
                 8590.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.origin.z + local_bounds(mariposa_walk).z0,
                 8414.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.origin.x + local_bounds(mariposa_drive).x1,
                 7890.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiSunwaveHotelSite.origin.x + local_bounds(sunwave_walk).x1,
                 8086.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiSunwaveHotelSite.origin.x + local_bounds(sunwave_club_walk).x1,
                 8086.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiSunwaveHotelSite.origin.x + local_bounds(sunwave_service).x0,
                 7910.0f, 1e-5f);
    apricot_test::pass("Miandi walks and service lanes meet exact sidewalk edges without road overlap");
}

void replaced_context_blocks_are_empty() {
    const auto context = city::bake_miandi_context();
    for (const std::size_t index : {3u, 4u, 7u, 8u}) {
        REQUIRE(city::miandi_context_block_is_replaced(index));
        const auto centre = city::kMiandiContextBlockCenters[index];
        const Bounds parcel{centre.x - 80.0f, centre.x + 80.0f,
                            centre.z - 75.0f, centre.z + 75.0f};
        for (const auto& part : context)
            REQUIRE_MSG(!overlaps(local_bounds(part), parcel),
                        "nightlife parcel still overlaps context massing",
                        part.name);
    }
    apricot_test::pass("four nightlife parcels replace their context massing exactly once");
}

void nightlife_packages_have_non_solid_neon_and_real_lights() {
    const auto noche = city::bake_miandi_calle_noche();
    const auto prism = city::bake_miandi_prism_works();
    const auto mariposa = city::bake_miandi_mariposa_motel();
    const auto palmera = city::bake_miandi_sunwave_hotel();
    const std::array<const std::vector<city::BuildingPiece>*, 4> packages{
        &noche, &prism, &mariposa, &palmera};
    std::size_t neon_total = 0;
    for (const auto* package : packages) {
        std::size_t package_neon = 0;
        for (const auto& part : *package) {
            if (!part.name || !std::strstr(part.name, "miandi neon")) continue;
            REQUIRE(!part.solid);
            ++package_neon;
            ++neon_total;
        }
        REQUIRE(package_neon >= 3u);
    }
    REQUIRE(neon_total >= 18u);
    REQUIRE(city::kMiandiNightLights.size() == 28u);
    apricot_test::pass("all nightlife packages have non-solid neon and twenty-eight bounded venue lights");
}

void only_far_west_context_massing_survives() {
    std::size_t retained = 0;
    std::size_t retained_solids = 0;
    for (const auto& part : city::kMiandiBuildingParts) {
        if (!city::miandi_keeps_rough_part(part)) continue;
        ++retained;
        retained_solids += part.solid;
        REQUIRE(part.centre.x <= -700.0f);
    }
    REQUIRE(retained == 4u);
    REQUIRE(retained_solids == 2u);
    apricot_test::pass("finished blocks replace rough massing while four far-west context pieces remain");
}

void approved_names_have_complete_oriented_signs() {
    const auto hotels = city::bake_miandi_ocean_drive();
    const auto palmera = city::bake_miandi_sunwave_hotel();
    const auto mirage = city::bake_miandi_prism_works();
    const auto noche = city::bake_miandi_calle_noche();
    const auto check = [&](const auto& parts, const city::BuildingPlan& plan,
                           const city::MiandiVenueIdentity& identity,
                           const char* name, const char* sign, const char* tag,
                           city::Vec2 plane, float y, float size,
                           city::BuildingFinish finish, float yaw) {
        REQUIRE(std::strcmp(plan.name, name) == 0);
        REQUIRE(std::strcmp(identity.name, name) == 0);
        REQUIRE(std::strcmp(identity.sign, sign) == 0);
        // The old alphabet silently omitted M/V/etc. Every non-space letter
        // must generate a stroke and core, not just pass a label-string test.
        for (const char* c = sign; *c; ++c) {
            std::vector<city::BuildingPiece> glyph;
            city::miandi_neon_glyph(glyph, tag, *c, 0, 0, 0, 0, size, finish);
            REQUIRE(*c == ' ' ? glyph.empty() : glyph.size() >= 4u);
        }
        std::vector<city::BuildingPiece> expected;
        city::miandi_neon_facade_name(expected, tag, sign, plane, y, size, finish, yaw);
        std::vector<const city::BuildingPiece*> lettering;
        for (const auto& p : parts) {
            for (const char* old : {"Coral Crown", "Blue Heron", "Sunwave",
                                    "Prism Works", "Sol Social", "Palma Dance"})
                REQUIRE(std::strstr(p.name, old) == nullptr);
            if (std::strcmp(p.name, tag) == 0) lettering.push_back(&p);
        }
        REQUIRE(lettering.size() * 2 == expected.size());
        REQUIRE(lettering.size() > 20u && lettering.size() < 180u);
        for (std::size_t i = 0; i < lettering.size(); ++i) {
            const auto& p = *lettering[i];
            const auto& e = expected[i * 2];
            REQUIRE(!p.solid && p.finish == finish);
            REQUIRE_NEAR(p.centre.x, e.centre.x, 1e-4f);
            REQUIRE_NEAR(p.centre.z, e.centre.z, 1e-4f);
            REQUIRE_NEAR(p.bottom_m, e.bottom_m, 1e-4f);
            REQUIRE_NEAR(p.height_m, e.height_m, 1e-4f);
            REQUIRE_NEAR(p.pitch_deg, e.pitch_deg, 1e-4f);
            REQUIRE_NEAR(p.yaw_deg, yaw, 1e-4f);
        }
    };
    using F = city::BuildingFinish;
    check(hotels, city::kCoralCrownHotelPlan, city::kBellmarIdentity,
          "The Bellmar", "THE BELLMAR", "miandi neon 80s pink Bellmar name lettering",
          {79.18f, -29.f}, 12.18f, 2.3f, F::RedTrim, 0.f);
    check(hotels, city::kBlueHeronHotelPlan, city::kMaravelleIdentity,
          "The Maravelle", "THE MARAVELLE", "miandi neon 80s cyan Maravelle name lettering",
          {79.18f, 28.f}, 8.98f, 2.1f, F::TealDoor, 0.f);
    check(palmera, city::kMiandiSunwaveHotelPlan, city::kPalmeraIdentity,
          "The Palmera", "THE PALMERA", "miandi neon warm-white Palmera lettering",
          {30.66f, -9.f}, 23.13f, 2.15f, F::White, 0.f);
    check(mirage, city::kMiandiPrismWorksPlan, city::kMirageIdentity,
          "Club Mirage", "CLUB MIRAGE", "miandi neon 80s cyan Mirage lettering",
          {15.f, -42.94f}, 5.97f, 1.5f, F::TealDoor, 90.f);
    check(noche, city::kMiandiSolSocialClubPlan, city::kCandelaIdentity,
          "Club Candela", "CLUB CANDELA", "miandi neon amber Candela lettering",
          {-44.f, -50.16f}, 5.43f, 1.35f, F::Yellow, 90.f);
    check(noche, city::kMiandiPalmaDanceHallPlan, city::kTropicoIdentity,
          "Tropico Ballroom", "TROPICO BALLROOM", "miandi neon amber Tropico lettering",
          {2.5f, -50.46f}, 6.36f, 1.5f, F::Yellow, 90.f);
    apricot_test::pass("six approved venue names have complete glyphs on their actual facade planes");
}

}  // namespace

int main() {
    finished_parcels_are_separate_and_clear_the_grid();
    solid_geometry_stays_inside_owned_parcels();
    public_and_service_routes_meet_sidewalk_edges_without_overlap();
    replaced_context_blocks_are_empty();
    nightlife_packages_have_non_solid_neon_and_real_lights();
    only_far_west_context_massing_survives();
    approved_names_have_complete_oriented_signs();
    return apricot_test::done("miandi_integration_tests");
}
