#pragma once

#include <cstdint>
#include <string>
#include "city/map.h"
#include "physics/vehicle.h"
#include "game/tractor_trailer.h"
#include "game/license_plate.h"
#include "game/molotov.h"
#include "game/player_economy.h"
#include "game/vehicle_paint.h"

namespace apricot {

// Values 0..2 are already stored in v1 saves. Append stages; never insert.
enum class MissionStage : uint8_t {
    Opening = 0,
    DeliveryActive = 1,
    DeliveryComplete = 2,
    DeliveryNeedsCar = 3,
};

// A stationary checkpoint, not a physics replay. Transient velocities, wheel
// loads, AI and particle state restart; actual damage and fluid reserves remain.
struct GameSave {
    uint64_t map_seed = city::kMapSeed;
    uint64_t session_seed = 0;
    uint64_t sim_step = 0;
    MissionStage mission = MissionStage::Opening;
    bool on_foot = true;
    glm::vec3 character_position{0};
    float character_yaw = 0, view_yaw = 0, view_pitch = -.16f;
    int car_model = 0;
    int driving_style = 8;
    glm::vec3 car_position{0};
    glm::quat car_rotation{1,0,0,0};
    float car_health = 100;
    VehicleDamageState car_damage{};
    VehicleMechanicalState car_mechanical{};
    uint64_t car_key = 0;
    VehicleRegistration car_registration{};
    bool has_trailer = false;
    TrailerState trailer{};
    // Version 4. The base is an index into the model's stock paint list, which
    // the app owns, so the save range-checks it without knowing the car. A
    // respray is the picked colour laid over that base; unpainted stores 0,0,0.
    uint8_t car_paint_base = 0;
    bool car_has_paint = false;
    PaintColor car_paint{};
    // Version 5: the wallet, what the player owns, and the ammunition they
    // carry. Older saves predate the economy: they load with a new game's
    // cash and every weapon owned, because every weapon was selectable then.
    PlayerEconomy economy{};
    int pistol_magazine = WeaponUseState::kMagazineCapacity;
    int pistol_reserve = WeaponUseState::kInitialReserve;
    int molotov_stock = MolotovUseState::kInitialStock;
};

inline constexpr int kMaxSavedPistolReserve = 9999;
inline constexpr int kMaxSavedMolotovStock = 99;

bool validate_game_save(const GameSave& data, std::string& error);
bool encode_game_save(const GameSave& data, std::string& bytes, std::string& error);
// Decode/load only assign out once every check passes.
bool decode_game_save(const std::string& bytes, GameSave& out, std::string& error);
bool load_game_save(const std::string& path, GameSave& out, std::string& error);
// Writes and flushes a same-directory temporary, then atomically replaces slot.
bool store_game_save(const std::string& path, const GameSave& data, std::string& error);

} // namespace apricot
