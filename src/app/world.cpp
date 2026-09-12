#include "app/world.h"
#include "city/precipitation_cover.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "city/airport.h"
#include "city/florangia_airport.h"
#include "city/airport_parking_garage.h"
#include "city/halberd_helicopter.h"
#include "city/marina.h"
#include "city/miandi_bayfront.h"
#include "city/miandi_calle_ocho.h"
#include "city/miandi_calle_noche.h"
#include "city/miandi_context.h"
#include "city/miandi_layout.h"
#include "city/miandi_mariposa_motel.h"
#include "city/miandi_ocean_drive.h"
#include "city/miandi_port_sol.h"
#include "city/miandi_presentation.h"
#include "city/miandi_resort_frontage.h"
#include "city/miandi_prism_works.h"
#include "city/miandi_streets.h"
#include "city/miandi_sunwave_hotel.h"
#include "game/boat.h"
#include "city/billboards.h"
#include "city/start_area.h"
#include "city/tacomaco.h"
#include "city/neighborhood_shops.h"
#include "city/pawn_shop.h"
#include "city/gun_store.h"
#include "city/graffiti.h"
#include "city/neighborhood_towers.h"
#include "city/pinatty_infill.h"
#include "city/construction_site.h"
#include "city/construction_neighbor_materials.h"
#include "city/construction_neighbor_equipment.h"
#include "city/construction_street_detail.h"
#include "city/construction_expansion.h"
#include "city/hospital_campus.h"
#include "city/hospital_exterior.h"
#include "city/emergency_stations.h"
#include "city/east_arm_plaza.h"
#include "city/neighborhood_bar.h"
#include "city/loom_cultural.h"
#include "city/north_airbase.h"
#include "city/burgerpiz_asset.h"
#include "city/imported_restaurants.h"
#include "gfx/street_lamp_light.h"
#include "city/miandi_gas_station_asset.h"
#include "city/luxury_neighborhood.h"
#include "city/westmere_streetscape.h"
#include "city/residential_neighborhood.h"
#include "city/tidewater_farm.h"
#include "app/pawn_merchandise_mesh.h"
#include "city/building_access.h"
#include "city/bank_vault_layout.h"
#include "city/interior_streaming.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/log.h"
#include "core/rng.h"
#include "game/aircraft.h"
#include "game/helicopter.h"
#include "gfx/primitives.h"
#include "gfx/texture.h"
#include "gfx/loom_museum_meshes.h"
#include "physics/vehicle.h"

