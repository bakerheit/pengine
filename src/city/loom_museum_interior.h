#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "city/start_area.h"

// The Pinatty Museum's interior: finishes, circulation furniture and the eight
// galleries' exhibits. It is split out of loom_cultural.h because the shell -
// the site record, the wall/roof plan and the Doric front - is a different
// job from dressing rooms, and the two were never going to stay legible in one
// file.
//
// Two conventions run through everything here:
//
//   * Walls are LINED, not retextured. One stone run is both the Loom Way
//     facade and a gallery's inside face, so a plaster material on the wall
//     itself would put plaster on the street. Every room gets a thin skirting,
//     plaster field, picture rail and cornice applied to the inside face. The
//     lining is not solid: it stands 8 cm proud of a wall the character
//     already stops 30 cm short of, so it adds no collision and can never trap
//     anyone.
//   * Interpretive graphics are atlas cells, named by index. Keeping the cell
//     in the part name means this header never learns that a texture exists;
//     the host layer maps a name to a sheet and a quadrant.

namespace apricot::city {

inline constexpr float kLoomMuseumFloor=.8f;
inline constexpr float kLoomMuseumUpperFloor=6.8f;

// Finished-face coordinates. Exhibits place against these rather than the
// structural centre lines, so the lining thickness is stated exactly once.
inline constexpr float kLoomOuterFace=26.58f;      // perimeter side walls, inside
inline constexpr float kLoomCorridorFace=7.32f;    // spine walls, gallery side
inline constexpr float kLoomHallFace=6.68f;        // spine walls, hall side
inline constexpr float kLoomFrontFace=9.58f;       // Loom Way wall, inside
inline constexpr float kLoomSouthDivider=-25.68f;  // divider, from the south room
inline constexpr float kLoomNorthDivider=-26.32f;  // divider, from the north room
inline constexpr float kLoomRearFace=-61.58f;
inline constexpr float kLoomLowerSoffit=6.48f;     // ground gallery ceiling
inline constexpr float kLoomUpperSoffit=13.6f;     // upper gallery ceiling
inline constexpr float kLoomBalconyLevel=6.55f;    // upper slab soffit, seen in the hall
inline constexpr float kLoomPortalHead=4.5f;       // doorway lintel, above its own floor

// The plaster field is 8 cm thick, so the surface a graphic hangs on is that
// far inside the finished-face constants above. Everything wall-mounted goes
// through loom_hang(), because a sheet placed on the raw face is invisible:
// the lining is drawn over it and nothing warns you.
inline constexpr float kLoomLining=.08f;
// Clearance for a floor plane over the slab it sits on. See loom_room_finish.
inline constexpr float kLoomFloorLift=.02f;
inline constexpr float loom_hang(float face,float inward,float proud=.05f) {
    return face+inward*(kLoomLining+proud);
}

inline constexpr std::size_t kLoomGalleryCount=8;
inline constexpr std::size_t kLoomLabelCells=24;

inline const char* loom_plaque_name(std::size_t cell) {
    static const char* kNames[]={"museum plaque 0","museum plaque 1","museum plaque 2",
        "museum plaque 3","museum plaque 4","museum plaque 5","museum plaque 6",
        "museum plaque 7"};
    return kNames[cell];
}
inline const char* loom_panel_name(std::size_t cell) {
    static const char* kNames[]={"museum panel 0","museum panel 1","museum panel 2",
        "museum panel 3","museum panel 4","museum panel 5","museum panel 6",
        "museum panel 7"};
    return kNames[cell];
}
inline const char* loom_label_name(std::size_t cell) {
    static const char* kNames[]={
        "museum label 00","museum label 01","museum label 02","museum label 03",
        "museum label 04","museum label 05","museum label 06","museum label 07",
        "museum label 08","museum label 09","museum label 10","museum label 11",
        "museum label 12","museum label 13","museum label 14","museum label 15",
        "museum label 16","museum label 17","museum label 18","museum label 19",
        "museum label 20","museum label 21","museum label 22","museum label 23"};
    return kNames[cell];
}

// One gallery. The rectangle is the finished face, and `wall` is the part name
// the lining carries so the host layer can colour eight rooms from a single
// near-white plaster sheet.
struct LoomRoomPlan {
    const char* wall;
    float x0,x1,z0,z1;
    float floor_m,ceiling_m;
    float portal_z;
    float side;       // -1 west bay, +1 east bay
    float cx,cz;      // room centre
};

// Index order matches kLoomGalleries and the plaque/panel atlas cells: ART,
// ANTIQUITIES, NATURAL HISTORY, PINATTY HISTORY, then the four upstairs.
inline LoomRoomPlan loom_room(std::size_t index) {
    static const char* kWalls[]={"museum wall art","museum wall antiquities",
        "museum wall natural","museum wall pinatty","museum wall science",
        "museum wall space","museum wall transport","museum wall design"};
    const float side=(index%2)?1.f:-1.f;
    const bool north=(index%4)>=2;
    const bool upper=index>=4;
    LoomRoomPlan room{kWalls[index],
        side<0?-kLoomOuterFace:kLoomCorridorFace,side<0?-kLoomCorridorFace:kLoomOuterFace,
        north?kLoomRearFace:kLoomSouthDivider,north?kLoomNorthDivider:kLoomFrontFace,
        upper?kLoomMuseumUpperFloor:kLoomMuseumFloor,
        upper?kLoomUpperSoffit:kLoomLowerSoffit,
        north?-44.f:-8.f,side,0,0};
    room.cx=(room.x0+room.x1)*.5f;
    room.cz=(room.z0+room.z1)*.5f;
    return room;
}

// A single lined face. `gaps` are openings the wall baker already cut, given as
// centres along the run; the field is split around them so a panel never covers
// a window or a doorway. sill/head are measured from this room's floor.
inline void loom_wall_face(std::vector<StartPart>& out,const char* wall_name,
                           bool along_x,float offset,float inward,float a,float b,
                           float floor_m,float ceiling_m,
                           const float* gaps,std::size_t gap_count,float gap_half,
                           float sill_m,float head_m,float string_course_m=0.f) {
    using F=BuildingFinish;
    const auto band=[&](const char* name,float from,float to,float y0,float y1,
                        float thick,F finish) {
        if(to-from<.05f || y1-y0<.01f) return;
        const float mid=(from+to)*.5f,run=to-from,centre=offset+inward*thick*.5f;
        out.push_back({name,along_x?Vec2{mid,centre}:Vec2{centre,mid},y0,
            along_x?run:thick,y1-y0,along_x?thick:run,finish,false});
    };
    const float rail=ceiling_m-.62f,cornice=ceiling_m-.50f;
    band("museum gallery skirting",a,b,floor_m,floor_m+.34f,.15f,F::WarmWall);
    band("museum gallery picture rail",a,b,rail,cornice,.17f,F::WarmWall);
    band("museum gallery cornice",a,b,cornice,ceiling_m,.24f,F::White);
    // The hall runs two storeys as one face. A moulding under the balcony slab
    // and a skirting on top of it keep that height legible, instead of leaving
    // 12 m of blank plaster with a slab edge floating across it. Both sit clear
    // of the slab itself: a band buried inside it would only z-fight.
    if(string_course_m>0) {
        band("museum gallery picture rail",a,b,string_course_m-.46f,string_course_m-.14f,
            .19f,F::WarmWall);
        band("museum gallery skirting",a,b,string_course_m+.25f,string_course_m+.59f,
            .15f,F::WarmWall);
    }
    if(gap_count==0) { band(wall_name,a,b,floor_m+.34f,rail,.08f,F::White); return; }
    band(wall_name,a,b,floor_m+.34f,floor_m+sill_m,.08f,F::White);
    band(wall_name,a,b,floor_m+head_m,rail,.08f,F::White);
    float edge=a;
    for(std::size_t i=0;i<gap_count;++i) {
        band(wall_name,edge,gaps[i]-gap_half,floor_m+sill_m,floor_m+head_m,.08f,F::White);
        // Reveals return the finish into the opening so the cut edge of the
        // stone never shows as a raw seam beside the glass.
        band("museum gallery reveal",gaps[i]-gap_half,gaps[i]-gap_half+.18f,
            floor_m+sill_m,floor_m+head_m,.05f,F::White);
        band("museum gallery reveal",gaps[i]+gap_half-.18f,gaps[i]+gap_half,
            floor_m+sill_m,floor_m+head_m,.05f,F::White);
        edge=gaps[i]+gap_half;
    }
    band(wall_name,edge,b,floor_m+sill_m,floor_m+head_m,.08f,F::White);
}

// All four sides of one gallery, plus its parquet. The window bays and the
// doorway are fixed by the wall records in loom_cultural.h, so the gap lists
// are derived here rather than authored a second time and left to drift.
inline void loom_room_finish(std::vector<StartPart>& out,const LoomRoomPlan& room) {
    const bool north=room.z1<-20.f;
    const float outer_gaps[2]={north?-53.f:-17.f,north?-35.f:1.f};
    const float portal[1]={room.portal_z};
    const float front_gaps[2]={room.side<0?-23.f:15.f,room.side<0?-15.f:23.f};
    loom_wall_face(out,room.wall,false,room.side*kLoomOuterFace,-room.side,room.z0,room.z1,
        room.floor_m,room.ceiling_m,outer_gaps,2,1.55f,1.3f,4.5f);
    loom_wall_face(out,room.wall,false,room.side*kLoomCorridorFace,-room.side,room.z0,room.z1,
        room.floor_m,room.ceiling_m,portal,1,3.2f,0.f,kLoomPortalHead);
    loom_wall_face(out,room.wall,true,north?kLoomRearFace:kLoomSouthDivider,1,
        room.x0,room.x1,room.floor_m,room.ceiling_m,nullptr,0,0,0,0);
    if(north)
        loom_wall_face(out,room.wall,true,kLoomNorthDivider,-1,room.x0,room.x1,
            room.floor_m,room.ceiling_m,nullptr,0,0,0,0);
    else
        loom_wall_face(out,room.wall,true,kLoomFrontFace,-1,room.x0,room.x1,
            room.floor_m,room.ceiling_m,front_gaps,2,1.55f,1.3f,4.5f);
    // Parquet as a thin plane, not a slab: a 2 cm box grows lit vertical edges
    // under the gallery fixtures and reads as a step in the floor. The 2 cm
    // lift is not cosmetic - at 4 mm the plane and the slab under it fell
    // inside the depth buffer's resolution and swapped in hard-edged wedges
    // right across the room.
    out.push_back({"museum gallery parquet",{room.cx,room.cz},room.floor_m+kLoomFloorLift,
        room.x1-room.x0,.004f,room.z1-room.z0,BuildingFinish::WarmWall,false});
}

// Shared furniture. These are the pieces that appear in more than one room;
// anything used once is written inline where it stands.
struct LoomFurniture {
    std::vector<StartPart>& out;
    using F=BuildingFinish;

