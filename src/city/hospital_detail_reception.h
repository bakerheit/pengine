#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Back-office registration details occupy the south side of the existing
// public desk. The desk, its terminals, and its public sign live in the
// shared lobby module, so this module only adds records, storage, and office
// tools behind it.
inline std::vector<StartPart> bake_hospital_detail_reception() {
    std::vector<StartPart> out;
    out.reserve(64);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    // A low file credenza gives the clerk a practical surface behind the
    // public counter. Its body is the collision base for the drawer fronts
    // and small office items above it. The ends leave broad paths around the
    // separate back-office partition.
    add("hospital detail reception records credenza tex laminate", -9.50f,
        14.92f, 0.315f, 5.80f, 0.90f, 0.82f, StartFinish::White, true);
    add("hospital detail reception records credenza top tex laminate", -9.50f,
        14.92f, 1.17f, 5.90f, 0.08f, 0.88f, StartFinish::White);

    // Four broad bays each have two shallow drawers and a small pull. All
    // fronts sit on the north face of the credenza, toward the reception desk.
    for (int bay = 0; bay < 4; ++bay) {
        const float x = -11.75f + 1.50f * static_cast<float>(bay);
        for (int row = 0; row < 2; ++row) {
            const float bottom = row == 0 ? 0.40f : 0.77f;
            add("hospital detail reception credenza drawer front tex laminate",
                x, 14.515f, bottom, 1.30f, 0.30f, 0.035f,
                StartFinish::White);
            add("hospital detail reception credenza drawer pull tex steel", x,
                14.485f, bottom + 0.12f, 0.20f, 0.035f, 0.045f,
                StartFinish::White);
        }
    }

    // A short back-office partition sits behind the file cabinets. It makes
    // a real backing for the sign while preserving 1.7 m clear side routes
    // inside the assigned x[-14,-5] bay.
    add("hospital detail reception registration backing partition tex wallpaint",
        -9.50f, 17.88f, 0.315f, 5.60f, 2.85f, 0.16f, StartFinish::White,
        true);

    // Lockable records cabinets stand in front of the new partition. Their
    // solid cabinet boxes supply collision; doors, label tabs, and handles
    // are shallow visual details on their north faces.
    for (int cabinet = 0; cabinet < 3; ++cabinet) {
        const float x = -11.60f + 2.10f * static_cast<float>(cabinet);
        add("hospital detail reception records cabinet tex laminate", x,
            17.58f, 0.315f, 1.30f, 2.00f, 0.56f, StartFinish::White, true);
        for (int door = 0; door < 2; ++door) {
            const float door_x = x + (door == 0 ? -0.32f : 0.32f);
            add("hospital detail reception cabinet door tex laminate", door_x,
                17.292f, 0.39f, 0.59f, 1.82f, 0.035f,
                StartFinish::White);
            add("hospital detail reception cabinet handle tex steel", door_x,
                17.262f, 1.22f, 0.045f, 0.20f, 0.045f,
                StartFinish::White);
        }
        add("hospital detail reception cabinet label tab", x, 17.268f, 2.14f,
            0.34f, 0.10f, 0.025f, StartFinish::TealDoor);
    }

    // One 4:1 image sign is mounted on the partition's north face, above the
    // storage cabinets. Its top stays below the 3.43 m ceiling fixture limit.
    add("hospital detail reception registration sign tex reception-sign",
        -9.50f, 17.785f, 2.40f, 2.80f, 0.70f, 0.035f, StartFinish::White);

    // A period-appropriate desk telephone rests on the credenza. The receiver
    // and dial are separate nonsolid pieces and touch the phone body.
    add("hospital detail reception telephone base tex laminate", -7.00f,
        14.90f, 1.25f, 0.27f, 0.075f, 0.22f, StartFinish::White);
    add("hospital detail reception telephone dial plate", -7.00f, 14.95f,
        1.322f, 0.13f, 0.008f, 0.12f, StartFinish::Steel);
    add("hospital detail reception telephone handset", -7.00f, 14.835f,
        1.325f, 0.22f, 0.035f, 0.045f, StartFinish::Steel);
    add("hospital detail reception telephone cord", -6.85f, 14.90f, 1.25f,
        0.025f, 0.018f, 0.17f, StartFinish::Steel);

    // In-trays, paperwork, a ledger, and upright binders make the storage
    // surface read as active registration work. Every piece rests on the
    // credenza top (top = 1.25 m); none has collision.
    add("hospital detail reception incoming paper tray tex steel", -8.50f,
        14.91f, 1.25f, 0.40f, 0.055f, 0.31f, StartFinish::White);
    add("hospital detail reception outgoing paper tray tex steel", -8.50f,
        14.91f, 1.305f, 0.40f, 0.055f, 0.31f, StartFinish::White);
    add("hospital detail reception appointment paper stack", -9.95f, 14.91f,
        1.25f, 0.38f, 0.045f, 0.30f, StartFinish::White);
    add("hospital detail reception appointment paper stack", -9.95f, 14.91f,
        1.295f, 0.36f, 0.04f, 0.28f, StartFinish::White);
    add("hospital detail reception appointment ledger", -10.95f, 14.91f,
        1.25f, 0.31f, 0.045f, 0.38f, StartFinish::WarmWall);
    add("hospital detail reception ledger paper edge", -10.95f, 14.91f,
        1.292f, 0.29f, 0.022f, 0.36f, StartFinish::White);

    for (int binder = 0; binder < 3; ++binder) {
        const float x = -12.10f + 0.30f * static_cast<float>(binder);
        add("hospital detail reception records binder tex laminate", x, 14.91f,
            1.25f, 0.24f, 0.30f, 0.075f, StartFinish::White);
        add("hospital detail reception binder spine label", x, 14.867f, 1.33f,
            0.14f, 0.11f, 0.012f, StartFinish::TealDoor);
    }

    // Small stamp set beside the paperwork; the handle sits directly on its
    // compact base so it reads as a single object rather than a floating prop.
    add("hospital detail reception date stamp base", -9.05f, 14.91f, 1.25f,
        0.10f, 0.045f, 0.10f, StartFinish::Steel);
    add("hospital detail reception date stamp handle", -9.05f, 14.91f, 1.295f,
        0.045f, 0.09f, 0.045f, StartFinish::TealDoor);

    return out;
}

}  // namespace apricot::city
