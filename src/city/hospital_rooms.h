#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// One ground-floor plan owns the partitions, openings and fixture anchors.
// N/S door coordinates are X; W/E coordinates are Z, all site-local metres.
struct HospitalRoomDoor { float at = 0.0f; float width = 0.0f; };
struct HospitalRoom {
    const char* name;
    float x0, x1, z0, z1;
    HospitalRoomDoor north{}, south{}, west{}, east{};
};
inline constexpr HospitalRoom kHospitalRooms[] = {
    {"Registration", -14.78f,-4.8f,5.9f,18.2f, {},{},{},{12.0f,1.8f}},
    {"Records office", -14.78f,-4.8f,18.2f,30.0f, {},{},{},{24.0f,1.8f}},
    {"Public waiting", 4.8f,27.0f,9.0f,27.5f, {21.0f,2.4f},{22.0f,2.4f},{17.0f,2.4f},{19.0f,2.4f}},
    {"Pharmacy dispensary", 27.0f,44.78f,4.0f,11.2f, {},{},{7.0f,1.8f},{}},
    {"Pharmacy waiting", 27.0f,44.78f,11.2f,27.5f, {},{33.0f,2.4f},{19.0f,2.4f},{}},
    {"Diagnostic registration", 66.0f,90.0f,-7.78f,10.0f, {},{86.0f,2.4f},{1.0f,2.4f},{}},
    {"Imaging room", 70.0f,80.0f,16.0f,27.0f, {76.0f,2.0f},{},{},{}},
    {"Examination room", 80.0f,90.0f,16.0f,27.0f, {86.0f,2.0f},{},{},{}},
    {"Diagnostic waiting", 94.0f,116.0f,14.0f,28.0f, {109.0f,2.4f},{109.0f,2.4f},{},{}},
    {"Consultation office", 118.0f,130.0f,14.0f,30.0f, {124.0f,1.8f},{},{},{}},
    {"Staff workroom", 130.0f,142.0f,14.0f,30.0f, {136.0f,1.8f},{},{},{}},
    {"Clinic records", 94.0f,118.0f,-7.78f,10.0f, {},{106.0f,1.8f},{},{}},
    {"Clinic office", 118.0f,142.0f,-7.78f,10.0f, {},{130.0f,1.8f},{},{}},
    {"ED reception", 146.0f,174.0f,-7.78f,10.0f, {},{166.0f,2.4f},{},{}},
    {"ED waiting", 184.0f,203.78f,-7.78f,30.0f, {},{},{4.0f,2.4f},{}},
    {"Triage", 150.0f,174.0f,14.0f,30.0f, {168.0f,2.0f},{},{},{}},
    {"North treatment support", 150.0f,174.0f,34.0f,62.0f, {},{},{},{48.0f,2.0f}},
    {"Treatment 1", 154.0f,164.0f,64.0f,74.0f, {},{161.0f,2.0f},{},{}},
    {"Treatment 2", 164.0f,174.0f,64.0f,74.0f, {},{171.0f,2.0f},{},{}},
    {"Treatment 3", 154.0f,164.0f,84.0f,94.0f, {161.0f,2.0f},{},{},{}},
    {"Clean utility", 164.0f,174.0f,84.0f,94.0f, {169.0f,1.8f},{},{},{}},
    {"Trauma preparation", 182.0f,195.0f,64.0f,74.0f, {},{187.0f,2.4f},{},{}},
    {"Trauma recovery", 182.0f,195.0f,84.0f,100.0f, {187.0f,2.4f},{},{},{}},
    {"Procedure support", 150.0f,174.0f,98.0f,114.0f, {},{},{},{106.0f,2.0f}},
    {"Sterile stores", 150.0f,174.0f,118.0f,137.78f, {},{},{},{128.0f,2.0f}},
    {"Receiving", 182.0f,195.0f,104.0f,137.78f, {},{},{120.0f,2.4f},{118.0f,2.4f}},
    {"West consultation", -14.78f,10.0f,34.0f,54.0f, {},{},{},{44.0f,2.0f}},
    {"Outpatient clinic", 14.0f,37.0f,34.0f,54.0f, {},{},{44.0f,2.0f},{47.0f,2.4f}},
    {"Rehabilitation", -14.78f,10.0f,62.0f,92.0f, {},{},{},{76.0f,2.4f}},
    {"Rehab consultation", 14.0f,37.0f,62.0f,78.0f, {},{},{70.0f,2.0f},{70.0f,2.0f}},
    {"Family room", 14.0f,37.0f,78.0f,92.0f, {},{},{86.0f,2.0f},{89.0f,2.0f}},
    {"Ward linen", -14.78f,0.0f,98.0f,104.0f, {-5.0f,1.8f},{},{},{}},
    {"Ward supply", 0.0f,16.0f,98.0f,104.0f, {8.0f,1.8f},{},{},{}},
    {"Ward office", 16.0f,37.0f,98.0f,104.0f, {30.0f,1.8f},{},{},{}},
    {"Patient 101", -10.0f,-3.5f,104.0f,112.0f, {},{-5.05f,1.8f},{},{}},
    {"Patient 102", -3.5f,3.0f,104.0f,112.0f, {},{1.45f,1.8f},{},{}},
    {"Patient 103", 3.0f,9.5f,104.0f,112.0f, {},{7.95f,1.8f},{},{}},
    {"Patient 104", 9.5f,16.0f,104.0f,112.0f, {},{14.45f,1.8f},{},{}},
    {"Nurses station", 16.0f,27.0f,104.0f,112.0f, {},{23.0f,2.0f},{},{}},
    {"Ward utility", 27.0f,37.0f,104.0f,112.0f, {},{32.0f,1.8f},{},{}},
    {"Ward staff room", -14.78f,0.0f,116.0f,137.78f, {-6.0f,1.8f},{},{},{}},
    {"Ward day room", 0.0f,16.0f,116.0f,137.78f, {8.0f,2.0f},{},{},{}},
    {"Ward stores", 16.0f,37.0f,116.0f,137.78f, {30.0f,2.0f},{},{},{}},
    {"Clinical workroom", 48.0f,69.0f,60.2f,66.0f, {},{60.0f,1.8f},{},{}},
    {"Clinical records", 75.0f,94.0f,60.2f,66.0f, {},{84.0f,1.8f},{},{}},
    {"Clinical supply", 94.0f,115.0f,60.2f,66.0f, {},{104.0f,1.8f},{},{}},
    {"Clinical staff", 121.0f,141.0f,60.2f,66.0f, {},{131.0f,1.8f},{},{}},
    {"Clinical prep", 48.0f,69.0f,72.0f,77.8f, {60.0f,1.8f},{},{},{}},
    {"Clinical clean store", 75.0f,115.0f,72.0f,77.8f, {94.0f,1.8f},{},{},{}},
    {"Clinical office", 121.0f,141.0f,72.0f,77.8f, {131.0f,1.8f},{},{},{}},
    {"South consultation", 51.0f,69.0f,100.2f,114.0f, {},{60.0f,2.0f},{},{}},
    {"South clinic", 75.0f,95.0f,100.2f,114.0f, {},{85.0f,2.0f},{},{}},
    {"South preparation", 95.0f,115.0f,100.2f,114.0f, {},{105.0f,2.0f},{},{}},
    {"South stores", 121.0f,141.0f,100.2f,114.0f, {},{131.0f,2.0f},{},{}},
    {"Staff changing", 51.0f,75.0f,120.0f,137.78f, {63.0f,1.8f},{},{},{}},
    {"Staff lounge", 75.0f,99.0f,120.0f,137.78f, {87.0f,2.0f},{},{},{}},
    {"Support office", 99.0f,121.0f,120.0f,137.78f, {110.0f,1.8f},{},{},{}},
    {"Support storage", 121.0f,141.0f,120.0f,137.78f, {131.0f,2.0f},{},{},{}},
};