    void add(const char* name,float x,float z,float y,float w,float h,float d,
             F finish=F::WarmWall,bool solid=false,float yaw=0,float roll=0,float pitch=0) {
        out.push_back({name,{x,z},y,w,h,d,finish,solid,pitch,yaw,roll});
    }
    // A wall-hung sheet. Billboards face local +Z, so yaw turns the graphic to
    // whichever wall it hangs on and the caller never thinks about winding.
    void graphic(const char* name,float x,float z,float y,float w,float h,float yaw) {
        add(name,x,z,y,w,h,.02f,F::White,false,yaw);
    }
    // A floor label stand, for the exhibits that have no wall or case front to
    // carry their label. Kept upright: a raked plate needs the graphic raked
    // with it, and a two-axis rotation on a billboard is not worth the risk of
    // a label lying face-down in one room out of eight.
    void label_stand(std::size_t cell,float x,float z,float floor,float yaw) {
        const float c=std::cos(yaw*.0174532925f),s=std::sin(yaw*.0174532925f);
        add("museum label stand foot",x,z,floor,.44f,.05f,.3f,F::Steel);
        add("museum label stand post",x,z,floor+.05f,.07f,.86f,.07f,F::Steel);
        add("museum label stand plate",x,z,floor+.86f,.72f,.4f,.06f,F::Steel,false,yaw);
        graphic(loom_label_name(cell),x+s*.05f,z+c*.05f,floor+.9f,.62f,.31f,yaw);
    }
    void label(std::size_t cell,float x,float z,float y,float yaw) {
        graphic(loom_label_name(cell),x,z,y,.62f,.31f,yaw);
    }
    // A graphic with a moulded surround, drawn as four edge bars. A backing
    // plate set in front of the sheet is the obvious mistake here: it hides the
    // thing it frames, and it hides it identically in all eight rooms.
    void framed_graphic(const char* name,float x,float z,float y,float w,float h,
                        float yaw,float bar=.13f) {
        graphic(name,x,z,y,w,h,yaw);
        const float c=std::cos(yaw*.0174532925f),s=std::sin(yaw*.0174532925f);
        for(float e:{-1.f,1.f}) {
            const float dx=e*(w+bar)*.5f;
            add("museum panel frame",x+c*dx,z-s*dx,y-bar,bar,h+bar*2.f,.05f,F::Steel,false,yaw);
            add("museum panel frame",x,z,e<0?y-bar:y+h,w+bar*2.f,bar,.05f,F::Steel,false,yaw);
        }
    }
    // A framed painting: gilt frame, canvas, and its object label below.
    void painting(const char* canvas,std::size_t cell,float x,float z,float y,float yaw) {
        const float c=std::cos(yaw*.0174532925f),s=std::sin(yaw*.0174532925f);
        add("museum artwork gilt frame",x,z,y,3.4f,2.5f,.14f,F::Steel,false,yaw);
        add(canvas,x+s*.08f,z+c*.08f,y+.17f,3.06f,2.16f,.02f,F::White,false,yaw);
        label(cell,x+s*.09f-c*2.f,z+c*.09f+s*2.f,y-.62f,yaw);
    }
    // A suspended luminaire: housing, two drop rods and the lens the host layer
    // turns into a light source.
    void pendant(float x,float z,float ceiling,float length) {
        for(float e:{-1.f,1.f})
            add("museum gallery light rod",x+e*length*.34f,z,ceiling-.62f,.05f,.62f,.05f,F::Steel);
        add("museum gallery light housing",x,z,ceiling-.74f,length,.16f,.44f,F::Steel);
        add("museum gallery light lens",x,z,ceiling-.80f,length-.18f,.06f,.34f,F::White);
    }
    // A glass case. Real panes and posts rather than one tinted box, so what is
    // inside stays readable through the front.
    void vitrine(float x,float z,float floor,float w,float d,float h) {
        add("museum vitrine base",x,z,floor,w,.72f,d,F::WarmWall,true);
        add("museum vitrine deck",x,z,floor+.72f,w+.07f,.06f,d+.07f,F::Steel);
        for(float sx:{-1.f,1.f}) {
            add("museum vitrine glass",x+sx*w*.5f,z,floor+.78f,.02f,h,d,F::Glass);
            for(float sz:{-1.f,1.f})
                add("museum vitrine post",x+sx*w*.5f,z+sz*d*.5f,floor+.78f,.05f,h,.05f,F::Steel);
        }
        for(float sz:{-1.f,1.f})
            add("museum vitrine glass",x,z+sz*d*.5f,floor+.78f,w,h,.02f,F::Glass);
        add("museum vitrine glass",x,z,floor+.78f+h,w,.02f,d,F::Glass);
        add("museum vitrine cap",x,z,floor+.80f+h,w+.07f,.07f,d+.07f,F::Steel);
    }
    // Slatted oak on a blackened steel frame, sized to actually sit on.
    void bench(float x,float z,float floor,float length,bool across) {
        const float w=across?.62f:length,d=across?length:.62f;
        add("museum gallery bench",x,z,floor+.36f,w,.10f,d,F::WarmWall,true);
        add("museum gallery bench cushion",x,z,floor+.46f,w-.07f,.045f,d-.07f,F::WarmWall);
        for(float e:{-1.f,1.f})
            add("museum bench frame",x+(across?0.f:e*length*.36f),z+(across?e*length*.36f:0.f),
                floor,across?.5f:.09f,.36f,across?.09f:.5f,F::Steel,true);
    }
    // Rope barrier in front of a floor-standing exhibit.
    void stanchions(float x,float z,float floor,float length,bool along_x) {
        for(int i=0;i<3;++i) {
            const float t=(static_cast<float>(i)-1.f)*length*.5f;
            const float px=x+(along_x?t:0.f),pz=z+(along_x?0.f:t);
            add("museum stanchion post",px,pz,floor,.10f,.90f,.10f,F::Steel);
            add("museum stanchion finial",px,pz,floor+.90f,.15f,.13f,.15f,F::Steel);
            if(i==2)continue;
            const float m=t+length*.25f;
            add("museum stanchion rope",x+(along_x?m:0.f),z+(along_x?0.f:m),floor+.78f,
                along_x?length*.5f:.05f,.05f,along_x?.05f:length*.5f,F::RedTrim);
        }
    }
    // A wall-mounted specimen case, hung between the window bays.
    void wall_case(float x,float z,float floor,float run,bool along_x) {
        const float w=along_x?run:.55f,d=along_x?.55f:run;
        add("museum vitrine base",x,z,floor+1.05f,w,.14f,d,F::WarmWall);
        add("museum vitrine glass",x,z,floor+1.19f,w,1.9f,d,F::Glass);
        add("museum vitrine cap",x,z,floor+3.09f,w+.08f,.12f,d+.08f,F::Steel);
        for(float e:{-1.f,1.f})
            add("museum vitrine post",x+(along_x?e*run*.5f:0.f),z+(along_x?0.f:e*run*.5f),
                floor+1.19f,.06f,1.9f,.06f,F::Steel);
    }
    // A shallow pedestal for a single object, with its label on the front face.
    void pedestal(std::size_t cell,float x,float z,float floor,float w,float h,float label_yaw) {
        add("museum design pedestal",x,z,floor,w,h,w,F::WarmWall,true);
        add("museum pedestal cap",x,z,floor+h,w+.07f,.05f,w+.07f,F::Steel);
        const float c=std::cos(label_yaw*.0174532925f),s=std::sin(label_yaw*.0174532925f);
        label(cell,x+s*(w*.5f+.03f),z+c*(w*.5f+.03f),floor+h-.46f,label_yaw);
    }
};

// --- Rooms -----------------------------------------------------------------

// ART. A double-sided hanging spine splits a 35 m room into two hangs without
// closing the entry axis: the gap in the spine lines up with the doorway, so
// the walk in still runs straight through to the far wall.
inline void loom_gallery_art(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    static const char* kCanvas[]={"museum painting landscape","museum painting portrait",
        "museum painting still life","museum painting harbor"};
    const float spine=-14.f;
    for(const Vec2 span:{Vec2{-20.5f,-11.f},Vec2{-5.f,4.5f}}) {
        const float cz=(span.x+span.z)*.5f,depth=span.z-span.x;
        f.add("museum art hanging wall",spine,cz,room.floor_m,.36f,4.3f,depth,F::WarmWall,true);
        f.add("museum art hanging wall cap",spine,cz,room.floor_m+4.3f,.5f,.12f,depth+.14f,F::WarmWall);
    }
    // Four on the perimeter wall, clear of both window bays; two on each face
    // of the spine. Eight canvases, four plates, hung as pairs.
    const float wall_z[4]={-20.6f,-13.5f,-8.f,-2.4f};
    for(int i=0;i<4;++i)
        f.painting(kCanvas[static_cast<std::size_t>(i)],static_cast<std::size_t>(i),
            loom_hang(room.x0,1.f,.07f),wall_z[i],2.f,90.f);
    for(int i=0;i<2;++i) {
        const float cz=i?-.25f:-15.75f;
        f.painting(kCanvas[static_cast<std::size_t>(i)],static_cast<std::size_t>(i),
            spine+.20f,cz,2.f,90.f);
        f.painting(kCanvas[static_cast<std::size_t>(i)+2],static_cast<std::size_t>(i)+2,
            spine-.20f,cz,2.f,-90.f);
    }
    // Picture lights. One head per canvas, on a track a metre off the wall, so
    // the art is the brightest thing in the room and the pendants only fill.
    const auto track=[&](float x,float z,float yaw) {
        f.add("museum art track arm",x,z,room.floor_m+3.86f,.09f,.62f,.09f,F::Steel,false,yaw);
        f.add("museum art light head",x,z,room.floor_m+3.7f,.44f,.22f,.30f,F::Steel,false,yaw);
        f.add("museum art light lens",x,z,room.floor_m+3.66f,.34f,.05f,.24f,F::White,false,yaw);
    };
    for(int i=0;i<4;++i) track(room.x0+1.25f,wall_z[i],90.f);
    for(int i=0;i<2;++i) {
        const float cz=i?-.25f:-15.75f;
        track(spine+1.35f,cz,90.f);
        track(spine-1.35f,cz,-90.f);
    }
    f.bench(-10.6f,-14.f,room.floor_m,2.8f,true);
    f.bench(-10.6f,-2.f,room.floor_m,2.8f,true);
    f.bench(-20.4f,-11.f,room.floor_m,2.8f,true);
    f.framed_graphic(loom_panel_name(0),room.cx,loom_hang(room.z0,1.f),room.floor_m+1.7f,3.2f,3.2f,0);
}

// ANTIQUITIES. The entry axis ends on the stele; the vessels sit in real cases
// off to either side, at the size a wheel-thrown pot actually is.
inline void loom_gallery_antiquities(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    f.add("museum antiquities monument base",23.2f,-8,room.floor_m,3.6f,.55f,3.6f,F::WarmWall,true);
    f.add("museum antiquities monument shaft",23.2f,-8,room.floor_m+.55f,1.15f,3.3f,1.15f,F::WarmWall);
    f.add("museum antiquities monument crown",23.2f,-8,room.floor_m+3.85f,1.5f,.3f,1.5f,F::Steel);
    for(int band=0;band<6;++band)
        f.add("museum antiquities inscription",22.6f,-8.f,room.floor_m+1.0f+static_cast<float>(band)*.42f,
            .03f,.07f,.82f,F::DarkRoof);
    f.label_stand(6,21.f,-10.4f,room.floor_m,180);
    f.stanchions(23.2f,-10.6f,room.floor_m,4.4f,true);
    // Two tall cases with a storage and a transport amphora, one each side of
    // the axis. A metre tall, which is what these vessels are.
    for(int i=0;i<2;++i) {
        const float cz=i?-2.f:-14.f;
        f.vitrine(16.4f,cz,room.floor_m,2.5f,2.5f,1.85f);
        f.add("museum sculpture amphora",16.4f,cz,room.floor_m+.78f,
            i?.66f:.60f,i?1.02f:1.12f,i?.66f:.60f,F::RedTrim);
        f.label(i?5u:4u,16.4f,cz-1.31f,room.floor_m+1.02f,180);
    }
    // A long low case of six offering vessels against the north end.
    f.vitrine(17.f,-21.6f,room.floor_m,10.4f,2.f,.95f);
    for(int i=0;i<6;++i)
        f.add("museum sculpture amphora",12.4f+static_cast<float>(i)*1.85f,-21.6f,
            room.floor_m+.78f,.30f+static_cast<float>(i%3)*.06f,
            .46f+static_cast<float>(i%3)*.10f,.30f+static_cast<float>(i%3)*.06f,F::RedTrim);
    f.label(7,17.f,-22.62f,room.floor_m+1.16f,180);
    // Fragments, laid flat under the front windows.
    f.vitrine(17.f,5.6f,room.floor_m,10.4f,2.f,.75f);
    for(int i=0;i<9;++i)
        f.add("museum exhibit stone fragment",12.6f+static_cast<float>(i)*1.1f,
            5.6f+static_cast<float>((i%3)-1)*.55f,room.floor_m+.78f,.42f,.20f+static_cast<float>(i%4)*.06f,.34f,
            F::Concrete,false,static_cast<float>(i)*23.f);
    f.bench(11.8f,-14.f,room.floor_m,2.8f,true);
    f.bench(11.8f,-2.f,room.floor_m,2.8f,true);
    f.framed_graphic(loom_panel_name(1),room.cx,loom_hang(room.z0,1.f),room.floor_m+1.9f,3.2f,3.2f,0);
}

// NATURAL HISTORY. The mount keeps its ribcage but gains a real skull on a
// raised neck, so the animal reads as an animal from the doorway instead of
// as a white box on a rack.
inline void loom_gallery_natural(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    const float z0=-49.f,deck=room.floor_m+.5f;
    f.add("museum fossil display plinth",-17.5f,z0,room.floor_m,16.4f,.5f,6.6f,F::Concrete,true);
    f.add("museum fossil plinth cap",-17.5f,z0,deck,16.6f,.05f,6.8f,F::Concrete);
    // Every bone is fitted between actual joint positions. Rotated primitives
    // use a centre-based transform, so bottom must subtract half its length.
    const auto rib=[&](float x,float y0,float za,float y1,float zb,float width) {
        const float dy=y1-y0,dz=zb-za,length=std::sqrt(dy*dy+dz*dz);
        f.add("museum exhibit round fossil rib",x,(za+zb)*.5f,(y0+y1-length)*.5f,
            width,length,width,F::White,false,0,0,std::atan2(dz,dy)*57.29578f);
    };
    const auto bone=[&](float z,float x0,float y0,float x1,float y1,float width) {
        const float dx=x1-x0,dy=y1-y0,length=std::sqrt(dx*dx+dy*dy);
        f.add("museum exhibit round fossil bone",(x0+x1)*.5f,z,(y0+y1-length)*.5f,
            width,length,width,F::White,false,0,-std::atan2(dx,dy)*57.29578f);
    };
    const float spine_y=deck+2.1f;
    bone(z0,-21.5f,spine_y,-13.f,spine_y,.22f);
    // Twelve close-set pairs. Nine at 0.8 m spacing read as a pipe rack rather
    // than a ribcage: the gaps were wider than the ribs.
    for(int i=0;i<12;++i) {
        const float x=-21.f+static_cast<float>(i)*.62f;
        const float span=.88f+.32f*std::sin(static_cast<float>(i)*3.14159265f/11);
        for(float side:{-1.f,1.f})for(int j=0;j<6;++j) {
            const float a=static_cast<float>(j)*3.14159265f/6;
            const float b=static_cast<float>(j+1)*3.14159265f/6;
            rib(x,spine_y-.9f+.92f*std::cos(a),z0+side*span*std::sin(a),
                  spine_y-.9f+.92f*std::cos(b),z0+side*span*std::sin(b),.145f);
        }
        f.add("museum exhibit fossil vertebra",x,z0,spine_y-.16f,.34f,.36f,.44f,F::White);
    }
    // Pelvis and shoulder girdle. Without them the legs hang off a bare rod.
    f.add("museum exhibit fossil pelvis",-20.7f,z0,spine_y-1.15f,1.5f,1.1f,1.9f,F::White);
    f.add("museum exhibit fossil pelvis",-14.4f,z0,spine_y-.95f,1.3f,.95f,1.75f,F::White);
    // Tail, tapering back over the west end of the plinth.
    bone(z0,-21.5f,spine_y,-23.f,spine_y-.35f,.16f);
    bone(z0,-23.f,spine_y-.35f,-24.5f,spine_y-.9f,.12f);
    bone(z0,-24.5f,spine_y-.9f,-25.4f,spine_y-1.4f,.08f);
    // Neck: a long shallow S rather than a steep ramp. The head finishes level
    // and a little above eye height, which is where a mount is posed and where
    // a visitor coming through the door will actually see it.
    const float neck[7][2]={{-13.f,spine_y},{-12.2f,spine_y+.42f},{-11.4f,spine_y+.80f},
        {-10.6f,spine_y+1.10f},{-9.8f,spine_y+1.32f},{-9.f,spine_y+1.44f},{-8.2f,spine_y+1.48f}};
    for(int i=0;i<6;++i) {
        bone(z0,neck[i][0],neck[i][1],neck[i+1][0],neck[i+1][1],.26f-static_cast<float>(i)*.022f);
        f.add("museum exhibit fossil vertebra",neck[i][0],z0,neck[i][1],.32f,.36f,.42f,F::White);
    }
    f.add("museum exhibit skull fossil",-8.8f,z0,spine_y+1.16f,1.9f,.78f,.66f,F::White,false,180,-6.f);
    for(float side:{-1.f,1.f})for(int i=0;i<2;++i) {
        const float x=i?-14.4f:-20.7f;
        rib(x,spine_y,z0,spine_y-.5f,z0+side*.85f,.22f);
        const float z=z0+side*.85f;
        bone(z,x,spine_y-.5f,x-.45f,spine_y-1.35f,.24f);
        bone(z,x-.45f,spine_y-1.35f,x+.05f,spine_y-2.f,.18f);
        f.add("museum exhibit fossil foot",x+.25f,z,deck+.12f,.7f,.15f,.36f,F::White);
    }
    f.stanchions(-17.5f,-45.2f,room.floor_m,11.f,true);
    f.stanchions(-17.5f,-52.8f,room.floor_m,11.f,true);
    f.label_stand(8,-12.6f,-45.4f,room.floor_m,0);
    // A bedding-plane block, tilted the way it came out of the ground.
    f.add("museum specimen plinth",-12.4f,-33.f,room.floor_m,3.4f,.62f,3.f,F::WarmWall,true);
    f.add("museum exhibit bedding block",-12.4f,-33.f,room.floor_m+.62f,2.7f,.42f,2.3f,
        F::Concrete,false,0,17.f);
    f.add("museum exhibit bedding block",-12.4f,-33.f,room.floor_m+.96f,2.5f,.16f,2.1f,
        F::DarkRoof,false,0,17.f);
    f.label(9,-12.4f,-34.6f,room.floor_m+.9f,180);
    // Specimen cases along the perimeter, hung between the window bays.
    for(float z:{-58.f,-44.f,-30.f}) f.wall_case(room.x0+.35f,z,room.floor_m,3.6f,false);
    f.bench(-11.f,-56.f,room.floor_m,2.8f,true);
    f.bench(-11.f,-38.f,room.floor_m,2.8f,true);
    f.framed_graphic(loom_panel_name(2),room.cx,loom_hang(room.z1,-1.f),room.floor_m+1.7f,3.2f,3.2f,180);
}

// PINATTY HISTORY. The city model gets its streets, its water and a tint per
// block, under glass. White blocks on a black board read as unfinished, which
// is exactly what they were.
inline void loom_gallery_pinatty(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    // Set back from the doorway: at its old centre the table reached to within
    // three metres of the portal and blocked the way in.
    const float deck=room.floor_m+1.f;
    f.add("museum history model table",18.4f,-48.5f,room.floor_m,10.8f,1.f,7.8f,F::WarmWall,true);
    f.add("museum history model deck",18.4f,-48.5f,deck,10.4f,.05f,7.4f,F::White);
    // Street grid first, then the blocks in the cells it leaves.
    // 1:500. At that scale a city block is under a metre across and a tall
    // building is a hand's width high; the model read as a row of grey slabs
    // when its blocks were the size of whole districts.
    constexpr int kBlocksX=8,kBlocksZ=5;
    constexpr float kPitch=1.24f,kBlock=.9f;
    const float grid_x=18.4f-static_cast<float>(kBlocksX-1)*kPitch*.5f;
    const float grid_z=-48.5f-static_cast<float>(kBlocksZ-1)*kPitch*.5f;
    for(int i=0;i<=kBlocksX;++i)
        f.add("museum history model street",grid_x+(static_cast<float>(i)-.5f)*kPitch,-48.5f,
            deck+.05f,kPitch-kBlock,.012f,7.1f,F::DarkRoof);
    for(int i=0;i<=kBlocksZ;++i)
        f.add("museum history model street",18.4f,grid_z+(static_cast<float>(i)-.5f)*kPitch,
            deck+.05f,10.1f,.012f,kPitch-kBlock,F::DarkRoof);
    f.add("museum history model water",22.6f,-51.9f,deck+.04f,3.f,.014f,2.6f,F::PoolWater,false,18.f);
    for(int gx=0;gx<kBlocksX;++gx)for(int gz=0;gz<kBlocksZ;++gz) {
        const int cell=gx*kBlocksZ+gz;
        if(gx>=6 && gz==0) continue;   // the bay corner stays open water
        // Height falls away from the centre, the way a city with a downtown
        // does. Three finishes read as three periods of construction.
        const float from_core=std::fabs(static_cast<float>(gx)-3.f)+std::fabs(static_cast<float>(gz)-2.f);
        const float height=std::max(.07f,.40f-from_core*.055f)+static_cast<float>(cell%3)*.03f;
        const float bx=grid_x+static_cast<float>(gx)*kPitch;
        const float bz=grid_z+static_cast<float>(gz)*kPitch;
        f.add("museum exhibit history city block",bx,bz,deck+.06f,kBlock,height,kBlock,
            (cell%3==0)?F::Brick:((cell%3==1)?F::WarmWall:F::Concrete));
        if(from_core<1.5f)
            f.add("museum exhibit history tower",bx,bz,deck+.06f+height,.42f,.34f,.42f,F::Concrete);
    }
    // Loom Way and the museum's own block, called out in brass.
    f.add("museum history model route",18.4f,-47.8f,deck+.062f,10.1f,.014f,.2f,F::Steel);
    f.add("museum history model marker",18.4f,-48.5f,deck+.07f,.3f,.62f,.3f,F::Yellow);
    for(float e:{-1.f,1.f}) {
        f.add("museum vitrine glass",18.4f,-48.5f+e*3.9f,deck+.06f,10.6f,.9f,.02f,F::Glass);
        f.add("museum vitrine glass",18.4f+e*5.3f,-48.5f,deck+.06f,.02f,.9f,7.8f,F::Glass);
    }
    f.add("museum vitrine glass",18.4f,-48.5f,deck+.96f,10.6f,.02f,7.8f,F::Glass);
    f.add("museum vitrine cap",18.4f,-48.5f,deck+.98f,10.8f,.08f,8.f,F::Steel);
    f.label(10,18.4f,-52.44f,deck+.4f,180);
    // The First Street closure, on a drafting table you can read standing up.
    f.add("museum survey table",12.6f,-33.5f,room.floor_m,3.6f,.94f,2.2f,F::WarmWall,true);
    f.add("museum survey top",12.6f,-33.5f,room.floor_m+.94f,3.7f,.07f,2.3f,F::White,false,0,0,-14.f);
    f.label(11,12.6f,-34.7f,room.floor_m+.92f,180);
    for(float z:{-56.f,-32.f}) f.wall_case(room.x1-.35f,z,room.floor_m,3.6f,false);
    f.bench(22.6f,-38.f,room.floor_m,2.8f,true);
    f.bench(12.f,-55.f,room.floor_m,2.8f,true);
    f.framed_graphic(loom_panel_name(3),room.cx,loom_hang(room.z1,-1.f),room.floor_m+1.7f,3.2f,3.2f,180);
}

// SCIENCE. A 6.8 m room is tall enough for a real pendulum, so the swing gets
// the entry axis and the gear train sits off to the side.
inline void loom_gallery_science(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    const float floor=room.floor_m;
    f.add("museum science display base",-20.f,-8.f,floor,7.2f,.22f,11.f,F::WarmWall,true);
    f.add("museum science dial",-20.f,-8.f,floor+.22f,4.6f,.008f,4.6f,F::White);
    for(int i=0;i<24;++i) {
        const float a=static_cast<float>(i)*.2617994f;
        f.add("museum science dial pin",-20.f+2.05f*std::sin(a),-8.f+2.05f*std::cos(a),
            floor+.22f,.07f,.20f,.07f,F::Steel);
    }
    for(float z:{-12.6f,-3.4f}) {
        f.add("museum exhibit pendulum frame",-20.f,z,floor+.22f,.34f,5.68f,.36f,F::Steel);
        f.add("museum pendulum footing",-20.f,z,floor+.22f,.72f,.26f,.74f,F::Steel);
    }
    f.add("museum exhibit pendulum crossbar",-20.f,-8.f,floor+5.9f,.4f,.34f,9.6f,F::Steel);
    f.add("museum pendulum pivot",-20.f,-8.f,floor+5.66f,.3f,.26f,.3f,F::Steel);
    f.add("museum exhibit pendulum cable",-20.f,-8.f,floor+1.62f,.05f,4.04f,.05f,F::Steel);
    f.add("museum exhibit sphere pendulum bob",-20.f,-8.f,floor+1.f,.66f,.66f,.66f,F::Yellow);
    f.label_stand(12,-16.2f,-4.6f,floor,0);
    f.stanchions(-16.f,-8.f,floor,8.2f,false);
    // A gear train on a bench, cut away so the mesh of the teeth is visible.
    f.add("museum science gear plinth",-13.2f,-19.f,floor,4.4f,.86f,3.4f,F::WarmWall,true);
    f.add("museum gear frame",-13.2f,-19.f,floor+.86f,.24f,2.3f,3.f,F::Steel);
    const auto gear=[&](float x,float z,float radius,int teeth) {
        f.add("museum exhibit round gear wheel",x,z,floor+1.5f,radius*2.f,.16f,radius*2.f,
            F::Steel,false,0,0,90.f);
        for(int i=0;i<teeth;++i) {
            const float a=static_cast<float>(i)*6.2831853f/static_cast<float>(teeth);
            f.add("museum exhibit gear tooth",x,z+radius*std::cos(a),
                floor+1.5f+radius*std::sin(a),.2f,.22f,.16f,F::Steel,false,0,0,a*57.29578f);
        }
        f.add("museum gear hub",x,z,floor+1.5f,.3f,.2f,.3f,F::Steel,false,0,0,90.f);
    };
    gear(-13.32f,-19.9f,.86f,18);
    gear(-13.32f,-18.28f,.72f,15);
    gear(-13.08f,-17.1f,.42f,9);
    f.add("museum gear crank",-12.8f,-17.1f,floor+1.44f,.3f,.12f,.12f,F::Steel);
    f.label(13,-13.2f,-20.75f,floor+1.15f,180);
    f.bench(-11.5f,-6.f,floor,2.8f,true);
    f.bench(-24.f,-19.f,floor,2.8f,true);
    f.framed_graphic(loom_panel_name(4),-17.f,loom_hang(room.z0,1.f),floor+2.1f,3.4f,3.4f,0);
}

// SPACE. The old rocket was as wide as it was tall and read as a hydrant. This
// one keeps a real fineness ratio and gets a gantry to give it scale.
inline void loom_gallery_space(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    const float floor=room.floor_m,pad=floor+.5f;
    f.add("museum space rocket plinth",21.f,-8.f,floor,5.4f,.5f,5.4f,F::WarmWall,true);
    f.add("museum space rocket pad",21.f,-8.f,pad,4.6f,.05f,4.6f,F::DarkRoof);
    f.add("museum exhibit round rocket body",21.f,-8.f,pad+.35f,1.12f,3.95f,1.12f,F::White);
    f.add("museum rocket band",21.f,-8.f,pad+1.5f,1.16f,.34f,1.16f,F::RedTrim);
    f.add("museum rocket band",21.f,-8.f,pad+3.4f,1.16f,.22f,1.16f,F::RedTrim);
    f.add("museum exhibit cone rocket nose",21.f,-8.f,pad+4.3f,1.12f,1.5f,1.12f,F::RedTrim);
    f.add("museum exhibit round rocket skirt",21.f,-8.f,pad+.05f,1.26f,.34f,1.26f,F::Steel);
    f.add("museum exhibit round rocket nozzle",21.f,-8.f,pad-.34f,.72f,.42f,.72f,F::DarkRoof);
    // Four swept fins, stepped so the trailing edge rakes back.
    for(float yaw:{0.f,90.f}) {
        f.add("museum exhibit rocket fins",21.f,-8.f,pad+.35f,2.9f,.85f,.09f,F::RedTrim,false,yaw);
        f.add("museum exhibit rocket fins",21.f,-8.f,pad+1.2f,2.1f,.62f,.09f,F::RedTrim,false,yaw);
        f.add("museum exhibit rocket fins",21.f,-8.f,pad+1.82f,1.5f,.42f,.09f,F::RedTrim,false,yaw);
    }
    // Service gantry: it is what tells you the rocket is six metres tall.
    for(float e:{-1.f,1.f}) f.add("museum gantry leg",23.4f,-8.f+e*1.5f,pad,.22f,5.4f,.22f,F::Steel);
    for(int i=0;i<3;++i)
        f.add("museum gantry deck",23.4f,-8.f,pad+1.5f+static_cast<float>(i)*1.7f,1.4f,.1f,3.2f,F::Steel);
    f.add("museum gantry mast",23.4f,-8.f,pad,.3f,5.6f,3.2f,F::Steel);
    f.label_stand(14,18.6f,-11.6f,floor,180);
    f.stanchions(21.f,-11.4f,floor,5.6f,true);
    // The solar sequence: turned spheres on brass rods over an orbit inlay.
    f.add("museum space model table",14.4f,-18.5f,floor,9.4f,.92f,2.6f,F::WarmWall,true);
    f.add("museum space model top",14.4f,-18.5f,floor+.92f,9.5f,.06f,2.7f,F::DarkRoof);
    f.add("museum exhibit sphere planet",10.3f,-18.5f,floor+1.12f,.86f,.86f,.86f,F::Yellow);
    for(int i=0;i<7;++i) {
        const float x=11.4f+static_cast<float>(i)*1.15f;
        const float d=.24f+static_cast<float>(i%4)*.075f;
        f.add("museum planet rod",x,-18.5f,floor+.98f,.035f,.44f,.035f,F::Steel);
        f.add("museum exhibit sphere planet",x,-18.5f,floor+1.42f,d,d,d,
            (i%3==0)?F::TealDoor:((i%3==1)?F::RedTrim:F::Concrete));
        f.add("museum planet arc",x,-18.5f,floor+.99f,static_cast<float>(i)*.24f+.5f,.01f,.9f,F::Steel);
    }
    f.label(15,14.4f,-19.85f,floor+1.12f,180);
    f.bench(12.f,-6.f,floor,2.8f,true);
    f.bench(24.f,-19.f,floor,2.8f,true);
    f.framed_graphic(loom_panel_name(5),17.f,loom_hang(room.z0,1.f),floor+2.1f,3.4f,3.4f,0);
}

// TRANSPORT. Same locomotive, built the way one is: spoked wheels on the rail
// head, a rod that connects them, a smokebox, and a tender behind.
inline void loom_gallery_transport(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    const float floor=room.floor_m,ballast=floor+.35f,rail_top=ballast+.22f;
    const float z0=-49.f,gauge=.72f;
    f.add("museum transport display base",-17.f,z0,floor,17.f,.35f,6.4f,F::Concrete,true);
    for(int i=0;i<22;++i)
        f.add("museum exhibit track sleeper",-24.8f+static_cast<float>(i)*.76f,z0,ballast,
            .3f,.14f,2.3f,F::WarmWall);
    for(float e:{-1.f,1.f})
        f.add("museum exhibit train rail",-17.f,z0+e*gauge,ballast+.14f,16.4f,.08f,.11f,F::Steel);
    // Frames, buffer beam and the running gear.
    f.add("museum exhibit train chassis",-17.4f,z0,rail_top+.62f,10.4f,.42f,2.16f,F::DarkRoof);
    f.add("museum exhibit train buffer beam",-12.f,z0,rail_top+.5f,.28f,.72f,2.5f,F::RedTrim);
    for(float e:{-1.f,1.f})
        f.add("museum exhibit train buffer",-11.8f,z0+e*.86f,rail_top+.86f,.34f,.26f,.26f,F::Steel);
    f.add("museum exhibit train cowcatcher",-11.5f,z0,rail_top+.02f,.7f,.86f,2.1f,F::Steel,false,0,0,26.f);
    const float wheel_x[3]={-20.6f,-17.4f,-14.2f};
    const float wheel_y=rail_top+.72f;
    for(int i=0;i<3;++i)for(float e:{-1.f,1.f}) {
        f.add("museum exhibit wheel train",wheel_x[i],z0+e*(gauge+.12f),wheel_y-.72f,
            1.44f,1.44f,.24f,F::RedTrim);
        f.add("museum exhibit round train axle",wheel_x[i],z0,wheel_y-.09f,
            .18f,1.9f,.18f,F::Steel,false,0,0,90.f);
    }
    // The coupling rod, dropped to the crank pin so the wheels read as driven.
    for(float e:{-1.f,1.f}) {
        f.add("museum exhibit train coupling rod",-17.4f,z0+e*(gauge+.26f),wheel_y-.52f,
            6.9f,.14f,.07f,F::Steel);
        for(int i=0;i<3;++i)
            f.add("museum exhibit train crank pin",wheel_x[i],z0+e*(gauge+.26f),wheel_y-.56f,
                .2f,.2f,.13f,F::Steel);
    }
    // Boiler, smokebox, dome and chimney.
    f.add("museum exhibit round train boiler",-17.6f,z0,rail_top+.14f,1.86f,6.f,1.86f,
        F::DarkRoof,false,0,90.f);
    for(int i=0;i<5;++i)
        f.add("museum exhibit boiler band",-20.f+static_cast<float>(i)*1.4f,z0,rail_top+.12f,
            .12f,1.92f,1.92f,F::Steel);
    f.add("museum exhibit round train smokebox",-13.9f,z0,rail_top+.1f,1.98f,1.5f,1.98f,
        F::DarkRoof,false,0,90.f);
    f.add("museum exhibit round smokebox door",-13.f,z0,rail_top+.25f,1.6f,.2f,1.6f,
        F::Steel,false,0,90.f);
    f.add("museum smokebox hinge",-12.9f,z0,rail_top+.8f,.16f,.9f,.14f,F::Steel);
    f.add("museum exhibit round train chimney",-13.9f,z0,rail_top+1.98f,.58f,.86f,.58f,F::Steel);
    f.add("museum exhibit round train chimney cap",-13.9f,z0,rail_top+2.72f,.78f,.22f,.78f,F::Steel);
    f.add("museum exhibit round train dome",-17.4f,z0,rail_top+1.9f,.86f,.52f,.86f,F::Steel);
    f.add("museum exhibit train headlamp",-12.6f,z0,rail_top+1.86f,.46f,.5f,.46f,F::Steel);
    f.add("museum accent light lens",-12.38f,z0,rail_top+1.98f,.04f,.26f,.26f,F::White);
    // Cab, with real openings rather than a painted-on window.
    f.add("museum exhibit train cab",-21.6f,z0,rail_top+.84f,2.9f,2.5f,2.5f,F::TealDoor);
    f.add("museum exhibit train cab roof",-21.6f,z0,rail_top+3.34f,3.3f,.18f,2.9f,F::DarkRoof);
    for(float e:{-1.f,1.f}) {
        f.add("museum exhibit train cab window",-21.f,z0+e*1.26f,rail_top+2.26f,1.f,.86f,.06f,F::DarkRoof);
        f.add("museum exhibit train cab window",-22.4f,z0+e*1.26f,rail_top+2.26f,.86f,.86f,.06f,F::DarkRoof);
    }
    f.add("museum exhibit train cab window",-23.06f,z0,rail_top+2.26f,.06f,.86f,1.5f,F::DarkRoof);
    // Tender, on four smaller wheels.
    f.add("museum exhibit train tender",-25.4f,z0,rail_top+.62f,3.f,1.9f,2.4f,F::DarkRoof);
    f.add("museum exhibit train coal",-25.4f,z0,rail_top+2.52f,2.5f,.34f,2.f,F::Concrete);
    for(float ex:{-1.f,1.f})for(float ez:{-1.f,1.f})
        f.add("museum exhibit wheel train",-25.4f+ex*1.f,z0+ez*(gauge+.12f),rail_top+.02f,
            1.f,1.f,.2f,F::RedTrim);
    f.stanchions(-17.f,z0+3.6f,floor,12.f,true);
    f.stanchions(-17.f,z0-3.6f,floor,12.f,true);
    f.label_stand(16,-12.6f,-45.f,floor,0);
    f.label_stand(17,-21.f,-45.f,floor,0);
    f.bench(-11.4f,-38.f,floor,2.8f,true);
    f.bench(-11.4f,-56.f,floor,2.8f,true);
    f.framed_graphic(loom_panel_name(6),-17.f,loom_hang(room.z1,-1.f),floor+2.1f,3.4f,3.4f,180);
}

// DESIGN. Three stacks of coloured boxes were the weakest room in the building.
// It now shows objects: two chairs, a glass service, a lamp and a study wall.
inline void loom_gallery_design(LoomFurniture& f,const LoomRoomPlan& room) {
    using F=BuildingFinish;
    const float floor=room.floor_m;
    // Side chair, model 12. Bent ply shell on a steel frame, twice, in two
    // finishes - the way a design collection actually shows a production run.
    const auto chair=[&](float x,float z,float yaw,F shell) {
        const float c=std::cos(yaw*.0174532925f),s=std::sin(yaw*.0174532925f);
        const auto at=[&](float lx,float lz){ return Vec2{x+c*lx+s*lz,z-s*lx+c*lz}; };
        for(float ex:{-1.f,1.f})for(float ez:{-1.f,1.f}) {
            const auto p=at(ex*.19f,ez*.19f);
            f.add("museum design chair leg",p.x,p.z,floor+.55f,.035f,.42f,.035f,F::Steel);
        }
        f.add("museum design chair seat",x,z,floor+.97f,.48f,.045f,.46f,shell,false,yaw);
        f.add("museum design chair rail",x,z,floor+.93f,.5f,.045f,.48f,F::Steel,false,yaw);
        const auto back=at(0,-.2f);
        f.add("museum design chair back",back.x,back.z,floor+1.015f,.44f,.5f,.045f,shell,false,yaw,0,-11.f);
        for(float ex:{-1.f,1.f}) {
            const auto p=at(ex*.19f,-.19f);
            f.add("museum design chair leg",p.x,p.z,floor+1.f,.03f,.5f,.03f,F::Steel);
        }
    };
    f.pedestal(18,12.4f,-52.f,floor,2.2f,.55f,0);
    chair(12.4f,-52.f,18.f,F::RedTrim);
    f.pedestal(18,12.4f,-46.f,floor,2.2f,.55f,0);
    chair(12.4f,-46.f,-24.f,F::TealDoor);
    // Pressed glass service, in a case at reading height.
    f.vitrine(17.4f,-52.5f,floor,4.2f,2.2f,1.f);
    for(int i=0;i<7;++i) {
        const float x=15.8f+static_cast<float>(i%4)*1.05f;
        const float z=-53.1f+static_cast<float>(i/4)*1.15f;
        f.add("museum design glassware",x,z,floor+.78f,.24f+static_cast<float>(i%3)*.05f,
            .2f+static_cast<float>(i%4)*.09f,.24f+static_cast<float>(i%3)*.05f,F::Glass);
    }
    f.label(19,17.4f,-53.62f,floor+1.16f,180);
    // Table lamp, type C.
    f.pedestal(20,22.4f,-52.f,floor,1.5f,.92f,-90);
    f.add("museum design lamp base",22.4f,-52.f,floor+.97f,.36f,.06f,.36f,F::Steel);
    f.add("museum design lamp stem",22.4f,-52.f,floor+1.03f,.05f,.5f,.05f,F::Steel);
    f.add("museum design lamp shade",22.4f,-52.f,floor+1.44f,.52f,.34f,.52f,F::Yellow,false,0,0,14.f);
    f.add("museum accent light lens",22.4f,-52.f,floor+1.42f,.34f,.04f,.34f,F::White);
    // A study wall of framed proportion boards on the perimeter, between the
    // window bays. Four boards, one rule, no boxes of colour on plinths.
    // Two boards each side of the east window bay at z = -53. Spaced evenly
    // they landed across it, and one of the four framed Loom Way.
    const float board_z[4]={-59.4f,-56.4f,-49.f,-46.f};
    for(int i=0;i<4;++i) {
        const float z=board_z[i];
        f.add("museum design study frame",room.x1-.12f,z,floor+1.6f,.08f,2.f,1.5f,F::Steel);
        f.add("museum design study board",room.x1-.17f,z,floor+1.7f,.03f,1.7f,1.24f,
            (i%2)?F::White:F::Concrete);
    }
    f.bench(21.f,-57.f,floor,2.8f,false);
    f.label_stand(21,13.9f,-41.f,floor,0);
    f.bench(12.4f,-40.6f,floor,2.8f,true);
    f.framed_graphic(loom_panel_name(7),17.f,loom_hang(room.z1,-1.f),floor+2.1f,3.4f,3.4f,180);
}


// --- Entrance hall ---------------------------------------------------------

// Reception, directory, centrepiece and chandelier. The hall is 26 m long and
// 12.8 m tall in the atrium; its job is to hold that height and hand the
// visitor four legible doorways plus the stair.
inline void loom_entrance_hall(LoomFurniture& f) {
    using F=BuildingFinish;
    const float floor=kLoomMuseumFloor;
    // Reception on the west side with a return, the directory opposite, so an
    // arriving visitor meets both and neither blocks the view up the hall.
    f.add("museum reception desk",-4.4f,5.f,floor,4.2f,1.05f,1.5f,F::WarmWall,true);
    f.add("museum reception desk",-2.9f,3.4f,floor,1.5f,1.05f,1.7f,F::WarmWall,true);
    f.add("museum reception top",-4.4f,5.f,floor+1.05f,4.5f,.09f,1.72f,F::Steel);
    f.add("museum reception top",-2.9f,3.4f,floor+1.05f,1.8f,.09f,1.92f,F::Steel);
    f.add("museum reception apron",-4.4f,5.72f,floor+.06f,4.1f,.86f,.06f,F::TealDoor);
    for(float dx:{-1.3f,0.f,1.3f})
        f.add("museum reception stool",-4.4f+dx,4.f,floor,.4f,.64f,.4f,F::Steel,true);
    f.label(22,-4.4f,5.79f,floor+1.28f,0);
    f.framed_graphic("museum directory",loom_hang(kLoomHallFace,-1.f,.04f),4.4f,
        floor+.55f,1.55f,2.3f,-90,.11f);
    f.label(23,loom_hang(kLoomHallFace,-1.f,.04f),1.4f,floor+1.28f,-90);
    // Against the walls, running with the hall, and clear of the doorway
    // approach at z = -8. Set across the hall, or level with a portal, a bench
    // stands in the only lane between the front door and the stair.
    for(float z:{2.f,-14.f})for(float e:{-1.f,1.f}) f.bench(e*5.5f,z,floor,2.8f,true);

    // Armillary centrepiece. A 12.8 m hall wants a tall object on its axis; the
    // vase that used to stand here was a metre taller than the visitor and read
    // as a prop rather than as a collection piece.
    f.add("museum hall plinth",0,-2.f,floor,4.f,.86f,4.f,F::WarmWall,true);
    f.add("museum hall plinth cap",0,-2.f,floor+.86f,4.4f,.14f,4.4f,F::Steel);
    f.add("museum exhibit armillary column",0,-2.f,floor+1.f,.44f,1.4f,.44f,F::Steel);
    for(int i=0;i<3;++i)
        f.add("museum exhibit ring armillary",0,-2.f,floor+1.05f,3.3f,3.3f,3.3f,F::Steel,false,
            static_cast<float>(i)*60.f,i==2?28.f:0.f);
    f.add("museum exhibit sphere armillary sun",0,-2.f,floor+2.36f,.72f,.72f,.72f,F::Yellow);
    f.stanchions(0,-4.6f,floor,5.2f,true);
    f.stanchions(0,.6f,floor,5.2f,true);
    // Chandelier, hung in the void over the floor inlay. Its lamps are emissive
    // only: two real sources at the ring do the lighting, because twelve point
    // lights in one fitting buys nothing a player can see.
    f.add("museum chandelier stem",0,-2.f,9.9f,.09f,3.7f,.09f,F::Steel);
    f.add("museum exhibit ring chandelier",0,-2.f,7.3f,5.2f,5.2f,5.2f,F::Steel,false,0,90.f);
    for(int i=0;i<10;++i) {
        const float a=static_cast<float>(i)*.6283185f;
        const float x=2.42f*std::sin(a),z=-2.f+2.42f*std::cos(a);
        f.add("museum chandelier lamp",x,z,9.42f,.2f,.5f,.2f,F::Steel);
        f.add("museum chandelier light lens",x,z,9.38f,.16f,.05f,.16f,F::White);
    }
    for(float e:{-1.f,1.f})
        f.add("museum gallery light lens",e*1.1f,-2.f,9.5f,.4f,.05f,.4f,F::White);
    for(float side:{-1.f,1.f})for(float z:{4.f,-2.f,-8.f,-14.f}) {
        const float x=loom_hang(side*kLoomHallFace,-side,.06f);
        f.add("museum hall sconce",x,z,floor+3.4f,.26f,.62f,.72f,F::Steel);
        f.add("museum gallery light lens",x-side*.16f,z,floor+3.36f,.06f,.05f,.5f,F::White);
    }
    // Niches in the long blank wall between the two pairs of portals, each with
    // a vessel on a corbel. Fourteen metres of empty plaster reads as a corridor
    // that ran out of budget.
    for(float side:{-1.f,1.f})for(float z:{-20.f,-26.f,-32.f}) {
        const float x=side*(kLoomHallFace-.05f);
        f.add("museum hall niche",x,z,floor+.9f,.22f,3.f,2.2f,F::White);
        f.add("museum hall niche arch",x,z,floor+3.9f,.28f,.4f,2.5f,F::WarmWall);
        f.add("museum hall corbel",x-side*.34f,z,floor+1.1f,.7f,.24f,1.f,F::WarmWall);
        f.add("museum sculpture amphora",x-side*.5f,z,floor+1.34f,.56f,.94f,.56f,F::RedTrim);
        f.add("museum accent light lens",x-side*.16f,z,floor+3.6f,.24f,.05f,.7f,F::White);
    }
}

// --- Assembly --------------------------------------------------------------

// Room partitions, exhibits and balcony share the same render/collision bake.
inline void append_loom_galleries(std::vector<StartPart>& out) {
    using F=BuildingFinish;
    LoomFurniture f{out};
    // Spine walls, dividers and the four portals, on both floors.
    for(float floor:{kLoomMuseumFloor,kLoomMuseumUpperFloor}) {
        const float height=floor<1?5.75f:6.8f;
        for(float side:{-1.f,1.f}) {
            for(const Vec2 span:{Vec2{-62,-47},Vec2{-41,-11},Vec2{-5,10}})
                f.add("museum gallery corridor wall",side*7,(span.x+span.z)*.5f,floor,
                    .4f,height,span.z-span.x,F::WarmWall,true);
            for(float z:{-44.f,-8.f}) {
                f.add("museum gallery doorway lintel",side*7,z,floor+kLoomPortalHead,
                    .4f,height-kLoomPortalHead,6,F::WarmWall,true);
                // A moulded architrave on the hall side of each portal. Two
                // jambs and a head read as a doorway from across a 26 m hall;
                // a rectangular hole in a wall does not.
                for(float e:{-1.f,1.f})
                    f.add("museum portal architrave",side*6.6f,z+e*3.36f,floor,
                        .74f,kLoomPortalHead+.44f,.72f,F::WarmWall);
                f.add("museum portal architrave",side*6.6f,z,floor+kLoomPortalHead,
                    .74f,.44f,7.44f,F::WarmWall);
                f.add("museum portal keystone",side*6.55f,z,floor+kLoomPortalHead+.1f,
                    .84f,.62f,.9f,F::WarmWall);
            }
            f.add("museum gallery dividing wall",side*17,-26,floor,20,height,.4f,F::WarmWall,true);
            if(floor<1)
                f.add("museum lower gallery ceiling",side*17,-26,kLoomLowerSoffit,
                    19.6f,.07f,71.4f,F::White);
        }
    }
    for(std::size_t i=0;i<kLoomGalleryCount;++i) loom_room_finish(out,loom_room(i));

    // The hall runs both storeys as one lined face, broken by the balcony
    // course. Its segments match the spine wall runs, so no lining ever crosses
    // a portal.
    for(float side:{-1.f,1.f})
        for(const Vec2 span:{Vec2{kLoomRearFace,-47},Vec2{-41,-11},Vec2{-5,kLoomFrontFace}})
            loom_wall_face(out,"museum wall hall",false,side*kLoomHallFace,-side,
                span.x,span.z,kLoomMuseumFloor,kLoomUpperSoffit,nullptr,0,0,0,0,
                kLoomBalconyLevel);
    {
        const float door[1]={0.f};
        loom_wall_face(out,"museum wall hall",true,kLoomFrontFace,-1,
            -kLoomHallFace,kLoomHallFace,kLoomMuseumFloor,kLoomUpperSoffit,
            door,1,3.f,0.f,6.2f,kLoomBalconyLevel);
        loom_wall_face(out,"museum wall hall",true,kLoomRearFace,1,
            -kLoomHallFace,kLoomHallFace,kLoomMuseumFloor,kLoomUpperSoffit,
            nullptr,0,0,0,0,kLoomBalconyLevel);
    }
    // Stone bands in the terrazzo. A 72 m room has no way to show its own scale
    // without a border and a centred inlay to read it against.
    for(float e:{-1.f,1.f}) {
        f.add("museum hall floor band",e*5.85f,-26.f,kLoomMuseumFloor+kLoomFloorLift,.5f,.004f,70.6f,F::DarkRoof);
        f.add("museum hall floor band",0,-26.f+e*35.3f,kLoomMuseumFloor+kLoomFloorLift,12.2f,.004f,.5f,F::DarkRoof);
    }
    for(int ring=0;ring<3;++ring) {
        const float size=7.2f-static_cast<float>(ring)*1.8f;
        f.add(ring%2?"museum hall floor inlay":"museum hall floor band",0,-2.f,
            kLoomMuseumFloor+kLoomFloorLift+static_cast<float>(ring)*.003f,
            size,.004f,size,ring%2?F::WarmWall:F::DarkRoof);
    }

    // Balcony walks, parapets and the grand stair.
    for(float x:{-6.f,6.f})
        f.add("museum upper interior floor front balcony",x,-3,6.55f,2,.25f,26,F::Concrete,true);
    const auto parapet=[&](float x,float z,float w,float d) {
        f.add("museum balcony solid parapet",x,z,6.8f,w,.85f,d,F::WarmWall,true);
        f.add("museum balcony plaster face",x,z,6.88f,w+.1f,.64f,d+.1f,F::White);
        f.add("museum balcony bronze cap",x,z,7.65f,w+.18f,.12f,d+.18f,F::Steel);
        f.add("museum balcony parapet fascia",x,z,6.31f,w+.14f,.24f,d+.14f,F::WarmWall);
    };
    parapet(-5,-3,.22f,26);parapet(5,-3,.22f,26);parapet(0,-16,10,.22f);
    parapet(-3,-34,.22f,20);parapet(3,-34,.22f,20);
    // Solid stepped cheek walls prevent falling off the grand stair. Caps rise
    // with the treads and leave both landings unobstructed.
    for(int i=0;i<40;++i)for(float x:{-2.7f,2.7f}) {
        const float z=-24.25f-static_cast<float>(i)*.5f,top=.8f+static_cast<float>(i+1)*.15f;
        f.add("museum grand stair cheek",x,z,.8f,.28f,top,.5f,F::WarmWall,true);
        f.add("museum grand stair handrail",x,z,top+.8f,.35f,.1f,.51f,F::Steel);
        // A brass nosing every fourth riser. Forty identical treads in one
        // material give the eye nothing to judge the climb by.
        if(i%4==0)
            f.add("museum stair riser nosing",0,z+.24f,.8f+static_cast<float>(i)*.15f,
                5.f,.02f,.05f,F::Steel);
    }
    for(float x:{-2.7f,2.7f}) {
        for(int end=0;end<2;++end) {
            const float z=end?-44.75f:-23.75f;
            const float base=end?.8f+40*.15f:.8f;
            f.add("museum stair newel",x,z,base,.62f,1.1f,.62f,F::WarmWall,true);
            f.add("museum stair newel lamp",x,z,base+1.1f,.34f,.42f,.34f,F::Steel);
            f.add("museum gallery light lens",x,z,base+1.5f,.26f,.05f,.26f,F::White);
        }
    }
    loom_entrance_hall(f);

    // Portal plaques, on the hall side beside each doorway.
    for(std::size_t i=0;i<kLoomGalleryCount;++i) {
        const auto room=loom_room(i);
        const float x=loom_hang(room.side*kLoomHallFace,-room.side,.04f);
        f.framed_graphic(loom_plaque_name(i),x,room.portal_z+4.5f,room.floor_m+1.5f,1.34f,.67f,
            room.side>0?-90.f:90.f,.09f);
    }

    // Gallery lighting: two rows of three pendants, set 5.4 m either side of
    // the centre line, plus one uplight in the cove.
    //
    // The rows are off-centre on purpose. The light grid's cone has a hard
    // outer edge - no inner falloff - so a fitting on the centre line of a
    // 19 m room cuts a visible straight line across the floor where its cone
    // ends, and leaves the walls unlit. Two rows 4.2 m off each wall put every
    // surface inside a cone. Six narrow cones also cost the grid less than
    // three wide ones: its span radius grows as range/outer.
    for(std::size_t i=0;i<kLoomGalleryCount;++i) {
        const auto room=loom_room(i);
        for(float dx:{-5.4f,5.4f})for(float dz:{-11.f,0.f,11.f})
            f.pendant(room.cx+dx,room.cz+dz,room.ceiling_m,2.8f);
        f.add("museum cove light lens",room.cx,room.cz,room.ceiling_m-.34f,2.6f,.05f,.5f,F::White);
    }
    // Hall fixtures, under the balcony soffits where the atrium does not reach.
    for(float z:{-12.f,-20.f,-50.f,-56.f}) f.pendant(0,z,kLoomBalconyLevel,3.6f);
    for(float z:{-8.f,-52.f}) f.add("museum cove light lens",0,z,kLoomUpperSoffit-.34f,3.f,.05f,.6f,F::White);

    // The four ground galleries, then the four upstairs.
    loom_gallery_art(f,loom_room(0));
    loom_gallery_antiquities(f,loom_room(1));
    loom_gallery_natural(f,loom_room(2));
    loom_gallery_pinatty(f,loom_room(3));
    loom_gallery_science(f,loom_room(4));
    loom_gallery_space(f,loom_room(5));
    loom_gallery_transport(f,loom_room(6));
    loom_gallery_design(f,loom_room(7));
}
} // namespace apricot::city