namespace apricot {
namespace {

constexpr std::size_t kStartFinishCount = 12u;
constexpr std::size_t kGraffitiTagCount = city::kGraffitiTagCount;
using StartMaterialSet = std::array<MaterialId, kStartFinishCount>;

struct StartMaterials {
    StartMaterialSet finish{};
    MaterialId canopy_skin = kInvalidId;
    MaterialId pillar_body = kInvalidId;
    MaterialId pump_body = kInvalidId;
    MaterialId pump_control = kInvalidId;
    MaterialId dumpster_body = kInvalidId;
    MaterialId quickbite_brand=kInvalidId, quickbite_menu_interior_a=kInvalidId;
    MaterialId quickbite_menu_interior_b=kInvalidId, quickbite_menu_drive=kInvalidId;
    MaterialId quickbite_tile=kInvalidId;
    MaterialId quickbite_vinyl=kInvalidId, quickbite_floor=kInvalidId, quickbite_steel=kInvalidId;
    MaterialId glass=kInvalidId;
    MaterialId museum_artwork=kInvalidId;
    MeshId museum_amphora=kInvalidId;
    std::array<MeshId,4> museum_paintings{};
    MaterialId halberd_gate_sign=kInvalidId, halberd_hangar_numbers=kInvalidId;
    MaterialId halberd_chain_link=kInvalidId;
    std::array<MeshId,3> halberd_number_cells{};
    MaterialId museum_plaster=kInvalidId, museum_parquet=kInvalidId, museum_coffer=kInvalidId;
    MaterialId museum_plaque_sheet=kInvalidId, museum_panel_sheet=kInvalidId;
    MaterialId museum_label_sheet=kInvalidId, museum_directory=kInvalidId;
    std::array<MeshId,city::kLoomGalleryCount> museum_plaque_cells{};
    std::array<MeshId,city::kLoomGalleryCount> museum_panel_cells{};
    std::array<MeshId,city::kLoomLabelCells> museum_label_cells{};
    MeshId museum_sphere=kInvalidId, museum_cone=kInvalidId, museum_wheel=kInvalidId;
    MeshId museum_skull=kInvalidId, museum_ring=kInvalidId;
    MaterialId store_snacks=kInvalidId,store_drinks=kInvalidId;
    MaterialId airport_paving = kInvalidId;
    MaterialId airport_facade = kInvalidId;
    MaterialId airport_landside_paving = kInvalidId;
    MaterialId airport_directory = kInvalidId;
    MaterialId airport_furnishing = kInvalidId;
    MaterialId billboard_ness = kInvalidId;
    MaterialId billboard_taxi = kInvalidId;
    MaterialId bank_sign = kInvalidId;
    MaterialId repair_sign = kInvalidId;
    MaterialId pawn_sign = kInvalidId;
    MaterialId gun_store_sign = kInvalidId;
    MaterialId pawn_tv=kInvalidId,pawn_radio=kInvalidId,pawn_guitar=kInvalidId;
    MaterialId fire_sign=kInvalidId,police_sign=kInvalidId,bar_sign=kInvalidId;
    MaterialId laundry_sign = kInvalidId;
    MaterialId plaza_galleria_sign = kInvalidId;
    MaterialId plaza_cinema_sign = kInvalidId;
    MaterialId plaza_bookstore_sign = kInvalidId;
    MaterialId plaza_coffee_sign = kInvalidId;
    MaterialId plaza_restaurant_sign = kInvalidId;
    MaterialId bank_floor = kInvalidId;
    MaterialId bank_wood = kInvalidId;
    MaterialId bank_ceiling = kInvalidId;
    MaterialId bank_atm = kInvalidId;
    MaterialId bank_deposits = kInvalidId;
    MaterialId bank_vault_face = kInvalidId;
    MaterialId bank_vault_note = kInvalidId;
    MaterialId bank_vault_keypad = kInvalidId;
    MaterialId marina_timber = kInvalidId;
    MaterialId marina_paint = kInvalidId;
    MaterialId marina_roof = kInvalidId;
    MaterialId marina_sign = kInvalidId;
    std::array<MaterialId, city::kResidentialHouses.size()> residential_wall{};
    std::array<MaterialId, city::kResidentialHouses.size()> residential_roof{};
    MaterialId farm_barn_boards = kInvalidId;
    MaterialId farm_house_clapboard = kInvalidId;
    MaterialId farm_soil = kInvalidId;
    MaterialId farm_drive = kInvalidId;
    MaterialId farm_galvanized = kInvalidId;
    MaterialId construction_fence = kInvalidId;
    MaterialId construction_plywood = kInvalidId;
    MaterialId construction_crane_steel = kInvalidId;
    MaterialId construction_rebar_stock = kInvalidId;
    MaterialId construction_depot_steel = kInvalidId;
    MaterialId construction_depot_concrete = kInvalidId;
    MaterialId construction_equipment_yellow = kInvalidId;
    MaterialId construction_equipment_rubber = kInvalidId;
    MaterialId construction_street_barrier = kInvalidId;
    MaterialId construction_wayfinding_board = kInvalidId;
    MaterialId construction_fence_section = kInvalidId;
    MaterialId construction_crane_section = kInvalidId;
    MaterialId construction_rebar_section = kInvalidId;
    MaterialId construction_depot_section = kInvalidId;
    MaterialId construction_equipment_section = kInvalidId;
    MaterialId construction_barrier_section = kInvalidId;
    MaterialId construction_wayfinding_section = kInvalidId;
    MaterialId hospital_entry_mural = kInvalidId;
    MaterialId hospital_emergency_bay_two = kInvalidId;
    MaterialId hospital_garage_pay_station = kInvalidId;
    MaterialId hospital_healing_art_glass = kInvalidId;
    MaterialId hospital_emergency_shore_power = kInvalidId;
    MaterialId hospital_healing_garden_mosaic = kInvalidId;
    MaterialId hospital_garage_west_entry_control = kInvalidId;
    MaterialId hospital_north_parking_wayfinding = kInvalidId;
    MaterialId westmere_peach_stucco = kInvalidId;
    MaterialId westmere_cream_stucco = kInvalidId;
    MaterialId westmere_roof_tile = kInvalidId;
    MaterialId westmere_limestone = kInvalidId;
    MaterialId westmere_green_metal = kInvalidId;
    MaterialId westmere_pool_tile = kInvalidId;
    MaterialId westmere_tennis_surface = kInvalidId;
    MaterialId westmere_white_slats = kInvalidId;
    MaterialId westmere_tree_bark = kInvalidId;
    MaterialId westmere_foliage = kInvalidId;
    MaterialId westmere_bench_timber = kInvalidId;
    MaterialId westmere_lounger_fabric = kInvalidId;
    MaterialId westmere_entry_sign = kInvalidId;
    std::array<MaterialId, kGraffitiTagCount> graffiti{};
};

static_assert(static_cast<std::size_t>(city::StartFinish::PoolWater) + 1u ==
                  kStartFinishCount,
              "start finish material table is out of sync");

std::size_t finish_index(city::StartFinish finish) {
    return static_cast<std::size_t>(finish);
}

bool load_start_texture(Renderer& renderer, const char* relative_path,
                        MaterialId& out) {
    Texture texture;
    if (!texture.load_file(asset_path(relative_path))) return false;
    out = renderer.add_material(std::move(texture));
    return out != kInvalidId;
}

bool load_start_alpha_texture(Renderer& renderer, const char* relative_path,
                              MaterialId& out) {
    Texture texture;
    if (!texture.load_file(asset_path(relative_path))) return false;
    out = renderer.add_material(std::move(texture), true);
    return out != kInvalidId;
}

// One quad per atlas cell, with inset UVs. Texture upload flips vertically, so
// v counts DOWN from 1: sheet row 0 is the v range nearest 1. This matches the
// painting quads above, which is the reference for the convention. Getting it
// backwards does not fail loudly - every room quietly shows another room's
// graphic. The inset keeps a neighbouring cell out of the bilinear sample at
// the frame edge.
bool add_atlas_cell_quads(Renderer& renderer, int columns, int rows,
                          MeshId* out, std::size_t count) {
    const float du = 1.0f / static_cast<float>(columns);
    const float dv = 1.0f / static_cast<float>(rows);
    for (std::size_t i = 0; i < count; ++i) {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        MeshData quad = make_billboard_quad();
        for (MeshVertex& vertex : quad.vertices) {
            vertex.uv = glm::vec2{static_cast<float>(column) * du + du * 0.004f,
                                  1.0f - static_cast<float>(row + 1) * dv + dv * 0.004f} +
                        vertex.uv * glm::vec2{du * 0.992f, dv * 0.992f};
        }
        out[i] = renderer.add_mesh(quad);
        if (out[i] == kInvalidId) return false;
    }
    return true;
}

// Authored graphics name their atlas cell. Keeping the index in the part name
// is what lets the sim side place a wall label, or a hangar number, without
// ever learning that a texture atlas exists.
int site_atlas_cell(const city::StartPart& part, const char* prefix) {
    if (!part.name) return -1;
    const std::size_t length = std::strlen(prefix);
    if (std::strncmp(part.name, prefix, length) != 0) return -1;
    const char* digits = part.name + length;
    if (*digits == '\0') return -1;
    int cell = 0;
    for (const char* c = digits; *c; ++c) {
        if (*c < '0' || *c > '9') return -1;
        cell = cell * 10 + (*c - '0');
    }
    return cell;
}

bool load_start_materials(Renderer& renderer, StartMaterials& out) {
    out.finish.fill(renderer.white_material());
    out.graffiti.fill(kInvalidId);
    out.residential_wall.fill(kInvalidId);
    out.residential_roof.fill(kInvalidId);
    out.glass = renderer.add_glass_material();
    if (out.glass == kInvalidId) return false;

    if (!load_start_texture(renderer,"textures/world/loom_museum/paintings-atlas.png",out.museum_artwork)) return false;
    for (std::size_t i=0;i<out.museum_paintings.size();++i) {
        auto canvas=make_billboard_quad();
        // Texture upload flips the image vertically. Inset each atlas cell to
        // keep neighboring paintings out of bilinear samples at frame edges.
        for(auto& vertex:canvas.vertices)
            vertex.uv=glm::vec2{.002f+static_cast<float>(i%2)*.5f,
                .502f-static_cast<float>(i/2)*.5f}+vertex.uv*.496f;
        out.museum_paintings[i]=renderer.add_mesh(canvas);
        if(out.museum_paintings[i]==kInvalidId)return false;
    }

    out.museum_amphora=renderer.add_mesh(make_loom_amphora());
    if(out.museum_amphora==kInvalidId)return false;

    // The museum's interior sheets. The plaster, parquet and coffer are
    // near-neutral on purpose: each gallery tints them, so eight rooms get
    // their own colour out of one 256px texture apiece.
    if (!load_start_texture(renderer,"textures/world/loom_museum/gallery-plaster.png",out.museum_plaster) ||
        !load_start_texture(renderer,"textures/world/loom_museum/gallery-parquet.png",out.museum_parquet) ||
        !load_start_texture(renderer,"textures/world/loom_museum/coffer-ceiling.png",out.museum_coffer) ||
        !load_start_texture(renderer,"textures/world/loom_museum/gallery-plaques.png",out.museum_plaque_sheet) ||
        !load_start_texture(renderer,"textures/world/loom_museum/gallery-panels.png",out.museum_panel_sheet) ||
        !load_start_texture(renderer,"textures/world/loom_museum/exhibit-labels.png",out.museum_label_sheet) ||
        !load_start_texture(renderer,"textures/world/loom_museum/museum-directory.png",out.museum_directory))
        return false;
    if (!add_atlas_cell_quads(renderer,4,2,out.museum_plaque_cells.data(),out.museum_plaque_cells.size()) ||
        !add_atlas_cell_quads(renderer,4,2,out.museum_panel_cells.data(),out.museum_panel_cells.size()) ||
        !add_atlas_cell_quads(renderer,4,8,out.museum_label_cells.data(),out.museum_label_cells.size()))
        return false;
    if (!load_start_texture(renderer,"textures/world/halberd/gate-sign.png",out.halberd_gate_sign) ||
        !load_start_texture(renderer,"textures/world/halberd/hangar-numbers.png",out.halberd_hangar_numbers) ||
        !load_start_alpha_texture(renderer,"textures/world/halberd/chain-link.png",
                                  out.halberd_chain_link))
        return false;
    if (!add_atlas_cell_quads(renderer,2,2,out.halberd_number_cells.data(),
                              out.halberd_number_cells.size()))
        return false;

    out.museum_sphere=renderer.add_mesh(make_loom_sphere());
    out.museum_cone=renderer.add_mesh(make_loom_cone());
    out.museum_wheel=renderer.add_mesh(make_loom_spoked_wheel());
    out.museum_skull=renderer.add_mesh(make_loom_skull());
    out.museum_ring=renderer.add_mesh(make_loom_ring());
    if(out.museum_sphere==kInvalidId || out.museum_cone==kInvalidId ||
       out.museum_wheel==kInvalidId || out.museum_skull==kInvalidId ||
       out.museum_ring==kInvalidId) return false;

    Texture asphalt;
    if (!asphalt.make_asphalt(256, 0x48414C4C4F574159ull)) return false;
    const MaterialId asphalt_material = renderer.add_material(std::move(asphalt), false, 0.18f);

    // Match the loose brown aggregate used by the road system's unpaved
    // layer. Crop furrows have their own strongly directional texture; using
    // that on a driveway makes the whole track look planted.
    Texture farm_drive;
    if (!farm_drive.make_noise(128, 8, 4, {0.29f, 0.23f, 0.16f},
                               {0.49f, 0.40f, 0.28f}, 0xD127F4A2ull)) {
        return false;
    }
    out.farm_drive = renderer.add_material(std::move(farm_drive));
    if (out.farm_drive == kInvalidId) return false;

    MaterialId metal = kInvalidId;
    MaterialId concrete = kInvalidId;
    MaterialId brick = kInvalidId;
    MaterialId stucco = kInvalidId;
    if (!load_start_texture(renderer,"textures/world/quickbite/brand-sign-albedo.png",out.quickbite_brand) ||
        !load_start_texture(renderer,"textures/world/quickbite/menu-board-interior-a-v2-albedo.png",out.quickbite_menu_interior_a) ||
        !load_start_texture(renderer,"textures/world/quickbite/menu-board-interior-b-v2-albedo.png",out.quickbite_menu_interior_b) ||
        !load_start_texture(renderer,"textures/world/quickbite/menu-board-drive-through-v2-albedo.png",out.quickbite_menu_drive) ||
        !load_start_texture(renderer,"textures/world/quickbite/cream-tile-albedo.png",out.quickbite_tile) ||
        !load_start_texture(renderer,"textures/world/quickbite/red-vinyl-albedo.png",out.quickbite_vinyl) ||
        !load_start_texture(renderer,"textures/world/quickbite/linoleum-floor-albedo.png",out.quickbite_floor) ||
        !load_start_texture(renderer,"textures/world/quickbite/brushed-stainless-albedo.png",out.quickbite_steel) ||
        !load_start_texture(renderer,"textures/world/gas_station/snacks-shelf-albedo.png",out.store_snacks) ||
        !load_start_texture(renderer,"textures/world/gas_station/chilled-drinks-albedo.png",out.store_drinks) ||
        !load_start_texture(renderer, "textures/world/neighborhood/rooks-auto-repair.png", out.repair_sign) ||
        !load_start_texture(renderer, "textures/world/neighborhood/second-chance-pawn.png", out.pawn_sign) ||
        !load_start_texture(renderer, "textures/world/neighborhood/brassline-arms.png", out.gun_store_sign) ||
        !load_start_texture(renderer,"textures/world/neighborhood/pawn-tv-front.png",out.pawn_tv) ||
        !load_start_texture(renderer,"textures/world/neighborhood/pawn-radio-front.png",out.pawn_radio) ||
        !load_start_texture(renderer,"textures/world/neighborhood/pawn-guitar-soundboard.png",out.pawn_guitar) ||
        !load_start_texture(renderer,"textures/world/neighborhood/fire-station.png",out.fire_sign) ||
        !load_start_texture(renderer,"textures/world/neighborhood/bent-elbow.png",out.bar_sign) ||
        !load_start_texture(renderer,"textures/world/neighborhood/police-station.png",out.police_sign) ||
        !load_start_texture(renderer, "textures/world/neighborhood/spin-cycle.png", out.laundry_sign) ||
        !load_start_texture(renderer, "textures/city/east_arm_plaza/galleria-sign-neon-v2.png", out.plaza_galleria_sign) ||
        !load_start_texture(renderer, "textures/city/east_arm_plaza/cinema-sign-neon-v2.png", out.plaza_cinema_sign) ||
        !load_start_texture(renderer, "textures/city/east_arm_plaza/bookstore-sign-neon-v2.png", out.plaza_bookstore_sign) ||
        !load_start_texture(renderer, "textures/city/east_arm_plaza/coffee-sign-neon-v2.png", out.plaza_coffee_sign) ||
        !load_start_texture(renderer, "textures/city/east_arm_plaza/restaurant-sign-neon-v2.png", out.plaza_restaurant_sign) ||
        !load_start_texture(
            renderer, "textures/world/gas_station/painted-metal-albedo.png",
            metal) ||
        !load_start_texture(
            renderer, "textures/world/gas_station/concrete-albedo.png",
            concrete) ||
        !load_start_texture(renderer,
                            "textures/world/gas_station/brick-albedo.png",
                            brick) ||
        !load_start_texture(renderer,
                            "textures/world/gas_station/stucco-albedo.png",
                            stucco) ||
        !load_start_texture(
            renderer, "textures/world/gas_station/canopy-panel-albedo.png",
            out.canopy_skin) ||
        !load_start_texture(
            renderer,
            "textures/world/gas_station/pillar-painted-steel-albedo.png",
            out.pillar_body) ||
        !load_start_texture(
            renderer, "textures/world/gas_station/fuel-pump-panel-albedo.png",
            out.pump_body) ||
        !load_start_texture(
            renderer, "textures/world/gas_station/fuel-pump-control-panel.png",
            out.pump_control) ||
        !load_start_texture(
            renderer, "textures/world/gas_station/dumpster-albedo.png",
            out.dumpster_body) ||
        !load_start_texture(
            renderer, "textures/world/airport/runway-apron-albedo.png",
            out.airport_paving) ||
        !load_start_texture(
            renderer, "textures/world/airport/terminal-facade-albedo.png",
            out.airport_facade) ||
        !load_start_texture(
            renderer, "textures/world/airport/landside-paving-albedo.png",
            out.airport_landside_paving) ||
        !load_start_texture(renderer,
                            "textures/world/airport/terminal-directory-generated.png",
                            out.airport_directory) ||
        !load_start_texture(renderer, "textures/world/airport/furnishing-steel-generated.png",out.airport_furnishing) ||
        !load_start_texture(
            renderer, "textures/world/billboards/ness-and-ness.jpg",
            out.billboard_ness) ||
        !load_start_texture(
            renderer, "textures/world/billboards/pinnaty-taxi.png",
            out.billboard_taxi) ||
        !load_start_texture(
            renderer, "textures/world/bank/pinatty-savings-sign.png",
            out.bank_sign) ||
        !load_start_texture(renderer, "textures/world/bank/terrazzo-floor.png",
                            out.bank_floor) ||
        !load_start_texture(renderer, "textures/world/bank/walnut-panel.png",
                            out.bank_wood) ||
        !load_start_texture(renderer, "textures/world/bank/acoustic-ceiling.png",
                            out.bank_ceiling) ||
        !load_start_texture(renderer, "textures/world/bank/atm-face.png",
                            out.bank_atm) ||
        !load_start_texture(renderer, "textures/world/bank/deposit-boxes.png",
                            out.bank_deposits) ||
        !load_start_texture(renderer, "textures/world/bank/vault-door-face.png",
                            out.bank_vault_face) ||
        !load_start_texture(renderer, "textures/world/bank/vault-office-note.png",
                            out.bank_vault_note) ||
        !load_start_texture(renderer, "textures/world/bank/vault-keypad.png",
                            out.bank_vault_keypad)) {
        AP_ERROR("world: authored district material set failed to load");
        return false;
    }

    if (!load_start_texture(renderer,"textures/world/marina/weathered-timber.png",out.marina_timber) ||
        !load_start_texture(renderer,"textures/world/marina/painted-boards.png",out.marina_paint) ||
        !load_start_texture(renderer,"textures/world/marina/corrugated-roof.png",out.marina_roof) ||
        !load_start_texture(renderer,"textures/world/marina/boatworks-sign.png",out.marina_sign) ||
        !load_start_texture(renderer,"textures/world/farm/weathered-red-barn-boards-albedo.png",out.farm_barn_boards) ||
        !load_start_texture(renderer,"textures/world/farm/farmhouse-clapboard-albedo.png",out.farm_house_clapboard) ||
        !load_start_texture(renderer,"textures/world/farm/tilled-furrow-soil-albedo.png",out.farm_soil) ||
        !load_start_texture(renderer,"textures/world/farm/aged-galvanized-roof-albedo.png",out.farm_galvanized) ||
        !load_start_texture(renderer,"textures/world/construction/safety-mesh-generated.png",out.construction_fence) ||
        !load_start_texture(renderer,"textures/world/construction/plywood-boards-generated.png",out.construction_plywood) ||
        !load_start_texture(renderer,"textures/world/construction/crane-safety-steel-generated.png",out.construction_crane_steel) ||
        !load_start_texture(renderer,"textures/world/construction/rebar-stock-generated.png",out.construction_rebar_stock) ||
        !load_start_texture(renderer,"textures/world/construction/materials-depot-steel-generated.png",out.construction_depot_steel) ||
        !load_start_texture(renderer,"textures/world/construction/materials-depot-concrete-generated.png",out.construction_depot_concrete) ||
        !load_start_texture(renderer,"textures/world/construction/equipment-yard-yellow-generated.png",out.construction_equipment_yellow) ||
        !load_start_texture(renderer,"textures/world/construction/equipment-yard-rubber-generated.png",out.construction_equipment_rubber) ||
        !load_start_texture(renderer,"textures/world/construction/street-barrier-generated.png",out.construction_street_barrier) ||
        !load_start_texture(renderer,"textures/world/construction/street-wayfinding-board-generated.png",out.construction_wayfinding_board) ||
        !load_start_texture(renderer,"textures/world/construction/fence-section-generated.png",out.construction_fence_section) ||
        !load_start_texture(renderer,"textures/world/construction/crane-section-generated.png",out.construction_crane_section) ||
        !load_start_texture(renderer,"textures/world/construction/rebar-section-generated.png",out.construction_rebar_section) ||
        !load_start_texture(renderer,"textures/world/construction/depot-facade-section-generated.png",out.construction_depot_section) ||
        !load_start_texture(renderer,"textures/world/construction/equipment-body-section-generated.png",out.construction_equipment_section) ||
        !load_start_texture(renderer,"textures/world/construction/barrier-face-section-generated.png",out.construction_barrier_section) ||
        !load_start_texture(renderer,"textures/world/construction/wayfinding-board-section-generated.png",out.construction_wayfinding_section) ||
        !load_start_texture(renderer,"textures/world/hospital/facade/pinatty-main-entry-mural-face-generated.png",out.hospital_entry_mural) ||
        !load_start_texture(renderer,"textures/world/hospital/emergency/ambulance-bay-2-marker-face-generated.png",out.hospital_emergency_bay_two) ||
        !load_start_texture(renderer,"textures/world/hospital/garage/entry-pay-station-control-face.png",out.hospital_garage_pay_station) ||
        !load_start_texture(renderer,"textures/world/hospital/facade/polish/northwest-healing-art-glass-panel-generated.png",out.hospital_healing_art_glass) ||
        !load_start_texture(renderer,"textures/world/hospital/arrivals/emergency-shore-power-cabinet-face-generated.png",out.hospital_emergency_shore_power) ||
        !load_start_texture(renderer,"textures/world/hospital/grounds/healing-garden-botanical-mosaic-face-generated.png",out.hospital_healing_garden_mosaic) ||
        !load_start_texture(renderer,"textures/world/hospital/garage/polish/west-entry-control-face-generated.png",out.hospital_garage_west_entry_control) ||
        !load_start_texture(renderer,"textures/world/hospital/parking/north-visitor-wayfinding-generated.png",out.hospital_north_parking_wayfinding) ||
        !load_start_texture(renderer,"textures/world/westmere/peach-stucco-generated.png",out.westmere_peach_stucco) ||
        !load_start_texture(renderer,"textures/world/westmere/cream-stucco-generated.png",out.westmere_cream_stucco) ||
        !load_start_texture(renderer,"textures/world/westmere/burgundy-roof-tile-generated.png",out.westmere_roof_tile) ||
        !load_start_texture(renderer,"textures/world/westmere/limestone-wall-generated.png",out.westmere_limestone) ||
        !load_start_texture(renderer,"textures/world/westmere/green-painted-metal-generated.png",out.westmere_green_metal) ||
        !load_start_texture(renderer,"textures/world/westmere/pool-waterline-tile-generated.png",out.westmere_pool_tile) ||
        !load_start_texture(renderer,"textures/world/westmere/tennis-court-surface-generated.png",out.westmere_tennis_surface) ||
        !load_start_texture(renderer,"textures/world/westmere/white-painted-slats-generated.png",out.westmere_white_slats) ||
        !load_start_texture(renderer,"textures/world/westmere/tree-bark-generated.png",out.westmere_tree_bark) ||
        !load_start_texture(renderer,"textures/world/westmere/hedge-foliage-generated.png",out.westmere_foliage) ||
        !load_start_texture(renderer,"textures/world/westmere/bench-hardwood-generated.png",out.westmere_bench_timber) ||
        !load_start_texture(renderer,"textures/world/westmere/lounger-fabric-generated.png",out.westmere_lounger_fabric) ||
        !load_start_texture(renderer,"textures/world/westmere/westmere-entry-sign-face-generated.png",out.westmere_entry_sign)) return false;

    constexpr std::array<const char*, kGraffitiTagCount> kGraffitiPaths{{
        "textures/world/graffiti/arrow-tag.png",
        "textures/world/graffiti/crown-tag.png",
        "textures/world/graffiti/starburst-tag.png",
        "textures/world/graffiti/monogram-tag.png",
        "textures/world/graffiti/ghost-tag.png",
        "textures/world/graffiti/bolt-tag.png",
        "textures/world/graffiti/serpent-tag.png",
        "textures/world/graffiti/flower-tag.png",
        "textures/world/graffiti/mask-tag.png",
        "textures/world/graffiti/halo-tag.png",
    }};
    for (std::size_t i = 0; i < kGraffitiPaths.size(); ++i) {
        if (!load_start_alpha_texture(renderer, kGraffitiPaths[i], out.graffiti[i]))
            return false;
    }

    constexpr const char* kResidentialTextureRoot =
        "models/world/psx_house_textures/";
    for(std::size_t i=0;i<city::kResidentialTextureStyles.size();++i) {
        if(!city::residential_has_imported_texture(i))continue;
        const auto& style=city::kResidentialTextureStyles[i];
        const std::string wall=std::string(kResidentialTextureRoot)+style.wall_texture;
        const std::string roof=std::string(kResidentialTextureRoot)+style.roof_texture;
        if(!load_start_texture(renderer,wall.c_str(),out.residential_wall[i]) ||
           !load_start_texture(renderer,roof.c_str(),out.residential_roof[i])) {
            AP_ERROR("world: residential PSX texture set failed to load");
            return false;
        }
    }

    using city::StartFinish;
    out.finish[finish_index(StartFinish::Asphalt)] = asphalt_material;
    out.finish[finish_index(StartFinish::Concrete)] = concrete;
    out.finish[finish_index(StartFinish::WarmWall)] = stucco;
    out.finish[finish_index(StartFinish::Brick)] = brick;
    out.finish[finish_index(StartFinish::RedTrim)] = metal;
    out.finish[finish_index(StartFinish::DarkRoof)] = metal;
    out.finish[finish_index(StartFinish::TealDoor)] = metal;
    out.finish[finish_index(StartFinish::Steel)] = metal;
    out.finish[finish_index(StartFinish::White)] = metal;
    out.finish[finish_index(StartFinish::Yellow)] = metal;
    return true;
}

enum class StartShape { Box, FlatDecal, BillboardFace, RoundedBox, Cylinder };
enum class SiteMaterialStyle {
    Plain,
    Marina,
    TexturedBuilding,
    Farm,
    GasStation,
    Quickbite,
    Airport,
    Billboard,
    Construction,
    Westmere,
};

bool part_name_has(const city::StartPart& part, const char* text) {
    return part.name != nullptr && std::strstr(part.name, text) != nullptr;
}

bool part_name_is(const city::StartPart& part, const char* text) {
    return part.name != nullptr && std::strcmp(part.name, text) == 0;
}

bool is_pump_body(const city::StartPart& part) {
    return part_name_is(part, "pump 1") || part_name_is(part, "pump 2") ||
           part_name_is(part, "pump 3") || part_name_is(part, "pump 4");
}

bool is_pump_control_face(const city::StartPart& part) {
    return part_name_has(part, "pump ") && part_name_has(part, " face");
}

bool is_canopy_skin(const city::StartPart& part) {
    return part_name_is(part, "canopy roof") ||
           (part_name_has(part, "canopy ") && part_name_has(part, " fascia"));
}

bool is_pillar_body(const city::StartPart& part) {
    return part_name_is(part, "canopy column nw") ||
           part_name_is(part, "canopy column ne") ||
           part_name_is(part, "canopy column sw") ||
           part_name_is(part, "canopy column se");
}

bool is_dumpster_body(const city::StartPart& part) {
    return part_name_is(part, "east service bin");
}

bool is_billboard_face(const city::StartPart& part) {
    return part_name_has(part, "billboard face ");
}

bool is_taxi_billboard_face(const city::StartPart& part) {
    return part_name_is(part, "billboard face pinnaty taxi");
}

bool is_bank_sign_face(const city::StartPart& part) {
    return part_name_is(part, "bank sign face") ||
           part_name_is(part, "bank interior sign face");
}

bool is_bank_atm_face(const city::StartPart& part) {
    return part_name_has(part, "bank atm face");
}

bool is_bank_wood(const city::StartPart& part) {
    return part_name_has(part, "bank wainscot") ||
           part_name_is(part, "bank teller counter") ||
           part_name_is(part, "bank writing desk") ||
           part_name_is(part, "bank manager desk") ||
           part_name_is(part, "bank manager credenza") ||
           part_name_is(part, "bank vault table");
}

bool is_bank_deposits(const city::StartPart& part) {
    return part_name_is(part, "bank vault deposit wall") ||
           part_name_is(part, "bank vault cabinet east");
}

bool is_bank_keypad(const city::StartPart& part) {
    return part_name_is(part, "bank vault keypad face") ||
           part_name_is(part, "bank vault inside keypad face");
}

StartShape part_shape(const city::StartPart& part, SiteMaterialStyle style) {
    if (style == SiteMaterialStyle::Westmere &&
        part_name_is(part, "westmere streetscape entry sign face"))
        return StartShape::BillboardFace;
    if (style == SiteMaterialStyle::Westmere &&
        part_name_is(part, "westmere tennis surface"))
        return StartShape::FlatDecal;
    if (style==SiteMaterialStyle::GasStation && part_name_has(part,"store interior ") &&
        part_name_has(part,"sign face")) return StartShape::BillboardFace;
    if (style==SiteMaterialStyle::Quickbite && part_name_has(part,"quickbite ") && part_name_has(part,"sign face"))
        return StartShape::BillboardFace;
    if (style==SiteMaterialStyle::Quickbite && !part.solid &&
        part.height_m<=.05f && part.pitch_deg==0.f && part.roll_deg==0.f)
        return StartShape::FlatDecal;
    if (style==SiteMaterialStyle::Marina) {
        if (part_name_has(part,"piling") || part_name_has(part,"drum") ||
            part_name_has(part,"fender") || part_name_has(part,"light pole") ||
            part_name_has(part,"supply tin")) return StartShape::Cylinder;
    }
    if (style == SiteMaterialStyle::Construction &&
        (part_name_has(part, "construction traffic barrel") ||
         part_name_has(part, "construction pipe bundle"))) {
        return StartShape::Cylinder;
    }
    if (style == SiteMaterialStyle::Construction &&
        part_name_is(part, "hospital mobility curved arrival apron")) {
        return StartShape::Cylinder;
    }
    if (part_name_is(part,"store interior ATM face")) return StartShape::BillboardFace;
    if (part_name_has(part, "shop sign face") || part_name_has(part,"station sign face") || part_name_is(part,"pawn television front face") || part_name_is(part,"pawn radio front face")) return StartShape::BillboardFace;
    if (part_name_has(part, "tire stack") || part_name_has(part, "washer drum"))
        return StartShape::Cylinder;
    if (part_name_has(part, "east arm plaza palm trunk") ||
        part_name_has(part, "east arm plaza lamp pole"))
        return StartShape::Cylinder;
    if (part_name_has(part, "east arm plaza palm crown") ||
        part_name_has(part, "east arm plaza lamp head") ||
        part_name_has(part, "coffee shop patio umbrella"))
        return StartShape::RoundedBox;
    if (style == SiteMaterialStyle::TexturedBuilding) {
        if (part_name_is(part, "bank office access note")) return StartShape::FlatDecal;
        if (is_bank_keypad(part)) return StartShape::BillboardFace;
        if (part_name_has(part, "bank vault hinge")) return StartShape::Cylinder;
    }
    if ((style == SiteMaterialStyle::Billboard && is_billboard_face(part)) ||
        (style == SiteMaterialStyle::TexturedBuilding &&
         (is_bank_sign_face(part) || is_bank_atm_face(part)))) {
        return StartShape::BillboardFace;
    }
    if (style==SiteMaterialStyle::Airport && part_name_is(part,"terminal directory sign face"))
        return StartShape::BillboardFace;
    // Airport paint is authored as a thin BuildingPiece so its visible plane
    // can use the same bottom + height convention as every other fixture. It
    // must not use the box mesh, though: even a 2 cm box grows bright vertical
    // faces under headlights and reads as a physical speed bump.
    if (style == SiteMaterialStyle::Airport && !part.solid &&
        part.height_m <= 0.05f) {
        return StartShape::FlatDecal;
    }
    if ((style == SiteMaterialStyle::Airport &&
         part_name_has(part, "airliner ") &&
         (part_name_has(part, "fuselage") ||
          part_name_has(part, "cockpit") ||
          part_name_has(part, "engine"))) ||
        (style == SiteMaterialStyle::Airport &&
         part_name_has(part, "baggage tug")) ||
        part_name_has(part, "wash blower") ||
        (part_name_has(part, "wash vacuum") &&
         part_name_has(part, "body")) ||
        part_name_has(part, "air water") ||
        part_name_has(part, "rooftop hvac")) {
        return StartShape::RoundedBox;
    }
    if (part_name_is(part, "garden tree crown") || part_name_is(part, "luxury tree crown") ||
        part_name_is(part, "westmere court shrub") ||
        part_name_has(part, "westmere streetscape ornamental shrub") ||
        part_name_has(part, "westmere streetscape palm crown"))
        return StartShape::RoundedBox;
    // Halberd's tanks and masts are cylinders; its gate board and hangar
    // numbers are image planes.
    if (part_name_has(part, "halberd round")) return StartShape::Cylinder;
    if (part_name_is(part, "halberd gate sign") ||
        part_name_has(part, "halberd hangar number"))
        return StartShape::BillboardFace;

    // Museum graphics are image planes, and its floor inlays are paint. A 4 mm
    // box for an inlay grows lit vertical edges under the gallery pendants and
    // reads as a step, which is the same trap the airport paint fell into.
    if (part_name_has(part, "museum plaque ") || part_name_has(part, "museum panel ") ||
        part_name_has(part, "museum label ") || part_name_is(part, "museum directory") ||
        part_name_has(part, "museum design study board") ||
        part_name_has(part, "museum painting "))
        return StartShape::BillboardFace;
    if (part_name_is(part, "museum gallery parquet") ||
        part_name_has(part, "museum hall floor ") ||
        part_name_is(part, "museum science dial") ||
        part_name_is(part, "museum space rocket pad") ||
        part_name_has(part, "museum history model street") ||
        part_name_is(part, "museum history model route") ||
        part_name_is(part, "museum history model water") ||
        part_name_is(part, "museum planet arc"))
        return StartShape::FlatDecal;
    if (part_name_has(part,"museum round column") || part_name_has(part,"museum exhibit round") ||
        part_name_is(part, "garden tree trunk") || part_name_has(part, "canopy column") ||
        part_name_has(part, "shelter column") ||
        part_name_has(part, "light pole") ||
        part_name_has(part, "beacon") ||
        part_name_is(part, "farm water tank") ||
        part_name_has(part, "canopy drain") ||
        part_name_has(part, "bollard") ||
        part_name_has(part, "price sign pole") ||
        part_name_is(part, "hospital grounds tree trunk") ||
        part_name_is(part, "luxury tree trunk") ||
        part_name_is(part, "westmere streetscape palm trunk") ||
        part_name_is(part, "luxury fountain column") ||
        part_name_is(part, "luxury fountain bowl") ||
        part_name_has(part, "luxury pool ladder") ||
        part_name_has(part, "wash brush") ||
        (part_name_has(part, "wash vacuum") &&
         part_name_has(part, "hose")) ||
        (part_name_has(part, "pump ") &&
         (part_name_has(part, "hose") || part_name_has(part, "nozzle"))) ||
        part_name_has(part, "air water hose") ||
        part_name_has(part, "rooftop vent")) {
        return StartShape::Cylinder;
    }
    return StartShape::Box;
}

MeshId part_mesh(const city::StartPart& part, SiteMaterialStyle style,
                 MeshId box, MeshId flat_decal, MeshId billboard_face,
                 MeshId rounded_box, MeshId cylinder) {
    switch (part_shape(part, style)) {
        case StartShape::FlatDecal: return flat_decal;
        case StartShape::BillboardFace: return billboard_face;
        case StartShape::RoundedBox: return rounded_box;
        case StartShape::Cylinder: return cylinder;
        case StartShape::Box: return box;
    }
    return box;
}

bool is_airport_paving(const city::StartPart& part) {
    const bool runway_surface =
        part_name_has(part, "runway ") &&
        (part_name_has(part, "09-27") || part_name_has(part, "08-26") ||
         part_name_has(part, "shoulder"));
    return runway_surface ||
           part_name_has(part, "airport apron") ||
           part_name_has(part, "passenger apron") ||
           part_name_has(part, "maintenance apron") ||
           part_name_has(part, "taxiway alpha") ||
           part_name_has(part, "taxiway bravo") ||
           part_name_has(part, "dropoff road") ||
           part_name_has(part, "public parking") ||
           part_name_has(part, "airport access road") ||
           (part_name_has(part,"terminal parking") && part.finish==city::StartFinish::Asphalt) ||
           part_name_has(part, "hangar apron") ||
           part_name_is(part, "airport fire response apron") ||
           part_name_is(part, "airport hotel lot") ||
           part_name_is(part, "rental car lot") ||
           part_name_is(part, "air cargo yard");
}

bool is_airport_landside_paving(const city::StartPart& part) {
    return part_name_is(part,"airport frontage pedestrian connector") ||
           part_name_has(part,"terminal parking walk") ||
           part_name_has(part,"airport taxi pedestrian") ||
           part_name_has(part,"airport taxi shelter approach") ||
           (part_name_has(part,"airport hotel ") && (part_name_has(part,"walk") || part_name_has(part,"approach"))) ||
           part_name_is(part,"rental entrance walk") ||
           part_name_has(part, "terminal pedestrian plaza") ||
           part_name_has(part, "terminal splitter island") ||
           part_name_has(part, "terminal parking island") ||
           part_name_has(part, "terminal pedestrian connector") ||
           part_name_is(part, "airport hotel courtyard") ||
           part_name_is(part, "airport hotel service court") ||
           part_name_is(part, "rental forecourt") ||
           part_name_is(part, "airport security lane island");
}

bool is_airport_facade(const city::StartPart& part) {
    return part_name_has(part, "terminal facade") ||
           part_name_has(part, "concourse ") ||
           part_name_has(part, "control tower cab") ||
           part_name_has(part, "airport hotel facade") ||
           part_name_has(part, "rental centre glass") ||
           part_name_has(part, "air cargo office glass") ||
           part_name_is(part, "airport security window");
}

bool is_building_plot_pavement(const city::StartPart& part) {
    return part.finish == city::StartFinish::Asphalt &&
           part_name_has(part, " lot") && part.bottom_m >= 0.0f &&
           part.height_m > 0.0f;
}

// Append `src` into `dst`, shifting its indices. Local rather than promoted
// into gfx/primitives.h because composing boxes into a prop is a modelling
// convenience for THIS file, and primitives.h earns its place by being the
// thing headless suites can exercise.
void append(MeshData& dst, const MeshData& src, glm::vec3 offset,
            glm::vec3 scale) {
    const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + src.vertices.size());
    for (const MeshVertex& v : src.vertices) {
        MeshVertex out = v;
        out.position = v.position * scale + offset;
        // Non-uniform scale skews a normal; renormalising the componentwise
        // divide is the cheap correct form. Skipping it makes a squat rock
        // light like a tall one, which reads as "the lighting is a bit off".
        out.normal = glm::normalize(v.normal / scale);
        dst.vertices.push_back(out);
        dst.bounds.expand(out.position);
    }
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (const uint32_t i : src.indices) dst.indices.push_back(base + i);
}

// One broad, tapered palm frond. It is double-sided because the world renderer
// culls back faces and a leaf seen from below still has to exist. Fronds are
// deliberately cheap: four points per face give the palm a readable crown at
// driving distance without turning every scattered tree into a tiny model.
void append_palm_frond(MeshData& dst, glm::vec3 root, glm::vec3 tip,
                       float half_width) {
    glm::vec3 radial{tip.x - root.x, 0.0f, tip.z - root.z};
    const float length = glm::length(radial);
    if (length <= 0.001f) return;
    radial /= length;
    const glm::vec3 side{-radial.z * half_width, 0.0f,
                         radial.x * half_width};
    const glm::vec3 middle = glm::mix(root, tip, 0.56f) +
                             glm::vec3{0.0f, 0.16f, 0.0f};
    const glm::vec3 points[4] = {root, middle + side, tip, middle - side};
    glm::vec3 normal = glm::normalize(
        glm::cross(points[1] - points[0], points[2] - points[0]));
    if (normal.y < 0.0f) normal = -normal;
    constexpr glm::vec4 solid{1.0f, 0.0f, 0.0f, 0.0f};
    constexpr glm::vec2 uv[4] = {{0.5f, 0.0f}, {0.0f, 0.56f},
                                 {0.5f, 1.0f}, {1.0f, 0.56f}};
    const uint32_t top = static_cast<uint32_t>(dst.vertices.size());
    for (int i = 0; i < 4; ++i) {
        dst.vertices.push_back({points[i], normal, uv[i], solid});
        dst.bounds.expand(points[i]);
    }
    dst.indices.insert(dst.indices.end(),
                       {top, top + 1u, top + 2u,
                        top, top + 2u, top + 3u});
    const uint32_t bottom = static_cast<uint32_t>(dst.vertices.size());
    for (int i = 0; i < 4; ++i)
        dst.vertices.push_back({points[i], -normal, uv[i], solid});
    dst.indices.insert(dst.indices.end(),
                       {bottom, bottom + 2u, bottom + 1u,
                        bottom, bottom + 3u, bottom + 2u});
}

MeshData make_palm_tree(uint8_t variant) {
    const PropDims& d = prop_dims(PropKind::Tree);
    const MeshData unit = make_box(glm::vec3{0.5f});
    MeshData m;

    const float trunk_h = d.height * 0.82f;
    const float segment_h = trunk_h * 0.25f;
    const float bend_x = (variant & 1u ? 1.0f : -1.0f) * d.radius * 0.12f;
    const float bend_z = (variant & 2u ? 1.0f : -1.0f) * d.radius * 0.09f;
    for (int segment = 0; segment < 4; ++segment) {
        const float t = (static_cast<float>(segment) + 0.5f) * 0.25f;
        const float taper = 1.0f - 0.11f * static_cast<float>(segment);
        append(m, unit,
               {bend_x * t, segment_h * (static_cast<float>(segment) + 0.5f),
                bend_z * t},
               {d.radius * 0.18f * taper, segment_h + 0.04f,
                d.radius * 0.18f * taper});
    }

    const glm::vec3 crown{bend_x, trunk_h + 0.10f, bend_z};
    append(m, unit, crown, {0.48f, 0.34f, 0.48f});
    constexpr float kTwoPiLocal = 6.28318530718f;
    const float turn = 0.31f * static_cast<float>(variant);
    for (int leaf = 0; leaf < 8; ++leaf) {
        const float angle = turn + kTwoPiLocal * static_cast<float>(leaf) / 8.0f;
        const float reach = d.radius * (0.78f + 0.035f *
            static_cast<float>((leaf + variant) % 3));
        const float drop = d.height * (0.065f + 0.025f *
            static_cast<float>((leaf + 2 * variant) % 3));
        append_palm_frond(m, crown + glm::vec3{0.0f, 0.13f, 0.0f},
                          crown + glm::vec3{std::cos(angle) * reach, -drop,
                                            std::sin(angle) * reach},
                          d.radius * 0.14f);
    }
    // Two upright new leaves stop the crown reading as a flat umbrella.
    append_palm_frond(m, crown + glm::vec3{0.0f, 0.12f, 0.0f},
                      crown + glm::vec3{0.36f, d.height * 0.12f, 0.18f},
                      d.radius * 0.09f);
    append_palm_frond(m, crown + glm::vec3{0.0f, 0.12f, 0.0f},
                      crown + glm::vec3{-0.28f, d.height * 0.10f, -0.30f},
                      d.radius * 0.09f);
    return m;
}

// A tree, from a trunk and two canopy blocks.
//
// THE MODEL IS A STAND-IN AND THE PLACEMENT IS NOT. Where trees go, how dense
// they are, what they stand on and how they cull is terrain/scatter.cpp and is
// real. What a tree LOOKS like is still three boxes: the static model path now
// serves authored vehicles, but replacing terrain props is separate content
// work. Sized from prop_dims(PropKind::Tree) so the drawn thing matches the
// bounds it is culled by; a model larger than its bounds pops at the frustum.
MeshData make_tree(uint8_t variant) {
    if (is_palm_tree_variant(variant))
        return make_palm_tree(
            static_cast<uint8_t>(variant - kPalmTreeVariantBase));

    const PropDims& d = prop_dims(PropKind::Tree);
    const float lean = 0.85f + 0.10f * static_cast<float>(variant);
    const float bushy = 0.80f + 0.14f * static_cast<float>(variant % 3u);

    MeshData m;
    const MeshData unit = make_box(glm::vec3{0.5f});

    const float trunk_h = d.height * 0.42f * lean;
    append(m, unit, glm::vec3{0.0f, trunk_h * 0.5f, 0.0f},
           glm::vec3{d.radius * 0.30f, trunk_h, d.radius * 0.30f});

    const float lower_h = d.height * 0.34f;
    append(m, unit, glm::vec3{0.0f, trunk_h + lower_h * 0.5f, 0.0f},
           glm::vec3{d.radius * 1.85f * bushy, lower_h,
                     d.radius * 1.85f * bushy});

    const float upper_h = d.height * 0.30f * lean;
    append(m, unit,
           glm::vec3{0.0f, trunk_h + lower_h + upper_h * 0.45f, 0.0f},
           glm::vec3{d.radius * 1.15f * bushy, upper_h,
                     d.radius * 1.15f * bushy});
    return m;
}

MeshData make_rock(uint8_t variant) {
    const PropDims& d = prop_dims(PropKind::Rock);
    const float squat[3] = {1.0f, 0.72f, 1.35f};
    const float wide[3] = {1.0f, 1.30f, 0.80f};
    const std::size_t i = variant % 3u;

    MeshData m;
    append(m, make_box(glm::vec3{0.5f}),
           glm::vec3{0.0f, d.height * squat[i] * 0.5f, 0.0f},
           glm::vec3{d.radius * 2.0f * wide[i], d.height * squat[i],
                     d.radius * 2.0f * wide[i]});
    return m;
}

glm::vec4 start_finish_tint(city::StartFinish finish) {
    using city::StartFinish;
    switch (finish) {
        case StartFinish::Asphalt:   return {0.13f, 0.15f, 0.17f, 1.0f};
        case StartFinish::Concrete:  return {0.72f, 0.72f, 0.68f, 1.0f};
        case StartFinish::WarmWall:  return {0.82f, 0.69f, 0.50f, 1.0f};
        case StartFinish::Brick:     return {0.42f, 0.16f, 0.11f, 1.0f};
        case StartFinish::RedTrim:   return {0.78f, 0.08f, 0.055f, 1.0f};
        case StartFinish::DarkRoof:  return {0.12f, 0.10f, 0.095f, 1.0f};
        case StartFinish::Glass:     return {0.07f, 0.23f, 0.28f, 1.0f};
        case StartFinish::TealDoor:  return {0.05f, 0.34f, 0.33f, 1.0f};
        case StartFinish::Steel:     return {0.42f, 0.46f, 0.48f, 1.0f};
        case StartFinish::White:     return {0.92f, 0.90f, 0.82f, 1.0f};
        case StartFinish::Yellow:    return {0.95f, 0.67f, 0.08f, 1.0f};
        case StartFinish::PoolWater: return {0.05f, 0.44f, 0.66f, 1.0f};
    }
    return glm::vec4{1.0f};
}

glm::vec3 part_world_centre(const city::StartSite& site,
                            const city::StartPart& part) {
    return {
        site.origin.x + site.cos_yaw * part.centre.x +
            site.sin_yaw * part.centre.z,
        site.ground_m + part.bottom_m + part.height_m * 0.5f,
        site.origin.z - site.sin_yaw * part.centre.x +
            site.cos_yaw * part.centre.z,
    };
}

Transform part_transform(const city::StartSite& site,
                         const city::StartPart& part) {
    Transform t;
    t.position = part_world_centre(site, part);
    const glm::quat site_rotation =
        glm::angleAxis(std::atan2(site.sin_yaw, site.cos_yaw),
                       glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat local_rotation =
        glm::quat(glm::radians(glm::vec3{part.pitch_deg, part.yaw_deg,
                                         part.roll_deg}));
    t.rotation = site_rotation * local_rotation;
    t.scale = {part.width_m, part.height_m, part.depth_m};
    return t;
}

void append_graffiti(Scene& scene, const StartMaterials& materials,
                     MeshId billboard_face, const AABB& unit_bounds,
                     std::vector<NodeId>& nodes) {
    for (const city::GraffitiPlacement& tag : city::kGraffitiPlacements) {
        Renderable r;
        r.mesh = billboard_face;
        r.material = materials.graffiti[tag.tag_index];
        r.uv_scale = {1.0f, 1.0f};
        Transform t;
        t.position = {
            tag.site->origin.x + tag.site->cos_yaw * tag.local_centre.x +
                tag.site->sin_yaw * tag.local_centre.z,
            tag.site->ground_m + tag.centre_height_m,
            tag.site->origin.z - tag.site->sin_yaw * tag.local_centre.x +
                tag.site->cos_yaw * tag.local_centre.z,
        };
        const float site_yaw = std::atan2(tag.site->sin_yaw, tag.site->cos_yaw);
        t.rotation = glm::angleAxis(site_yaw + glm::radians(tag.local_yaw_deg),
                                    glm::vec3{0.0f, 1.0f, 0.0f});
        // The paper-thin box only moves the decal's front plane 1 cm off its
        // wall, avoiding z-fighting while preserving the drawn wall geometry.
        t.scale = {tag.width_m, tag.height_m, 0.02f};
        const NodeId id = scene.create(r, t, unit_bounds);
        if (SceneNode* node = scene.get(id))
            node->max_draw_distance = tag.site->max_draw_distance_m;
        nodes.push_back(id);
    }
}

struct SkyscraperWindowRegistration {
    std::vector<SkyscraperWindowRuntime>* windows = nullptr;
    uint64_t building_key = 0;
    city::SkyscraperUse use = city::SkyscraperUse::Mixed;
    bool split_twin_towers = false;
    bool existing_dark_pane = false;
};

int32_t window_key_coord(float metres) {
    return static_cast<int32_t>(std::lround(metres * 100.0f));
}

uint64_t window_piece_key(uint64_t building_key,
                          const city::StartPart& part) {
    uint64_t key = splitmix64_mix(building_key ^ 0x57494E444F575FULL);
    key = splitmix64_mix(key ^
        static_cast<uint64_t>(static_cast<uint32_t>(window_key_coord(part.centre.x))));
    key = splitmix64_mix(key ^
        (static_cast<uint64_t>(static_cast<uint32_t>(window_key_coord(part.centre.z))) << 1u));
    key = splitmix64_mix(key ^
        (static_cast<uint64_t>(static_cast<uint32_t>(window_key_coord(part.bottom_m))) << 2u));
    return key;
}

uint64_t window_site_key(const city::StartSite& site, uint64_t salt) {
    // Site coordinates are authored identity. Unlike an array index, they do
    // not change when another parcel is inserted earlier in a source table.
    uint64_t key=splitmix64_mix(salt);
    key=splitmix64_mix(key ^ static_cast<uint64_t>(static_cast<uint32_t>(
        window_key_coord(site.origin.x))));
    key=splitmix64_mix(key ^
        (static_cast<uint64_t>(static_cast<uint32_t>(
             window_key_coord(site.origin.z))) << 1u));
    key=splitmix64_mix(key ^
        (static_cast<uint64_t>(static_cast<uint32_t>(
             window_key_coord(site.ground_m))) << 2u));
    return key;
}

bool is_skyscraper_window_light(const city::StartPart& part) {
    return part_name_is(part,"tower office window light") ||
           part_name_is(part,"twin tower office window light");
}

bool is_pinatty_apartment_window(const city::StartPart& part) {
    return part_name_is(part,"infill apartment window") ||
           part_name_is(part,"infill side apartment window") ||
           part_name_is(part,"infill rear apartment window");
}

void append_start_site(Scene& scene, TerrainCollider& collider,
                       std::vector<StaticBox>& precipitation_cover,
                       const city::StartSite& site,
                       const city::StartPart* parts, std::size_t count,
                       const Renderable& prototype, const AABB& unit_bounds,
                       const StartMaterials& materials, SiteMaterialStyle style,
                       MeshId flat_decal, MeshId billboard_face,
                       MeshId rounded_box, MeshId cylinder,
                       std::vector<NodeId>& nodes,MeshId pawn_guitar_mesh=kInvalidId,
                       MeshId gable_prism=kInvalidId,
                       const SkyscraperWindowRegistration* window_registration=nullptr) {
    const bool residential=count>0 && part_name_has(parts[0],"house ");
    const bool loom_site = &site == &city::kLoomMuseumSite || &site == &city::kLoomParkSite;
    const bool natural_yard = residential || &site == &city::kTidewaterFarmSite || &site == &city::kLoomParkSite;
    // Sites on Miandi's physical-scale paint kit. Club Mirage joined with its
    // detail pass: on the Plain path its walls took a flat finish tint at no
    // texture scale, so the same brick shed read maroon from Bayfront, grey
    // from Solana and black from Mango. The kit also turns its authored
    // paving into firm ground the player can walk and drive on.
    const bool miandi_detailed_site = &site == &city::kMiandiOceanDriveSite ||
                                      &site == &city::kMiandiContextSite ||
                                      &site == &city::kMiandiPrismWorksSite;
    for (std::size_t i = 0; i < count; ++i) {
        const city::StartPart& part = parts[i];
        if (city::building_access_replaces_pavement(site,part)) continue;
        Renderable r = prototype;
        r.tint = start_finish_tint(part.finish);
        bool fitted_section_texture = false;
        if (style != SiteMaterialStyle::Plain) {
            r.mesh = part_mesh(part, style, prototype.mesh, flat_decal,
                               billboard_face, rounded_box, cylinder);
            r.material = materials.finish[finish_index(part.finish)];
            if (style == SiteMaterialStyle::GasStation) {
                if (is_canopy_skin(part)) r.material = materials.canopy_skin;
                if (is_pillar_body(part)) r.material = materials.pillar_body;
                if (is_pump_body(part)) r.material = materials.pump_body;
                if (is_pump_control_face(part)) {
                    r.material = materials.pump_control;
                }
                if (is_dumpster_body(part)) r.material = materials.dumpster_body;
            } else if (style == SiteMaterialStyle::TexturedBuilding) {
                if (is_bank_sign_face(part)) r.material = materials.bank_sign;
                if (is_bank_atm_face(part)) r.material = materials.bank_atm;
                if (is_bank_keypad(part)) r.material = materials.bank_vault_keypad;
                if (part_name_is(part, "bank office access note")) {
                    r.material = materials.bank_vault_note;
                }
                if (is_bank_wood(part)) r.material = materials.bank_wood;
                if (is_bank_deposits(part)) r.material = materials.bank_deposits;
                if (part_name_is(part, "bank interior floor") ||
                    part_name_is(part, "bank teller counter top")) {
                    r.material = materials.bank_floor;
                }
                if (part_name_is(part, "bank interior ceiling")) {
                    r.material = materials.bank_ceiling;
                }
            } else if (style == SiteMaterialStyle::Farm) {
                if ((part_name_has(part, "farm barn") &&
                     part_name_has(part, "wall")) ||
                    part_name_is(part, "farm barn parked sliding door") ||
                    part_name_is(part, "farm barn loft hatch")) {
                    r.material = materials.farm_barn_boards;
                    r.tint = {1, 1, 1, 1};
                }
                if (part_name_has(part, "farm house") &&
                    part_name_has(part, "wall")) {
                    r.material = materials.farm_house_clapboard;
                    r.tint = {1, 1, 1, 1};
                }
                if (part_name_has(part, "farm crop soil") ||
                    part_name_has(part, "farm drainage") ||
                    part_name_has(part, "farm damp patch") ||
                    part_name_has(part, "farm wheel rut")) {
                    r.material = materials.farm_soil;
                    r.tint = {1, 1, 1, 1};
                }
                if (part_name_has(part, "farm yard drive")) {
                    r.material = materials.farm_drive;
                    r.tint = {1, 1, 1, 1};
                }
                if (part_name_has(part, "farm galvanized") ||
                    part_name_has(part, "farm water tank") ||
                    (part_name_has(part, "farm house") &&
                     part_name_has(part, "roof")) ||
                    (part_name_has(part, "farm barn") &&
                     part_name_has(part, "roof"))) {
                    r.material = materials.farm_galvanized;
                    r.tint = {1, 1, 1, 1};
                }
            } else if (style == SiteMaterialStyle::Construction) {
                if (part_name_is(part, "hospital main entry mural face")) {
                    r.material = materials.hospital_entry_mural;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(part,
                                 "hospital emergency bay-two marker face")) {
                    r.material = materials.hospital_emergency_bay_two;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(
                        part,
                        "hospital garage entry pay station control face")) {
                    r.material = materials.hospital_garage_pay_station;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(
                        part,
                        "hospital northwest healing art glass face")) {
                    r.material = materials.hospital_healing_art_glass;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(
                        part,
                        "hospital arrival emergency shore power cabinet fitted face")) {
                    r.material = materials.hospital_emergency_shore_power;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(part,
                                 "hospital grounds healing mosaic face")) {
                    r.material = materials.hospital_healing_garden_mosaic;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(part,
                                 "hospital garage west entry control face")) {
                    r.material = materials.hospital_garage_west_entry_control;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_is(
                        part,
                        "hospital north parking wayfinding fitted face")) {
                    r.material = materials.hospital_north_parking_wayfinding;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "construction fence mesh") ||
                    part_name_has(part, "construction top safety mesh")) {
                    r.material = materials.construction_fence_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "construction plywood")) {
                    r.material = materials.construction_plywood;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "construction crane")) {
                    r.material = materials.construction_crane_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "construction rebar bundle") ||
                    part_name_has(part, "construction steel beam stack") ||
                    part_name_has(part, "materials rebar pallet") ||
                    part_name_has(part, "materials pipe pallet")) {
                    r.material = materials.construction_rebar_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "materials depot front") ||
                    part_name_has(part, "materials depot rear") ||
                    part_name_has(part, "materials depot west") ||
                    part_name_has(part, "materials depot east") ||
                    part_name_has(part, "materials depot roof") ||
                    part_name_has(part, "materials depot door")) {
                    r.material = materials.construction_depot_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "materials depot floor") ||
                    part_name_has(part, "materials loading dock") ||
                    part_name_has(part, "materials aggregate silo")) {
                    r.material = materials.construction_depot_concrete;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "equipment compact loader") ||
                    part_name_has(part, "equipment loader bucket") ||
                    part_name_has(part, "equipment rental generator") ||
                    part_name_has(part, "equipment rental dumpster") ||
                    part_name_has(part, "equipment dumpster lid") ||
                    part_name_has(part, "equipment safety cone") ||
                    part_name_has(part, "equipment garage bay stripe") ||
                    part_name_has(part, "equipment yard light head")) {
                    r.material = materials.construction_equipment_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "construction street barrier")) {
                    r.material = materials.construction_barrier_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
                if (part_name_has(part, "construction plywood wayfinding board")) {
                    r.material = materials.construction_wayfinding_section;
                    r.tint = {1, 1, 1, 1};
                    fitted_section_texture = true;
                }
            } else if (style == SiteMaterialStyle::Airport) {
                if (part_name_is(part,"terminal directory sign face")) r.material=materials.airport_directory;
                if (part_name_has(part,"terminal furnishing ")) r.material=materials.airport_furnishing;
                if (is_airport_paving(part)) r.material = materials.airport_paving;
                if (is_airport_facade(part)) r.material = materials.airport_facade;
                if (is_airport_landside_paving(part)) {
                    r.material = materials.airport_landside_paving;
                }
            } else if (style == SiteMaterialStyle::Billboard &&
                       is_billboard_face(part)) {
                r.material = is_taxi_billboard_face(part)
                                 ? materials.billboard_taxi
                                 : materials.billboard_ness;
            }
            // Flat slabs expose X/Z as their important face; upright props and
            // walls expose X/Y. Mixing those is what turns pavement aggregate
            // into long blurry streaks down the forecourt.
            const bool flat = part.height_m <= 0.35f ||
                              part_name_is(part, "canopy roof");
            const float texture_v_extent = flat ? part.depth_m : part.height_m;
            const float metres_per_tile =
                is_airport_landside_paving(part)
                    ? 6.0f
                    : (style == SiteMaterialStyle::Airport ? 16.0f : 2.0f);
            r.uv_scale = {
                std::max(1.0f, part.width_m / metres_per_tile),
                std::max(1.0f, texture_v_extent / metres_per_tile)};
            if (style==SiteMaterialStyle::Airport &&
                (part_name_is(part,"terminal directory sign face") || part_name_has(part,"terminal furnishing ")))
                r.uv_scale={1.f,1.f};
            if (style == SiteMaterialStyle::Billboard &&
                is_billboard_face(part)) {
                r.uv_scale = {1.0f, 1.0f};
            }
            if (style == SiteMaterialStyle::TexturedBuilding) {
                // Physical tile size, including long side-facing wall panels.
                const float u_extent = flat ? part.width_m :
                    std::max(part.width_m, part.depth_m);
                r.uv_scale = {u_extent / 2.4f, texture_v_extent / 2.4f};
                if (is_bank_sign_face(part) || is_bank_atm_face(part) ||
                    is_bank_keypad(part) || part_name_is(part, "bank office access note")) {
                    r.uv_scale = {1.0f, 1.0f};
                }
                // The horizontal decal's V axis runs toward the reader;
                // reverse it so handwritten text is not mirrored from above.
                if (part_name_is(part, "bank office access note")) {
                    r.uv_scale.y = -1.0f;
                }
            }
            if (style == SiteMaterialStyle::Farm) {
                const float u_extent = flat ? part.width_m :
                    std::max(part.width_m, part.depth_m);
                r.uv_scale = {
                    std::max(1.0f, u_extent / 1.8f),
                    std::max(1.0f, texture_v_extent / 1.8f)};
            }
            if (fitted_section_texture) {
                // Each generated section owns one named plane. Fit the full
                // image once instead of applying generic world tiling.
                r.uv_scale = {1.0f, 1.0f};
            }

            // The panel is visible as an emitter as well as owning a real
            // spotlight below. Alpha over one is the lit shader's compact
            // per-instance emissive channel.
            if (style == SiteMaterialStyle::GasStation &&
                part_name_has(part, "canopy light")) {
                r.tint.a = 5.0f;
            }
            if (style == SiteMaterialStyle::Airport &&
                (part_name_has(part, "runway edge light") ||
                 part_name_has(part, "taxiway edge light") ||
                 part_name_is(part, "control tower cab") ||
                 part_name_has(part, "beacon") ||
                 part_name_is(part, "airport arrival pylon face") ||
                 part_name_is(part, "airport arrival pylon accent"))) {
                r.tint.a = 3.5f;
            }
            if (style == SiteMaterialStyle::Construction &&
                (part_name_is(part, "hospital facade wall light lens") ||
                 part_name_is(part, "hospital arrival canopy light lens") ||
                 part_name_is(part, "hospital grounds path light lens") ||
                 part_name_is(part,
                              "hospital garage ceiling light lens") ||
                 part_name_is(part, "hospital parking lot light lens"))) {
                r.material = prototype.material;
                r.tint = {1.0f, 0.88f, 0.70f, 3.2f};
            }
            if (style == SiteMaterialStyle::TexturedBuilding &&
                part_name_has(part, "bank ceiling light")) {
                r.tint.a = 4.0f;
            }

            // These source textures already carry their real-world colour.
            // Tinting them a second time would turn brick black and concrete
            // muddy; painted metal keeps the authored brand colours.
            bool owns_colour = false;
            if (style == SiteMaterialStyle::Billboard) {
                owns_colour = is_billboard_face(part);
            } else if (style == SiteMaterialStyle::TexturedBuilding) {
                owns_colour = is_bank_sign_face(part) || is_bank_atm_face(part) ||
                    is_bank_keypad(part) || part_name_is(part, "bank office access note") ||
                    is_bank_wood(part) || is_bank_deposits(part) ||
                    part_name_is(part, "bank interior floor") ||
                    part_name_is(part, "bank teller counter top") ||
                    part_name_is(part, "bank interior ceiling") ||
                    part.finish == city::StartFinish::Asphalt ||
                    part.finish == city::StartFinish::Concrete ||
                    part.finish == city::StartFinish::WarmWall ||
                    part.finish == city::StartFinish::Brick;
            } else if (style == SiteMaterialStyle::Farm) {
                owns_colour =
                    (part_name_has(part, "farm barn") &&
                     (part_name_has(part, "wall") ||
                      part_name_has(part, "roof"))) ||
                    part_name_is(part, "farm barn parked sliding door") ||
                    part_name_is(part, "farm barn loft hatch") ||
                    (part_name_has(part, "farm house") &&
                     (part_name_has(part, "wall") ||
                      part_name_has(part, "roof"))) ||
                    part_name_has(part, "farm crop soil") ||
                    part_name_has(part, "farm yard drive") ||
                    part_name_has(part, "farm drainage") ||
                    part_name_has(part, "farm damp patch") ||
                    part_name_has(part, "farm wheel rut") ||
                    part_name_has(part, "farm galvanized") ||
                    part_name_has(part, "farm water tank");
            } else if (style == SiteMaterialStyle::Airport) {
                owns_colour = part_name_is(part,"terminal directory sign face") ||
                              part_name_has(part,"terminal furnishing ") || is_airport_paving(part) ||
                              is_airport_facade(part) ||
                              is_airport_landside_paving(part);
            } else if (style == SiteMaterialStyle::Construction) {
                owns_colour = part_name_has(part, "construction fence mesh") ||
                              part_name_has(part, "construction top safety mesh") ||
                              part_name_has(part, "construction plywood") ||
                              part_name_has(part, "construction crane") ||
                              part_name_has(part, "construction rebar bundle") ||
                              part_name_has(part, "construction steel beam stack") ||
                              part_name_has(part, "materials rebar pallet") ||
                              part_name_has(part, "materials pipe pallet") ||
                              part_name_has(part, "materials depot front") ||
                              part_name_has(part, "materials depot rear") ||
                              part_name_has(part, "materials depot west") ||
                              part_name_has(part, "materials depot east") ||
                              part_name_has(part, "materials depot roof") ||
                              part_name_has(part, "materials depot door") ||
                              part_name_has(part, "materials depot floor") ||
                              part_name_has(part, "materials loading dock") ||
                              part_name_has(part, "materials aggregate silo") ||
                              part_name_has(part, "equipment compact loader") ||
                              part_name_has(part, "equipment loader bucket") ||
                              part_name_has(part, "equipment rental generator") ||
                              part_name_has(part, "equipment rental dumpster") ||
                              part_name_has(part, "equipment dumpster lid") ||
                              part_name_has(part, "equipment safety cone") ||
                              part_name_has(part, "equipment garage bay stripe") ||
                              part_name_has(part, "equipment yard light head") ||
                              part_name_has(part, "construction street barrier") ||
                              part_name_has(part, "construction plywood wayfinding board");
            } else {
                owns_colour =
                    part.finish == city::StartFinish::Asphalt ||
                    part.finish == city::StartFinish::Concrete ||
                    part.finish == city::StartFinish::WarmWall ||
                    part.finish == city::StartFinish::Brick ||
                    is_pump_body(part) || is_pump_control_face(part) ||
                    is_dumpster_body(part);
            }
            if (owns_colour) {
                r.tint.r = 1.0f;
                r.tint.g = 1.0f;
                r.tint.b = 1.0f;
            }
        }

        if (part_name_has(part, "shop sign face")) {
            r.material = part_name_has(part,"bar shop") ? materials.bar_sign :
                part_name_has(part,"gun store") ? materials.gun_store_sign :
                part_name_has(part,"pawn") ? materials.pawn_sign :
                (part_name_has(part, "repair") ? materials.repair_sign : materials.laundry_sign);
            r.tint = {1,1,1,1.3f};
            r.uv_scale = {1,1};
        }
        if (&site == &city::kEastArmPlazaSite) {
            if (part_name_is(part, "mall arcade sign face"))
                r.material = materials.plaza_galleria_sign;
            else if (part_name_is(part, "cinema marquee sign face"))
                r.material = materials.plaza_cinema_sign;
            else if (part_name_is(part, "bookstore sign face"))
                r.material = materials.plaza_bookstore_sign;
            else if (part_name_is(part, "coffee shop sign face"))
                r.material = materials.plaza_coffee_sign;
            else if (part_name_is(part, "restaurant sign face"))
                r.material = materials.plaza_restaurant_sign;
            if (part_name_has(part, "sign face") &&
                !part_name_is(part, "mall directory sign face")) {
                // These textures carry dark enamel behind their bright tubes;
                // a focused emissive lift keeps the curved lettering readable
                // after dusk without turning the whole storefront into a lamp.
                r.tint = {1,1,1,1.7f};
                r.uv_scale = {1,1};
            }
            if (part.finish == city::StartFinish::Glass) {
                r.material = materials.glass;
                r.tint = {.72f,.92f,.94f,.72f};
            }
            if (part_name_has(part, "arcade light lens") ||
                part_name_has(part, "lamp head")) {
                r.material = prototype.material;
                r.tint = {1.0f,.78f,.48f,3.0f};
            }
            if (part_name_has(part, "palm crown"))
                r.tint = {.16f,.42f,.24f,1};
            if (part_name_has(part, "palm trunk"))
                r.tint = {.48f,.28f,.14f,1};
        }
        if (style==SiteMaterialStyle::GasStation && part_name_has(part,"store interior ")) {
            if(part_name_is(part,"store interior ATM face")) {
                r.material=materials.bank_atm;r.tint={1,1,1,1};r.uv_scale={1,1};
            } else if (part_name_has(part,"sign face")) {
                r.material=part_name_has(part,"snack") ? materials.store_snacks:materials.store_drinks;
                r.tint={1,1,1,1};r.uv_scale={1,1};
            } else if (part_name_is(part,"store interior floor")) {
                r.material=materials.quickbite_floor;r.tint={1,1,1,1};
                r.uv_scale={part.width_m/1.2f,part.depth_m/1.2f};
            } else if (part_name_is(part,"store interior ceiling")) {
                r.material=materials.bank_ceiling;r.tint={1,1,1,1};
                r.uv_scale={part.width_m/2.4f,part.depth_m/2.4f};
            } else if (part_name_is(part,"store interior ceiling light lens")) {
                r.material=prototype.material;r.tint={1,.88f,.70f,2.5f};
            } else if (part.finish==city::StartFinish::Steel) {
                r.material=materials.quickbite_steel;r.tint={1,1,1,1};
                r.uv_scale={std::max(part.width_m,part.depth_m),
                    part.height_m<.35f ? part.depth_m:part.height_m};
            }
        }
        if (part_name_has(part,"station sign face")) {
            r.material=part_name_has(part,"fire") ? materials.fire_sign:materials.police_sign;
            r.tint={1,1,1,1.2f};r.uv_scale={1,1};
        }
        if(residential) {
            if(part_name_is(part,"house interior floor") || part_name_has(part,"house garden fence") ||
                part_name_has(part,"house timber ") ||
                (part.finish==city::StartFinish::WarmWall &&
                 (part_name_has(part,"house study ") || part_name_has(part,"house dining ") ||
                  part_name_has(part,"bookcase") || part_name_has(part,"coffee table") ||
                  part_name_has(part,"bed base")))) {
                const bool boards=part_name_is(part,"house interior floor") || part_name_has(part,"house garden fence");
                r.material=boards?materials.marina_timber:materials.bank_wood;
                r.tint=boards?glm::vec4{.72f,.65f,.54f,1}:glm::vec4{.70f,.60f,.48f,1};
            }
            if(part_name_has(part,"house pitched roof") || part_name_is(part,"house carport roof"))
                r.material=materials.marina_roof;
            const std::size_t house_index=city::residential_house_index(site);
            if(city::residential_has_imported_texture(house_index)) {
                if(city::residential_wall_texture_piece(part)) {
                    r.material=materials.residential_wall[house_index];
                    r.tint={1,1,1,1};
                    r.uv_scale={std::max(part.width_m,part.depth_m)/2.f,
                                std::max(1.f,part.height_m/1.5f)};
                } else if(city::residential_roof_texture_piece(part)) {
                    r.material=materials.residential_roof[house_index];
                    r.tint={1,1,1,1};
                    r.uv_scale={std::max(1.f,part.width_m/2.f),
                                std::max(1.f,part.depth_m/2.f)};
                }
            }
            if(part_name_is(part,"house interior ceiling")) {
                r.material=prototype.material;r.tint={.88f,.86f,.80f,1};
            }
            if(part_name_has(part,"sofa")) {
                r.material=prototype.material;r.tint={.26f,.40f,.35f,1};
                if(part.finish==city::StartFinish::RedTrim)r.tint={.39f,.17f,.13f,1};
                if(part.finish==city::StartFinish::White)r.tint={.84f,.79f,.66f,1};
            }
            // Domestic painted/enamel and textile accents must not inherit the
            // exterior stucco (or the broad study/dining timber override).
            if(part_name_has(part,"house painted ") || part_name_has(part,"house ceramic ") ||
                part_name_has(part,"house paper ") || part_name_has(part,"house picture ") ||
                part_name_has(part,"house curtain ") || part_name_has(part," rug ") ||
                part_name_has(part," quilt") || part_name_has(part," towel") ||
                part_name_has(part," cloth ") || part_name_has(part," tile ") ||
                part_name_has(part,"cabinet panel") || part_name_has(part,"headboard inset") ||
                part_name_is(part,"house study writing blotter") ||
                part_name_is(part,"house study lamp shade")) {
                r.material=prototype.material;
                if(part.finish==city::StartFinish::White)r.tint={.84f,.79f,.66f,1};
                if(part.finish==city::StartFinish::TealDoor)r.tint={.26f,.40f,.35f,1};
                if(part.finish==city::StartFinish::RedTrim)r.tint={.39f,.17f,.13f,1};
                if(part.finish==city::StartFinish::WarmWall)r.tint={.58f,.45f,.29f,1};
            }
            if(&site==&city::kResidentialHouses[city::kResidentialTargetHouse].site &&
                part.finish==city::StartFinish::Steel) {
                r.material=materials.quickbite_steel;r.tint={.75f,.73f,.67f,1};
            }
            if(part_name_has(part,"light lens")) {r.material=prototype.material;r.tint={1,.87f,.62f,2.5f};}
        }
        if (style == SiteMaterialStyle::Westmere) {
            const auto role = city::westmere_material_role(part);
            const auto use_material = [&](MaterialId material,
                                          glm::vec2 uv_scale) {
                r.material = material;
                r.tint = {1, 1, 1, 1};
                r.uv_scale = uv_scale;
            };
            const bool horizontal = part.height_m <= 0.35f;
            const float u_extent = horizontal ? part.width_m
                : std::max(part.width_m, part.depth_m);
            const float v_extent = horizontal ? part.depth_m : part.height_m;
            switch (role) {
                case city::WestmereMaterialRole::PeachStucco:
                    use_material(materials.westmere_peach_stucco,
                        {std::max(1.0f, u_extent / 4.0f),
                         std::max(1.0f, v_extent / 4.0f)});
                    break;
                case city::WestmereMaterialRole::CreamStucco:
                    use_material(materials.westmere_cream_stucco,
                        {std::max(1.0f, u_extent / 4.0f),
                         std::max(1.0f, v_extent / 4.0f)});
                    break;
                case city::WestmereMaterialRole::RoofTile:
                    use_material(materials.westmere_roof_tile,
                        {std::max(1.0f, part.width_m / 3.0f),
                         std::max(1.0f, part.depth_m / 3.0f)});
                    break;
                case city::WestmereMaterialRole::Limestone:
                    use_material(materials.westmere_limestone,
                        {std::max(1.0f, u_extent / 3.0f),
                         std::max(1.0f, v_extent / 1.5f)});
                    break;
                case city::WestmereMaterialRole::GreenMetal:
                    use_material(materials.westmere_green_metal, {1, 1});
                    break;
                case city::WestmereMaterialRole::PoolTile:
                    use_material(materials.westmere_pool_tile,
                        {std::max(1.0f, u_extent / 2.0f), 1.0f});
                    break;
                case city::WestmereMaterialRole::TennisSurface:
                    use_material(materials.westmere_tennis_surface, {1, 1});
                    break;
                case city::WestmereMaterialRole::WhiteSlats:
                    use_material(materials.westmere_white_slats,
                        {std::max(1.0f, u_extent / 2.0f), 1.0f});
                    break;
                case city::WestmereMaterialRole::TreeBark:
                    use_material(materials.westmere_tree_bark,
                        {1.0f, std::max(1.0f, part.height_m / 2.5f)});
                    break;
                case city::WestmereMaterialRole::Foliage:
                    use_material(materials.westmere_foliage,
                        {std::max(1.0f, u_extent / 2.5f),
                         std::max(1.0f, v_extent / 2.5f)});
                    break;
                case city::WestmereMaterialRole::BenchTimber:
                    use_material(materials.westmere_bench_timber,
                        {std::max(1.0f, u_extent / 2.0f), 1.0f});
                    break;
                case city::WestmereMaterialRole::LoungerFabric:
                    use_material(materials.westmere_lounger_fabric, {1, 1});
                    break;
                case city::WestmereMaterialRole::EntrySign:
                    use_material(materials.westmere_entry_sign, {1, 1});
                    break;
                case city::WestmereMaterialRole::Default:
                    break;
            }
            if (part_name_has(part, "light lens")) {
                r.material = prototype.material;
                r.tint = {1, .87f, .62f, 2.5f};
            }
        }
        if (loom_site) {
            if (part.finish == city::StartFinish::Glass) {
                r.material = materials.glass; r.tint = {.88f,.96f,1.f,.72f};
            }
            if (part.finish == city::StartFinish::WarmWall) {
                r.material = materials.westmere_limestone;
                r.tint = {1, .96f, .88f, 1};
            }
            if (part.finish == city::StartFinish::Steel) {
                r.material = materials.quickbite_steel;
                r.tint = {.52f,.35f,.20f,1};
            }
            if (part_name_has(part,"garden timber") || part_name_has(part,"gazebo timber") ||
                part_name_is(part,"garden tree trunk") || part_name_is(part,"garden gazebo floor")) {
                r.material = prototype.material; r.tint = {.40f,.24f,.12f,1};
            }
            if (part_name_is(part,"garden tree crown")) {
                r.material = prototype.material; r.tint = {.17f,.36f,.12f,1};
            }
            if (part_name_has(part,"artwork") || part_name_has(part,"sculpture") ||
                part_name_has(part,"raised sign letter") || part_name_has(part,"flower cluster"))
                r.material = prototype.material;
            if (part_name_has(part,"light lens")) {
                r.material = prototype.material; r.tint = {1,.91f,.75f,2.5f};
            }
        }
        if (&site==&city::kLoomMuseumSite) {
            if (part_name_has(part,"interior floor")) {
                r.material=materials.bank_floor;r.tint={.92f,.88f,.78f,1};
                r.uv_scale={part.width_m/3.f,part.depth_m/3.f};
            }
            const char* painting_names[]={"museum painting landscape","museum painting portrait",
                "museum painting still life","museum painting harbor"};
            for(std::size_t painting=0;painting<4;++painting)if(part_name_is(part,painting_names[painting])) {
                r.mesh=materials.museum_paintings[painting];r.material=materials.museum_artwork;
                r.tint={1,1,1,1};r.uv_scale={1,1};
            }
            if(part_name_has(part,"gallery ceiling") || part_name_has(part,"hall ceiling") ||
               part_name_is(part,"museum portico ceiling")) {
                r.material=materials.bank_ceiling;r.tint={.92f,.88f,.78f,1};
                r.uv_scale={part.width_m/4.f,part.depth_m/4.f};
            }
            if(part_name_has(part,"museum exhibit ") || part_name_is(part,"museum gallery sign panel")) r.material=prototype.material;
            if(part_name_is(part,"museum sculpture amphora")) {
                r.mesh=materials.museum_amphora;r.material=prototype.material;
                r.tint={.65f,.32f,.14f,1};
            }
            if(part_name_is(part,"museum round column amphora band")) {r.material=prototype.material;r.tint={.16f,.09f,.055f,1};}
            if(part_name_is(part,"museum column flute")) {r.material=prototype.material;r.tint={.43f,.39f,.30f,1};}
            if(part_name_is(part,"museum artwork gilt frame"))r.tint={.64f,.43f,.14f,1};
            if(part_name_is(part,"museum forecourt shrub")) {r.material=prototype.material;r.tint={.18f,.29f,.12f,1};}

            // --- Interior finish ------------------------------------------
            // One near-white plaster sheet dressed eight ways. The lining
            // carries its room in the part name for exactly this: a gallery
            // colour is a tint here, not a texture in the asset folder.
            struct MuseumWallTint { const char* name; glm::vec4 tint; };
            // Values are pitched against a cool ambient: anything already
            // blue-grey here lands as navy in game, and anything dark lands as
            // black. Only the art room's burgundy is meant to read dark.
            static constexpr MuseumWallTint kMuseumWalls[] = {
                {"museum wall art",        {.47f,.27f,.26f,1}},   // old-master hang
                {"museum wall antiquities",{.92f,.85f,.70f,1}},
                {"museum wall natural",    {.72f,.74f,.70f,1}},
                {"museum wall pinatty",     {.93f,.88f,.76f,1}},
                {"museum wall science",    {.87f,.88f,.87f,1}},
                {"museum wall space",      {.40f,.44f,.60f,1}},   // to sit the star chart on
                {"museum wall transport",  {.88f,.82f,.73f,1}},
                {"museum wall design",     {.97f,.97f,.96f,1}},
                {"museum wall hall",       {.95f,.92f,.84f,1}},
            };
            // The art room's freestanding hanging spine is a wall in every way
            // that matters here, so it takes the room's own colour.
            const bool art_spine = part_name_has(part, "museum art hanging wall");
            for (const MuseumWallTint& wall : kMuseumWalls) {
                if (!part_name_is(part, wall.name) && !art_spine) continue;
                r.material = materials.museum_plaster;
                r.tint = wall.tint;
                r.uv_scale = {std::max(part.width_m, part.depth_m) / 2.5f,
                              std::max(part.height_m, 1.f) / 2.5f};
                break;   // kMuseumWalls[0] is the art room, which is the spine's
            }
            if (part_name_is(part, "museum gallery cornice") ||
                part_name_has(part, "museum gallery reveal") ||
                part_name_is(part, "museum ceiling cove") ||
                part_name_is(part, "museum balcony plaster face")) {
                r.material = materials.museum_plaster;
                r.tint = {1, .98f, .94f, 1};
                r.uv_scale = {std::max(part.width_m, part.depth_m) / 2.5f, 1};
            }
            if (part_name_is(part, "museum gallery parquet")) {
                r.material = materials.museum_parquet;
                r.tint = {.74f, .72f, .70f, 1};
                r.uv_scale = {part.width_m / 2.4f, part.depth_m / 2.4f};
            }
            if (part_name_has(part, "museum gallery ceiling") ||
                part_name_is(part, "museum lower gallery ceiling") ||
                part_name_is(part, "museum portico ceiling")) {
                r.material = materials.museum_coffer;
                r.tint = {.96f, .94f, .90f, 1};
                r.uv_scale = {part.width_m / 4.f, part.depth_m / 4.f};
            }
            if (part_name_is(part, "museum hall floor band")) {
                r.material = materials.bank_floor;
                r.tint = {.44f, .40f, .35f, 1};
                r.uv_scale = {part.width_m / 3.f, part.depth_m / 3.f};
            }
            if (part_name_is(part, "museum hall floor inlay")) {
                r.material = materials.bank_floor;
                r.tint = {.74f, .62f, .40f, 1};
                r.uv_scale = {part.width_m / 3.f, part.depth_m / 3.f};
            }
            // Oak where a visitor's hand or seat lands, walnut on the cases.
            if (part_name_has(part, "museum gallery bench") ||
                part_name_is(part, "museum vitrine base") ||
                part_name_is(part, "museum survey table") ||
                part_name_is(part, "museum history model table") ||
                part_name_is(part, "museum space model table") ||
                part_name_is(part, "museum reception desk")) {
                r.material = materials.bank_wood;
                r.tint = {.74f, .68f, .60f, 1};
                r.uv_scale = {std::max(part.width_m, part.depth_m) / 1.8f,
                              std::max(part.height_m, .5f) / 1.8f};
            }
            if (part_name_is(part, "museum reception apron")) r.tint = {.16f, .30f, .30f, 1};

            // --- Interpretive graphics ------------------------------------
            if (const int cell = site_atlas_cell(part, "museum plaque ");
                cell >= 0 && cell < static_cast<int>(materials.museum_plaque_cells.size())) {
                r.mesh = materials.museum_plaque_cells[static_cast<std::size_t>(cell)];
                r.material = materials.museum_plaque_sheet;
                r.tint = {1, 1, 1, 1}; r.uv_scale = {1, 1};
            }
            if (const int cell = site_atlas_cell(part, "museum panel ");
                cell >= 0 && cell < static_cast<int>(materials.museum_panel_cells.size())) {
                r.mesh = materials.museum_panel_cells[static_cast<std::size_t>(cell)];
                r.material = materials.museum_panel_sheet;
                r.tint = {1, 1, 1, 1}; r.uv_scale = {1, 1};
            }
            if (const int cell = site_atlas_cell(part, "museum label ");
                cell >= 0 && cell < static_cast<int>(materials.museum_label_cells.size())) {
                r.mesh = materials.museum_label_cells[static_cast<std::size_t>(cell)];
                r.material = materials.museum_label_sheet;
                r.tint = {1, 1, 1, 1}; r.uv_scale = {1, 1};
            }
            if (part_name_is(part, "museum directory")) {
                r.material = materials.museum_directory;
                r.tint = {1, 1, 1, 1}; r.uv_scale = {1, 1};
            }

            // --- Exhibit meshes -------------------------------------------
            if (part_name_has(part, "museum exhibit sphere ")) {
                r.mesh = materials.museum_sphere; r.material = prototype.material;
            }
            if (part_name_has(part, "museum exhibit cone ")) {
                r.mesh = materials.museum_cone; r.material = prototype.material;
            }
            if (part_name_is(part, "museum exhibit wheel train")) {
                r.mesh = materials.museum_wheel; r.material = prototype.material;
                r.tint = {.52f, .13f, .10f, 1};
            }
            if (part_name_is(part, "museum exhibit skull fossil")) {
                r.mesh = materials.museum_skull; r.material = prototype.material;
                r.tint = {.90f, .88f, .82f, 1};
            }
            if (part_name_has(part, "museum exhibit ring ")) {
                r.mesh = materials.museum_ring; r.material = prototype.material;
                r.tint = {.52f, .35f, .20f, 1};
            }
            if (part_name_is(part, "museum exhibit sphere armillary sun")) r.tint = {.82f, .62f, .18f, 1};
            if (part_name_is(part, "museum exhibit sphere pendulum bob")) r.tint = {.72f, .52f, .18f, 1};
            if (part_name_is(part, "museum design glassware")) {
                r.material = materials.glass; r.tint = {.90f, .96f, 1.f, .34f};
            }
            // Case glass sits between the visitor and the exhibit, often two
            // panes deep. At the shopfront alpha it turned a painted city model
            // into a blue haze, so it is barely there.
            if (part_name_is(part, "museum vitrine glass")) {
                r.material = materials.glass; r.tint = {.96f, .98f, 1.f, .13f};
            }
            // The model is read from above at half a metre. It needs its own
            // contrast: a pale card base, dark streets, and blocks that differ
            // by more than a shade.
            if (part_name_is(part, "museum history model deck")) {
                r.material = materials.bank_floor; r.tint = {.90f, .87f, .79f, 1};
                r.uv_scale = {part.width_m / 2.f, part.depth_m / 2.f};
            }
            if (part_name_has(part, "museum history model street")) {
                r.material = prototype.material; r.tint = {.30f, .29f, .28f, 1};
            }
            if (part_name_is(part, "museum exhibit history city block") ||
                part_name_is(part, "museum exhibit history tower")) {
                r.material = prototype.material;
                r.tint = part.finish == city::StartFinish::Brick ? glm::vec4{.62f,.34f,.26f,1}
                       : part.finish == city::StartFinish::WarmWall ? glm::vec4{.86f,.78f,.60f,1}
                       : glm::vec4{.50f,.54f,.58f,1};
            }
            if (part_name_is(part, "museum history model water")) r.tint = {.30f, .48f, .56f, 1};
            if (part_name_is(part, "museum science dial")) {
                r.material = prototype.material; r.tint = {.90f, .88f, .82f, 1};
            }
        }
        // --- Halberd Field ------------------------------------------------
        // Concrete, olive and galvanised steel. Camber Point is glass and
        // white render; if this one reads the same from the air it was not
        // worth building.
        if (&site == &city::kHalberdFieldSite) {
            if (part_name_has(part, "halberd ") && part.finish == city::StartFinish::Concrete) {
                r.material = materials.construction_depot_concrete;
                r.tint = {.80f, .79f, .74f, 1};
                r.uv_scale = {std::max(part.width_m, part.depth_m) / 4.f,
                              std::max(part.height_m, 1.f) / 4.f};
            }
            if (part_name_is(part, "halberd runway") || part_name_has(part, "halberd taxiway") ||
                part_name_is(part, "halberd parade ground") ||
                part_name_is(part, "halberd station road") ||
                part_name_is(part, "halberd motor pool")) {
                r.material = materials.finish[finish_index(city::StartFinish::Asphalt)];
                r.tint = {.62f, .62f, .63f, 1};
                r.uv_scale = {part.width_m / 8.f, part.depth_m / 8.f};
            }
            // Every concrete paving plane, or the ones left out fall through to
            // the building rule and come out a different colour from the apron
            // they are part of.
            if (part_name_is(part, "halberd apron") || part_name_is(part, "halberd east apron") ||
                part_name_is(part, "halberd gate apron") ||
                part_name_is(part, "halberd support apron") ||
                part_name_is(part, "halberd barracks walk") ||
                part_name_is(part, "halberd revetment hardstanding") ||
                part_name_is(part, "halberd fuel compound") ||
                part_name_is(part, "halberd magazine apron")) {
                r.material = materials.airport_paving;
                r.tint = {.88f, .87f, .84f, 1};
                r.uv_scale = {part.width_m / 12.f, part.depth_m / 12.f};
            }
            // Earthworks are grassed over, which is what makes a revetment
            // read as dug in rather than as another concrete box.
            if (part_name_is(part, "halberd revetment bank") ||
                part_name_is(part, "halberd magazine mound")) {
                r.material = materials.farm_soil;
                r.tint = {.44f, .52f, .34f, 1};
                r.uv_scale = {std::max(part.width_m, part.depth_m) / 6.f, 1};
            }
            if (part_name_has(part, "halberd fence")) {
                r.material = materials.quickbite_steel; r.tint = {.62f, .64f, .66f, 1};
            }
            // The mesh is alpha-cut. Drawn solid, 2.5 km of perimeter reads as
            // a concrete wall, and a wall is a different building.
            if (part_name_is(part, "halberd fence mesh")) {
                r.material = materials.halberd_chain_link;
                r.tint = {1, 1, 1, 1};
                r.uv_scale = {std::max(part.width_m, part.depth_m) / 2.f, part.height_m / 2.f};
            }
            if (part_name_is(part, "halberd vehicle shed") ||
                part_name_is(part, "halberd shed pillar")) {
                r.material = materials.quickbite_steel; r.tint = {.40f, .44f, .34f, 1};
            }
            // Olive, but not black. The station's north faces are away from
            // the sun all day, and at .34 every building on it read as a
            // silhouette from the apron.
            if (part_name_has(part, " roof") || part_name_is(part, "halberd radar head")) {
                r.material = materials.finish[finish_index(city::StartFinish::DarkRoof)];
                r.tint = {.46f, .49f, .43f, 1};
            }
            if (part_name_is(part, "halberd barracks")) {
                r.material = materials.westmere_cream_stucco; r.tint = {.80f, .76f, .66f, 1};
                r.uv_scale = {part.width_m / 5.f, part.height_m / 5.f};
            }
            if (part_name_is(part, "halberd hangar door") ||
                part_name_is(part, "halberd hangar door rib")) {
                r.material = materials.quickbite_steel; r.tint = {.60f, .63f, .55f, 1};
            }
            if (part_name_has(part, "halberd round")) {
                r.material = materials.quickbite_steel; r.tint = {.70f, .71f, .68f, 1};
            }
            if (part.finish == city::StartFinish::Glass) {
                r.material = materials.glass; r.tint = {.60f, .74f, .78f, .58f};
            }
            if (part_name_is(part, "halberd gate sign")) {
                r.material = materials.halberd_gate_sign;
                r.tint = {1, 1, 1, 1}; r.uv_scale = {1, 1};
            }
            if (const int cell = site_atlas_cell(part, "halberd hangar number ");
                cell >= 0 && cell < static_cast<int>(materials.halberd_number_cells.size())) {
                r.mesh = materials.halberd_number_cells[static_cast<std::size_t>(cell)];
                r.material = materials.halberd_hangar_numbers;
                r.tint = {1, 1, 1, 1}; r.uv_scale = {1, 1};
            }
            if (part_name_has(part, "light lens")) {
                r.material = prototype.material; r.tint = {1, .93f, .78f, 2.4f};
            }
            if (part_name_is(part, "halberd tower beacon lens")) r.tint = {1, .28f, .22f, 3.f};
            if (part_name_is(part, "halberd flag")) {
                r.material = prototype.material; r.tint = {.62f, .16f, .14f, 1};
            }
            if (part_name_is(part, "halberd barrier arm")) {
                r.material = prototype.material; r.tint = {.78f, .22f, .18f, 1};
            }
            // Markings, and ONLY markings. "halberd runway" without the
            // trailing space also matches the runway itself, which is a 900 m
            // plane of asphalt: caught by this rule it came out solid white.
            if (part_name_has(part, "halberd runway ") ||
                part_name_is(part, "halberd taxiway centreline") ||
                part_name_is(part, "halberd parking bay") ||
                part_name_is(part, "halberd station road edge") ||
                part_name_is(part, "halberd apron lead-in")) {
                if (part.height_m < .01f) {
                    r.material = prototype.material;
                    r.tint = part.finish == city::StartFinish::Yellow
                                 ? glm::vec4{.92f, .78f, .18f, 1}
                                 : glm::vec4{.94f, .94f, .92f, 1};
                }
            }
        }
        if(&site==&city::kNeighborhoodBarSite) {
            if(part_name_is(part,"bar interior floor") || part_name_has(part,"counter") ||
                part_name_has(part,"shelf") || part_name_has(part,"pool table cabinet") || part_name_has(part,"booth table")) {
                r.material=materials.marina_timber;r.tint={.62f,.51f,.41f,1};
                r.uv_scale={std::max(part.width_m,part.depth_m)/1.5f,
                    (part.height_m<.35f?part.depth_m:part.height_m)/1.5f};
            }
            if(part_name_is(part,"bar ceiling")) {r.material=materials.bank_ceiling;r.tint={.55f,.48f,.38f,1};}
            if(part_name_is(part,"bar warm light lens")) {r.material=prototype.material;r.tint={1,.69f,.32f,3};}
        }
        if (&site==&city::kGunStoreSite) {
            if(part.finish==city::StartFinish::RedTrim) r.tint={.38f,.14f,.13f,1};
            if(part.finish==city::StartFinish::Steel) r.tint={.30f,.32f,.30f,1};
            if(part_name_is(part,"gun store interior floor")) {
                r.material=materials.finish[finish_index(city::StartFinish::Concrete)];
                r.tint={.66f,.64f,.58f,1};
                r.uv_scale={part.width_m/2.4f,part.depth_m/2.4f};
            }
            if(part_name_is(part,"gun store interior ceiling")) {
                r.material=materials.bank_ceiling;r.tint={.85f,.83f,.75f,1};
            }
            if(part_name_has(part,"light lens")) {
                r.material=prototype.material;r.tint={1,.91f,.76f,2.5f};
            }
            if(part_name_has(part,"display rifle stock") || part_name_has(part,"display rifle foregrip") ||
               part_name_has(part,"rack backboard")) {
                r.material=materials.bank_wood;r.tint={.62f,.44f,.27f,1};
            }
            if(part_name_is(part,"gun store counter solid display volume")) {
                r.material=prototype.material;r.tint={.11f,.16f,.16f,1};
            }
        }
        if (&site==&city::kPawnShopSite) {
            if(part_name_is(part,"pawn television front face") || part_name_is(part,"pawn radio front face") || part_name_is(part,"pawn guitar body")) {
                r.material=part_name_is(part,"pawn television front face")?materials.pawn_tv:
                    (part_name_is(part,"pawn radio front face")?materials.pawn_radio:materials.pawn_guitar);
                r.tint={1,1,1,1};r.uv_scale={1,1};
                if(part_name_is(part,"pawn guitar body") && pawn_guitar_mesh!=kInvalidId)
                    r.mesh=pawn_guitar_mesh;
            }

            if (part_name_is(part,"pawn interior floor")) {
                r.material=materials.quickbite_floor;r.tint={1,1,1,1};
                r.uv_scale={part.width_m/1.2f,part.depth_m/1.2f};
            }
            if (part_name_is(part,"pawn ceiling light lens")) {
                r.material=prototype.material;r.tint={1,.88f,.70f,2.5f};
            }
        }
        if (style==SiteMaterialStyle::Quickbite) {
            const bool tiled_wall=part.solid &&
                (part.finish==city::StartFinish::WarmWall || part.finish==city::StartFinish::Brick);
            const bool restaurant_glass =
                part_name_has(part, "restaurant ") ||
                part_name_is(part, "drive through window");
            if (restaurant_glass && part.finish == city::StartFinish::Glass) {
                r.material = materials.glass;
                r.tint = {1, 1, 1, 1};
            }
            if (tiled_wall) {
                r.material=materials.quickbite_tile;
                r.tint={1,1,1,1};
                r.uv_scale={std::max(part.width_m,part.depth_m)/1.2f,part.height_m/1.2f};
            }
            if (part_name_has(part,"quickbite ") && part_name_has(part,"sign face")) {
                if (part_name_has(part,"menu sign face")) {
                    const bool interior_left=part_name_is(part,"quickbite interior menu sign face left");
                    const bool interior_right=part_name_is(part,"quickbite interior menu sign face right");
                    r.material=interior_left
                        ? materials.quickbite_menu_interior_a
                        : interior_right
                            ? materials.quickbite_menu_interior_b
                            : materials.quickbite_menu_drive;
                } else {
                    r.material=materials.quickbite_brand;
                }
                r.tint={1,1,1,1};r.uv_scale={1,1};
            }
            if (part_name_is(part,"quickbite interior floor")) {
                r.material=materials.quickbite_floor;r.tint={1,1,1,1};
                r.uv_scale={part.width_m/1.2f,part.depth_m/1.2f};
            }
            if (part_name_has(part,"quickbite interior booth ") &&
                !part_name_has(part,"support")) {
                r.material=materials.quickbite_vinyl;r.tint={1,1,1,1};
                r.uv_scale={std::max(part.width_m,part.depth_m)/.8f,
                    (part.height_m<.35f ? part.depth_m:part.height_m)/.8f};
            }
            if ((part_name_has(part,"quickbite interior kitchen ") &&
                 part.finish==city::StartFinish::Steel) ||
                part_name_is(part,"quickbite interior counter top")) {
                r.material=materials.quickbite_steel;r.tint={1,1,1,1};
                r.uv_scale={std::max(part.width_m,part.depth_m),
                    part.height_m<.35f ? part.depth_m:part.height_m};
            }
            if (part_name_is(part,"quickbite interior ceiling")) {
                r.material=materials.bank_ceiling;r.tint={1,1,1,1};
                r.uv_scale={part.width_m/2.4f,part.depth_m/2.4f};
            }
            if (part_name_has(part,"light emitter") ||
                part_name_is(part,"quickbite interior ceiling light lens")) {
                r.material=prototype.material;r.tint={1,.97f,.90f,3.5f};
            }
            // Cheap baked fluorescent bounce keeps the whole restaurant
            // readable at night. The actual ceiling fixtures still cast the
            // dynamic light; this only replaces a costly grid of broad fills.
            if (part_name_has(part,"quickbite interior ") &&
                !part_name_is(part,"quickbite interior ceiling light lens")) {
                r.tint.a=std::max(r.tint.a,1.35f);
            }
            if (tiled_wall && part_name_has(part,"restaurant ")) {
                r.tint.a=std::max(r.tint.a,1.40f);
            }
        }
        if (style==SiteMaterialStyle::Marina) {
            const bool access=part_name_has(part,"marina parking") ||
                              part_name_has(part,"marina boat ramp");
            if(!access) r.material=materials.marina_paint;
            r.uv_scale={std::max(part.width_m,part.depth_m)/2.f,
                        (part.height_m<.35f ? part.depth_m:part.height_m)/2.f};
            if (part.finish==city::StartFinish::WarmWall) {
                r.material=materials.marina_timber;r.tint={1,1,1,1};
                // Each physical plank gets ONE board-width from the chart,
                // not eight tiny boards repeated across a 36 cm strip.
                if (part_name_has(part,"plank")) r.uv_scale={.125f,part.depth_m/2.f};
            }
            if (part_name_has(part,"shed roof")) {
                r.material=materials.marina_roof;r.tint={1,1,1,1};
            }
            if (part_name_is(part,"marina sign face")) {
                r.material=materials.marina_sign;r.tint={1,1,1,1.2f};r.uv_scale={1,1};
            }
            if (part_name_has(part,"rope") || part_name_has(part,"cord")) {
                r.material=prototype.material;r.tint={.68f,.57f,.37f,1};
            }
            if (part_name_has(part,"light emitter")) {
                r.material=prototype.material;r.tint={1,.82f,.52f,3.5f};
            }
        }
        // Reuse the existing plaster/concrete/metal assets at physical scale.
        // Miandi's ordinary paint stays non-emissive; the tubes override below.
        if (miandi_detailed_site) {
            const auto paint = city::miandi_presentation(
                part, &site == &city::kMiandiOceanDriveSite);
            r.tint = {paint.tint[0], paint.tint[1], paint.tint[2], paint.tint[3]};
            r.uv_scale = {paint.u_tiles, paint.v_tiles};
            using city::MiandiSurface;
            switch (paint.surface) {
                case MiandiSurface::Stucco:
                    r.material = materials.finish[finish_index(city::StartFinish::WarmWall)]; break;
                case MiandiSurface::Concrete:
                    r.material = materials.finish[finish_index(city::StartFinish::Concrete)]; break;
                case MiandiSurface::Metal:
                    r.material = materials.finish[finish_index(city::StartFinish::Steel)]; break;
                case MiandiSurface::Asphalt:
                    r.material = materials.finish[finish_index(city::StartFinish::Asphalt)]; break;
                case MiandiSurface::Brick:
                    r.material = materials.finish[finish_index(city::StartFinish::Brick)]; break;
                case MiandiSurface::Plain: break;
            }
            if (part_name_has(part, "resort planter soil")) {
                r.material = materials.farm_soil;
                r.tint = {.8f, .8f, .8f, 1.f};
            }
        }
        if (miandi_detailed_site || &site == &city::kMiandiSunwaveHotelSite ||
            &site == &city::kMiandiCalleNocheSite) {
            const auto tint = city::miandi_venue_tint(
                part, {r.tint.r, r.tint.g, r.tint.b, r.tint.a});
            r.tint = {tint[0], tint[1], tint[2], tint[3]};
        }
        // Miandi venue packages identify their thin tubes explicitly. The
        // alpha-above-one channel makes the tube itself glow; separate authored
        // tiled lights provide the actual wash on walls and paving at night.
        if (part_name_has(part, "miandi neon")) {
            r.material = prototype.material;
            r.tint.a = 4.5f;
            if (part_name_has(part, "80s")) {
                // Violet is Club Mirage's, and it needs the saturated palette
                // to exist at all: outside it, a tube takes its pastel finish
                // tint times an emissive 4.5 and blows out to white, which is
                // how the club's "violet" trim rendered as plain strip light.
                const bool violet = part_name_has(part, "violet");
                const bool pink = !violet &&
                                  part.finish == city::StartFinish::RedTrim;
                const bool core = part_name_has(part, "core");
                r.tint = violet ? glm::vec4{.42f, .05f, 1.0f, 2.0f}
                       : pink   ? glm::vec4{1.0f, .015f, .32f, 2.0f}
                                : glm::vec4{.015f, .75f, 1.0f, 2.0f};
                if (core)
                    r.tint = violet ? glm::vec4{.76f, .55f, 1.0f, 2.0f}
                           : pink   ? glm::vec4{1.0f, .48f, .73f, 2.0f}
                                    : glm::vec4{.48f, 1.0f, 1.0f, 2.0f};
            } else if (part_name_has(part, "amber") &&
                       part_name_has(part, "lettering")) {
                r.tint = part_name_has(part, "core")
                    ? glm::vec4{1.0f, .80f, .40f, 2.0f}
                    : glm::vec4{1.0f, .40f, .06f, 2.0f};
            }
        }
        if(part.shape==city::BuildingPieceShape::GablePrism)r.mesh=gable_prism;
        const Transform t = part_transform(site, part);
        city::append_precipitation_cover(part, t, precipitation_cover);
        const NodeId id = scene.create(r, t, unit_bounds);
        if (SceneNode* node = scene.get(id)) {
            node->max_draw_distance = site.max_draw_distance_m;
        }
        nodes.push_back(id);

        if (window_registration && window_registration->windows &&
            (is_skyscraper_window_light(part) ||
             (window_registration->existing_dark_pane &&
              is_pinatty_apartment_window(part)))) {
            uint64_t building_key=window_registration->building_key;
            if(window_registration->split_twin_towers) {
                building_key=splitmix64_mix(building_key ^
                    (part.centre.x<0.f ? 0x4C454654ull : 0x5249474854ull));
            }
            const uint64_t floor_key=splitmix64_mix(building_key ^
                static_cast<uint64_t>(static_cast<uint32_t>(
                    window_key_coord(part.bottom_m))));
            window_registration->windows->push_back({id,
                {building_key,floor_key,window_piece_key(building_key,part),
                 window_registration->use},
                !window_registration->existing_dark_pane,
                {site.origin.x,site.origin.z}});
            // Emitter overlays start hidden. Existing infill panes retain
            // their dark glass surface until the first schedule sync.
            if(!window_registration->existing_dark_pane) {
                if(SceneNode* node=scene.get(id)) node->visible=false;
            }
        }

        if (part.solid) {
            if ((loom_site || miandi_detailed_site || natural_yard || style == SiteMaterialStyle::Construction ||
                 style == SiteMaterialStyle::Westmere || &site == &city::kGunStoreSite || part_name_has(part,"bar ") || part_name_has(part,"station ") || part_name_has(part,"tower ") || part_name_has(part,"infill ") || &site == &city::kPawnShopSite || &site == &city::kGasStationSite || &site == &city::kBankSite || &site == &city::kAutoRepairSite ||
                 &site == &city::kLaundromatSite || &site == &city::kFastFoodSite ||
                 &site == &city::kTacomacoSite || &site == &city::kEastArmPlazaSite) && part.pitch_deg == 0.0f &&
                part.roll_deg == 0.0f) {
                collider.add_static_oriented_box(t.position, t.scale * 0.5f,
                    std::atan2(site.sin_yaw, site.cos_yaw) + glm::radians(part.yaw_deg));
            } else {
                collider.add_static_box(unit_bounds.transformed(t.matrix()),
                                        Surface::Rock);
            }
        }
        if (style==SiteMaterialStyle::Marina && part_name_is(part,"marina deck structure")) {
            collider.add_static_ground_rect({t.position.x,t.position.z},city::kMarinaDeckTop,
                {part.width_m*.5f,part.depth_m*.5f},0,Surface::Rock);
        }
        if ((loom_site && city::loom_ground_piece(part)) || city::residential_ground_piece(part) || city::luxury_ground_piece(part) || city::tidewater_farm_ground_piece(part) || city::gun_store_ground_piece(part) || city::pinatty_infill_ground_piece(part) || (&site == &city::kEastArmPlazaSite && (part_name_has(part, "parking lot") || part_name_has(part, " walk") || part_name_has(part, "court") || part_name_has(part, "service lane") || part_name_has(part, "interior floor") || part_name_has(part, "threshold"))) || is_building_plot_pavement(part) ||
            part_name_is(part,"marina parking lot") || part_name_has(part,"terminal parking walk") ||
            part_name_has(part,"terminal splitter island") ||
            part_name_has(part,"terminal pedestrian connector") ||
            part_name_has(part,"terminal pedestrian plaza") ||
            part_name_is(part,"quickbite front pedestrian walk") ||
            part_name_is(part,"quickbite interior floor") ||
            part_name_is(part,"pawn entrance walk") || part_name_is(part,"pawn interior floor") ||
            part_name_is(part,"pawn interior threshold") ||
            part_name_is(part,"tower plaza lot") || part_name_is(part,"tower podium floor") ||
            part_name_is(part,"tower entrance walk") ||
            part_name_is(part,"construction site lot") ||
            part_name_is(part,"construction haul pad") ||
            part_name_is(part,"construction sidewalk") ||
            part_name_is(part,"materials depot lot") ||
            part_name_is(part,"materials loading apron") ||
            part_name_is(part,"materials parking strip") ||
            part_name_is(part,"equipment rental lot") ||
            part_name_is(part,"equipment service apron") ||
            part_name_is(part,"equipment rental entrance walk") ||
            part_name_is(part,"equipment storage pad") ||
            part_name_is(part,"construction detail service apron") ||
            part_name_is(part,"construction detail worker parking pad") ||
            part_name_is(part,"construction detail truck turn pad") ||
            part_name_is(part,"construction expansion site lot") ||
            part_name_is(part,"construction expansion haul pad") ||
            part_name_is(part,"construction twin block lot") ||
            part_name_is(part,"construction twin block plaza") ||
            part_name_is(part,"hospital campus superblock lot") ||
            part_name_is(part,"hospital wing floor") ||
            ((part_name_has(part,"hospital overhaul ") ||
              part_name_has(part,"hospital mobility ")) &&
             ((part_name_has(part," floor") &&
               !part_name_has(part,"floor band")) ||
              part_name_has(part," hardstand") ||
              part_name_has(part," lane") ||
              part_name_has(part," apron") ||
              part_name_has(part," throat") ||
              part_name_has(part," walk") ||
              part_name_has(part," landing") ||
              part_name_has(part," boarding pad") ||
              part_name_has(part," parking pad"))) ||
            (part_name_has(part,"hospital north court ") &&
             part_name_has(part," walk")) ||
            (part_name_has(part,"hospital south court ") &&
             (part_name_has(part," walk") ||
              part_name_has(part," floor"))) ||
            part_name_is(part,"hospital entrance walk") ||
            part_name_is(part,"hospital emergency dropoff") ||
            part_name_is(part,"hospital emergency ambulance entrance throat") ||
            part_name_is(part,"hospital emergency ambulance exit throat") ||
            part_name_is(part,"hospital helipad deck") ||
            part_name_is(part,"hospital garage lot") ||
            part_name_is(part,"hospital garage ground deck") ||
            part_name_is(part,"hospital garage upper deck") ||
            part_name_is(part,"hospital garage entry walk") ||
            part_name_is(part,"hospital north parking pedestrian spine") ||
            part_name_is(part,"station interior floor") || part_name_is(part,"station entrance walk") ||
            part_name_is(part,"station apparatus apron") ||
            part_name_is(part,"bar interior floor") || part_name_is(part,"bar front pavement") ||
            part_name_is(part,"bar back alley") || part_name_is(part,"bar alley entrance walk") ||
            part_name_is(part,"bar front threshold") || part_name_is(part,"bar alley threshold") ||
            part_name_is(part,"store interior floor") || part_name_is(part,"store interior entrance threshold") ||
            (&site == &city::kHalberdFieldSite && city::halberd_ground_piece(part)) ||
            (miandi_detailed_site && city::miandi_ground_piece(part))) {
            const float site_yaw = std::atan2(site.sin_yaw, site.cos_yaw);
            const float local_yaw = glm::radians(part.yaw_deg);
            collider.add_static_ground_rect(
                {t.position.x, t.position.z},
                site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f},
                site_yaw + local_yaw, Surface::Rock);
        }
    }

    if(natural_yard) return; // Supported paths stay firm; the yards stay grass.
    // The lot is pavement visually and should drive like pavement too. The
    // collider's paint is axis-aligned, so use the rotated lot's world AABB;
    // the six-degree excess at the corners is under two metres and stays
    // inside the surrounding downtown block.
    city::StartPart lot;
    lot.centre = site.lot_centre;
    lot.width_m = site.lot_width_m;
    lot.height_m = 0.10f;
    lot.depth_m = site.lot_depth_m;
    const AABB lot_bounds = unit_bounds.transformed(part_transform(site, lot).matrix());
    collider.paint_surface(lot_bounds, Surface::Rock);
}

}  // namespace

