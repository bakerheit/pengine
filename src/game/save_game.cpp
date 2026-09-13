#include "game/save_game.h"
#include "app/player_car_catalog.h"
#include "app/driving_mechanics.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_registration.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>
#include <unistd.h>

namespace apricot {
namespace {
uint64_t checksum(const std::string& bytes) {
    uint64_t value = 14695981039346656037ull;
    for (char byte : bytes) { value ^= static_cast<unsigned char>(byte); value *= 1099511628211ull; }
    return value;
}
bool range(float x, float low, float high) { return std::isfinite(x) && x >= low && x <= high; }
bool position(glm::vec3 p) {
    return range(p.x, -city::kWorldHalfMetres, city::kWorldHalfMetres) &&
           range(p.z, -city::kWorldHalfMetres, city::kWorldHalfMetres) && range(p.y,-100,3000);
}
}

bool validate_game_save(const GameSave& d, std::string& error) {
    error.clear();
    if (d.map_seed != city::kMapSeed) error = "This save belongs to a different world.";
    else if (d.mission != MissionStage::Opening &&
             d.mission != MissionStage::DeliveryActive &&
             d.mission != MissionStage::DeliveryComplete &&
             d.mission != MissionStage::DeliveryNeedsCar) error = "Unknown mission progress.";
    else if (d.car_model < 0 || d.car_model >= static_cast<int>(kPlayerCarCount) || !valid_driving_mechanics_style(d.driving_style)) error = "Unknown vehicle or driving style.";
    else if (!position(d.character_position) || !position(d.car_position)) error = "Invalid saved position.";
    else if (!range(d.character_yaw,-10000,10000) || !range(d.view_yaw,-10000,10000) || !range(d.view_pitch,-1.6f,1.6f)) error = "Invalid saved facing.";
    else if (!range(glm::dot(d.car_rotation,d.car_rotation),.999f,1.001f) || !range(d.car_health,0,100)) error = "Invalid saved vehicle.";
    else if (d.sim_step > (uint64_t{1} << 53)) error = "Invalid saved clock.";
    else if (!valid_registration(d.car_registration)) error = "Invalid saved license plate.";
    for (float zone : d.car_damage.zones) if (!range(zone,0,1)) error = "Invalid saved damage.";
    for (const auto& s : d.car_damage.stamps) {
        if (!range(s.contact_xz.x,-1,1) || !range(s.contact_xz.y,-1,1) || !range(s.severity,0,1) || !range(s.motion_angle,-10000,10000) || !range(s.radius,0,1) || !range(s.height,0,1) || !range(s.glancing,0,1)) error = "Invalid saved dent.";
    }
    const auto& m = d.car_mechanical;
    if (!range(m.oil_remaining,0,1) || !range(m.fuel_remaining,0,1) || !range(m.oil_lifetime_s,0,1000000) || !range(m.fuel_lifetime_s,0,1000000)) error = "Invalid saved engine.";
    if (d.has_trailer) {
        if (!position(d.trailer.position) || !range(d.trailer.yaw,-10000,10000) ||
            !range(d.trailer.pitch,-.31f,.31f)) error="Invalid saved trailer.";
        if (d.trailer.attached) {
            if (d.car_model!=static_cast<int>(PlayerCarId::HarrowHauler)) error="Trailer requires a tractor.";
            else if(error.empty()) {
                VehicleState car;car.position=d.car_position;car.orientation=d.car_rotation;
                const auto tuning=player_model_tuning(static_cast<DrivingMechanicsStyle>(d.driving_style),PlayerCarId::HarrowHauler);
                if(glm::distance(tractor_hitch(car,tuning),trailer_point(d.trailer,kTrailerKingpin))>.15f ||
                   std::fabs(trailer_angle(tractor_yaw(car)-d.trailer.yaw))>kTrailerMaxAngle+.01f)
                    error="Invalid saved hitch.";
            }
        }
    }
    return error.empty();
}

bool encode_game_save(const GameSave& d, std::string& bytes, std::string& error) {
    if (!validate_game_save(d,error)) return false;
    std::ostringstream b; b.imbue(std::locale::classic());
    b << std::setprecision(std::numeric_limits<float>::max_digits10);
    b << d.map_seed << ' ' << d.session_seed << ' ' << d.sim_step << ' ' << int(d.mission) << ' ' << d.on_foot << '\n';
    b << d.character_position.x << ' ' << d.character_position.y << ' ' << d.character_position.z << ' ' << d.character_yaw << ' ' << d.view_yaw << ' ' << d.view_pitch << '\n';
    b << d.car_model << ' ' << d.driving_style << ' ' << d.car_position.x << ' ' << d.car_position.y << ' ' << d.car_position.z << '\n';
    b << d.car_rotation.w << ' ' << d.car_rotation.x << ' ' << d.car_rotation.y << ' ' << d.car_rotation.z << ' ' << d.car_health << ' ' << d.car_key << '\n';
    for (float zone : d.car_damage.zones) b << zone << ' ';
    b << '\n';
    for (const auto& s : d.car_damage.stamps) b << s.contact_xz.x << ' ' << s.contact_xz.y << ' ' << s.severity << ' ' << s.motion_angle << ' ' << s.radius << ' ' << s.height << ' ' << s.glancing << '\n';
    const auto& m = d.car_mechanical;
    b << m.oil_remaining << ' ' << m.fuel_remaining << ' ' << m.oil_lifetime_s << ' ' << m.fuel_lifetime_s << ' ' << m.engine_failed << '\n';
    b << d.has_trailer << ' ' << d.trailer.attached << ' ' << d.trailer.position.x << ' '
      << d.trailer.position.y << ' ' << d.trailer.position.z << ' ' << d.trailer.yaw << ' ' << d.trailer.pitch << '\n';
    b << int(d.car_registration.state) << ' ' << int(d.car_registration.series) << ' '
      << d.car_registration.number << '\n';
    const auto body = b.str();
    bytes = "APRICOT_SAVE 3\n" + std::to_string(checksum(body)) + "\n" + body;
    return true;
}

bool decode_game_save(const std::string& bytes, GameSave& out, std::string& error) {
    error = "Save is damaged or incomplete.";
    if (bytes.size() > 16384) return false;
    const auto first = bytes.find('\n'), second = first == std::string::npos ? first : bytes.find('\n',first+1);
    if (first == std::string::npos || second == std::string::npos) return false;
    const bool version2=bytes.substr(0,first)=="APRICOT_SAVE 2";
    const bool version3=bytes.substr(0,first)=="APRICOT_SAVE 3";
    if (!version3 && !version2 && bytes.substr(0,first) != "APRICOT_SAVE 1") { error = "Unsupported save version."; return false; }
    uint64_t expected = 0;
    std::istringstream header(bytes.substr(first+1,second-first-1));
    if (!(header >> expected) || !(header >> std::ws).eof()) return false;
    const auto body = bytes.substr(second+1);
    if (expected != checksum(body)) return false;
    std::istringstream b(body); b.imbue(std::locale::classic());
    GameSave d; int mission=0, foot=0, failed=0;
    if (!(b >> d.map_seed >> d.session_seed >> d.sim_step >> mission >> foot) ||
        mission < 0 || mission > 3 || foot < 0 || foot > 1) return false;
    d.mission=static_cast<MissionStage>(mission);d.on_foot=foot!=0;
    b >> d.character_position.x >> d.character_position.y >> d.character_position.z >> d.character_yaw >> d.view_yaw >> d.view_pitch;
    b >> d.car_model >> d.driving_style >> d.car_position.x >> d.car_position.y >> d.car_position.z;
    b >> d.car_rotation.w >> d.car_rotation.x >> d.car_rotation.y >> d.car_rotation.z >> d.car_health >> d.car_key;
    for (float& zone : d.car_damage.zones) b >> zone;
    for (auto& s : d.car_damage.stamps) b >> s.contact_xz.x >> s.contact_xz.y >> s.severity >> s.motion_angle >> s.radius >> s.height >> s.glancing;
    auto& m=d.car_mechanical;
    b >> m.oil_remaining >> m.fuel_remaining >> m.oil_lifetime_s >> m.fuel_lifetime_s >> failed;
    if(version2 || version3) {
        int present=0,attached=0;
        b >> present >> attached >> d.trailer.position.x >> d.trailer.position.y >> d.trailer.position.z >> d.trailer.yaw >> d.trailer.pitch;
        if(present<0 || present>1 || attached<0 || attached>1 || (!present && attached))return false;
        d.has_trailer=present!=0;d.trailer.attached=attached!=0;
    }
    if (version3) {
        int state=0,series=0;
        int64_t number=0;
        b >> state >> series >> number;
        if (state<0 || state>=int(city::StateId::Count) || series<0 || series>=int(PlateSeries::Count) || number<0) return false;
        d.car_registration.state=static_cast<city::StateId>(state);
        d.car_registration.series=static_cast<PlateSeries>(series);
        d.car_registration.number=static_cast<uint64_t>(number);
    }
    if (!b || !(b >> std::ws).eof() || failed < 0 || failed > 1) return false;
    if (!version3 && d.car_model>=0 && d.car_model<static_cast<int>(kPlayerCarCount))
        d.car_registration=player_registration(static_cast<PlayerCarId>(d.car_model),d.car_key,
            d.car_position.x,d.car_position.z);
    m.engine_failed=failed!=0;
    if (!validate_game_save(d,error)) return false;
    out=d;error.clear();return true;
}

bool load_game_save(const std::string& path, GameSave& out, std::string& error) {
    std::ifstream file(path,std::ios::binary | std::ios::ate);
    if (!file) { error="No readable saved game found.";return false; }
    const auto size=file.tellg();
    if (size < 0 || size > 16384) { error="Save is damaged or too large.";return false; }
    std::string bytes(static_cast<std::size_t>(size),'\0'); file.seekg(0);
    if (!file.read(bytes.data(),size)) { error="Could not read saved game.";return false; }
    return decode_game_save(bytes,out,error);
}

bool store_game_save(const std::string& path, const GameSave& data, std::string& error) {
    std::string bytes;
    if (!encode_game_save(data,bytes,error)) return false;
    if (path.empty()) { error="Save folder is unavailable.";return false; }
    std::string temp=path+".tmp.XXXXXX";
    std::vector<char> name(temp.begin(),temp.end());name.push_back('\0');
    const int fd=mkstemp(name.data());
    if (fd < 0) { error="Could not create save file.";return false; }
    FILE* file=fdopen(fd,"wb");
    bool ok=false;
    if (file) {
        ok=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        if (std::fflush(file)!=0 || fsync(fd)!=0) ok=false;
        if (std::fclose(file)!=0) ok=false;
    } else close(fd);
    if (ok && std::rename(name.data(),path.c_str())==0) { error.clear();return true; }
    std::remove(name.data()); error="Could not save. Previous save kept.";return false;
}
} // namespace apricot
