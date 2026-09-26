#pragma once

#include <cstring>
#include <vector>

#include "city/hospital_rooms.h"

namespace apricot::city {

// These fixtures derive their positions from enclosed rooms. Moving a room
// also moves its desks, storage and lights; nothing is placed in a corridor.
inline std::vector<StartPart> bake_hospital_room_fixtures() {
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish, solid};
        part.yaw_deg = yaw;
        out.push_back(part);
    };
    const auto chair = [&](float x, float z) {
        add("hospital room chair seat tex upholstery", x,z,0.70f,
            0.56f,0.12f,0.56f,StartFinish::TealDoor,true);
        add("hospital room chair back tex upholstery", x,z+0.25f,0.82f,
            0.56f,0.60f,0.09f,StartFinish::TealDoor,true);
        for (const float dx : {-0.22f,0.22f})
            for (const float dz : {-0.22f,0.22f})
                add("hospital room chair leg tex steel",x+dx,z+dz,0.315f,
                    0.055f,0.385f,0.055f,StartFinish::White);
    };
    const auto desk = [&](float x, float z) {
        add("hospital room desk top tex laminate",x,z,1.05f,
            2.4f,0.09f,0.9f,StartFinish::White,true);
        for (const float dx : {-0.84f,0.84f}) {
            add("hospital room desk drawer pedestal tex laminate",x+dx,z,0.315f,
                0.52f,0.735f,0.76f,StartFinish::White,true);
            for (const float y : {0.55f,0.85f})
                add("hospital room desk drawer pull tex steel",x+dx,z+0.40f,y,
                    0.20f,0.04f,0.035f,StartFinish::White);
        }
        add("hospital room records terminal stand tex steel",x,z-0.05f,1.14f,
            0.24f,0.18f,0.20f,StartFinish::White);
        add("hospital room records terminal tex records-screen",x,z-0.05f,1.32f,
            0.56f,0.42f,0.14f,StartFinish::White,false,180.0f);
        add("hospital room records keyboard tex steel",x,z+0.26f,1.14f,
            0.48f,0.045f,0.20f,StartFinish::White);
        chair(x,z+1.25f);
    };
    const auto cabinet = [&](float x, float z) {
        add("hospital room fitted storage cabinet tex laminate",x,z,0.315f,
            1.8f,2.1f,0.64f,StartFinish::White,true);
        for (const float dx : {-0.45f,0.45f}) {
            add("hospital room storage door tex laminate",x+dx,z+0.338f,0.42f,
                0.84f,1.92f,0.025f,StartFinish::White);
            add("hospital room storage handle tex steel",x+dx*0.3f,z+0.365f,1.2f,
                0.04f,0.23f,0.045f,StartFinish::White);
        }
        add("hospital room storage label",x,z+0.359f,2.15f,
            0.42f,0.12f,0.025f,StartFinish::TealDoor);
    };
    const auto shelf = [&](float x, float z) {
        for (const float dx : {-0.96f,0.96f}) {
            add("hospital room supply shelf upright tex steel",x+dx,z,0.315f,
                0.07f,2.10f,0.62f,StartFinish::White,true);
        }
        for (const float bottom : {0.45f,1.05f,1.65f,2.25f}) {
            add("hospital room supply shelf tex steel",x,z,bottom,
                2.0f,0.05f,0.62f,StartFinish::White,true);
            if (bottom>2.0f) continue;
            for (const float dx : {-0.52f,0.18f}) {
                add("hospital room boxed supplies tex laminate",x+dx,z,bottom+0.05f,
                    0.54f,0.30f,0.44f,StartFinish::White);
                add("hospital room supply box label",x+dx,z+0.23f,bottom+0.14f,
                    0.25f,0.10f,0.025f,StartFinish::TealDoor);
            }
        }
    };
    const auto couch = [&](float x, float z, float wall_z) {
        add("hospital room examination couch frame tex steel",x,z,0.82f,
            1.00f,0.12f,2.15f,StartFinish::White,true);
        for (const float dz : {-0.75f,0.75f})
            add("hospital room examination couch support tex steel",x,z+dz,0.315f,
                0.8f,0.505f,0.14f,StartFinish::White,true);
        add("hospital room examination couch mattress tex upholstery",x,z,0.94f,
            0.95f,0.17f,2.10f,StartFinish::White);
        add("hospital room examination couch pillow tex upholstery",x,z-0.76f,1.11f,
            0.64f,0.11f,0.40f,StartFinish::White);
        add("hospital room examination wall service rail tex steel",x,wall_z+0.145f,1.5f,
            2.4f,0.12f,0.06f,StartFinish::White);
        add("hospital room examination wall outlet",x+0.8f,wall_z+0.185f,1.42f,
            0.15f,0.22f,0.035f,StartFinish::TealDoor);
        add("hospital room examination step tex steel",x+0.95f,z+0.65f,0.315f,
            0.60f,0.24f,0.48f,StartFinish::White,true);
    };

    enum class Use { Office, Store, Clinic, Lounge, Reception, Rehab, Changing };
    struct Fit { const char* room; Use use; };
    constexpr Fit fits[] = {
        {"Records office",Use::Office}, {"Consultation office",Use::Office},
        {"Staff workroom",Use::Office}, {"Clinic records",Use::Store},
        {"Clinic office",Use::Office}, {"ED reception",Use::Reception},
        {"ED waiting",Use::Lounge}, {"Triage",Use::Clinic},
        {"North treatment support",Use::Store}, {"Clean utility",Use::Store},
        {"Trauma preparation",Use::Clinic}, {"Trauma recovery",Use::Clinic},
        {"Procedure support",Use::Clinic}, {"Sterile stores",Use::Store},
        {"Receiving",Use::Store}, {"West consultation",Use::Clinic},
        {"Outpatient clinic",Use::Clinic}, {"Rehabilitation",Use::Rehab},
        {"Rehab consultation",Use::Clinic}, {"Family room",Use::Lounge},
        {"Ward linen",Use::Store}, {"Ward supply",Use::Store},
        {"Ward office",Use::Office}, {"Nurses station",Use::Reception},
        {"Ward utility",Use::Store}, {"Ward staff room",Use::Lounge},
        {"Ward day room",Use::Lounge}, {"Ward stores",Use::Store},
        {"Clinical workroom",Use::Office}, {"Clinical records",Use::Office},
        {"Clinical supply",Use::Store}, {"Clinical staff",Use::Lounge},
        {"Clinical prep",Use::Clinic}, {"Clinical clean store",Use::Store},
        {"Clinical office",Use::Office}, {"South consultation",Use::Clinic},
        {"South clinic",Use::Clinic}, {"South preparation",Use::Clinic},
        {"South stores",Use::Store}, {"Staff changing",Use::Changing},
        {"Staff lounge",Use::Lounge}, {"Support office",Use::Office},
        {"Support storage",Use::Store},
    };
    for (const auto& fit : fits) {
        const HospitalRoom* room = nullptr;
        for (const auto& candidate : kHospitalRooms) {
            if (std::strcmp(candidate.name,fit.room)==0) { room=&candidate; break; }
        }
        if (!room) continue;
        const float width=room->x1-room->x0;
        const float depth=room->z1-room->z0;
        const float x=room->x0+width*0.28f;
        const float z=room->z0;
        // Offset from the north door, where present. Wall storage stays to
        // either side; chairs and beds never occupy the centre entrance path.
        switch (fit.use) {
            case Use::Office:
            case Use::Reception:
                desk(x,z+1.0f);
                cabinet(room->x1-1.5f,z+0.5f);
                if (depth>10.0f) chair(x,z+4.5f);
                break;
            case Use::Store:
                shelf(x,z+0.55f);
                shelf(room->x1-1.6f,z+0.55f);
                if (depth>10.0f) {
                    shelf(x,z+4.0f);
                    cabinet(room->x1-1.6f,z+4.0f);
                }
                break;
            case Use::Clinic:
                couch(x,z+2.2f,z);
                cabinet(room->x1-1.5f,z+0.5f);
                if (width>16.0f) desk(room->x1-4.0f,z+3.0f);
                else chair(x+2.2f,z+3.0f);
                break;
            case Use::Lounge:
                for (const float dx : {-1.0f,0.0f,1.0f})
                    chair(x+dx,z+1.5f);
                add("hospital room lounge table tex laminate",x,z+3.0f,0.80f,
                    2.1f,0.10f,0.8f,StartFinish::White,true);
                for (const float dx : {-0.8f,0.8f})
                    add("hospital room lounge table leg tex steel",x+dx,z+3.0f,0.315f,
                        0.10f,0.485f,0.6f,StartFinish::White);
                cabinet(room->x1-1.5f,z+0.5f);
                break;
            case Use::Rehab:
                for (const float dx : {-0.65f,0.65f}) {
                    add("hospital rehabilitation parallel handrail tex steel",x+dx,z+5.0f,1.35f,
                        0.07f,0.07f,4.0f,StartFinish::White,true);
                    for (const float dz : {-1.7f,1.7f})
                        add("hospital rehabilitation rail support tex steel",x+dx,z+5.0f+dz,0.315f,
                            0.08f,1.035f,0.08f,StartFinish::White,true);
                }
                couch(room->x1-4.0f,z+2.2f,z);
                cabinet(x,z+0.5f);
                break;
            case Use::Changing:
                for (const float dx : {0.0f,2.0f,4.0f}) cabinet(room->x0+1.5f+dx,z+0.5f);
                add("hospital changing room bench tex laminate",room->x0+3.5f,z+2.7f,0.72f,
                    4.2f,0.10f,0.65f,StartFinish::White,true);
                for (const float dx : {-1.6f,1.6f})
                    add("hospital changing room bench pedestal tex steel",room->x0+3.5f+dx,z+2.7f,0.315f,
                        0.2f,0.405f,0.55f,StartFinish::White,true);
                break;
        }
        // Each occupied support room has a fixture over its working area.
        // Existing department/ward fixtures are authored in the surfaces pass.
        if (std::strcmp(fit.room,"Nurses station")!=0 &&
            std::strcmp(fit.room,"ED reception")!=0 &&
            std::strcmp(fit.room,"ED waiting")!=0)
            add("hospital interior ceiling light lens",x,z+2.5f,3.30f,
                1.00f,0.10f,0.32f,StartFinish::White);
    }
    return out;
}

} // namespace apricot::city