// How far out road ribbons are drawn, in metres.
//
// IT WAS 640, AND THE REASON IT WAS 640 HAS BEEN FIXED.
//
// Ribbons are draped onto the LEVEL 0 drawn surface by the baker, via
// mesh_height_at(). Past the level 0 and level 1 rings the terrain beneath them
// is drawn coarser, and the ribbon and the ground it was draped on stop being
// the same surface. Measured over Marrow's quarry before src/city/ authored any
// roads (tests/terrain_lod_tests.cpp):
//
//     level 1 (2 m)  mean 0.002 m  worst 0.324 m
//     level 2 (4 m)  mean 0.010 m  worst 0.677 m
//     level 3 (8 m)  mean 0.036 m  worst 1.020 m
//
// That last number is why this was pinned to the outer edge of the level 1
// ring. The proper fix was named there: a terrain operator that carves the road
// corridor into the height field itself, so every level agrees about where the
// road bed is. src/city/roads.h now derives one from every road that needs it,
// and tests/city_roads_tests.cpp measures the result over the vertices the
// baker actually emitted rather than over a patch of hillside:
//
//     level 1 (2 m)  mean 0.0001 m  worst 0.110 m
//     level 2 (4 m)  mean 0.0003 m  worst 0.146 m
//     level 3 (8 m)  mean 0.0011 m  worst 0.311 m
//
// Inside a corridor at full weight the height IS the corridor's own profile --
// linear along it, constant across it, a plane -- and every LOD level samples
// the same lattice and interpolates linearly, so a plane comes back a plane.
// The residual is at hairpins and at junctions where two graded roads meet at
// different gradients. So there is no longer a drape reason to draw roads any
// nearer than everything else, and this matches the renderer's distance.
//
// ONE HONEST CAVEAT, because the name of this constant over-promises.
// max_draw_distance is a PER-NODE cull and RoadMeshes::attach creates six
// nodes -- one per material -- each with the bounds of the whole island. The
// cull measures to the closest point of a node's box, so a camera standing
// anywhere on the island is zero metres from all six. This value is therefore a
// coarse on/off switch for the whole network and never was a ring; the 640 m
// version culled nothing either. Making it a real distance ring means tiling
// the bake spatially, which is src/road/ and src/gfx/ work and not this
// module's to do. Said out loud rather than left reading like a budget.
constexpr float kRoadDrawDistanceMetres = 2400.0f;

