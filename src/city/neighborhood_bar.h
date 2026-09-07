#pragma once
#include <vector>
#include "city/start_area.h"

namespace apricot::city {
// The unused block south of the fire station and west of Second Chance Pawn.
// A compact single-story corner bar leaves room for its working back alley and
// a small customer lot on the Mercer Avenue side. Local -X is the left side of
// the bar when it is seen from Halloway Street.
inline constexpr float kNeighborhoodBarLotCentreX=-8.505157f;
inline constexpr float kNeighborhoodBarLotWidthM=55.010315f;
inline constexpr StartSite kNeighborhoodBarSite{
    "The Bent Elbow",{70.f+kGridCos*-230.f+kGridSin*-33.f,
                     -40.f-kGridSin*-230.f+kGridCos*-33.f},
    kGridCos,kGridSin,{kNeighborhoodBarLotCentreX,0},kNeighborhoodBarLotWidthM,38};

inline std::vector<StartPart> bake_neighborhood_bar() {
    std::vector<StartPart> out;
    const auto add=[&](const char* name,float x,float z,float y,float w,float h,float d,
                       StartFinish finish,bool solid=false) {
        out.push_back({name,{x,z},y,w,h,d,finish,solid});
    };
    // The widened slab reaches Mercer Avenue's outer sidewalk at about x=-36. The
    // access baker replaces this box with its clipped road mesh so the inlet's
    // visible ramp and collision are the exact same triangles.
    add("bar lot",kNeighborhoodBarLotCentreX,0,0,kNeighborhoodBarLotWidthM,.10f,38,
        StartFinish::Asphalt);
    add("bar front pavement",0,15.5f,.10f,38,.10f,7,StartFinish::Concrete);
    add("bar back alley",0,-14,.10f,38,.10f,10,StartFinish::Concrete);
    add("bar alley entrance walk",0,-8.5f,.10f,2.4f,.10f,1,StartFinish::Concrete);
    add("bar interior floor",0,2,.10f,24,.10f,20,StartFinish::WarmWall,true);
    for(float x:{-11.85f,11.85f})
        add("bar side brick wall",x,2,.20f,.30f,4.6f,20,StartFinish::Brick,true);
    // Both entrances are real gaps. The alley door opens into the same room,
    // with a clear service route around the end of the counter.
    for(float x:{-6.6f,6.6f}) {
        add("bar rear brick wall",x,-7.85f,.20f,10.8f,4.6f,.30f,StartFinish::Brick,true);
        add("bar front window sill",x,11.85f,.20f,10.8f,.65f,.30f,StartFinish::Brick,true);
        add("bar front window lintel",x,11.85f,3.15f,10.8f,1.65f,.30f,StartFinish::Brick,true);
    }
    add("bar front entrance lintel",0,11.85f,3.25f,2.4f,1.55f,.30f,StartFinish::Brick,true);
    add("bar alley entrance lintel",0,-7.85f,3.05f,2.4f,1.75f,.30f,StartFinish::Brick,true);
    for(float x:{-11.65f,-1.3f,1.3f,11.65f})
        add("bar front timber pier",x,11.85f,.85f,.30f,2.30f,.32f,StartFinish::DarkRoof,true);
    for(float x:{-1.12f,1.12f}) {
        add("bar front doorway frame",x,12.03f,.20f,.12f,3.05f,.18f,StartFinish::Steel,true);
        add("bar alley doorway frame",x,-8.03f,.20f,.12f,2.85f,.18f,StartFinish::Steel,true);
    }
    add("bar front threshold",0,11.85f,.10f,2.1f,.10f,.65f,StartFinish::Concrete,true);
    add("bar alley threshold",0,-7.85f,.10f,2.1f,.10f,.65f,StartFinish::Concrete,true);
    // Open glazed frames expose the room rather than hiding it behind an
    // opaque blue panel. Narrow dirty glass edges and grime collect at sills.
    for(float sign:{-1.f,1.f}) {
        const float x=sign*6.45f;
        add("bar window lower frame",x,12.02f,.85f,10.05f,.12f,.14f,StartFinish::DarkRoof,true);
        add("bar window upper frame",x,12.02f,3.02f,10.05f,.13f,.14f,StartFinish::DarkRoof,true);
        add("bar grime at window sill",x,12.06f,.91f,10.05f,.12f,.035f,StartFinish::Steel);
        add("bar window dirty glass edge",x,12.04f,1.03f,10.05f,.13f,.018f,StartFinish::Glass);
        for(float offset:{-3.1f,0.f,3.1f})
            add("bar window mullion",x+offset,12.02f,.98f,.075f,2.04f,.14f,StartFinish::Steel,true);
        add("bar weathered sill ledge",x,12.12f,.78f,10.4f,.10f,.55f,StartFinish::Concrete);
    }
    add("bar roof slab",0,2,4.8f,24.5f,.22f,20.5f,StartFinish::DarkRoof,true);
    add("bar front parapet",0,12,4.8f,24.5f,.5f,.4f,StartFinish::Brick);
    add("bar rear roof coping",0,-8,4.8f,24.5f,.16f,.4f,StartFinish::Steel);
    add("bar ceiling",0,2,4.35f,23.6f,.12f,19.6f,StartFinish::WarmWall);
    add("bar sign backing",0,12.19f,3.35f,8.5f,1.85f,.22f,StartFinish::DarkRoof);
    add("bar shop sign face",0,12.31f,3.47f,8,1.6f,.02f,StartFinish::White);
    for(float x:{-3.8f,3.8f})
        add("bar rusty sign bracket",x,12.15f,4.6f,.10f,.45f,.55f,StartFinish::Steel);
    add("bar faded door canopy",0,12.65f,3.18f,3.1f,.13f,1.65f,StartFinish::RedTrim);
    for(float x:{-10.f,-5.f,6.f,10.f}) {
        add("bar flaked plaster scar",x,12.025f,.24f,1.3f,.34f,.025f,StartFinish::WarmWall);
        add("bar wall damp stain",x+.4f,-8.025f,.22f,.85f,.62f,.025f,StartFinish::Steel);
    }
    add("bar rusty drainpipe",-11.65f,-8.15f,.20f,.16f,4.8f,.16f,StartFinish::Steel);
    add("bar roof exhaust",-8,-4,5.02f,1.1f,1.2f,1.1f,StartFinish::Steel);
    add("bar roof exhaust cap",-8,-4,6.15f,1.5f,.15f,1.5f,StartFinish::Steel);
    add("bar roof patched flashing",7,-2,5.02f,3,.04f,2,StartFinish::RedTrim);
    // Counter and staff aisle: 1.8m between the back bar and serving counter.
    add("bar serving counter",-7.6f,1,.20f,1.4f,1.05f,10,StartFinish::WarmWall,true);
    add("bar worn counter top",-7.6f,1,1.25f,1.65f,.13f,10.2f,StartFinish::DarkRoof);
    add("bar counter foot rail",-6.65f,1,.42f,.08f,.08f,9.3f,StartFinish::Steel);
    add("bar back bar cabinet",-11,1,.20f,1.25f,1.35f,10,StartFinish::DarkRoof,true);
    for(float y:{1.55f,2.45f,3.35f}) {
        add("bar bottle shelf",-11,1,y,1.2f,.09f,10,StartFinish::WarmWall);
        for(int bottle=0;bottle<14;++bottle) {
            const float z=-3.4f+static_cast<float>(bottle)*.66f;
            const auto finish=bottle%3==0?StartFinish::RedTrim:StartFinish::TealDoor;
            add("bar dusty bottle",-10.65f,z,y+.09f,.15f,.32f,.15f,finish);
            add("bar bottle neck",-10.65f,z,y+.41f,.065f,.10f,.065f,finish);
        }
    }
    for(float z:{-2.6f,-.8f,1.f,2.8f,4.6f}) {
        add("bar stool base",-5.75f,z,.20f,.52f,.07f,.52f,StartFinish::Steel);
        add("bar stool stem",-5.75f,z,.27f,.12f,.63f,.12f,StartFinish::Steel,true);
        add("bar cracked stool seat",-5.75f,z,.90f,.62f,.13f,.62f,StartFinish::RedTrim,true);
    }
    add("bar old till",-7.6f,4.3f,1.38f,.6f,.42f,.55f,StartFinish::Steel);
    // Pool table and cue props leave a continuous walking circuit around it.
    add("bar pool table cabinet",3.5f,-1,.55f,3,0.55f,5,StartFinish::WarmWall,true);
    add("bar pool table felt",3.5f,-1,1.10f,2.65f,.025f,4.65f,StartFinish::TealDoor);
    for(float x:{2.1f,4.9f}) for(float z:{-3.3f,1.3f})
        add("bar pool table leg",x,z,.20f,.26f,.35f,.26f,StartFinish::DarkRoof,true);
    for(float x:{2.08f,4.92f})
        add("bar pool side rail",x,-1,1.10f,.17f,.10f,5,StartFinish::DarkRoof);
    for(float z:{-3.42f,1.42f})
        add("bar pool end rail",3.5f,z,1.10f,3,.10f,.17f,StartFinish::DarkRoof);
    for(float x:{2.12f,4.88f}) for(float z:{-3.35f,-1.f,1.35f})
        add("bar pool pocket",x,z,1.19f,.20f,.025f,.20f,StartFinish::DarkRoof);
    for(int ball=0;ball<6;++ball)
        add("bar pool ball",3.1f+static_cast<float>(ball%3)*.14f,-1.2f+static_cast<float>(ball/3)*.14f,
            1.13f,.09f,.09f,.09f,ball==0?StartFinish::White:StartFinish::Yellow);
    for(float z:{4.4f,4.7f,5.f,5.3f})
        add("bar cue rack stick",11.5f,z,.45f,.045f,1.5f,.045f,StartFinish::WarmWall);
    for(float z:{-5.5f,5.5f}) {
        add("bar booth table",9.2f,z,.91f,2,.12f,1.15f,StartFinish::WarmWall,true);
        add("bar booth table leg",9.2f,z,.20f,.14f,.71f,.14f,StartFinish::Steel);
        for(float side:{-1.f,1.f}) {
            add("bar booth seat",9.2f,z+side*1.12f,.20f,3.1f,.45f,.68f,StartFinish::RedTrim,true);
            add("bar worn booth back",9.2f,z+side*1.45f,.65f,3.1f,.80f,.15f,StartFinish::DarkRoof,true);
        }
        add("bar booth ashtray",9.7f,z,1.03f,.24f,.055f,.24f,StartFinish::Steel);
    }
    for(float x:{-3.f,4.f}) {
        add("bar hanging light stem",x,3,3.5f,.055f,.85f,.055f,StartFinish::Steel);
        add("bar hanging light shade",x,3,3.37f,.75f,.16f,.75f,StartFinish::DarkRoof);
        add("bar warm light lens",x,3,3.35f,.48f,.025f,.48f,StartFinish::Yellow);
    }
    add("bar alley dumpster",-9,-14,.20f,3.2f,1.55f,1.7f,StartFinish::Steel,true);
    add("bar crooked dumpster lid",-9,-14,1.75f,3.3f,.11f,1.8f,StartFinish::DarkRoof);
    for(float x:{6.f,7.f,8.f})
        add("bar empty keg",x,-12.5f,.20f,.72f,.95f,.72f,StartFinish::Steel,true);
    add("bar alley utility box",11.98f,-5.7f,1.6f,.35f,.9f,.75f,StartFinish::Steel);
    add("bar alley doormat",0,-8.7f,.20f,2,.025f,.85f,StartFinish::DarkRoof);
    // Five nose-in spaces occupy the new left-hand strip. The aisle behind
    // them lines up with the side inlet and the east ends open directly onto
    // the bar's front pavement.
    for(float x:{-33.5f,-30.3f,-27.1f,-23.9f,-20.7f,-17.5f})
        add("bar parking stripe",x,13.25f,.104f,.10f,.008f,5.5f,
            StartFinish::White);
    for(float x:{-31.9f,-28.7f,-25.5f,-22.3f,-19.1f})
        add("bar parking stop",x,15.75f,.10f,2.25f,.15f,.25f,
            StartFinish::Concrete,true);
    return out;
}
} // namespace apricot::city
