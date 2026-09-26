#include "game/save_game.h"
#include "app/player_car_catalog.h"
#include "test_assert.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
using namespace apricot;
namespace {
std::string body_of(const std::string& bytes) { return bytes.substr(bytes.find('\n',bytes.find('\n')+1)+1); }
// Cash leads the row: kNewGameCash, $250 (game/wallet_rules.h has the scale).
constexpr const char* kNewGameEconomyRow="250 5 12 48 5\n";
constexpr const char* kStockedVaultRow="0 0\n";
std::string rewrap(const std::string& body,int version) {
    uint64_t hash=14695981039346656037ull;
    for (const char c:body) { hash^=static_cast<unsigned char>(c);hash*=1099511628211ull; }
    return "APRICOT_SAVE "+std::to_string(version)+"\n"+std::to_string(hash)+"\n"+body;
}
// Cuts the body's last row and returns it, so a test can say which row it cut.
std::string strip_row(std::string& body) {
    const auto cut=body.rfind('\n',body.size()-2)+1;
    std::string row=body.substr(cut);body.erase(cut);return row;
}
// The body a version 4 save would carry: the encoder's, less the v6 and v5 rows.
std::string v4_body_of(const std::string& bytes) {
    std::string body=body_of(bytes);strip_row(body);strip_row(body);return body;
}
// Version 4 stores a respray: the stock paint it sits on and the picked colour.
void paint_checkpoints() {
    GameSave stock;std::string bytes,error;
    REQUIRE(encode_game_save(stock,bytes,error));
    REQUIRE(bytes.compare(0,15,"APRICOT_SAVE 6\n")==0);
    std::string body=body_of(bytes);
    REQUIRE(rewrap(body,6)==bytes); // Control: the framing below is the encoder's own.
    REQUIRE(strip_row(body)==kStockedVaultRow);
    REQUIRE(strip_row(body)==kNewGameEconomyRow);
    REQUIRE(strip_row(body)=="0 0 0 0 0\n"); // Stock base, no respray.
    GameSave painted;painted.car_paint_base=3;painted.car_has_paint=true;painted.car_paint={12,200,77};
    // Every drivable car takes paint, emergency vehicles included. The save
    // never asks which car it is.
    for (int model=0;model<static_cast<int>(kPlayerCarCount);++model) {
        painted.car_model=model;REQUIRE(encode_game_save(painted,bytes,error));
        GameSave back;REQUIRE(decode_game_save(bytes,back,error));REQUIRE(back.car_model==model);
        REQUIRE(back.car_paint_base==3 && back.car_has_paint && back.car_paint==painted.car_paint);
    }
    painted.car_model=static_cast<int>(PlayerCarId::MunicipalCruiser91C);
    REQUIRE(encode_game_save(painted,bytes,error));
    const std::string paint_row="3 1 12 200 77\n";
    body=v4_body_of(bytes);REQUIRE(strip_row(body)==paint_row);
    const std::string v3_body=body;
    GameSave read;
    // Black is a colour a player can pick, not "no paint".
    auto black=painted;black.car_paint={};REQUIRE(encode_game_save(black,bytes,error));
    REQUIRE(decode_game_save(bytes,read,error));REQUIRE(read.car_has_paint && read.car_paint==PaintColor{});
    // A stock alternate with no respray is a base on its own, at any index.
    auto alternate=stock;alternate.car_paint_base=255;REQUIRE(encode_game_save(alternate,bytes,error));
    REQUIRE(decode_game_save(bytes,read,error));REQUIRE(read.car_paint_base==255 && !read.car_has_paint);
    // An unpainted car carries no colour, on the way in and on the way out.
    auto stray=stock;stray.car_paint={0,0,1};REQUIRE(!encode_game_save(stray,bytes,error));
    REQUIRE(error=="Invalid saved paint.");
    REQUIRE(decode_game_save(rewrap(v3_body+paint_row,4),read,error)); // Control: the good row loads.
    REQUIRE(read.car_paint==painted.car_paint && read.car_paint_base==3);
    REQUIRE(!decode_game_save(rewrap(v3_body+"0 0 5 0 0\n",4),read,error));
    REQUIRE(error=="Invalid saved paint.");
    for (const char* row:{"256 1 12 200 77\n","-1 1 12 200 77\n","3 2 12 200 77\n","3 -1 12 200 77\n",
                          "3 1 256 200 77\n","3 1 12 -1 77\n","3 1 12 200 256\n","3 1 12 200\n",
                          "3 1 12 200 77 9\n","3 x 12 200 77\n"}) {
        REQUIRE_MSG(!decode_game_save(rewrap(v3_body+row,4),read,error),"bad paint row decoded",row);
    }
    REQUIRE(read.car_paint==painted.car_paint && read.car_has_paint && read.car_paint_base==3);
    // Versions 1-3 wore stock paint. `read` holds a respray, so passing here
    // proves the default is written rather than left over.
    REQUIRE(!decode_game_save(rewrap(v3_body,4),read,error));           // Control: v4 needs the row.
    REQUIRE(!decode_game_save(rewrap(v3_body+paint_row,3),read,error)); // Control: v3 has none.
    REQUIRE(decode_game_save(rewrap(v3_body,3),read,error));
    REQUIRE(read.car_paint_base==0 && !read.car_has_paint && read.car_paint==PaintColor{});
    REQUIRE(read.car_model==static_cast<int>(PlayerCarId::MunicipalCruiser91C));
    std::string padded=rewrap(v3_body+paint_row,4);padded.insert(13,"0"); // "APRICOT_SAVE 04"
    REQUIRE(!decode_game_save(padded,read,error));REQUIRE(error=="Unsupported save version.");
    REQUIRE(!decode_game_save(rewrap(v3_body+paint_row,7),read,error));REQUIRE(error=="Unsupported save version.");
}
// Version 5 stores the wallet, the owned weapons and the ammunition carried.
void economy_checkpoints() {
    GameSave rich;rich.economy.cash=123456;rich.economy.owned_weapons=kAllWeaponBits;
    rich.pistol_magazine=7;rich.pistol_reserve=300;rich.molotov_stock=0;
    std::string bytes,error;REQUIRE(encode_game_save(rich,bytes,error));
    GameSave read;REQUIRE(decode_game_save(bytes,read,error));
    REQUIRE(read.economy.cash==123456 && read.economy.owned_weapons==kAllWeaponBits);
    REQUIRE(read.pistol_magazine==7 && read.pistol_reserve==300 && read.molotov_stock==0);
    std::string body=body_of(bytes);
    REQUIRE(strip_row(body)==kStockedVaultRow);
    const std::string row="123456 7 7 300 0\n";
    REQUIRE(strip_row(body)==row);
    const std::string v4=body;
    REQUIRE(decode_game_save(rewrap(v4+row,5),read,error)); // Control: the good row loads.
    // A save made before the economy owns every weapon and a new game's cash;
    // `read` holds 123456, so passing proves the default is written.
    REQUIRE(!decode_game_save(rewrap(v4,5),read,error));     // Control: v5 needs the row.
    REQUIRE(!decode_game_save(rewrap(v4+row,4),read,error)); // Control: v4 has none.
    REQUIRE(decode_game_save(rewrap(v4,4),read,error));
    REQUIRE(read.economy.cash==kNewGameCash && read.economy.owned_weapons==kLegacyOwnedWeapons);
    REQUIRE(read.pistol_magazine==WeaponUseState::kMagazineCapacity);
    REQUIRE(read.pistol_reserve==WeaponUseState::kInitialReserve);
    REQUIRE(read.molotov_stock==MolotovUseState::kInitialStock);
    // A new game owns bare hands and molotovs; the pistol is for sale.
    GameSave fresh;REQUIRE(encode_game_save(fresh,bytes,error));
    body=body_of(bytes);REQUIRE(strip_row(body)==kStockedVaultRow);
    REQUIRE(strip_row(body)==kNewGameEconomyRow);
    for (const char* bad:{"-1 7 7 300 0\n","1000000000 7 7 300 0\n","5 6 7 300 0\n","5 8 7 300 0\n",
                          "5 256 7 300 0\n","5 7 13 300 0\n","5 7 -1 300 0\n","5 7 7 10000 0\n",
                          "5 7 7 300 100\n","5 7 7 300\n","5 7 7 300 0 1\n","5 x 7 300 0\n"}) {
        REQUIRE_MSG(!decode_game_save(rewrap(v4+bad,5),read,error),"bad economy row decoded",bad);
    }
    REQUIRE(read.economy.cash==kNewGameCash); // Refusals leave the output alone.
    auto invalid=rich;invalid.economy.cash=-5;REQUIRE(!encode_game_save(invalid,bytes,error));
    REQUIRE(error=="Invalid saved wallet.");
    invalid=rich;invalid.pistol_magazine=13;REQUIRE(!encode_game_save(invalid,bytes,error));
    REQUIRE(error=="Invalid saved ammunition.");
    apricot_test::pass("v5 saves the wallet, owned weapons and ammunition; v1-4 load owning every weapon");
}
// Version 6 stores the bank vault: which piles are gone and when it restocks.
void bank_vault_checkpoints() {
    GameSave robbed;robbed.sim_step=5000;robbed.bank_loot_taken=0b10101;
    robbed.bank_restock_step=5000+kBankHeistRestockSteps;
    std::string bytes,error;REQUIRE(encode_game_save(robbed,bytes,error));
    GameSave read;REQUIRE(decode_game_save(bytes,read,error));
    REQUIRE(read.bank_loot_taken==0b10101 && read.bank_restock_step==robbed.bank_restock_step);
    std::string body=body_of(bytes);
    const std::string row="21 "+std::to_string(robbed.bank_restock_step)+"\n";
    REQUIRE(strip_row(body)==row);
    const std::string v5=body;
    REQUIRE(decode_game_save(rewrap(v5+row,6),read,error)); // Control: the good row loads.
    // Before version 6 the vault was not saved, so it loads stocked; `read`
    // holds a robbed vault, so passing proves the default is written.
    REQUIRE(!decode_game_save(rewrap(v5,6),read,error));     // Control: v6 needs the row.
    REQUIRE(!decode_game_save(rewrap(v5+row,5),read,error)); // Control: v5 has none.
    REQUIRE(decode_game_save(rewrap(v5,5),read,error));
    REQUIRE(read.bank_loot_taken==0 && read.bank_restock_step==0);
    for (const char* bad:{"32 9000\n","-1 9000\n","256 9000\n","1 0\n","1 999999999\n",
                          "1\n","1 9000 1\n","x 9000\n"}) {
        REQUIRE_MSG(!decode_game_save(rewrap(v5+bad,6),read,error),"bad vault row decoded",bad);
    }
    REQUIRE(read.bank_loot_taken==0); // Refusals leave the output alone.
    auto invalid=robbed;invalid.bank_restock_step=0;REQUIRE(!encode_game_save(invalid,bytes,error));
    REQUIRE(error=="Invalid saved bank vault.");
    apricot_test::pass("v6 saves the emptied vault and its restock; v1-5 load it stocked");
}
}
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
    // mkdtemp's job, portably: create_directory refuses a folder that already
    // exists, so the first one it makes is ours alone. Windows has no mkdtemp.
    std::string directory;
    for (int attempt=0; directory.empty() && attempt<1000; ++attempt) {
        const auto candidate=std::filesystem::temp_directory_path()/("apricot-save-test-"+std::to_string(attempt));
        std::error_code created;
        if (std::filesystem::create_directory(candidate,created)) directory=candidate.string();
    }
    REQUIRE(!directory.empty());
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
    paint_checkpoints();
    economy_checkpoints();
    bank_vault_checkpoints();
    apricot_test::pass("save roundtrip, damage preservation, atomic overwrite, invalid and missing saves, v4 paint and its v1-v3 default");
    return 0;
}