bool World::init(Renderer& renderer, uint64_t seed, const StreamerConfig& cfg) {
    streamer_ = Streamer(seed, cfg);
    seed_ = seed;

    if (!roads_.init(renderer, seed)) {
        AP_ERROR("world: road materials failed");
        return false;
    }


    // --- terrain material ----------------------------------------------------
    // ONE material for every chunk at every level. That is what lets the batch
    // key vary only by mesh, so the whole visible ring collapses into one draw
    // per chunk mesh rather than one program switch per chunk.
    //
    // The shared noise provides grain; per-vertex surface weights tint grass,
    // rock, gravel and sand without adding per-chunk material switches.
    Texture ground;
    if (!ground.make_noise(256, 8, 4, glm::vec3{0.19f, 0.28f, 0.13f},
                           glm::vec3{0.45f, 0.52f, 0.28f},
                           seed ^ 0x6C0FFEEull)) {
        AP_ERROR("world: terrain texture generation failed");
        return false;
    }
    proto_.terrain.material = renderer.add_material(std::move(ground));
    proto_.terrain.mesh = kInvalidId;  // per chunk, filled by the streamer
    proto_.terrain.tint = glm::vec4{1.0f};
    // Chunk UVs are world-space in chunk units, so one repeat per 8 m keeps the
    // tiling identical at every level: the coarse rings are the same ground,
    // sampled less often, not a different-looking ground.
    // Negative Y opts into terrain weights; vehicles retain their own UV mask.
    proto_.terrain.uv_scale = {kChunkMetres / 8.0f,-kChunkMetres / 8.0f};

    Texture bark;
    if (!bark.make_noise(128, 6, 3, glm::vec3{0.16f, 0.22f, 0.11f},
                         glm::vec3{0.34f, 0.44f, 0.20f}, seed ^ 0x77EEull)) {
        AP_ERROR("world: foliage texture generation failed");
        return false;
    }
    const MaterialId tree_mat = renderer.add_material(std::move(bark));

    Texture palm;
    if (!palm.make_noise(128, 5, 3, glm::vec3{0.17f, 0.22f, 0.07f},
                         glm::vec3{0.48f, 0.55f, 0.16f},
                         seed ^ 0x0A11CEu)) {
        AP_ERROR("world: palm texture generation failed");
        return false;
    }
    const MaterialId palm_mat = renderer.add_material(std::move(palm));

    Texture stone;
    if (!stone.make_noise(128, 8, 3, glm::vec3{0.32f, 0.31f, 0.30f},
                          glm::vec3{0.63f, 0.62f, 0.59f}, seed ^ 0x57012Eull)) {
        AP_ERROR("world: stone texture generation failed");
        return false;
    }
    const MaterialId rock_mat = renderer.add_material(std::move(stone));

    // --- prop models ---------------------------------------------------------
    // One mesh per variant, shared by every instance of it in the world. That
    // sharing is the entire reason a hillside of trees is a handful of draws:
    // the batch key is (material, mesh), so ten thousand trees of four variants
    // is four draws.
    for (uint8_t v = 0; v < kTreeVariants; ++v) {
        proto_.tree[v].mesh = renderer.add_mesh(make_tree(v));
        proto_.tree[v].material = is_palm_tree_variant(v) ? palm_mat : tree_mat;
        if (proto_.tree[v].mesh == kInvalidId) {
            AP_ERROR("world: tree variant %u failed to upload", v);
            return false;
        }
    }
    for (uint8_t v = 0; v < kRockVariants; ++v) {
        proto_.rock[v].mesh = renderer.add_mesh(make_rock(v));
        proto_.rock[v].material = rock_mat;
        if (proto_.rock[v].mesh == kInvalidId) {
            AP_ERROR("world: rock variant %u failed to upload", v);
            return false;
        }
    }

    AP_INFO("world: seed 0x%016llX, rings %d/%d/%d/%d chunks "
            "(%.0f/%.0f/%.0f/%.0f m), scatter to level %d",
            static_cast<unsigned long long>(seed), cfg.lod_ring[0],
            cfg.lod_ring[1], cfg.lod_ring[2], cfg.load_radius,
            static_cast<double>(cfg.lod_ring[0]) * kChunkMetres,
            static_cast<double>(cfg.lod_ring[1]) * kChunkMetres,
            static_cast<double>(cfg.lod_ring[2]) * kChunkMetres,
            static_cast<double>(cfg.load_radius) * kChunkMetres,
            cfg.max_scatter_lod);
    return true;
}

