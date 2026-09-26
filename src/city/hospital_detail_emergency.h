#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Three furnished emergency treatment bays sit on either side of the clear
// trauma spine. Each monitor, outlet and curtain track is attached to a
// purpose-built freestanding headwall rather than an assumed room wall.
inline std::vector<StartPart> bake_hospital_detail_emergency() {
    std::vector<StartPart> out;
    out.reserve(109);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    struct Station {
        float x;
        float z;
    };
    // The first two bays serve the north resuscitation area. The third is a
    // matching overflow bay south of the trauma corridor.
    constexpr Station stations[] = {
        {161.0f, 59.0f},
        {184.0f, 59.0f},
        {172.0f, 98.5f},
    };

    for (const Station& station : stations) {
        const float x = station.x;
        const float z = station.z;
        const float headwall_z = z + 1.19f;

        // Floor-supported service backing: the outlet plates and monitor
        // attach to this physical panel, which stands behind the stretcher.
        add("hospital detail emergency freestanding headwall backing tex wallpaint",
            x, headwall_z, 0.315f, 3.50f, 2.38f, 0.16f,
            StartFinish::White, true);
        add("hospital detail emergency headwall service rail tex steel", x,
            headwall_z - 0.095f, 1.43f, 2.30f, 0.10f, 0.055f,
            StartFinish::White);
        add("hospital detail emergency duplex power outlet plate", x - 0.56f,
            headwall_z - 0.0975f, 1.47f, 0.27f, 0.17f, 0.035f,
            StartFinish::Steel);
        add("hospital detail emergency oxygen gas outlet", x + 0.10f,
            headwall_z - 0.10f, 1.47f, 0.16f, 0.16f, 0.04f,
            StartFinish::Yellow);
        add("hospital detail emergency suction gas outlet", x + 0.38f,
            headwall_z - 0.10f, 1.47f, 0.16f, 0.16f, 0.04f,
            StartFinish::TealDoor);

        // One-metre-wide, 2.2 m overall stretcher. The chassis carries
        // collision; the padded mattress, pillow and guards remain visual.
        add("hospital detail emergency stretcher chassis tex steel", x, z,
            0.56f, 0.92f, 0.10f, 2.12f, StartFinish::White, true);
        add("hospital detail emergency stretcher underframe tex steel", x,
            z, 0.34f, 0.70f, 0.22f, 1.70f, StartFinish::White);
        for (float dx : {-0.38f, 0.38f}) {
            for (float dz : {-0.82f, 0.82f}) {
                add("hospital detail emergency stretcher caster", x + dx,
                    z + dz, 0.315f, 0.13f, 0.10f, 0.16f,
                    StartFinish::Steel);
            }
        }
        add("hospital detail emergency stretcher mattress tex upholstery", x,
            z, 0.66f, 0.88f, 0.14f, 1.96f, StartFinish::White);
        add("hospital detail emergency stretcher pillow tex upholstery", x,
            z + 0.70f, 0.80f, 0.53f, 0.10f, 0.33f,
            StartFinish::White);
        add("hospital detail emergency stretcher head rail", x, z + 1.045f,
            0.61f, 0.94f, 0.25f, 0.06f, StartFinish::Steel);
        add("hospital detail emergency stretcher foot rail", x, z - 1.045f,
            0.61f, 0.94f, 0.25f, 0.06f, StartFinish::Steel);
        for (float dx : {-0.48f, 0.48f}) {
            add("hospital detail emergency stretcher side rail", x + dx,
                z + 0.37f, 0.80f, 0.055f, 0.22f, 0.72f,
                StartFinish::Steel);
        }

        // The display's fitted texture is a 4:3 north-facing screen. Its
        // bracket fixes it to the north face of the headwall, toward the bed.
        add("hospital detail emergency monitor swing arm", x + 0.83f,
            headwall_z - 0.16f, 1.62f, 0.62f, 0.06f, 0.20f,
            StartFinish::Steel);
        add("hospital detail emergency bedside vitals monitor tex vitals-screen",
            x + 1.08f, headwall_z - 0.245f, 1.34f, 0.48f, 0.36f, 0.045f,
            StartFinish::White);

        // Compact IV pole stays at the west bedside, with the hook and fluid
        // bag below the 3.43 m ceiling.
        const float iv_x = x - 1.16f;
        const float iv_z = z - 0.18f;
        add("hospital detail emergency IV stand base", iv_x, iv_z, 0.315f,
            0.38f, 0.055f, 0.38f, StartFinish::Steel);
        add("hospital detail emergency IV stand pole", iv_x, iv_z, 0.37f,
            0.045f, 1.91f, 0.045f, StartFinish::Steel);
        add("hospital detail emergency IV stand hook bar", iv_x, iv_z,
            2.27f, 0.34f, 0.045f, 0.045f, StartFinish::Steel);
        add("hospital detail emergency IV fluid bag", iv_x + 0.11f, iv_z,
            1.95f, 0.17f, 0.30f, 0.075f, StartFinish::White);

        // A small rolling instrument cart has a solid cabinet base, four
        // casters and a stainless work tray.
        const float cart_x = x + 1.17f;
        const float cart_z = z - 0.08f;
        add("hospital detail emergency instrument cart cabinet tex laminate",
            cart_x, cart_z, 0.405f, 0.56f, 0.49f, 0.45f,
            StartFinish::White, true);
        for (float dx : {-0.20f, 0.20f}) {
            for (float dz : {-0.15f, 0.15f}) {
                add("hospital detail emergency instrument cart caster",
                    cart_x + dx, cart_z + dz, 0.315f, 0.10f, 0.09f, 0.11f,
                    StartFinish::Steel);
            }
        }
        add("hospital detail emergency instrument cart top tex steel", cart_x,
            cart_z, 0.895f, 0.61f, 0.045f, 0.50f, StartFinish::White);
        add("hospital detail emergency instrument tray", cart_x, cart_z,
            0.94f, 0.38f, 0.035f, 0.28f, StartFinish::Steel);

        // Two supported ceiling-track curtains screen the long sides while
        // leaving the foot and head approaches open for a rolling stretcher.
        for (float dx : {-1.80f, 1.80f}) {
            add("hospital detail emergency curtain track tex steel", x + dx,
                z - 0.045f, 2.61f, 0.045f, 0.04f, 2.31f,
                StartFinish::White);
            add("hospital detail emergency curtain track ceiling hanger",
                x + dx, z - 1.18f, 2.66f, 0.035f, 0.77f, 0.035f,
                StartFinish::Steel);
            add("hospital detail emergency privacy curtain tex curtain",
                x + dx, z - 0.12f, 0.38f, 0.055f, 2.23f, 1.78f,
                StartFinish::White);
        }
    }

    add("hospital detail emergency station sign tex emergency-sign",
        161.0f, 60.085f, 2.09f, 2.40f, 0.60f, 0.025f,
        StartFinish::White);
    return out;
}

}  // namespace apricot::city
