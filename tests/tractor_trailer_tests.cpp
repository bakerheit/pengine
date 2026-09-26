#include <cstring>

#include "game/tractor_trailer.h"
#include "app/vehicle_model_tuning.h"
#include "app/dev_menu.h"
#include "game/save_game.h"
#include "test_assert.h"
using namespace apricot;
namespace {
TerrainCollider flat() {
    TerrainCollider g(123);g.add_static_ground_rect({0,0},100,{200,200},0);return g;
}
VehicleState aligned(const TrailerState& trailer,const VehicleTuning& t,const TerrainCollider& g) {
    auto pin=trailer_point(trailer,kTrailerKingpin);
    const auto p=pin+glm::angleAxis(trailer.yaw,glm::vec3{0,1,0})*glm::vec3{0,0,-1.9f};
    auto car=spawn_vehicle(t,g,p.x,p.z,trailer.yaw);
    car.position.y=trailer.position.y+static_ride_height(t);
    car.orientation=glm::angleAxis(trailer.yaw,glm::vec3{0,1,0});
    return car;
}
void coupling_and_parking() {
    auto g=flat();const auto t=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowHauler);
    TrailerState s;s.position={0,100,0};auto car=aligned(s,t,g);
    REQUIRE(trailer_coupling(car,t,s)==TrailerCoupling::Ready);
    car.velocity.x=1;REQUIRE(trailer_coupling(car,t,s)==TrailerCoupling::Moving);car.velocity={};
    car.position.x+=1;REQUIRE(trailer_coupling(car,t,s)==TrailerCoupling::OutOfReach);car.position.x-=1;
    car.orientation=glm::angleAxis(.4f,glm::vec3{0,1,0});REQUIRE(trailer_coupling(car,t,s)==TrailerCoupling::Misaligned);
    car=aligned(s,t,g);car.position.y+=1;REQUIRE(trailer_coupling(car,t,s)==TrailerCoupling::OutOfReach);
    const auto old=s;const auto previous=car;car.position.x+=10;
    REQUIRE(step_tractor_trailer(s,car,previous,t,g));REQUIRE(s.position==old.position); // detached stays put
}
void articulation_and_obstacles() {
    auto g=flat();const auto t=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowHauler);
    for(float yaw:{0.f,1.5707963f,3.1415926f,-1.5707963f}) {
        TrailerState s;s.position={0,100,0};s.yaw=yaw;s.attached=true;auto car=aligned(s,t,g);
        const auto start=s;
        for(int i=0;i<240;++i) {
            const auto prev=car;
            car.orientation=glm::angleAxis(yaw+float(i)*.001f,glm::vec3{0,1,0});
            car.position+=car.orientation*glm::vec3{0,0,-.03f};
            REQUIRE(step_tractor_trailer(s,car,prev,t,g));
            REQUIRE(glm::distance(tractor_hitch(car,t),trailer_point(s,kTrailerKingpin))<.001f);
        }
        REQUIRE(glm::distance(start.position,s.position)>6.f);
        REQUIRE(std::fabs(trailer_angle(s.yaw-yaw))>.015f);
        REQUIRE(std::fabs(trailer_angle(tractor_yaw(car)-s.yaw))>.03f); // articulated, not rigid
        REQUIRE(s.wheel_spin>8.f);
        const float angle=std::fabs(trailer_angle(tractor_yaw(car)-s.yaw));
        for(int i=0;i<80;++i) {const auto prev=car;car.position+=car.orientation*glm::vec3{0,0,.025f};REQUIRE(step_tractor_trailer(s,car,prev,t,g));}
        REQUIRE(std::fabs(trailer_angle(tractor_yaw(car)-s.yaw))>angle); // reverse jackknife tendency
    }
    TrailerState s;s.position={0,100,0};s.attached=true;auto car=aligned(s,t,g);const auto prev=car;
    g.add_static_box({{1.30f,101.5f,-.08f},{1.38f,103.5f,.08f}});
    car.position.x+=.3f;
    REQUIRE(!step_tractor_trailer(s,car,prev,t,g));REQUIRE(s.blocked);REQUIRE(car.position==prev.position);
    REQUIRE(glm::length(car.velocity)==0);REQUIRE(s.position==glm::vec3(0,100,0));
    // Full-face SAT catches a thin post; swept sampling catches crossing it.
    auto to=s;to.position.x=3;REQUIRE(!trailer_clear(s,to,g));
    g=flat();car=aligned(s,t,g);
    // Combined crest pitch and yaw can put the box's top corner into the cab
    // even below the yaw cap. Exact OBB guard rejects it.
    auto crest=s;crest.yaw=-.64577f;crest.pitch=-.30f;
    crest.position=tractor_hitch(car,t)-trailer_rotation(crest)*kTrailerKingpin;
    REQUIRE(!trailer_cab_clear(crest,car,t));
    REQUIRE(trailer_cab_clear(s,car,t));
    const auto prev2=car;car.orientation=glm::angleAxis(1.3f,glm::vec3{0,1,0});
    REQUIRE(!step_tractor_trailer(s,car,prev2,t,g));
}
void checkpoints_and_catalog() {
    REQUIRE(static_cast<int>(PlayerCarId::VesperVx91)==18);
    for(const auto& model:kPlayerCars) REQUIRE(player_car_definition(model.id).id==model.id);
    // The contract is that the hauler is filed under its OWN brand and is
    // reachable there, not that HARROW sits at any particular ordinal. This
    // used to hard-code 3; inserting the FANG brand ahead of HARROW moved it
    // to 4 and the assertion went stale without a single behaviour changing.
    const auto& hauler=player_car_definition(PlayerCarId::HarrowHauler);
    const auto& hauler_brand=kPlayerCarBrands[static_cast<std::size_t>(
        player_car_brand_index(PlayerCarId::HarrowHauler))];
    REQUIRE(std::strcmp(hauler_brand.name,"HARROW")==0);
    REQUIRE(std::strcmp(hauler.brand,hauler_brand.name)==0);
    // Every brand's declared span must really hold the cars that claim it, or
    // the dev-menu brand pages silently list the wrong models.
    for(const auto& brand:kPlayerCarBrands) {
        REQUIRE(brand.car_count>0u);
        REQUIRE(brand.first_car+brand.car_count<=kPlayerCars.size());
        for(std::size_t i=brand.first_car;i<brand.first_car+brand.car_count;++i)
            REQUIRE(std::strcmp(kPlayerCars[i].brand,brand.name)==0);
    }
    for(const auto& model:kPlayerCars) {
        const auto& brand=kPlayerCarBrands[static_cast<std::size_t>(
            player_car_brand_index(model.id))];
        REQUIRE(std::strcmp(model.brand,brand.name)==0);
    }
    GameSave save;save.has_trailer=true;save.trailer.position={10,5,20};save.trailer.yaw=1;
    std::string bytes,error;REQUIRE(encode_game_save(save,bytes,error));GameSave out;
    REQUIRE(decode_game_save(bytes,out,error));REQUIRE(out.has_trailer);REQUIRE(out.trailer.position==save.trailer.position);
    save.car_model=static_cast<int>(PlayerCarId::HarrowHauler);save.trailer.attached=true;
    auto g=flat();const auto t=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowHauler);
    save.trailer.position={0,100,0};save.trailer.yaw=0;auto car=aligned(save.trailer,t,g);
    save.car_position=car.position;save.car_rotation=car.orientation;
    REQUIRE(encode_game_save(save,bytes,error));REQUIRE(decode_game_save(bytes,out,error));REQUIRE(out.trailer.attached);
    save.car_model=0;REQUIRE(!encode_game_save(save,bytes,error));
    // Genuine v1 payload (same pre-trailer schema/checksum) still loads old ids.
    // `out` still holds the attached hauler, so the id and the missing trailer
    // below can only come from this payload.
    save.has_trailer=false;save.trailer={};save.car_model=18;REQUIRE(encode_game_save(save,bytes,error));
    const auto rewrap=[](const std::string& body,int version){
        uint64_t hash=14695981039346656037ull;for(char c:body){hash^=static_cast<unsigned char>(c);hash*=1099511628211ull;}
        return "APRICOT_SAVE "+std::to_string(version)+"\n"+std::to_string(hash)+"\n"+body;
    };
    const auto strip=[](std::string& body){
        const auto cut=body.rfind('\n',body.size()-2)+1;std::string row=body.substr(cut);body.erase(cut);return row;
    };
    auto body=bytes.substr(bytes.find('\n',bytes.find('\n')+1)+1);
    REQUIRE(rewrap(body,6)==bytes); // Control: rewrap is the encoder's own framing.
    REQUIRE(strip(body)=="0 0\n"); // v6 bank vault: stocked.
    REQUIRE(strip(body)=="250 5 12 48 5\n"); // v5 wallet and ammunition: a new game's.
    REQUIRE(strip(body)=="0 0 0 0 0\n"); // v4 paint.
    REQUIRE(strip(body)==std::to_string(int(save.car_registration.state))+" "+
            std::to_string(int(save.car_registration.series))+" "+std::to_string(save.car_registration.number)+"\n"); // v3 plate.
    const auto v2=body;
    REQUIRE(strip(body)=="0 0 0 0 0 0 0\n"); // v2 trailer.
    REQUIRE(!decode_game_save(rewrap(v2,1),out,error));  // Control: v1 refuses the trailer row...
    REQUIRE(!decode_game_save(rewrap(body,2),out,error)); // ...and v2 needs it.
    REQUIRE(out.car_model==static_cast<int>(PlayerCarId::HarrowHauler));
    REQUIRE(decode_game_save(rewrap(body,1),out,error));REQUIRE(out.car_model==18);REQUIRE(!out.has_trailer);
}
}
int main(){coupling_and_parking();articulation_and_obstacles();checkpoints_and_catalog();apricot_test::pass("tractor trailer coupling, four headings, reverse, obstacles, parked state and checkpoints");}