void World::sync_burgerpiz_parking_lamps(Scene& scene,float night_level) {
    for(const auto id:burgerpiz_parking_lens_nodes_)
        if(auto* node=scene.get(id))node->renderable.tint=street_lamp_lens_tint(night_level);
}

void World::update(Scene& scene, Renderer& renderer, glm::vec3 focus,
                   StepMode mode) {
    const StreamerStats st = streamer_.step(scene, proto_, focus, mode);

    stats_.chunks_refitted = st.chunks_refitted;
    stats_.chunks_evicted = st.chunks_evicted;
    stats_.instances_activated = st.instances_activated;
    stats_.budget_exhausted = st.budget_exhausted;
    stats_.chunks_built = 0;
    stats_.quads_built = 0;
    stats_.meshes_freed = 0;

    // --- free first, then upload --------------------------------------------
    // In that order on purpose. Freeing first lets the mesh table hand the same
    // slot straight back to this frame's uploads, so a player driving in a
    // straight line holds a flat number of slots instead of a growing one. The
    // generation tag is what makes reuse this eager safe: a stale handle
    // resolves to nullptr rather than to whatever moved in.
    //
    // Every id here is one the streamer has already removed the scene nodes
    // for -- Streamer::step() ends its eviction with Scene::remove_many()
    // before it returns.
    released_scratch_.clear();
    streamer_.take_released_meshes(released_scratch_);
    for (const MeshId id : released_scratch_) {
        if (renderer.remove_mesh(id)) ++stats_.meshes_freed;
    }

    for (const ChunkRequest& r : streamer_.pending_loads()) {
        const ChunkMesh mesh = build_chunk(streamer_.seed(), r.coord, r.lod);
        const MeshId id = renderer.add_mesh(mesh);
        if (id == kInvalidId) {
            // The upload failed. Do NOT deliver: an invalid handle activated
            // into a scene node is a chunk that draws nothing forever, and the
            // streamer would consider that coordinate resident and never ask
            // again. Dropping the delivery leaves it neither resident nor in
            // flight, so the next plan simply asks for it again.
            AP_ERROR("world: chunk (%d, %d) lod %d failed to upload; it will be "
                     "requested again next step",
                     r.coord.x, r.coord.z, r.lod);
            continue;
        }
        streamer_.deliver(r.coord, r.lod, id, mesh.bounds);
        ++stats_.chunks_built;
        stats_.quads_built += lod_quads(r.lod) * lod_quads(r.lod);
    }

    // deliver() can drop a stale delivery and hand its mesh straight back, so
    // drain once more rather than leaving it until next frame.
    released_scratch_.clear();
    streamer_.take_released_meshes(released_scratch_);
    for (const MeshId id : released_scratch_) {
        if (renderer.remove_mesh(id)) ++stats_.meshes_freed;
    }

    stats_.resident_chunks = streamer_.resident_count();
    streamer_.residency_by_lod(stats_.resident_by_lod);
    stats_.live_meshes = renderer.mesh_count();
    stats_.mesh_bytes = renderer.mesh_bytes();
}

