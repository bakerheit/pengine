#include "../tools/cutscene_document.h"
#include "test_assert.h"
#include <limits>
using namespace apricot::cutscene;
int main() {
    Document doc;Shot a;a.duration=2;a.smooth=false;
    a.from={{0,2,4},{0,1,0},40};a.to={{4,2,4},{4,1,0},60};
    Shot b=a;b.name="Second";b.duration=3;b.from.eye={10,2,4};b.to=b.from;
    doc.shots={a,b};
    REQUIRE(doc.duration()==5);
    REQUIRE(shot_at(doc,-1)==0 && shot_at(doc,1.999f)==0);
    REQUIRE(shot_at(doc,2)==1 && shot_at(doc,5)==1);
    REQUIRE(sample(doc,1).eye==glm::vec3(2,2,4));
    REQUIRE(sample(doc,-1).eye==a.from.eye);
    REQUIRE(sample(doc,2).eye==b.from.eye);
    REQUIRE(sample(doc,500).eye==b.to.eye);
    Actor actor;actor.name="Johnny \"Mercer\"";actor.mesh="models/test.emesh";
    actor.from={0,0,0};actor.to={10,0,0};actor.start=2;actor.end=4;
    actor.yaw_from=350;actor.yaw_to=10;
    REQUIRE(actor_position(actor,1)==actor.from);
    REQUIRE(actor_position(actor,3)==glm::vec3(5,0,0));
    REQUIRE(actor_position(actor,5)==actor.to);
    REQUIRE(actor_yaw(actor,3)==360);
    actor.end=actor.start;REQUIRE(actor_position(actor,2)==actor.to);
    Actor routed=actor;
    routed.keys={{0,{1,0,1},350,0,{0,1,0},{1,1,0}},
                 {2,{3,0,1},10,1,{2,1,0},{3,1,0}},
                 {4,{3,0,1},10,1,{2,1,0},{3,1,0}}};
    routed.gestures={{1,2,.75f,GestureKind::Explain}};
    REQUIRE(gesture_weight(routed.gestures[0],0)==0);
    REQUIRE(gesture_weight(routed.gestures[0],1)==0);
    REQUIRE_NEAR(gesture_weight(routed.gestures[0],2),.75f,.001f);
    REQUIRE(gesture_weight(routed.gestures[0],3)==0);
    REQUIRE(actor_position(routed,1)==glm::vec3(2,0,1));
    REQUIRE(actor_yaw(routed,1)==360);
    REQUIRE(actor_key(routed,1).reach==.5f);
    REQUIRE(actor_key(routed,9).left_hand==glm::vec3(2,1,0));
    REQUIRE(actor_walk_weight(routed,1)==1);
    REQUIRE(actor_walk_weight(routed,3)==0);
    REQUIRE(actor_position(routed,-5)==glm::vec3(1,0,1));
    doc.actors={actor};doc.cues={{"Johnny","A line\nwith a quote: \"hey\"","",1,2}};
    Document restored;std::string error;
    REQUIRE(decode(encode(doc),restored,error));
    const std::string legacy="APRICOT_CUTSCENE 1\n\"Old scene\" 0.46\n1 1 0\n"
        "\"Shot\" 2 1 0 2 4 0 1 0 50 0 2 4 0 1 0 50\n"
        "\"Actor\" \"test.emesh\" \"\" \"\" 0 0 0 1 0 0 0 90 1.76 0 2\n";
    Document old;REQUIRE(decode(legacy,old,error));REQUIRE(old.actors[0].keys.empty());
    REQUIRE(actor_position(old.actors[0],1)==glm::vec3(.5f,0,0));
    const std::string legacy_v2="APRICOT_CUTSCENE 2\n\"Action scene\" 0.46\n1 1 0\n"
        "\"Shot\" 2 1 0 2 4 0 1 0 50 0 2 4 0 1 0 50\n"
        "\"Actor\" \"test.emesh\" \"\" \"\" 0 0 0 1 0 0 0 90 1.76 0 2 \"walk.eanim\" 2\n"
        "0 0 0 0 0 0 0 1 0 1 1 0\n"
        "2 2 0 0 90 0 0 1 0 1 1 0\n";
    REQUIRE(decode(legacy_v2,old,error));
    REQUIRE(old.actors[0].gestures.empty());
    REQUIRE(old.actors[0].walk_animation=="walk.eanim");
    REQUIRE(actor_position(old.actors[0],1)==glm::vec3(1,0,0));
    REQUIRE(encode(restored)==encode(doc));
    auto route_doc=doc;route_doc.actors={routed};
    REQUIRE(decode(encode(route_doc),restored,error));
    REQUIRE(encode(restored)==encode(route_doc));
    route_doc.actors[0].keys[1].time=0;REQUIRE(!validate(route_doc,error));
    route_doc.actors[0]=routed;route_doc.actors[0].keys[1].reach=2;REQUIRE(!validate(route_doc,error));
    route_doc.actors[0]=routed;route_doc.actors[0].gestures[0].duration=0;REQUIRE(!validate(route_doc,error));
    route_doc.actors[0]=routed;route_doc.actors[0].gestures[0].kind=static_cast<GestureKind>(99);REQUIRE(!validate(route_doc,error));
    REQUIRE(decode(encode(doc),restored,error));
    const auto before=encode(restored);
    REQUIRE(!decode(encode(doc).substr(0,50),restored,error));
    REQUIRE(encode(restored)==before);
    REQUIRE(!decode(encode(doc)+"unexpected",restored,error));
    REQUIRE(!decode("APRICOT_CUTSCENE 99\n",restored,error));
    auto bad=doc;bad.shots[0].duration=0;REQUIRE(!validate(bad,error));
    bad=doc;bad.shots[0].from.fov=std::numeric_limits<float>::quiet_NaN();REQUIRE(!validate(bad,error));
    bad=doc;bad.shots[0].from.target=bad.shots[0].from.eye;REQUIRE(!validate(bad,error));
    bad=doc;bad.shots[0].from={{0,0,1},{0,0,0},45};
    bad.shots[0].to={{0,0,-1},{0,0,0},45};REQUIRE(!validate(bad,error));
    bad=doc;bad.actors[0].end=bad.actors[0].start-1;REQUIRE(!validate(bad,error));
    bad=doc;bad.cues[0].duration=-1;REQUIRE(!validate(bad,error));
    REQUIRE(decode(encode(doc),restored,error));
    const auto path=std::filesystem::current_path()/"cutscene-document-test.cutscene";
    REQUIRE(save(path,doc,error));
    REQUIRE(load(path,restored,error));
    REQUIRE(encode(restored)==encode(doc));
    REQUIRE(!save(path,bad,error));
    REQUIRE(load(path,restored,error));
    REQUIRE(encode(restored)==encode(doc));
    std::filesystem::remove(path);
    return 0;
}
