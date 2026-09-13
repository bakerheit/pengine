#include "app/vehicle_plate_mesh.h"
#include "app/vehicle_registration.h"
#include "core/asset_root.h"
#include "game/save_game.h"
#include "test_assert.h"
#include <limits>
#include <set>
using namespace apricot;

namespace {
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
void catalog_and_identity() {
    for (std::size_t i=0;i<kPlateDesigns.size();++i) {
        const auto& d=kPlateDesigns[i];
        REQUIRE(plate_design_index(d.state,d.series)==i);
        REQUIRE(d.name && d.slogan && plate_capacity(d.pattern)>1000000000ull);
        const uint64_t capacity=plate_capacity(d.pattern);
        for (const uint64_t n:{uint64_t{0},uint64_t{123456789},capacity-1}) {
            const VehicleRegistration r{d.state,d.series,n};
            const auto serial=plate_serial(r);REQUIRE(serial.size()==std::string_view(d.pattern).size());
            for (std::size_t j=0;j<serial.size();++j) {
                if (d.pattern[j]=='@') REQUIRE(kPlateLetters.find(serial[j])!=std::string_view::npos);
                else if (d.pattern[j]=='#') REQUIRE(serial[j]>='0' && serial[j]<='9');
                else REQUIRE(serial[j]==d.pattern[j]);
            }
        }
        REQUIRE(!valid_registration({d.state,d.series,capacity}));
    }
    REQUIRE(!valid_registration({city::StateId::Count,PlateSeries::Standard,0}));
    REQUIRE(!valid_registration({city::StateId::OHaven,PlateSeries::Count,0}));
    REQUIRE(registration_state_at(75,175)==city::StateId::OHaven);
    REQUIRE(registration_state_at(7500,8400)==city::StateId::Florangia);
    const auto issue=[](uint64_t lane,uint32_t slot,int64_t gen,uint64_t domain=7) {
        return issue_registration(city::StateId::Florangia,PlateUse::Private,lane,slot,gen,domain);
    };
    const auto original=issue(123,4,-8);
    REQUIRE(original==issue(123,4,-8));
    REQUIRE(original!=issue(124,4,-8));REQUIRE(original!=issue(123,5,-8));
    REQUIRE(original!=issue(123,4,-7));REQUIRE(original!=issue(123,4,-8,9));
    REQUIRE(issue(123,4,5)!=issue(123,5,4)); // slot/generation hashes must not be interchangeable.
    std::set<std::pair<int,std::string>> sample;
    for (uint32_t slot=0;slot<40;++slot) for (int64_t gen=-30;gen<30;++gen) {
        const auto r=issue(0xDEADBEEF,slot,gen);
        REQUIRE(sample.emplace(int(r.series),plate_serial(r)).second);
    }
    REQUIRE(player_plate_use(PlayerCarId::RodeoGrazer)==PlateUse::Private);
    REQUIRE(player_plate_use(PlayerCarId::HarrowWorkman)==PlateUse::Commercial);
    REQUIRE(player_plate_use(PlayerCarId::MunicipalCruiser91C)==PlateUse::Government);
    REQUIRE(traffic_plate_use(TrafficVehicleKind::Snowplow)==PlateUse::Government);
}
void cooked_mounts_and_meshes() {
    for (const auto& car:kPlayerCars) {
        StaticEmesh body;REQUIRE(read_static_emesh(asset_path(car.mesh_path),body));
        const auto mounts=vehicle_plate_mounts(body,car.mesh_path);
        REQUIRE(mounts.size()==(is_motorbike(car.id)?1u:2u));
        if (car.id!=PlayerCarId::RodeoGrazer && !is_motorbike(car.id)) {
            for (const auto& mount:mounts) for (float x:{-.5f,.5f}) for (float y:{-.5f,.5f}) {
                const auto right=glm::normalize(glm::cross(glm::vec3{0,1,0},mount.normal));
                const auto up=glm::cross(mount.normal,right);
                const auto corner=mount.centre+right*x*mount.width+up*y*mount.height;
                float skin=0;
                REQUIRE(vehicle_body_surface_z(body,corner.x,corner.y,mount.normal.z>0,skin));
                const float clearance=(corner.z-skin)*mount.normal.z;
                REQUIRE(clearance>=0 && clearance<.10f);
            }
        }
        for (const auto& d:kPlateDesigns) {
            const auto mesh=make_vehicle_plate_mesh(mounts,{d.state,d.series,123456});
            REQUIRE(mesh.bounds.valid());REQUIRE(!mesh.indices.empty());
            for (const auto& v:mesh.vertices) {
                REQUIRE(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z));
                REQUIRE(v.uv.x>=0 && v.uv.x<=1 && v.uv.y>=0 && v.uv.y<=1);
                REQUIRE(mesh.bounds.contains(v.position));
            }
            for (std::size_t i=0;i<mesh.indices.size();i+=3) {
                const auto& a=mesh.vertices[mesh.indices[i]];
                const auto& b=mesh.vertices[mesh.indices[i+1]];
                const auto& c=mesh.vertices[mesh.indices[i+2]];
                REQUIRE(glm::dot(glm::cross(b.position-a.position,c.position-a.position),a.normal)>0);
            }
        }
    }
    StaticEmesh grazer;REQUIRE(read_static_emesh(asset_path("models/vehicles/rodeo_grazer/body.emesh"),grazer));
    const auto mounts=vehicle_plate_mounts(grazer,"rodeo_grazer/body.emesh");
    REQUIRE(mounts[0].centre==glm::vec3(0,.625f,2.446f));
    REQUIRE(mounts[1].centre==glm::vec3(0,.560f,-2.646f));
    REQUIRE(mounts[0].normal.z==1 && mounts[1].normal.z==-1);
    StaticEmesh bike;REQUIRE(read_static_emesh(asset_path("models/vehicles/fang_venom_v2/body.emesh"),bike));
    const auto bike_mount=vehicle_plate_mounts(bike,"fang_venom_v2/body.emesh");
    REQUIRE(bike_mount.size()==1);
    REQUIRE(bike_mount[0].centre==glm::vec3(0,.715f,-1.134f));
}
void checkpoints() {
    GameSave saved; saved.car_model=int(PlayerCarId::RodeoGrazer);saved.car_key=9381;
    saved.car_position={7500,5,8400};
    saved.car_registration={city::StateId::OHaven,PlateSeries::Heritage,99991234};
    saved.car_paint_base=2;saved.car_has_paint=true;saved.car_paint={40,90,200};
    std::string bytes,error;REQUIRE(encode_game_save(saved,bytes,error));
    GameSave loaded;REQUIRE(decode_game_save(bytes,loaded,error));
    REQUIRE(loaded.car_registration==saved.car_registration); // Home plate stays across the border.
    const std::string v4=bytes.substr(bytes.find('\n',bytes.find('\n')+1)+1);
    REQUIRE(rewrap(v4,4)==bytes); // Control: rewrap is the encoder's own framing.
    const std::string paint_row="2 1 40 90 200\n";
    const std::string plate_row=std::to_string(int(saved.car_registration.state))+" "+
        std::to_string(int(saved.car_registration.series))+" 99991234\n";
    // Each strip names the row it cut, and each older header is shown to refuse
    // the body before the cut, so no migration below passes by reading the
    // wrong row.
    std::string body=v4;
    REQUIRE(strip_row(body)==paint_row);
    REQUIRE(!decode_game_save(rewrap(v4,3),loaded,error));
    REQUIRE(!decode_game_save(rewrap(body,4),loaded,error));
    REQUIRE(decode_game_save(rewrap(body,3),loaded,error));
    REQUIRE(loaded.car_registration==saved.car_registration);
    REQUIRE(loaded.car_paint_base==0 && !loaded.car_has_paint && loaded.car_paint==PaintColor{});
    const std::string v3=body;
    REQUIRE(strip_row(body)==plate_row);
    REQUIRE(!decode_game_save(rewrap(v3,2),loaded,error));
    REQUIRE(!decode_game_save(rewrap(body,3),loaded,error));
    REQUIRE(decode_game_save(rewrap(body,2),loaded,error));
    REQUIRE(loaded.car_registration==player_registration(PlayerCarId::RodeoGrazer,9381,7500,8400));
    REQUIRE(loaded.car_registration!=saved.car_registration); // Derived, not the stored plate.
    const std::string v2=body;
    REQUIRE(strip_row(body)=="0 0 0 0 0 0 0\n"); // v2 trailer row: none.
    REQUIRE(!decode_game_save(rewrap(v2,1),loaded,error));
    REQUIRE(!decode_game_save(rewrap(body,2),loaded,error));
    REQUIRE(decode_game_save(rewrap(body,1),loaded,error));
    REQUIRE(loaded.car_registration.state==city::StateId::Florangia);
    REQUIRE(loaded.car_paint_base==0 && !loaded.car_has_paint);
    // Corrupt plate rows, in both versions that carry one. The good row is
    // spliced the same way first, so a refusal is about the values.
    body=v3;strip_row(body);
    REQUIRE(decode_game_save(rewrap(body+plate_row,3),loaded,error));
    REQUIRE(decode_game_save(rewrap(body+plate_row+paint_row,4),loaded,error));
    const auto before=loaded.car_registration;
    for (const char* row:{"256 0 0\n","0 256 0\n","0 0 -1\n","0 0 -18446744073709551615\n",
                          "0 0 18446744073709551615\n"}) {
        REQUIRE_MSG(!decode_game_save(rewrap(body+row,3),loaded,error),"bad v3 plate row decoded",row);
        REQUIRE_MSG(!decode_game_save(rewrap(body+row+paint_row,4),loaded,error),"bad v4 plate row decoded",row);
    }
    REQUIRE(loaded.car_registration==before);
    saved.car_registration.number=std::numeric_limits<uint64_t>::max();
    REQUIRE(!encode_game_save(saved,bytes,error));
}
}
int main() {
    catalog_and_identity();cooked_mounts_and_meshes();checkpoints();
    apricot_test::pass("state plate formats, departure identity, catalog vehicle mounts, winding/UVs and v1-v4 saves");
}