void World::sync_skyscraper_window_lights(
    Scene& scene, uint64_t session_seed, uint64_t absolute_step,
    float visible_time_of_day, float darkness, float time_of_day_per_step,
    glm::vec3 viewer_position) {
    // Four schedule samples a second is plenty for room switches, and avoids
    // hashing nine thousand panes on every high-refresh render frame.
    constexpr uint64_t kSyncSteps=30u;
    const uint64_t bucket=absolute_step/kSyncSteps;
    const float clamped_darkness=std::clamp(darkness,0.0f,1.0f);
    float time_delta=std::fabs(visible_time_of_day-skyscraper_window_sync_time_);
    time_delta=std::min(time_delta,std::fabs(1.0f-time_delta));
    const bool visible_clock_jump=time_delta>=0.01f;
    const bool darkness_jump=
        std::fabs(clamped_darkness-skyscraper_window_sync_darkness_)>=0.10f;
    const bool clock_rate_changed=
        time_of_day_per_step!=skyscraper_window_sync_time_rate_;
    const bool refresh_targets=bucket!=skyscraper_window_sync_bucket_ ||
        session_seed!=skyscraper_window_sync_seed_ || visible_clock_jump ||
        darkness_jump || clock_rate_changed;
    // Rendering can outpace the 120 Hz simulation clock. There is no visual
    // progress to apply twice at the same step, but target refreshes still
    // need to run for a dev-clock or lighting change.
    if(!refresh_targets &&
       absolute_step==skyscraper_window_presentation_step_) return;

    if(refresh_targets) {
        skyscraper_window_sync_bucket_=bucket;
        skyscraper_window_sync_seed_=session_seed;
        skyscraper_window_sync_darkness_=clamped_darkness;
        skyscraper_window_sync_time_=visible_time_of_day;
        skyscraper_window_sync_time_rate_=time_of_day_per_step;
    }
    skyscraper_window_presentation_step_=absolute_step;
    skyscraper_window_stats_={};

    constexpr glm::vec4 kDarkGlass{0.045f,0.115f,0.14f,1.0f};
    for(auto& window:skyscraper_windows_) {
        SceneNode* node=scene.get(window.node);
        if(!node) continue;
        ++skyscraper_window_stats_.panes;
        switch(window.address.use) {
            case city::SkyscraperUse::Office:
                ++skyscraper_window_stats_.office;break;
            case city::SkyscraperUse::Residential:
                ++skyscraper_window_stats_.residential;break;
            case city::SkyscraperUse::Mixed:
                ++skyscraper_window_stats_.mixed;break;
        }
        if(refresh_targets) {
            const float viewer_distance=glm::length(
                window.building_position-glm::vec2{viewer_position.x,
                                                    viewer_position.z});
            window.target_light=city::skyscraper_window_lod_light(
                window.lod,window.address,session_seed,absolute_step,
                visible_time_of_day,clamped_darkness,viewer_distance,
                time_of_day_per_step);
        }
        const auto light=city::skyscraper_window_smoothed_light(
            window.presentation,window.target_light,absolute_step);
        if(window.lod.dynamic) ++skyscraper_window_stats_.dynamic_lod;
        else ++skyscraper_window_stats_.static_lod;
        if(light.lit) {
            node->visible=true;
            node->renderable.tint={light.tint,light.emissive_alpha};
            ++skyscraper_window_stats_.lit;
        } else {
            node->visible=!window.hide_when_dark;
            node->renderable.tint=kDarkGlass;
        }
    }
}

bool World::set_roads(Renderer& renderer, Scene& scene, TerrainCollider& collider,
                      const std::vector<RoadSpine>& spines) {
    roads_.detach(scene);
    roads_.release(renderer);
    crowd_.clear();
    lane_graph_.clear();
    road_graph_.clear();

    if (spines.empty()) {
        collider.clear_road_collision();
        return true;  // nothing authored yet; not a failure
    }

    // The ground the ribbons drape onto is the MESHED surface, not the height
    // field. TerrainGround binds the right one — road_graph.h makes that a
    // parameter rather than a call precisely so it cannot be got wrong here,
    // and tests/city_roads_tests.cpp re-checks it across all 65,514 draped
    // vertices of the real map: every one of them sits on the level 0 surface
    // to within a millimetre, and the suite fails if one does not.
    const TerrainGround ground{seed_};

    road_graph_.build(spines, RoadGraphParams{}, ground.sampler());
    // O'Haven is an American setting: traffic keeps right, and all turn
    // priorities are derived from this same handedness choice in LaneGraph.
    LaneBuildParams lane_params;
    lane_params.drive_on_right = true;
    lane_graph_.build(road_graph_, ground.sampler(), lane_params);

    // The analytic schedule default is a stress-test density. In the authored
    // city that packed roughly 300 active cars into the 220 m player bubble and
    // made every signal look like rush hour. Keep the streets alive without
    // filling every available lane slot.
    ambient_tuning_.vehicle_spacing_m = 48.0f;
    ambient_tuning_.max_vehicle_slots = 16;
    // Kerbside parked cars are drawn (PENG-48) and are obstacles to AI drivers
    // (PENG-49). The GAME used to opt out of the second half, because a full
    // kerb deadlocked everything that tried to move sideways past it: a
    // civilian's pull-aside for a siren was vetoed by the parked bodies, it
    // stopped in the lane, and this check's cruiser sat boxed at 70 m for a
    // minute where it reaches the player in fifteen seconds with the obstacles
    // off. Drivers passing THROUGH parked cars was the lesser of the two.
    //
    // THE OPT-OUT IS GONE, because the deadlock it worked around is fixed: see
    // emergency_path_clear's scenery rule and tests/parked_corridor_tests.cpp.
    // Measured on --police-pursuit-check, all three states:
    //
    //   obstacles on, before the fix   still 66 m out at 36 s, never arrives
    //   obstacles off (the opt-out)    PASS, officer on the player at 15.2 s
    //   obstacles on, after the fix    PASS, officer on the player at 26.0 s
    //
    // The 11 s between the last two is not a deadlock and not a regression to
    // chase here: it is what parked cars cost. A Street with a full kerb has
    // nowhere for a civilian to pull over, so a siren waits behind it, and
    // that is the answer a real city gives. Buying those seconds back by
    // driving every AI car through every parked body is the worse trade —
    // that one is visible on every street in the game, and the pursuit check
    // passes either way. If pursuit pacing needs them, that is a police
    // routing ticket; restoring the opt-out is the one line below.
    // See docs/traffic-realism.md.
    // The same active-set code now drives finished character models. A calmer
    // gap keeps pavements alive without turning each road into a marching line.
    ambient_tuning_.ped_spacing_m = 52.0f;
    ambient_tuning_.max_ped_slots = 8;
    crowd_.build(lane_graph_, seed_, ambient_tuning_, crowd_tuning_);

    RibbonBake bake = bake_ribbons(road_graph_, ground.sampler());
    const auto access=city::bake_building_access(road_graph_,bake,ground.sampler());
    city::append_building_access(bake,access);
    city::append_luxury_driveways(bake,ground.sampler());
    access_layout_.lots.clear();
    for(const auto& lot:access.lots) {
        city::BuildingAccessResult layout;
        layout.name=lot.name;layout.site=lot.site;layout.plot=lot.plot;
        layout.parking_shift=lot.parking_shift;
        access_layout_.lots.push_back(std::move(layout));
    }

    for (const auto& lot:access.lots) {
        if (!lot.connected) AP_WARN("building driveway not connected: %s",lot.name);
    }
    RoadCollision road_collision = build_road_collision(bake);
    city::append_airport_garage_collision(road_collision,city::bake_airport_parking_garage());
    city::append_marina_access_collision(road_collision,city::bake_marina_access());
    collider.set_road_collision(road_collision);
    AP_INFO("roads: %zu spines -> %zu nodes, %zu edges, %zu junctions, "
            "%zu plates, %zu crosswalks, %zu triangles / %zu collision",
            spines.size(), road_graph_.node_count(), road_graph_.edge_count(),
            road_graph_.junctions().size(), bake.plates_baked, bake.crosswalks_baked,
            bake.total_triangles(), collider.road_triangle_count());
    AP_INFO("traffic: %zu directed lanes, %zu junctions, deterministic active "
            "radius %.0f m",
            lane_graph_.lane_count(), lane_graph_.junction_count(),
            static_cast<double>(crowd_tuning_.vehicle_activate_m));

    if (!roads_.upload(renderer, bake)) return false;
    roads_.attach(scene, kRoadDrawDistanceMetres);
    return true;
}

void World::step_traffic(int64_t step, const VehicleState& player,
                         const OnFootTrafficHazard* on_foot_player) {
    const int cadence = std::max(1, crowd_tuning_.refresh_every_steps);
    const glm::vec2 focus = on_foot_player
        ? on_foot_player->position
        : glm::vec2{player.position.x, player.position.z};
    if (step % cadence == 0)
        crowd_.refresh(step, focus);
    crowd_.rebuild_buckets();
    crowd_.step_vehicles(step, &player, on_foot_player);
    // The player's car is a hazard to people on the pavement, not only to
    // other drivers. A stopped car passes the same pointer and startles
    // nobody, which is what the closing-speed gate is for.
    crowd_.step_peds(step, &player);
}

bool World::resolve_traffic_collision(VehicleState& player,
                                      float player_half_width_m,
                                      float player_half_length_m,
                                      float player_mass_kg,
                                      float player_body_damage_gain) {
    return crowd_.resolve_player_collision(
        player, player_half_width_m, player_half_length_m, player_mass_kg, player_body_damage_gain);
}

bool World::inside_authored_interior(glm::vec3 position, float margin_m) const {
    return std::any_of(interior_streaming_volumes_.begin(),
                       interior_streaming_volumes_.end(),
                       [&](const city::InteriorStreamingVolume& volume) {
                           return city::contains(volume, position, margin_m);
                       });
}