inline std::vector<StartPart> bake_hospital_rooms() {
    struct Portal { float at, width, sill, height; };
    struct Run { bool horizontal; float line, lo, hi; std::vector<Portal> portals; };
    std::vector<Run> runs;
    const auto queue = [&](bool horizontal, float line, float lo, float hi,
                           HospitalRoomDoor door) {
        Run run{horizontal,line,lo,hi,{}};
        if (door.width > 0.0f) run.portals.push_back({door.at,door.width,0.0f,2.40f});
        runs.push_back(run);
    };
    for (const auto& room : kHospitalRooms) {
        queue(true,room.z0,room.x0,room.x1,room.north);
        queue(true,room.z1,room.x0,room.x1,room.south);
        queue(false,room.x0,room.z0,room.z1,room.west);
        queue(false,room.x1,room.z0,room.z1,room.east);
        if (std::strcmp(room.name,"Registration")==0)
            runs[runs.size()-4].portals.push_back({-9.8f,8.7f,0.585f,1.715f});
        if (std::strcmp(room.name,"Pharmacy dispensary")==0)
            runs[runs.size()-3].portals.push_back({36.6f,9.0f,0.72f,1.58f});
    }
    // Merge collinear shared walls before baking. Adjacent rooms never emit
    // coplanar duplicates or fill each other's doors with a second wall.
    std::sort(runs.begin(),runs.end(),[](const Run& a,const Run& b) {
        if(a.horizontal!=b.horizontal)return a.horizontal<b.horizontal;
        if(a.line!=b.line)return a.line<b.line;
        return a.lo<b.lo;
    });
    std::vector<Run> merged;
    for (const auto& run : runs) {
        if (!merged.empty() && merged.back().horizontal==run.horizontal &&
            merged.back().line==run.line && run.lo<=merged.back().hi+0.001f) {
            auto& previous=merged.back();
            previous.hi=std::max(previous.hi,run.hi);
            previous.portals.insert(previous.portals.end(),run.portals.begin(),run.portals.end());
        } else merged.push_back(run);
    }
    std::vector<StartPart> out;
    for (auto& run : merged) {
        std::sort(run.portals.begin(),run.portals.end(),[](const Portal& a,const Portal& b){return a.at<b.at;});
        run.portals.erase(std::unique(run.portals.begin(),run.portals.end(),[](const Portal& a,const Portal& b){
            return a.at==b.at && a.width==b.width && a.sill==b.sill;
        }),run.portals.end());
        std::vector<BuildingOpening> openings;
        for (const auto& portal : run.portals)
            openings.push_back({"hospital room framed opening",OpeningKind::Door,
                portal.at-run.lo,portal.width,portal.sill,portal.height,
                StartFinish::TealDoor,0,0,StartFinish::TealDoor,false});
        const Vec2 a=run.horizontal?Vec2{run.lo,run.line}:Vec2{run.line,run.lo};
        const Vec2 b=run.horizontal?Vec2{run.hi,run.line}:Vec2{run.line,run.hi};
        const BuildingWall wall{"hospital room partition tex wallpaint",a,b,
            0.315f,3.115f,0.22f,StartFinish::White,openings.data(),openings.size()};
        const BuildingPlan plan{"hospital ground floor rooms",&wall,1};
        const auto pieces=bake_building(plan);
        out.insert(out.end(),pieces.begin(),pieces.end());
        for (const auto& piece : pieces) {
            if (std::strcmp(piece.name,"hospital room partition tex wallpaint")!=0 ||
                piece.bottom_m>0.32f) continue;
            auto trim=piece;
            trim.name="hospital room skirting";
            trim.height_m=0.12f; trim.depth_m=0.25f;
            trim.finish=StartFinish::TealDoor; trim.solid=false;
            out.push_back(trim);
            if(piece.height_m<1.15f)continue;
            trim.name="hospital room protective rail tex steel";
            trim.bottom_m=1.05f; trim.height_m=0.10f; trim.depth_m=0.27f;
            trim.finish=StartFinish::White;
            out.push_back(trim);
        }
    }
    // Open patient-room door leaves park against the inside jamb. The actual
    // 1.8 m portal remains clear, and the leaves have matching collision.
    int patient_number = 101;
    for (const auto& room : kHospitalRooms) {
        if(std::strncmp(room.name,"Patient ",8)!=0)continue;
        const float hinge=room.south.at-room.south.width*0.5f;
        out.push_back({"hospital patient room open door",{hinge+0.04f,room.z1-0.82f},
            0.335f,0.07f,2.30f,1.55f,StartFinish::TealDoor,true});
        out.push_back({"hospital patient door pull tex steel",{hinge+0.10f,room.z1-1.40f},
            1.25f,0.05f,0.20f,0.12f,StartFinish::White,false});
        // Small raised room numbers on the corridor side of the latch wall.
        const float label_x=room.south.at-1.55f;
        out.push_back({"hospital patient room number plate",{label_x,room.z1+0.135f},
            1.60f,0.64f,0.28f,0.035f,StartFinish::White,false});
        constexpr unsigned char digits[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
        constexpr float segment_x[]={0.0f,0.045f,0.045f,0.0f,-0.045f,-0.045f,0.0f};
        constexpr float segment_y[]={0.08f,0.04f,-0.04f,-0.08f,-0.04f,0.04f,0.0f};
        const int numbers[]={patient_number/100,(patient_number/10)%10,patient_number%10};
        for(int digit=0;digit<3;++digit) {
            for(int segment=0;segment<7;++segment) {
                if(!(digits[numbers[digit]] & (1u<<segment)))continue;
                const bool horizontal=segment==0 || segment==3 || segment==6;
                out.push_back({"hospital patient room number",
                    {label_x+(digit-1)*0.18f+segment_x[segment],room.z1+0.162f},
                    1.74f+segment_y[segment]-(horizontal?0.0075f:0.035f),
                    horizontal?0.075f:0.015f,horizontal?0.015f:0.07f,0.015f,
                    StartFinish::TealDoor,false});
            }
        }
        ++patient_number;
    }
    return out;
}

} // namespace apricot::city
