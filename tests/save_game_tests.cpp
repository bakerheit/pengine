#include "game/save_game.h"
#include "app/player_car_catalog.h"
#include "test_assert.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <unistd.h>
using namespace apricot;
int main() {
    static_assert(static_cast<int>(PlayerCarId::LegacyCar5)==10);
    static_assert(static_cast<int>(PlayerCarId::VesperVx91)==18);
    static_assert(static_cast<int>(PlayerCarId::HarrowHauler)==19);
    static_assert(static_cast<int>(PlayerCarId::OrisonCinderGt)==20);
    static_assert(static_cast<int>(PlayerCarId::MunicipalCruiser91C)==15);
    static_assert(static_cast<int>(PlayerCarId::LegacyCruiser91CSlot)==24);
    static_assert(static_cast<int>(PlayerCarId::MunicipalCruiser91D)==25);
    REQUIRE(canonical_player_car_id(PlayerCarId::LegacyCruiser91CSlot)==
            PlayerCarId::MunicipalCruiser91C);
    GameSave original;
    original.car_model=static_cast<int>(PlayerCarId::OrisonCinderGt);
    original.session_seed=0x123456789abcdef0ull;original.sim_step=42123;
    original.mission=MissionStage::DeliveryActive;original.on_foot=false;
    original.character_position={12,4,-321};original.character_yaw=1.2f;
    original.car_position={15,3,-300};original.car_health=31.25f;
    original.car_damage.zones[2]=.7f;original.car_damage.stamps[0].severity=.8f;
    original.car_mechanical.oil_remaining=.2f;original.car_mechanical.fuel_remaining=.4f;
    original.car_mechanical.engine_failed=true;original.car_key=98765;
    std::string bytes,error; REQUIRE(encode_game_save(original,bytes,error));
    GameSave read;
    REQUIRE(decode_game_save(bytes,read,error));
    REQUIRE(read.session_seed==original.session_seed);REQUIRE(read.sim_step==42123);
    REQUIRE(read.mission==MissionStage::DeliveryActive);REQUIRE(!read.on_foot);
    REQUIRE(read.character_position==original.character_position);
    REQUIRE(read.car_position==original.car_position);REQUIRE(read.car_health==31.25f);
    REQUIRE(read.car_damage.zones[2]==.7f);REQUIRE(read.car_damage.stamps[0].severity==.8f);
    REQUIRE(read.car_mechanical.oil_remaining==.2f);REQUIRE(read.car_mechanical.engine_failed);
    REQUIRE(read.car_key==98765);
    REQUIRE(read.car_model==static_cast<int>(PlayerCarId::OrisonCinderGt));
    auto needs_car=original;needs_car.mission=MissionStage::DeliveryNeedsCar;
    std::string needs_car_bytes;
    REQUIRE(encode_game_save(needs_car,needs_car_bytes,error));
    REQUIRE(decode_game_save(needs_car_bytes,read,error));
    REQUIRE(read.mission==MissionStage::DeliveryNeedsCar);
    std::string corrupt=bytes;corrupt.back()='x';
    REQUIRE(!decode_game_save(corrupt,read,error));REQUIRE(read.car_health==31.25f);
    REQUIRE(!decode_game_save(bytes.substr(0,bytes.size()/2),read,error));
    REQUIRE(!decode_game_save("APRICOT_SAVE 99\n0\n",read,error));
    auto invalid=original;invalid.map_seed++;REQUIRE(!encode_game_save(invalid,corrupt,error));
    invalid=original;invalid.car_health=std::numeric_limits<float>::quiet_NaN();REQUIRE(!encode_game_save(invalid,corrupt,error));
    invalid=original;invalid.car_rotation={0,0,0,0};REQUIRE(!encode_game_save(invalid,corrupt,error));
    invalid=original;invalid.car_model=99999;REQUIRE(!encode_game_save(invalid,corrupt,error));
    invalid=original;invalid.car_mechanical.oil_remaining=-1;REQUIRE(!encode_game_save(invalid,corrupt,error));
    char directory[]="/tmp/apricot-save-test-XXXXXX";REQUIRE(mkdtemp(directory)!=nullptr);
    const auto path=std::string(directory)+"/checkpoint.save";
    REQUIRE(!load_game_save(path,read,error));REQUIRE(read.car_health==31.25f);
    REQUIRE(store_game_save(path,original,error));REQUIRE(load_game_save(path,read,error));
    original.car_health=72.5f;REQUIRE(store_game_save(path,original,error));
    REQUIRE(load_game_save(path,read,error));REQUIRE(read.car_health==72.5f);
    // A rejected new state cannot clobber the existing working slot.
    REQUIRE(!store_game_save(path,invalid,error));REQUIRE(load_game_save(path,read,error));REQUIRE(read.car_health==72.5f);
    REQUIRE(!store_game_save(std::string(directory)+"/missing/save",original,error));
    { std::ofstream file(path,std::ios::trunc);file << "broken"; }
    REQUIRE(!load_game_save(path,read,error));REQUIRE(read.car_health==72.5f);
    for (const auto& entry : std::filesystem::directory_iterator(directory)) REQUIRE(entry.path().filename()=="checkpoint.save");
    std::filesystem::remove_all(directory);
    apricot_test::pass("save roundtrip, damage preservation, atomic overwrite, invalid and missing saves");
    return 0;
}