bool World::set_starting_area(Renderer& renderer, Scene& scene,
                              TerrainCollider& collider) {
    if (start_box_mesh_ != kInvalidId || start_decal_mesh_ != kInvalidId ||
        start_billboard_mesh_ != kInvalidId ||
        start_rounded_box_mesh_ != kInvalidId ||
        start_cylinder_mesh_ != kInvalidId || start_gable_mesh_ != kInvalidId || !start_nodes_.empty()) {
        AP_ERROR("world: starting area was attached more than once");
        return false;
    }

    StartMaterials start_materials;
    if (!load_start_materials(renderer, start_materials)) return false;
    museum_art_meshes_=start_materials.museum_paintings;
    museum_amphora_mesh_=start_materials.museum_amphora;
    museum_meshes_.clear();
    museum_meshes_.insert(museum_meshes_.end(),start_materials.museum_plaque_cells.begin(),
                          start_materials.museum_plaque_cells.end());
    museum_meshes_.insert(museum_meshes_.end(),start_materials.museum_panel_cells.begin(),
                          start_materials.museum_panel_cells.end());
    museum_meshes_.insert(museum_meshes_.end(),start_materials.museum_label_cells.begin(),
                          start_materials.museum_label_cells.end());
    for(const MeshId mesh:{start_materials.museum_sphere,start_materials.museum_cone,
                           start_materials.museum_wheel,start_materials.museum_skull,
                           start_materials.museum_ring})
        museum_meshes_.push_back(mesh);

    const MeshData unit = make_box(glm::vec3{0.5f});
    start_box_mesh_ = renderer.add_mesh(unit);
    start_decal_mesh_ = renderer.add_mesh(make_decal_quad());
    start_billboard_mesh_ = renderer.add_mesh(make_billboard_quad());
    start_rounded_box_mesh_ =
        renderer.add_mesh(make_rounded_box(glm::vec3{0.5f}, 0.16f, 8));
    start_cylinder_mesh_ = renderer.add_mesh(make_cylinder(0.5f, 0.5f, 18));
    start_gable_mesh_ = renderer.add_mesh(make_gable_prism());
    pawn_guitar_mesh_=renderer.add_mesh(make_pawn_guitar_body());
    if (start_box_mesh_ == kInvalidId ||
        start_decal_mesh_ == kInvalidId ||
        start_billboard_mesh_ == kInvalidId ||
        start_rounded_box_mesh_ == kInvalidId ||
        start_cylinder_mesh_ == kInvalidId || start_gable_mesh_==kInvalidId || pawn_guitar_mesh_==kInvalidId) {
        AP_ERROR("world: starting-area prop meshes failed to upload");
        return false;
    }

    Renderable r;
    r.mesh = start_box_mesh_;
    r.material = renderer.white_material();
    skyscraper_windows_.clear();
    skyscraper_windows_.reserve(10000u);
    skyscraper_window_sync_bucket_=UINT64_MAX;
    skyscraper_window_presentation_step_=UINT64_MAX;
    skyscraper_window_sync_darkness_=-1.0f;
    skyscraper_window_sync_time_=-1.0f;

    std::vector<city::StartPart> gas_parts =
        city::bake_building(city::kGasStationPlan);
    std::vector<city::StartPart> car_wash_parts =
        city::bake_building(city::kCarWashPlan);
    std::vector<city::StartPart> motel_parts =
        city::bake_building(city::kMotelPlan);
    std::vector<city::StartPart> apartment_parts =
        city::bake_building(city::kApartmentPlan);
    std::vector<city::StartPart> fast_food_parts =
        city::bake_building(city::kFastFoodPlan);
    std::vector<city::StartPart> bank_parts =
        city::bake_building(city::kBankPlan);
    auto repair_parts = city::bake_auto_repair();
    auto laundry_parts = city::bake_laundromat();
    std::vector<city::StartPart> airport_parts = city::bake_airport();
    std::vector<city::StartPart> florangia_airport_parts =
        city::bake_florangia_airport();
    auto miandi_street_parts = city::bake_miandi_street_fixtures();
    auto miandi_calle_ocho_parts = city::bake_miandi_calle_ocho();
    auto miandi_bayfront_parts = city::bake_miandi_bayfront();
    auto miandi_context_parts = city::bake_miandi_context();
    auto miandi_calle_noche_parts = city::bake_miandi_calle_noche();
    auto miandi_prism_works_parts = city::bake_miandi_prism_works();
    auto miandi_mariposa_parts = city::bake_miandi_mariposa_motel();
    auto miandi_sunwave_parts = city::bake_miandi_sunwave_hotel();
    auto miandi_ocean_drive_parts = city::bake_miandi_ocean_drive();
    const auto miandi_resort_frontage_parts = city::bake_miandi_resort_frontage();
    miandi_ocean_drive_parts.insert(miandi_ocean_drive_parts.end(),
        miandi_resort_frontage_parts.begin(), miandi_resort_frontage_parts.end());
    auto miandi_promenade_parts = city::bake_miandi_north_promenade();
    auto miandi_port_sol_parts = city::bake_miandi_port_sol();
    std::vector<city::StartPart> miandi_rough_parts;
    miandi_rough_parts.reserve(city::kMiandiBuildingPartCount);
    for (const city::StartPart& part : city::kMiandiBuildingParts) {
        if (city::miandi_keeps_rough_part(part))
            miandi_rough_parts.push_back(part);
    }
    const auto garage=city::bake_airport_parking_garage();
    airport_parts.erase(std::remove_if(airport_parts.begin(),airport_parts.end(),
        city::airport_garage_replaces),airport_parts.end());
    airport_parts.insert(airport_parts.end(),garage.parts.begin(),garage.parts.end());

    interior_streaming_volumes_.clear();
    precipitation_cover_.clear();
    const auto register_interior = [&](const city::StartSite& site,
                                       const auto& parts) {
        city::append_interior_streaming_volumes(
            site, parts.data(), parts.size(), interior_streaming_volumes_);
    };
    register_interior(city::kGasStationSite, gas_parts);
    register_interior(city::kCarWashSite, car_wash_parts);
    register_interior(city::kMotelSite, motel_parts);
    register_interior(city::kApartmentSite, apartment_parts);
    register_interior(city::kFastFoodSite, fast_food_parts);
    register_interior(city::kBankSite, bank_parts);
    register_interior(city::kAutoRepairSite, repair_parts);
    register_interior(city::kLaundromatSite, laundry_parts);

    // The ramp tops use the same vertices as the driving surface. Add a real
    // underside and side fascia so the ramps also read correctly from below.
    MeshData ramp_mesh;
    auto ramp_face=[&](const std::array<glm::vec3,4>& corners) {
        const auto base=static_cast<uint32_t>(ramp_mesh.vertices.size());
        const auto normal=glm::normalize(glm::cross(corners[1]-corners[0],corners[2]-corners[0]));
        const std::array<glm::vec2,4> uv{{{0,0},{0,glm::length(corners[1]-corners[0])},
            {glm::length(corners[2]-corners[1]),glm::length(corners[1]-corners[0])},
            {glm::length(corners[3]-corners[0]),0}}};
        for(std::size_t i=0;i<4;++i) {
            MeshVertex vertex{};vertex.position=city::airport_garage_world(corners[i]);
            vertex.normal=normal;vertex.uv=uv[i];
            ramp_mesh.vertices.push_back(vertex);ramp_mesh.bounds.expand(vertex.position);
        }
        for(uint32_t index:{0u,1u,2u,0u,2u,3u}) ramp_mesh.indices.push_back(base+index);
    };
    for(const auto& quad:garage.ramp_quads) {
        ramp_face(quad.corners);
        auto bottom=quad.corners;
        for(auto& p:bottom) p.y-=city::kAirportGarageSlab;
        ramp_face({bottom[3],bottom[2],bottom[1],bottom[0]});
        for(std::size_t i=0;i<4;++i) {
            const auto j=(i+1)%4;
            ramp_face({quad.corners[j],quad.corners[i],bottom[i],bottom[j]});
        }
    }
    airport_garage_ramp_mesh_=renderer.add_mesh(ramp_mesh);
    if(airport_garage_ramp_mesh_==kInvalidId) return false;
    Renderable ramp_render=r;ramp_render.mesh=airport_garage_ramp_mesh_;
    ramp_render.material=start_materials.finish[finish_index(city::StartFinish::Concrete)];
    ramp_render.tint=start_finish_tint(city::StartFinish::Concrete);
    const NodeId ramp_node=scene.create(ramp_render,Transform{},ramp_mesh.bounds);
    if(auto* node=scene.get(ramp_node)) node->max_draw_distance=city::kAirportSite.max_draw_distance_m;
    start_nodes_.push_back(ramp_node);

    start_nodes_.reserve(gas_parts.size() + car_wash_parts.size() +
                         motel_parts.size() +
                         apartment_parts.size() + fast_food_parts.size() +
                         bank_parts.size() +
                         airport_parts.size() + florangia_airport_parts.size() +
                         miandi_rough_parts.size() +
                         miandi_street_parts.size() +
                         miandi_calle_ocho_parts.size() +
                         miandi_bayfront_parts.size() +
                         miandi_context_parts.size() +
                         miandi_calle_noche_parts.size() +
                         miandi_prism_works_parts.size() +
                         miandi_mariposa_parts.size() +
                         miandi_sunwave_parts.size() +
                         miandi_ocean_drive_parts.size() +
                         miandi_promenade_parts.size() +
                         miandi_port_sol_parts.size() +
                         city::kNessBillboardPartCount +
                         city::kPinnatyTaxiBillboardPartCount);
    city::apply_building_access_layout(city::kGasStationSite,gas_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kGasStationSite,
                      gas_parts.data(), gas_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::GasStation,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_,
                      start_cylinder_mesh_, start_nodes_);
    city::apply_building_access_layout(city::kCarWashSite,car_wash_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kCarWashSite,
                      car_wash_parts.data(), car_wash_parts.size(), r,
                      unit.bounds, start_materials,
                      SiteMaterialStyle::GasStation, start_decal_mesh_,
                      start_billboard_mesh_, start_rounded_box_mesh_,
                      start_cylinder_mesh_, start_nodes_);
    city::apply_building_access_layout(city::kMotelSite,motel_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kMotelSite, motel_parts.data(),
                      motel_parts.size(), r, unit.bounds, start_materials,
                      SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_,
                      start_cylinder_mesh_, start_nodes_);
    city::apply_building_access_layout(city::kApartmentSite,apartment_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kApartmentSite,
                      apartment_parts.data(), apartment_parts.size(), r,
                      unit.bounds, start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_,
                      start_cylinder_mesh_, start_nodes_);
    city::apply_building_access_layout(city::kFastFoodSite,fast_food_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kFastFoodSite,
                      fast_food_parts.data(), fast_food_parts.size(), r,
                      unit.bounds, start_materials, SiteMaterialStyle::Quickbite,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_,
                      start_cylinder_mesh_, start_nodes_);
    city::apply_building_access_layout(city::kBankSite,bank_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kBankSite,
                      bank_parts.data(), bank_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::TexturedBuilding,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    city::apply_building_access_layout(city::kAutoRepairSite,repair_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kAutoRepairSite,
        repair_parts.data(), repair_parts.size(), r, unit.bounds, start_materials,
        SiteMaterialStyle::TexturedBuilding, start_decal_mesh_, start_billboard_mesh_,
        start_rounded_box_mesh_, start_cylinder_mesh_, start_nodes_);
    city::apply_building_access_layout(city::kLaundromatSite,laundry_parts,access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kLaundromatSite,
        laundry_parts.data(), laundry_parts.size(), r, unit.bounds, start_materials,
        SiteMaterialStyle::TexturedBuilding, start_decal_mesh_, start_billboard_mesh_,
        start_rounded_box_mesh_, start_cylinder_mesh_, start_nodes_);
    auto east_arm_plaza_parts = city::bake_east_arm_plaza();
    register_interior(city::kEastArmPlazaSite, east_arm_plaza_parts);
    city::apply_building_access_layout(city::kEastArmPlazaSite,
                                       east_arm_plaza_parts, access_layout_);
    append_start_site(scene, collider, precipitation_cover_, city::kEastArmPlazaSite,
        east_arm_plaza_parts.data(), east_arm_plaza_parts.size(), r, unit.bounds,
        start_materials, SiteMaterialStyle::TexturedBuilding, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_);
    for (const auto& part : east_arm_plaza_parts) {
        if (part_name_has(part, "arcade light lens") ||
            part_name_has(part, "lamp head"))
            residential_lights_.push_back(
                part_world_centre(city::kEastArmPlazaSite, part));
    }
    auto pawn_parts=city::bake_pawn_shop();
    register_interior(city::kPawnShopSite, pawn_parts);
    city::apply_building_access_layout(city::kPawnShopSite,pawn_parts,access_layout_);
    append_start_site(scene,collider,precipitation_cover_,city::kPawnShopSite,pawn_parts.data(),pawn_parts.size(),
        r,unit.bounds,start_materials,SiteMaterialStyle::TexturedBuilding,
        start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_,pawn_guitar_mesh_);
    auto gun_store_parts=city::bake_gun_store();
    register_interior(city::kGunStoreSite, gun_store_parts);
    city::apply_building_access_layout(city::kGunStoreSite,gun_store_parts,access_layout_);
    append_start_site(scene,collider,precipitation_cover_,city::kGunStoreSite,gun_store_parts.data(),gun_store_parts.size(),
        r,unit.bounds,start_materials,SiteMaterialStyle::TexturedBuilding,
        start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    for(std::size_t i=0;i<city::kNeighborhoodTowers.size();++i) {
        if (city::hospital_campus_replaces(
                city::kNeighborhoodTowers[i].site))
            continue;
        const auto tower_parts=city::bake_neighborhood_tower(i);
        const SkyscraperWindowRegistration window_registration{
            &skyscraper_windows_,
            window_site_key(city::kNeighborhoodTowers[i].site,
                            0x4E45494748424F52ull),
            city::kNeighborhoodTowers[i].use,false,false};
        append_start_site(scene,collider,precipitation_cover_,city::kNeighborhoodTowers[i].site,
            tower_parts.data(),tower_parts.size(),r,unit.bounds,start_materials,
            SiteMaterialStyle::TexturedBuilding,start_decal_mesh_,start_billboard_mesh_,
            start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_,kInvalidId,
            kInvalidId,&window_registration);
    }
    const std::size_t neighborhood_window_count=skyscraper_windows_.size();
    for (std::size_t i = 0; i < city::kPinattyInfillParcels.size(); ++i) {
        const auto parts = city::bake_pinatty_infill(i);
        const SkyscraperWindowRegistration window_registration{
            &skyscraper_windows_,
            window_site_key(city::kPinattyInfillParcels[i].site,
                            0x56454C4C554D494Eull),
            city::SkyscraperUse::Residential,false,true};
        append_start_site(
            scene, collider, precipitation_cover_, city::kPinattyInfillParcels[i].site,
            parts.data(), parts.size(), r, unit.bounds, start_materials,
            SiteMaterialStyle::TexturedBuilding, start_decal_mesh_,
            start_billboard_mesh_, start_rounded_box_mesh_,
            start_cylinder_mesh_, start_nodes_, kInvalidId,
            start_gable_mesh_, &window_registration);
    }
    const std::size_t pinatty_infill_window_count=
        skyscraper_windows_.size()-neighborhood_window_count;
    const auto construction_parts = city::bake_construction_site();
    append_start_site(scene, collider, precipitation_cover_, city::kConstructionSite.site,
        construction_parts.data(), construction_parts.size(), r, unit.bounds,
        start_materials, SiteMaterialStyle::Construction, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_);
    const auto materials_parts = city::bake_construction_neighbor_materials();
    append_start_site(scene, collider, precipitation_cover_, city::kConstructionNeighborMaterialsSite,
        materials_parts.data(), materials_parts.size(), r, unit.bounds,
        start_materials, SiteMaterialStyle::Construction, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_);
    const auto equipment_parts = city::bake_construction_neighbor_equipment();
    append_start_site(scene, collider, precipitation_cover_, city::kConstructionNeighborEquipment.site,
        equipment_parts.data(), equipment_parts.size(), r, unit.bounds,
        start_materials, SiteMaterialStyle::Construction, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_);
    const auto street_detail_parts = city::bake_construction_street_detail();
    append_start_site(scene, collider, precipitation_cover_, city::kConstructionStreetDetailSite,
        street_detail_parts.data(), street_detail_parts.size(), r, unit.bounds,
        start_materials, SiteMaterialStyle::Construction, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_);
    for (std::size_t i = 0; i < city::kAdditionalConstructionSites.size(); ++i) {
        const auto parts = city::bake_additional_construction_site(i);
        append_start_site(
            scene, collider, precipitation_cover_, city::kAdditionalConstructionSites[i].site,
            parts.data(), parts.size(), r, unit.bounds, start_materials,
            SiteMaterialStyle::Construction, start_decal_mesh_,
            start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
            start_nodes_);
    }
    for (std::size_t i = 0; i < city::kTwinSkyscraperBlockSites.size(); ++i) {
        const auto parts = city::bake_twin_skyscraper_block(i);
        const SkyscraperWindowRegistration window_registration{
            &skyscraper_windows_,
            window_site_key(city::kTwinSkyscraperBlockSites[i],
                            0x5457494E544F5745ull),
            city::SkyscraperUse::Mixed,true,false};
        append_start_site(
            scene, collider, precipitation_cover_, city::kTwinSkyscraperBlockSites[i], parts.data(),
            parts.size(), r, unit.bounds, start_materials,
            SiteMaterialStyle::TexturedBuilding, start_decal_mesh_,
            start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
            start_nodes_, kInvalidId, kInvalidId, &window_registration);
    }
    AP_INFO("city window lighting: %zu skyscraper suites + %zu Pinatty "
            "apartments registered",
            skyscraper_windows_.size()-pinatty_infill_window_count,
            pinatty_infill_window_count);
    const auto hospital_parts = city::bake_polished_hospital_campus();
    append_start_site(scene, collider, precipitation_cover_, city::kHospitalSite,
        hospital_parts.data(), hospital_parts.size(), r, unit.bounds,
        start_materials, SiteMaterialStyle::Construction, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_);
    const auto hospital_north_parking = city::bake_hospital_north_parking();
    append_start_site(scene, collider, precipitation_cover_, city::kHospitalNorthParkingSite,
        hospital_north_parking.data(), hospital_north_parking.size(), r,
        unit.bounds, start_materials, SiteMaterialStyle::Construction,
        start_decal_mesh_, start_billboard_mesh_, start_rounded_box_mesh_,
        start_cylinder_mesh_, start_nodes_);
    burgerpiz_lights_.clear();
    burgerpiz_parking_lights_.clear();
    burgerpiz_parking_lens_nodes_.clear();
    for(const auto& [where,root,plan]:city::kImportedRestaurants) {
        const auto& site=*where;
        auto burger_parts=city::bake_building(*plan);
        register_interior(site,burger_parts);
        city::apply_building_access_layout(site,burger_parts,access_layout_);
        append_start_site(scene,collider,precipitation_cover_,site,burger_parts.data(),burger_parts.size(),
            r,unit.bounds,start_materials,SiteMaterialStyle::Plain,
            start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
        city::BurgerPizAsset burger_asset;
        if(!city::load_burgerpiz_asset(burger_asset,root)) {
            AP_ERROR("%s: missing or invalid cooked private asset; run tools/cook_burgerpiz.py",site.name);
            return false;
        }
        Transform burger_pose;
        burger_pose.position=city::burgerpiz_world({0,0,0},site);
        burger_pose.rotation=glm::angleAxis(std::atan2(site.sin_yaw,
            site.cos_yaw),glm::vec3{0,1,0});
        for(const auto& part:burger_asset.materials) {
            StaticEmesh mesh;
            Texture texture;
            if(!read_static_emesh(city::burgerpiz_path(part.mesh,root),mesh) ||
               !(part.texture=="-"?texture.make_white():texture.load_file(city::burgerpiz_path(part.texture,root)))) {
                AP_ERROR("%s: failed loading %s",site.name,part.mesh.c_str());return false;
            }
            Renderable placed;
            placed.mesh=renderer.add_mesh(mesh);
            if(placed.mesh==kInvalidId)return false;
            burgerpiz_meshes_.push_back(placed.mesh);
            // Let the furnished shell occlude outdoor shading from inside.
            placed.material=part.glass?renderer.add_glass_material():
                renderer.add_material(std::move(texture),false,.25f,{},false,true);
            placed.tint=part.tint;
            if(part.emissive)placed.tint.a=2.5f;
            const auto node=scene.create(placed,burger_pose,mesh.bounds);
            if(part.mesh=="parking_lens.emesh")burgerpiz_parking_lens_nodes_.push_back(node);
            start_nodes_.push_back(node);
            if(auto* n=scene.get(node))n->max_draw_distance=site.max_draw_distance_m;
        }
        city::add_burgerpiz_collision(collider,burger_asset,site);
        for(const auto& p:burger_asset.lights)burgerpiz_lights_.push_back(city::burgerpiz_world(p,site));
        for(const auto& p:burger_asset.parking_lights)
            burgerpiz_parking_lights_.push_back(city::burgerpiz_world(p,site));
        AP_INFO("%s: %zu parking lamp heads",site.name,burger_asset.parking_lights.size());
        AP_INFO("%s: %zu material meshes, %zu solid boxes, %zu interior lights at %.1f %.1f",
            site.name,burger_asset.materials.size(),burger_asset.boxes.size(),burger_asset.lights.size(),
            site.origin.x,site.origin.z);
    }
    for(const auto& station:city::kImportedGasStations) {
        const auto& site=*station.site;
        const auto* root=station.root;
        auto parts=city::bake_miandi_gas_station_lot();
        register_interior(site,parts);
        city::apply_building_access_layout(site,parts,access_layout_);
        append_start_site(scene,collider,precipitation_cover_,site,parts.data(),parts.size(),
            r,unit.bounds,start_materials,SiteMaterialStyle::Plain,
            start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
        collider.add_static_ground_rect({site.origin.x,site.origin.z},site.ground_m+.07f,{20,28},
            std::atan2(site.sin_yaw,site.cos_yaw),Surface::Rock);
        city::MiandiGasStationAsset gas_asset;
        if(!city::load_miandi_gas_station_asset(gas_asset,root)) {
            AP_ERROR("%s: missing or invalid cooked private asset; run tools/cook_miandi_gas_station.py",site.name);
            return false;
        }
        Transform gas_pose;
        gas_pose.position=city::miandi_gas_station_world({0,0,0},site);
        gas_pose.rotation=glm::angleAxis(std::atan2(site.sin_yaw,
            site.cos_yaw),glm::vec3{0,1,0});
        for(const auto& part:gas_asset.materials) {
            StaticEmesh mesh;
            Texture texture;
            if(!read_static_emesh(city::miandi_gas_station_path(part.mesh,root),mesh) ||
               !(part.texture=="-"?texture.make_white():texture.load_file(city::miandi_gas_station_path(part.texture,root)))) {
                AP_ERROR("%s: failed loading %s",site.name,part.mesh.c_str());return false;
            }
            Renderable placed;
            placed.mesh=renderer.add_mesh(mesh);
            if(placed.mesh==kInvalidId)return false;
            miandi_gas_station_meshes_.push_back(placed.mesh);
            // Let the furnished shell occlude outdoor shading from inside.
            placed.material=part.glass?renderer.add_glass_material():
                renderer.add_material(std::move(texture),false,.25f,{},false,true);
            placed.tint=part.tint;
            if(part.emissive)placed.tint.a=2.5f;
            const auto node=scene.create(placed,gas_pose,mesh.bounds);
            start_nodes_.push_back(node);
            if(auto* n=scene.get(node))n->max_draw_distance=site.max_draw_distance_m;
        }
        city::add_miandi_gas_station_collision(collider,gas_asset,site);
        for(const auto& p:gas_asset.lights)miandi_gas_station_lights_.push_back(city::miandi_gas_station_world(p,site));
        AP_INFO("%s: %zu material meshes, %zu solid boxes, %zu interior lights at %.1f %.1f",
            site.name,gas_asset.materials.size(),gas_asset.boxes.size(),gas_asset.lights.size(),
            site.origin.x,site.origin.z);
        for(const auto& cover:gas_asset.covers) {
            StaticBox box;
            box.centre=city::miandi_gas_station_world(cover.centre,site);
            box.oriented=true;
            box.local_bounds={-cover.half,cover.half};
            box.axis_x={site.cos_yaw,-site.sin_yaw};
            box.axis_z={site.sin_yaw,site.cos_yaw};
            const glm::vec3 extent{
                std::abs(site.cos_yaw)*cover.half.x+std::abs(site.sin_yaw)*cover.half.z,
                cover.half.y,
                std::abs(site.sin_yaw)*cover.half.x+std::abs(site.cos_yaw)*cover.half.z};
            box.bounds={box.centre-extent,box.centre+extent};
            precipitation_cover_.push_back(box);
        }
    }
    auto museum_parts=city::bake_loom_museum();
    register_interior(city::kLoomMuseumSite,museum_parts);
    city::apply_building_access_layout(city::kLoomMuseumSite,museum_parts,access_layout_);
    append_start_site(scene,collider,precipitation_cover_,city::kLoomMuseumSite,museum_parts.data(),museum_parts.size(),
        r,unit.bounds,start_materials,SiteMaterialStyle::TexturedBuilding,
        start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_,kInvalidId,start_gable_mesh_);
    auto park_parts=city::bake_loom_park();
    city::apply_building_access_layout(city::kLoomParkSite,park_parts,access_layout_);
    append_start_site(scene,collider,precipitation_cover_,city::kLoomParkSite,park_parts.data(),park_parts.size(),
        r,unit.bounds,start_materials,SiteMaterialStyle::TexturedBuilding,
        start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    // Halberd Field. Airport material style: its paving and paint use the same
    // thin-plane convention, so the shape rules already do the right thing.
    auto halberd_parts=city::bake_halberd_field();
    append_start_site(scene,collider,precipitation_cover_,city::kHalberdFieldSite,
        halberd_parts.data(),halberd_parts.size(),r,unit.bounds,start_materials,
        SiteMaterialStyle::Airport,start_decal_mesh_,start_billboard_mesh_,
        start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    for(const auto& part:halberd_parts)
        if(part_name_is(part,"halberd runway light lens") ||
           part_name_is(part,"halberd apron light lens") ||
           part_name_is(part,"halberd gate light lens"))
            residential_lights_.push_back(part_world_centre(city::kHalberdFieldSite,part));

    auto bar_parts=city::bake_neighborhood_bar();
    register_interior(city::kNeighborhoodBarSite, bar_parts);
    city::apply_building_access_layout(city::kNeighborhoodBarSite,bar_parts,access_layout_);
    append_start_site(scene,collider,precipitation_cover_,city::kNeighborhoodBarSite,bar_parts.data(),bar_parts.size(),
        r,unit.bounds,start_materials,SiteMaterialStyle::TexturedBuilding,
        start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    for(bool fire:{true,false}) {
        const auto& site=fire?city::kFireStationSite:city::kPoliceStationSite;
        const auto parts=city::bake_emergency_station(fire);
        register_interior(site, parts);
        append_start_site(scene,collider,precipitation_cover_,site,parts.data(),parts.size(),r,unit.bounds,start_materials,
            SiteMaterialStyle::TexturedBuilding,start_decal_mesh_,start_billboard_mesh_,
            start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    }
    const TerrainGround residential_ground{seed_};
    for(std::size_t i=0;i<city::kResidentialHouses.size();++i) {
        const auto& site=city::kResidentialHouses[i].site;
        const auto parts=city::bake_residential_house(i,residential_ground.sampler());
        register_interior(site, parts);
        append_start_site(scene,collider,precipitation_cover_,site,parts.data(),parts.size(),r,unit.bounds,start_materials,
            SiteMaterialStyle::TexturedBuilding,start_decal_mesh_,start_billboard_mesh_,
            start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_,kInvalidId,start_gable_mesh_);
        for(const auto& part:parts) if(part_name_is(part,"house interior light lens")) {
            auto position=part_world_centre(site,part);position.y-=.08f;
            residential_lights_.push_back(position);
        }
    }
    for(std::size_t i=0;i<city::kLuxuryEstates.size();++i) {
        const auto& site=city::kLuxuryEstates[i].site;
        const auto parts=city::bake_luxury_estate(i,residential_ground.sampler());
        append_start_site(scene,collider,precipitation_cover_,site,parts.data(),parts.size(),r,unit.bounds,start_materials,
            SiteMaterialStyle::Westmere,start_decal_mesh_,start_billboard_mesh_,
            start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_,kInvalidId,start_gable_mesh_);
        for(const auto& part:parts) if(part_name_is(part,"house luxury entrance light lens")) {
            auto position=part_world_centre(site,part);position.y-=.08f;
            residential_lights_.push_back(position);
        }
    }
    for(std::size_t i=0;i<city::kWestmereCourtGreens.size();++i) {
        const auto& site=city::kWestmereCourtGreens[i];
        const auto parts=city::bake_westmere_court_green(
            i,residential_ground.sampler());
        append_start_site(scene,collider,precipitation_cover_,site,parts.data(),parts.size(),r,
            unit.bounds,start_materials,SiteMaterialStyle::Westmere,
            start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,
            start_cylinder_mesh_,start_nodes_);
        for(const auto& part:parts) if(part_name_is(part,"luxury light lens")) {
            auto position=part_world_centre(site,part);position.y-=.08f;
            residential_lights_.push_back(position);
        }
    }
    const auto westmere_common=city::bake_westmere_common(residential_ground.sampler());
    append_start_site(scene,collider,precipitation_cover_,city::kWestmereCommonSite,westmere_common.data(),
        westmere_common.size(),r,unit.bounds,start_materials,
        SiteMaterialStyle::Westmere,start_decal_mesh_,start_billboard_mesh_,
        start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    const auto westmere_gate=city::bake_westmere_gate(residential_ground.sampler());
    append_start_site(scene,collider,precipitation_cover_,city::kWestmereGateSite,westmere_gate.data(),
        westmere_gate.size(),r,unit.bounds,start_materials,
        SiteMaterialStyle::Westmere,start_decal_mesh_,start_billboard_mesh_,
        start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    for(const auto* site_parts:{&westmere_common,&westmere_gate}) {
        const auto& site=site_parts==&westmere_common ? city::kWestmereCommonSite
                                                      : city::kWestmereGateSite;
        for(const auto& part:*site_parts) if(part_name_is(part,"luxury light lens")) {
            auto position=part_world_centre(site,part);position.y-=.08f;
            residential_lights_.push_back(position);
        }
    }
    for (std::size_t i = 0;
         i < city::kWestmereStreetscapeSites.size(); ++i) {
        const auto& record = city::kWestmereStreetscapeSites[i];
        const auto parts = city::bake_westmere_streetscape_site(
            i, residential_ground.sampler());
        append_start_site(scene, collider, precipitation_cover_, record.site, parts.data(),
            parts.size(), r, unit.bounds, start_materials,
            SiteMaterialStyle::Westmere, start_decal_mesh_,
            start_billboard_mesh_, start_rounded_box_mesh_,
            start_cylinder_mesh_, start_nodes_);
    }
    const auto farm_parts = city::bake_tidewater_farm(residential_ground.sampler());
    append_start_site(scene, collider, precipitation_cover_, city::kTidewaterFarmSite,
        farm_parts.data(), farm_parts.size(), r, unit.bounds, start_materials,
        SiteMaterialStyle::Farm, start_decal_mesh_,
        start_billboard_mesh_, start_rounded_box_mesh_, start_cylinder_mesh_,
        start_nodes_, kInvalidId, start_gable_mesh_);
    house_doors_=city::residential_doors(residential_ground.sampler());
    house_door_states_.resize(house_doors_.size());
    for(const auto& door:house_doors_) {
        std::vector<NodeId> nodes;
        for(const auto& part:city::residential_door_parts(door,0)) {
            Renderable leaf=r;leaf.mesh=start_box_mesh_;
            leaf.material=start_materials.finish[finish_index(part.finish)];
            leaf.tint=start_finish_tint(part.finish);leaf.uv_scale={1,1};
            const auto transform=part_transform(city::kResidentialHouses[city::kResidentialTargetHouse].site,part);
            const auto node=scene.create(leaf,transform,unit.bounds);
            if(auto* placed=scene.get(node))placed->max_draw_distance=950;
            nodes.push_back(node);start_nodes_.push_back(node);
        }
        house_door_nodes_.push_back(nodes);
        const auto& d=door.physics;
        house_door_colliders_.push_back(collider.add_kinematic_oriented_box(
            house_door_centre(d,0),{d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw));
    }
    quickbite_doors_=city::quickbite_doors();
    quickbite_door_states_.resize(quickbite_doors_.size());
    for(const auto& door:quickbite_doors_) {
        std::vector<NodeId> nodes;
        for(const auto& part:city::quickbite_door_parts(door,0)) {
            Renderable leaf=r;leaf.mesh=start_box_mesh_;
            leaf.material = part.finish == city::StartFinish::Glass
                ? start_materials.glass
                : start_materials.finish[finish_index(part.finish)];
            leaf.tint=start_finish_tint(part.finish);leaf.uv_scale={1,1};
            const auto transform=part_transform(*door.site,part);
            const auto node=scene.create(leaf,transform,unit.bounds);
            if(auto* placed=scene.get(node))placed->max_draw_distance=950;
            nodes.push_back(node);start_nodes_.push_back(node);
        }
        quickbite_door_nodes_.push_back(nodes);
        const auto& d=door.physics;
        quickbite_door_colliders_.push_back(collider.add_kinematic_oriented_box(
            house_door_centre(d,0),{d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw));
    }
    for (const city::StartPart& part : city::bank_vault_leaf(0.0f)) {
        Renderable leaf = r;
        const bool face = part_name_has(part, "face");
        leaf.mesh = face ? start_billboard_mesh_ :
            (part_name_has(part, "hub") ? start_cylinder_mesh_ : start_box_mesh_);
        leaf.material = face ? start_materials.bank_vault_face :
            start_materials.finish[finish_index(part.finish)];
        leaf.tint = face ? glm::vec4{1.0f} : start_finish_tint(part.finish);
        leaf.uv_scale = {1.0f, 1.0f};
        const Transform transform = part_transform(city::kBankSite, part);
        const NodeId node = scene.create(leaf, transform, unit.bounds);
        bank_vault_nodes_.push_back(node);
        start_nodes_.push_back(node);
        if (part.solid) {
            bank_vault_collider_ = collider.add_kinematic_oriented_box(
                transform.position, transform.scale * 0.5f,
                std::atan2(city::kBankSite.sin_yaw, city::kBankSite.cos_yaw) +
                    glm::radians(part.yaw_deg));
        }
    }
    bank_vault_pose_ = 0.0f;
    append_start_site(scene, collider, precipitation_cover_, city::kAirportSite,
                      airport_parts.data(), airport_parts.size(), r,
                      unit.bounds, start_materials, SiteMaterialStyle::Airport,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_,
                      start_cylinder_mesh_, start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kFlorangiaAirportSite,
                      florangia_airport_parts.data(),
                      florangia_airport_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Airport,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiSite,
                      miandi_rough_parts.data(), miandi_rough_parts.size(), r,
                      unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiContextSite,
                      miandi_context_parts.data(),
                      miandi_context_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiStreetFixtureSite,
                      miandi_street_parts.data(), miandi_street_parts.size(), r,
                      unit.bounds, start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiCalleOchoSite,
                      miandi_calle_ocho_parts.data(),
                      miandi_calle_ocho_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiBayfrontSite,
                      miandi_bayfront_parts.data(),
                      miandi_bayfront_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiCalleNocheSite,
                      miandi_calle_noche_parts.data(),
                      miandi_calle_noche_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiPrismWorksSite,
                      miandi_prism_works_parts.data(),
                      miandi_prism_works_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiMariposaMotelSite,
                      miandi_mariposa_parts.data(),
                      miandi_mariposa_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiSunwaveHotelSite,
                      miandi_sunwave_parts.data(),
                      miandi_sunwave_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiOceanDriveSite,
                      miandi_ocean_drive_parts.data(),
                      miandi_ocean_drive_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    // Six authored palms reuse the actual Florangia meshes and materials.
    // Their slim trunks, not the full crown bounds, block ground movement.
    for (std::size_t i = 0; i < city::kMiandiResortPalms.size(); ++i) {
        const auto& palm = city::kMiandiResortPalms[i];
        const auto& site = city::kMiandiOceanDriveSite;
        const float scale = palm.height_m / prop_dims(PropKind::Tree).height;
        Transform pose;
        pose.position = {site.origin.x + palm.centre.x, site.ground_m + .2f,
                         site.origin.z + palm.centre.z};
        pose.scale = glm::vec3{scale};
        pose.set_euler_deg(0.f, palm.yaw_deg, 0.f);
        const auto variant = static_cast<uint8_t>(i % kPalmTreeVariants);
        const MeshData shape = make_palm_tree(variant);
        const auto& tree = proto_.tree[kPalmTreeVariantBase + variant];
        const NodeId palm_node = scene.create(tree, pose, shape.bounds);
        start_nodes_.push_back(palm_node);
        if (auto* node = scene.get(palm_node)) node->max_draw_distance = 600.f;
        // Match the four tapered/bent trunk segments in make_palm_tree.
        const float radius = prop_dims(PropKind::Tree).radius;
        const float segment_h = prop_dims(PropKind::Tree).height * .82f * .25f;
        const float bend_x = (variant & 1u ? 1.f : -1.f) * radius * .12f;
        const float bend_z = (variant & 2u ? 1.f : -1.f) * radius * .09f;
        for (int segment = 0; segment < 4; ++segment) {
            const float fraction = (static_cast<float>(segment) + .5f) * .25f;
            const float width = radius * .18f * (1.f - .11f * static_cast<float>(segment));
            const glm::vec3 centre{bend_x * fraction,
                segment_h * (static_cast<float>(segment) + .5f), bend_z * fraction};
            collider.add_static_oriented_box(pose.transform_point(centre),
                glm::vec3{width, segment_h + .04f, width} * (scale * .5f),
                glm::radians(palm.yaw_deg));
        }
    }
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiNorthPromenadeSite,
                      miandi_promenade_parts.data(),
                      miandi_promenade_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kMiandiPortSolSite,
                      miandi_port_sol_parts.data(),
                      miandi_port_sol_parts.size(), r, unit.bounds,
                      start_materials, SiteMaterialStyle::Plain,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    // Body and gear share the aircraft state's +Z-nose frame. Keep their
    // handles so the actor can move without reloading either cooked mesh.
    Texture aircraft_paint;
    if (!aircraft_paint.load_file(asset_path(city::kAirportAircraftTexture))) {
        AP_ERROR("airport: Aster A-80 atlas failed to load");
        return false;
    }
    const MaterialId aircraft_material = renderer.add_material(std::move(aircraft_paint));
    AircraftState initial_aircraft;
    initial_aircraft.position = {
        city::kAirportSite.origin.x + city::kAirportAircraftStand.x,
        city::kAirportSite.ground_m + city::kAirportPavingTopM,
        city::kAirportSite.origin.z + city::kAirportAircraftStand.z};
    initial_aircraft.yaw = city::kAirportAircraftYaw;
    Transform aircraft_pose;
    aircraft_pose.position = initial_aircraft.position;
    aircraft_pose.rotation = aircraft_rotation(initial_aircraft);
    for (const char* path : {city::kAirportAircraftBody, city::kAirportAircraftGear}) {
        StaticEmesh mesh;
        if (!read_static_emesh(asset_path(path), mesh)) {
            AP_ERROR("airport: aircraft mesh '%s' failed to load", path);
            return false;
        }
        Renderable aircraft;
        aircraft.mesh = renderer.add_mesh(mesh);
        aircraft.material = aircraft_material;
        if (aircraft.mesh == kInvalidId) return false;
        airport_aircraft_meshes_.push_back(aircraft.mesh);
        const NodeId node = scene.create(aircraft, aircraft_pose, mesh.bounds);
        start_nodes_.push_back(node);
        airport_aircraft_nodes_.push_back(node);
        if (auto* placed = scene.get(node)) placed->max_draw_distance = 1800.0f;
    }
    for (const auto& box : city::kAirportAircraftCollision) {
        const std::size_t id = collider.add_kinematic_oriented_box(aircraft_pose.transform_point(
            {box.centre.x,box.centre.y,box.centre.z}),
            {box.half.x,box.half.y,box.half.z},city::kAirportAircraftYaw);
        if (id == static_cast<std::size_t>(-1)) {
            AP_ERROR("airport: aircraft compound collider failed to register");
            return false;
        }
        airport_aircraft_colliders_.push_back(id);
        collider.set_kinematic_enabled(id, airport_aircraft_collision_enabled_);
    }
    AP_INFO("airport: %s [%s] parked at %.1f, %.1f, %.1f; body + landing gear loaded",
        city::kAirportAircraftName, city::kAirportAircraftId,
        aircraft_pose.position.x, aircraft_pose.position.y, aircraft_pose.position.z);
    // The Halberd gunship. Two meshes and two materials, not one: the rotor
    // disc is a single alpha-cut quad and an opaque material would draw it as
    // a 14 m black square over the apron.
    {
        Texture heli_paint, rotor_paint;
        if (!heli_paint.load_file(asset_path(city::kHalberdHelicopterTexture)) ||
            !rotor_paint.load_file(asset_path(city::kHalberdHelicopterRotorTexture))) {
            AP_ERROR("halberd: gunship atlas failed to load");
            return false;
        }
        const MaterialId airframe = renderer.add_material(std::move(heli_paint));
        const MaterialId disc = renderer.add_material(std::move(rotor_paint), true);
        HelicopterState initial;
        initial.position = {city::kHalberdHelicopterWorldX,
                            city::kHalberdHelicopterWorldY,
                            city::kHalberdHelicopterWorldZ};
        initial.yaw = city::kHalberdHelicopterYaw;
        Transform pose;
        pose.position = initial.position;
        pose.rotation = helicopter_rotation(initial);
        const struct { const char* path; MaterialId material; bool rotor; } kParts[] = {
            {city::kHalberdHelicopterBody, airframe, false},
            {city::kHalberdHelicopterRotor, disc, true},
        };
        for (const auto& part : kParts) {
            StaticEmesh mesh;
            if (!read_static_emesh(asset_path(part.path), mesh)) {
                AP_ERROR("halberd: gunship mesh '%s' failed to load", part.path);
                return false;
            }
            Renderable piece;
            piece.mesh = renderer.add_mesh(mesh);
            piece.material = part.material;
            if (piece.mesh == kInvalidId) return false;
            halberd_helicopter_meshes_.push_back(piece.mesh);
            Transform placed_pose = pose;
            if (part.rotor)
                placed_pose.position = pose.transform_point(
                    {city::kHalberdHelicopterRotorHub.x,
                     city::kHalberdHelicopterRotorHub.y,
                     city::kHalberdHelicopterRotorHub.z});
            const NodeId node = scene.create(piece, placed_pose, mesh.bounds);
            start_nodes_.push_back(node);
            if (part.rotor) halberd_helicopter_rotor_node_ = node;
            else halberd_helicopter_nodes_.push_back(node);
            if (auto* in_scene = scene.get(node)) in_scene->max_draw_distance = 1800.0f;
        }
        for (const auto& box : city::kHalberdHelicopterCollision) {
            const std::size_t id = collider.add_kinematic_oriented_box(
                pose.transform_point({box.centre.x, box.centre.y, box.centre.z}),
                {box.half.x, box.half.y, box.half.z}, city::kHalberdHelicopterYaw);
            if (id == static_cast<std::size_t>(-1)) {
                AP_ERROR("halberd: gunship compound collider failed to register");
                return false;
            }
            halberd_helicopter_colliders_.push_back(id);
            collider.set_kinematic_enabled(id, halberd_helicopter_collision_enabled_);
        }
        AP_INFO("halberd: %s [%s] parked at %.1f, %.1f, %.1f; airframe + rotor loaded",
                city::kHalberdHelicopterName, city::kHalberdHelicopterId,
                pose.position.x, pose.position.y, pose.position.z);
    }
    // A purpose-built, wheel-free boat actor with movable collision slots.
    Texture boat_paint;
    StaticEmesh boat_body;
    if (!boat_paint.load_file(asset_path(city::kMarlinTexture)) ||
        !read_static_emesh(asset_path(city::kMarlinBody),boat_body)) {
        AP_ERROR("marina: Marlin Sprint 22 asset failed to load");
        return false;
    }
    moored_boat_mesh_=renderer.add_mesh(boat_body);
    if (moored_boat_mesh_==kInvalidId) return false;
    Renderable boat;
    boat.mesh=moored_boat_mesh_;
    boat.material=renderer.add_material(std::move(boat_paint));
    Transform boat_pose;
    boat_pose.position={city::kMarlinMooring.x,0,city::kMarlinMooring.z};
    boat_pose.rotation=glm::angleAxis(city::kMarlinYaw,glm::vec3{0,1,0});
    const NodeId boat_node=scene.create(boat,boat_pose,boat_body.bounds);
    boat_node_=boat_node;
    start_nodes_.push_back(boat_node);
    if (auto* placed=scene.get(boat_node)) placed->max_draw_distance=900;
    // Hollow collision: side coamings and two decks, not a box filling the
    // cockpit. The waterline is local zero and stays at sea level.
    for (const auto& box:kBoatBoxes)
        boat_colliders_.push_back(collider.add_kinematic_oriented_box(
            boat_pose.transform_point(box.centre),box.half,city::kMarlinYaw));
    const auto marina_parts=city::bake_marina();
    append_start_site(scene,collider,precipitation_cover_,city::kMarlinDockSite,marina_parts.data(),
        marina_parts.size(),r,unit.bounds,start_materials,SiteMaterialStyle::Marina,
        start_decal_mesh_,start_billboard_mesh_,start_rounded_box_mesh_,start_cylinder_mesh_,start_nodes_);
    AP_INFO("marina: %zu detailed pier, service shed and mooring parts",marina_parts.size());
    AP_INFO("marina: Marlin Sprint 22 moored at %.1f, 0, %.1f; %zu triangles, no road-wheel rig",
        city::kMarlinMooring.x,city::kMarlinMooring.z,boat_body.indices.size()/3);
    append_start_site(scene, collider, precipitation_cover_, city::kNessBillboardSite,
                      city::kNessBillboardParts,
                      city::kNessBillboardPartCount, r, unit.bounds,
                      start_materials, SiteMaterialStyle::Billboard,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_start_site(scene, collider, precipitation_cover_, city::kPinnatyTaxiBillboardSite,
                      city::kPinnatyTaxiBillboardParts,
                      city::kPinnatyTaxiBillboardPartCount, r, unit.bounds,
                      start_materials, SiteMaterialStyle::Billboard,
                      start_decal_mesh_, start_billboard_mesh_,
                      start_rounded_box_mesh_, start_cylinder_mesh_,
                      start_nodes_);
    append_graffiti(scene, start_materials, start_billboard_mesh_, unit.bounds,
                    start_nodes_);

    canopy_lights_ = CanopyLightRig{};
    std::size_t canopy_index = 0;
    for (const city::StartPart& part : gas_parts) {
        if (!part_name_has(part, "canopy light")) continue;
        if (canopy_index >= canopy_lights_.position.size()) {
            AP_ERROR("world: more canopy emitters than the lighting rig supports");
            return false;
        }
        canopy_lights_.position[canopy_index++] =
            part_world_centre(city::kGasStationSite, part);
    }
    if (canopy_index != canopy_lights_.position.size()) {
        AP_ERROR("world: canopy lighting rig found %zu of %zu authored panels",
                 canopy_index, canopy_lights_.position.size());
        return false;
    }

    AP_INFO("authored districts: creator baked gas station %zu parts, U-motel "
            "%zu parts, apartments %zu parts, restaurant %zu parts, "
            "car wash %zu parts, bank %zu parts, Pinatty airport %zu parts, "
            "Florangia airport %zu parts, "
            "Miandi %zu parts, "
            "Ness billboard %zu parts, "
            "Pinnaty Taxi billboard %zu parts, "
            "%zu solid collision boxes, %zu paved plot surfaces",
            gas_parts.size(), motel_parts.size(), apartment_parts.size(),
            fast_food_parts.size(), car_wash_parts.size(), bank_parts.size(),
            airport_parts.size(), florangia_airport_parts.size(),
            miandi_rough_parts.size() + miandi_street_parts.size() +
                miandi_calle_ocho_parts.size() + miandi_bayfront_parts.size() +
                miandi_context_parts.size() + miandi_calle_noche_parts.size() +
                miandi_prism_works_parts.size() + miandi_mariposa_parts.size() +
                miandi_sunwave_parts.size() + miandi_ocean_drive_parts.size() +
                miandi_promenade_parts.size() + miandi_port_sol_parts.size(),
            city::kNessBillboardPartCount,
            city::kPinnatyTaxiBillboardPartCount,
            collider.static_boxes().size(),
            collider.static_ground_rects().size());
    AP_INFO("interior streaming: %zu authored floor volumes registered",
            interior_streaming_volumes_.size());
    return true;
}

int World::fill(Scene& scene, Renderer& renderer, glm::vec3 focus) {
    // A hard cap, and it is an assertion rather than a convenience. Fill mode
    // plans only prime_radius with no budget, so this converges in a handful of
    // steps; if it does not, something is wrong with the plan and spinning here
    // forever would present as the app hanging on startup with no message.
    constexpr int kMaxFillSteps = 256;

    int steps = 0;
    while (!streamer_.ready(focus) && steps < kMaxFillSteps) {
        update(scene, renderer, focus, StepMode::Fill);
        scene.update();
        ++steps;
    }
    if (!streamer_.ready(focus)) {
        AP_ERROR("world: fill did not converge in %d steps; resuming into a "
                 "world that is not ready",
                 kMaxFillSteps);
    }
    // ready() is re-asked by the caller rather than cached here: a cached flag
    // goes stale the moment the focus moves, and a stale "the world is ready"
    // is exactly the claim that lets a teleport resume into a hole.
    return steps;
}

void World::reset_session_objects(Scene& scene,TerrainCollider& collider) {
    crowd_.build(lane_graph_,seed_,ambient_tuning_,crowd_tuning_);
    const auto& site=city::kResidentialHouses[city::kResidentialTargetHouse].site;
    for(std::size_t i=0;i<house_doors_.size();++i) {
        house_door_states_[i]={};
        const auto& door=house_doors_[i];
        const auto parts=city::residential_door_parts(door,0);
        for(std::size_t j=0;j<parts.size();++j)
            scene.set_transform(house_door_nodes_[i][j],part_transform(site,parts[j]));
        const auto& d=door.physics;
        collider.set_kinematic_oriented_box(house_door_colliders_[i],house_door_centre(d,0),
            {d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw);
    }
    for(std::size_t i=0;i<quickbite_doors_.size();++i) {
        quickbite_door_states_[i]={};
        const auto& door=quickbite_doors_[i];
        const auto parts=city::quickbite_door_parts(door,0);
        for(std::size_t j=0;j<parts.size();++j)
            scene.set_transform(quickbite_door_nodes_[i][j],
                                part_transform(*door.site,parts[j]));
        const auto& d=door.physics;
        collider.set_kinematic_oriented_box(quickbite_door_colliders_[i],
            house_door_centre(d,0),{d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw);
    }
    sync_bank_vault(scene,collider,0);
}

void World::step_house_doors(Scene& scene,TerrainCollider& collider,
        const PlayerCharacterState* actor,const CharacterTuning& tuning,
        const InputFrame& input,float dt) {
    const auto velocity=actor?house_door_walk_velocity(*actor,tuning,input):glm::vec3{};
    const auto& site=city::kResidentialHouses[city::kResidentialTargetHouse].site;
    for(std::size_t i=0;i<house_doors_.size();++i) {
        const auto& door=house_doors_[i];const float previous=house_door_states_[i].angle;
        house_door_states_[i]=step_house_door(door.physics,house_door_states_[i],actor,tuning,velocity,dt);
        const float angle=house_door_states_[i].angle;if(angle==previous)continue;
        const auto parts=city::residential_door_parts(door,angle);
        for(std::size_t j=0;j<parts.size();++j)
            scene.set_transform(house_door_nodes_[i][j],part_transform(site,parts[j]));
        const auto& d=door.physics;
        collider.set_kinematic_oriented_box(house_door_colliders_[i],house_door_centre(d,angle),
            {d.width*.5f,d.height*.5f,d.thickness*.5f},d.closed_yaw+angle);
    }
    for(std::size_t i=0;i<quickbite_doors_.size();++i) {
        const auto& door=quickbite_doors_[i];
        const float previous=quickbite_door_states_[i].angle;
        quickbite_door_states_[i]=step_house_door(door.physics,
            quickbite_door_states_[i],actor,tuning,velocity,dt);
        const float angle=quickbite_door_states_[i].angle;
        if(angle==previous)continue;
        const auto parts=city::quickbite_door_parts(door,angle);
        for(std::size_t j=0;j<parts.size();++j)
            scene.set_transform(quickbite_door_nodes_[i][j],
                                part_transform(*door.site,parts[j]));
        const auto& d=door.physics;
        collider.set_kinematic_oriented_box(quickbite_door_colliders_[i],
            house_door_centre(d,angle),{d.width*.5f,d.height*.5f,d.thickness*.5f},
            d.closed_yaw+angle);
    }
}

void World::sync_bank_vault(Scene& scene, TerrainCollider& collider, float openness) {
    if (openness == bank_vault_pose_) return;
    const auto parts = city::bank_vault_leaf(openness);
    if (parts.size() != bank_vault_nodes_.size()) return;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const Transform t = part_transform(city::kBankSite, parts[i]);
        scene.set_transform(bank_vault_nodes_[i], t);
        if (parts[i].solid) collider.set_kinematic_oriented_box(
            bank_vault_collider_, t.position, t.scale * 0.5f,
            std::atan2(city::kBankSite.sin_yaw, city::kBankSite.cos_yaw) +
                glm::radians(parts[i].yaw_deg));
    }
    bank_vault_pose_ = openness;
}

void World::sync_boat(Scene& scene, TerrainCollider& collider,const BoatState& state) {
    Transform pose;pose.position=state.position;
    pose.position.y+=std::sin(state.phase)*.025f;
    pose.rotation=boat_rotation(state)*glm::angleAxis(-state.pitch,glm::vec3{1,0,0})*
        glm::angleAxis(state.roll+std::sin(state.phase*.7f)*.008f,glm::vec3{0,0,1});
    if(boat_node_!=kInvalidId) scene.set_transform(boat_node_,pose);
    for(std::size_t i=0;i<boat_colliders_.size();++i) {
        collider.set_kinematic_oriented_box(boat_colliders_[i],
            boat_point(state,kBoatBoxes[i].centre),kBoatBoxes[i].half,state.yaw);
        collider.set_kinematic_enabled(boat_colliders_[i],boat_collision_enabled_);
    }
}
void World::enable_boat_collision(TerrainCollider& collider,bool enabled) {
    boat_collision_enabled_=enabled;
    for(const auto id:boat_colliders_) collider.set_kinematic_enabled(id,enabled);
}

void World::sync_aircraft(Scene& scene, TerrainCollider& collider,
                          const AircraftState& state) {
    Transform pose;
    pose.position = state.position;
    pose.rotation = aircraft_rotation(state);
    for (NodeId node : airport_aircraft_nodes_) scene.set_transform(node, pose);

    const bool yaw_only = std::fabs(state.pitch) <= 1e-6f &&
                          std::fabs(state.roll) <= 1e-6f;
    for (std::size_t i = 0; i < airport_aircraft_colliders_.size(); ++i) {
        const auto& local = city::kAirportAircraftCollision[i];
        const glm::vec3 centre{local.centre.x, local.centre.y, local.centre.z};
        const glm::vec3 half{local.half.x, local.half.y, local.half.z};
        const std::size_t id = airport_aircraft_colliders_[i];
        if (yaw_only) {
            // Keep tight yaw OBBs while level/parked so the space beneath
            // wings and between landing-gear struts stays traversable.
            collider.set_kinematic_oriented_box(id, pose.transform_point(centre),
                                                 half, state.yaw);
        } else {
            // TerrainCollider supports yaw OBBs, not full 3D OBBs. Enclose
            // each individual tilted box, never the entire aircraft footprint.
            AABB bounds;
            for (int x : {-1, 1}) {
                for (int y : {-1, 1}) {
                    for (int z : {-1, 1}) {
                        const glm::vec3 corner = centre + half * glm::vec3{
                            static_cast<float>(x), static_cast<float>(y),
                            static_cast<float>(z)};
                        bounds.expand(pose.transform_point(corner));
                    }
                }
            }
            collider.set_kinematic_box(id, bounds);
        }
        // Both pose setters replace StaticBox and reset enabled to true.
        // Preserve query exclusion explicitly; crashed is not an exclusion.
        collider.set_kinematic_enabled(id, airport_aircraft_collision_enabled_);
    }
}

void World::sync_helicopter(Scene& scene, TerrainCollider& collider,
                            const HelicopterState& state) {
    Transform pose;
    pose.position = state.position;
    pose.rotation = helicopter_rotation(state);
    for (NodeId node : halberd_helicopter_nodes_) scene.set_transform(node, pose);

    // The disc rides the airframe and then spins on top of it. Its own mesh is
    // cooked about the hub, so the hub offset goes in the node's POSITION and
    // the spin goes in its rotation -- put the spin on the airframe pose and
    // the whole helicopter turns instead of the blades.
    if (halberd_helicopter_rotor_node_ != kInvalidId) {
        Transform rotor = pose;
        rotor.position = pose.transform_point({city::kHalberdHelicopterRotorHub.x,
                                               city::kHalberdHelicopterRotorHub.y,
                                               city::kHalberdHelicopterRotorHub.z});
        rotor.rotation = pose.rotation *
            glm::angleAxis(state.rotor_angle, glm::vec3{0, 1, 0});
        scene.set_transform(halberd_helicopter_rotor_node_, rotor);
    }

    const bool yaw_only = std::fabs(state.pitch) <= 1e-6f &&
                          std::fabs(state.roll) <= 1e-6f;
    for (std::size_t i = 0; i < halberd_helicopter_colliders_.size(); ++i) {
        const auto& local = city::kHalberdHelicopterCollision[i];
        const glm::vec3 centre{local.centre.x, local.centre.y, local.centre.z};
        const glm::vec3 half{local.half.x, local.half.y, local.half.z};
        const std::size_t id = halberd_helicopter_colliders_[i];
        if (yaw_only) {
            collider.set_kinematic_oriented_box(id, pose.transform_point(centre),
                                                half, state.yaw);
        } else {
            // Same rule as the airliner: enclose each tilted box on its own,
            // never the whole airframe, or a banked helicopter fences off the
            // street underneath it.
            AABB bounds;
            for (int x : {-1, 1})
                for (int y : {-1, 1})
                    for (int z : {-1, 1})
                        bounds.expand(pose.transform_point(centre + half * glm::vec3{
                            static_cast<float>(x), static_cast<float>(y),
                            static_cast<float>(z)}));
            collider.set_kinematic_box(id, bounds);
        }
        collider.set_kinematic_enabled(id, halberd_helicopter_collision_enabled_);
    }
}

void World::enable_helicopter_collision(TerrainCollider& collider, bool enabled) {
    halberd_helicopter_collision_enabled_ = enabled;
    for (std::size_t id : halberd_helicopter_colliders_)
        collider.set_kinematic_enabled(id, enabled);
}

void World::enable_aircraft_collision(TerrainCollider& collider, bool enabled) {
    airport_aircraft_collision_enabled_ = enabled;
    for (std::size_t id : airport_aircraft_colliders_)
        collider.set_kinematic_enabled(id, enabled);
}

void World::shutdown(Scene& scene, Renderer& renderer) {
    // Scene::clear() drops every node, which is every reference to every chunk
    // mesh. The meshes still RESIDENT are freed by Renderer::destroy(), which
    // runs next and destroys the whole table; what is drained here is only what
    // the streamer had already handed back and this frame had not collected, so
    // the two do not end the session disagreeing about who owns what.
    roads_.detach(scene);
    roads_.release(renderer);
    crowd_.clear();
    lane_graph_.clear();
    road_graph_.clear();

    scene.remove_many(start_nodes_);
    start_nodes_.clear();
    skyscraper_windows_.clear();
    skyscraper_window_stats_={};
    skyscraper_window_sync_bucket_=UINT64_MAX;
    skyscraper_window_sync_time_rate_=0.0f;
    skyscraper_window_presentation_step_=UINT64_MAX;
    residential_lights_.clear();
    interior_streaming_volumes_.clear();
    precipitation_cover_.clear();
    house_doors_.clear();house_door_states_.clear();house_door_nodes_.clear();house_door_colliders_.clear();
    quickbite_doors_.clear();quickbite_door_states_.clear();
    quickbite_door_nodes_.clear();quickbite_door_colliders_.clear();
    airport_aircraft_nodes_.clear();
    boat_node_=kInvalidId;boat_colliders_.clear();boat_collision_enabled_=true;
    airport_aircraft_colliders_.clear();
    airport_aircraft_collision_enabled_ = true;
    for(const auto mesh:burgerpiz_meshes_)renderer.remove_mesh(mesh);
    burgerpiz_meshes_.clear();
    for(const auto mesh:miandi_gas_station_meshes_)renderer.remove_mesh(mesh);
    miandi_gas_station_meshes_.clear();
    miandi_gas_station_lights_.clear();
    if(museum_amphora_mesh_!=kInvalidId) {renderer.remove_mesh(museum_amphora_mesh_);museum_amphora_mesh_=kInvalidId;}
    for(const MeshId mesh:museum_meshes_) if(mesh!=kInvalidId) renderer.remove_mesh(mesh);
    museum_meshes_.clear();
    for(auto& mesh:museum_art_meshes_) {
        if(mesh!=kInvalidId)renderer.remove_mesh(mesh);
        mesh=kInvalidId;
    }
    burgerpiz_lights_.clear();
    burgerpiz_parking_lights_.clear();
    burgerpiz_parking_lens_nodes_.clear();
    for (const MeshId mesh : airport_aircraft_meshes_) renderer.remove_mesh(mesh);
    airport_aircraft_meshes_.clear();
    if(airport_garage_ramp_mesh_!=kInvalidId) {
        renderer.remove_mesh(airport_garage_ramp_mesh_);
        airport_garage_ramp_mesh_=kInvalidId;
    }
    if (moored_boat_mesh_!=kInvalidId) {
        renderer.remove_mesh(moored_boat_mesh_);
        moored_boat_mesh_=kInvalidId;
    }
    bank_vault_nodes_.clear();
    bank_vault_pose_ = -1.0f;
    if (start_box_mesh_ != kInvalidId) {
        renderer.remove_mesh(start_box_mesh_);
        start_box_mesh_ = kInvalidId;
    }
    if (start_decal_mesh_ != kInvalidId) {
        renderer.remove_mesh(start_decal_mesh_);
        start_decal_mesh_ = kInvalidId;
    }
    if (start_billboard_mesh_ != kInvalidId) {
        renderer.remove_mesh(start_billboard_mesh_);
        start_billboard_mesh_ = kInvalidId;
    }
    if (start_rounded_box_mesh_ != kInvalidId) {
        renderer.remove_mesh(start_rounded_box_mesh_);
        start_rounded_box_mesh_ = kInvalidId;
    }
    if(pawn_guitar_mesh_!=kInvalidId) {renderer.remove_mesh(pawn_guitar_mesh_);pawn_guitar_mesh_=kInvalidId;}
    if (start_cylinder_mesh_ != kInvalidId) {
        renderer.remove_mesh(start_cylinder_mesh_);
        start_cylinder_mesh_ = kInvalidId;
    }
    if(start_gable_mesh_!=kInvalidId) {
        renderer.remove_mesh(start_gable_mesh_);start_gable_mesh_=kInvalidId;
    }
    canopy_lights_ = CanopyLightRig{};

    scene.clear();
    released_scratch_.clear();
    streamer_.take_released_meshes(released_scratch_);
    for (const MeshId id : released_scratch_) renderer.remove_mesh(id);
    released_scratch_.clear();
}

}  // namespace apricot
