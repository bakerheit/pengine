#pragma once

#include <vector>

#include "city/start_area.h"

namespace apricot::city {

inline constexpr float kHospitalBlockWidthM = 54.0f;
inline constexpr float kHospitalBlockDepthM = 38.0f;
inline constexpr float kHospitalBlockStepXM = 92.0f;
inline constexpr float kHospitalBlockStepZM = 62.0f;
inline constexpr int kHospitalFloorCount = 4;
inline constexpr float kHospitalFloorHeightM = 3.4f;
inline constexpr float kHospitalHeightM =
    kHospitalFloorCount * kHospitalFloorHeightM;
inline constexpr Vec2 kHospitalCampusCentre{92.0f, 62.0f};
inline constexpr float kHospitalCampusWidthM =
    3.0f * kHospitalBlockWidthM +
    2.0f * (kHospitalBlockStepXM - kHospitalBlockWidthM);
inline constexpr float kHospitalCampusDepthM =
    3.0f * kHospitalBlockDepthM +
    2.0f * (kHospitalBlockStepZM - kHospitalBlockDepthM);

constexpr Vec2 hospital_grid_point(float east, float south) {
    return {70.0f + kGridCos * east + kGridSin * south,
            -40.0f - kGridSin * east + kGridCos * south};
}

// The first three rows are one road-free hospital superblock. The east column
// stays regular city fabric; the first two cells of the last row are one
// stacked parking garage.
inline constexpr StartSite kHospitalSite{
    "Pinatty Regional Hospital Campus", hospital_grid_point(-138.0f, -279.0f),
    kGridCos, kGridSin, kHospitalCampusCentre, kHospitalCampusWidthM,
    kHospitalCampusDepthM, 12.0f, 2200.0f};

constexpr Vec2 hospital_block_centre(int column, int row) {
    return {static_cast<float>(column) * kHospitalBlockStepXM,
            static_cast<float>(row) * kHospitalBlockStepZM};
}

inline constexpr Vec2 kHospitalGarageCentre{46.0f, 3.0f * kHospitalBlockStepZM};
inline constexpr float kHospitalGarageWidthM =
    kHospitalBlockStepXM + kHospitalBlockWidthM;

constexpr bool hospital_rect_overlaps(float x, float z, float half_width,
                                      float half_depth, Vec2 centre,
                                      float width, float depth) {
    return x + half_width > centre.x - width * 0.5f &&
           x - half_width < centre.x + width * 0.5f &&
           z + half_depth > centre.z - depth * 0.5f &&
           z - half_depth < centre.z + depth * 0.5f;
}

// Authored lots that predate the hospital must not render through its
// clinical superblock or garage. All downtown sites share the same grid yaw,
// but convert through each site's basis so the replacement rule stays data
// driven instead of depending on a tower-array index.
constexpr bool hospital_campus_replaces(const StartSite& site) {
    const float site_world_x =
        site.origin.x + site.cos_yaw * site.lot_centre.x +
        site.sin_yaw * site.lot_centre.z;
    const float site_world_z =
        site.origin.z - site.sin_yaw * site.lot_centre.x +
        site.cos_yaw * site.lot_centre.z;
    const float dx = site_world_x - kHospitalSite.origin.x;
    const float dz = site_world_z - kHospitalSite.origin.z;
    const float local_x = kHospitalSite.cos_yaw * dx -
                          kHospitalSite.sin_yaw * dz;
    const float local_z = kHospitalSite.sin_yaw * dx +
                          kHospitalSite.cos_yaw * dz;
    return hospital_rect_overlaps(
               local_x, local_z, site.lot_width_m * 0.5f,
               site.lot_depth_m * 0.5f, kHospitalCampusCentre,
               kHospitalCampusWidthM, kHospitalCampusDepthM) ||
           hospital_rect_overlaps(
               local_x, local_z, site.lot_width_m * 0.5f,
               site.lot_depth_m * 0.5f, kHospitalGarageCentre,
               kHospitalGarageWidthM, kHospitalBlockDepthM);
}

inline std::vector<StartPart> bake_hospital_campus() {
    std::vector<StartPart> out;
    out.reserve(420);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    const auto block = [](int column, int row) {
        return hospital_block_centre(column, row);
    };

    // One continuous campus slab replaces the five clipped internal road
    // runs. The perimeter streets stay outside these exact superblock bounds.
    add("hospital campus superblock lot", kHospitalCampusCentre.x,
        kHospitalCampusCentre.z, 0.0f, kHospitalCampusWidthM, 0.10f,
        kHospitalCampusDepthM, StartFinish::Asphalt);

    // Three rows of four-story hospital wings. The old block rhythm remains
    // readable in the facades even though the streets between them are gone.
    for (int row = 0; row < 3; ++row) {
        const float wing_height = kHospitalHeightM;
        for (int column = 0; column < 3; ++column) {
            const Vec2 c = block(column, row);
            const StartFinish shell = (column == 1 && row == 0)
                                          ? StartFinish::WarmWall
                                          : StartFinish::Concrete;
            add("hospital wing floor", c.x, c.z, 0.10f,
                kHospitalBlockWidthM - 1.0f, 0.18f,
                kHospitalBlockDepthM - 1.0f, StartFinish::Concrete, true);
            add("hospital wing rear wall", c.x, c.z + 17.7f, 0.25f,
                53.0f, wing_height, 0.35f, shell, true);
            add("hospital wing side wall", c.x - 26.8f, c.z, 0.25f,
                0.35f, wing_height, 35.0f, shell, true);
            add("hospital wing side wall", c.x + 26.8f, c.z, 0.25f,
                0.35f, wing_height, 35.0f, shell, true);
            add("hospital wing front west", c.x - 15.0f, c.z - 17.7f,
                0.25f, 23.0f, wing_height, 0.35f, shell, true);
            add("hospital wing front east", c.x + 15.0f, c.z - 17.7f,
                0.25f, 23.0f, wing_height, 0.35f, shell, true);
            add("hospital entrance pier", c.x - 3.6f, c.z - 17.9f,
                0.25f, 0.65f, 4.8f, 0.55f, StartFinish::TealDoor, true);
            add("hospital entrance pier", c.x + 3.6f, c.z - 17.9f,
                0.25f, 0.65f, 4.8f, 0.55f, StartFinish::TealDoor, true);
            add("hospital entrance glass", c.x, c.z - 18.0f, 0.65f,
                6.2f, 4.2f, 0.06f, StartFinish::Glass);
            add("hospital entrance lintel", c.x, c.z - 17.9f,
                4.75f, 7.8f, 2.2f, 0.45f, StartFinish::WarmWall, true);
            add("hospital entrance canopy", c.x, c.z - 20.0f,
                5.0f, 10.0f, 0.28f, 3.8f, StartFinish::TealDoor, true);
            add("hospital entrance walk", c.x, c.z - 21.0f, 0.10f,
                7.0f, 0.10f, 5.5f, StartFinish::Concrete);
            if (column == 0 && row == 0) {
                add("hospital main entry mural backing", c.x - 13.0f,
                    c.z - 17.93f, 4.70f, 9.35f, 6.20f, 0.10f,
                    StartFinish::Steel, true);
                add("hospital main entry mural face", c.x - 13.0f,
                    c.z - 18.00f, 4.80f, 9.0f, 6.0f, 0.06f,
                    StartFinish::WarmWall);
            }
            add("hospital wing roof", c.x, c.z, 0.25f + wing_height,
                53.6f, 0.30f, 37.6f, StartFinish::DarkRoof, true);
            for (int floor = 0; floor < kHospitalFloorCount; ++floor) {
                const float sill = 1.05f + floor * kHospitalFloorHeightM;
                add("hospital wing front glazing west", c.x - 15.0f,
                    c.z - 17.91f, sill, 19.0f, 1.75f, 0.05f,
                    StartFinish::Glass);
                add("hospital wing front glazing east", c.x + 15.0f,
                    c.z - 17.91f, sill, 19.0f, 1.75f, 0.05f,
                    StartFinish::Glass);
                add("hospital wing rear glazing", c.x, c.z + 17.91f, sill,
                    45.0f, 1.75f, 0.05f, StartFinish::Glass);
                add("hospital wing floor band", c.x, c.z - 18.0f,
                    0.25f + (floor + 1) * kHospitalFloorHeightM - 0.18f,
                    53.5f, 0.20f, 0.22f, StartFinish::Steel, true);
            }
        }
    }

    // Enclosed four-story links cross the five former street corridors, tying
    // the nine wings into one hospital while leaving paved courts around them.
    for (int row = 0; row < 3; ++row) {
        for (int seam = 0; seam < 2; ++seam) {
            const float x = 46.0f + seam * kHospitalBlockStepXM;
            const float z = row * kHospitalBlockStepZM;
            add("hospital east west connector core", x, z, 0.25f,
                39.0f, kHospitalHeightM, 12.0f,
                StartFinish::Concrete, true);
            for (int floor = 0; floor < kHospitalFloorCount; ++floor) {
                const float sill = 1.05f + floor * kHospitalFloorHeightM;
                add("hospital east west connector glazing", x, z - 6.08f,
                    sill, 35.0f, 1.75f, 0.06f, StartFinish::Glass);
                add("hospital east west connector glazing", x, z + 6.08f,
                    sill, 35.0f, 1.75f, 0.06f, StartFinish::Glass);
            }
            add("hospital east west connector roof", x, z,
                0.25f + kHospitalHeightM, 39.5f, 0.30f, 12.5f,
                StartFinish::DarkRoof, true);
        }
    }
    for (int row_seam = 0; row_seam < 2; ++row_seam) {
        for (int column = 0; column < 3; ++column) {
            const float x = column * kHospitalBlockStepXM;
            const float z = 31.0f + row_seam * kHospitalBlockStepZM;
            add("hospital north south connector core", x, z, 0.25f,
                12.0f, kHospitalHeightM, 25.0f,
                StartFinish::WarmWall, true);
            for (int floor = 0; floor < kHospitalFloorCount; ++floor) {
                const float sill = 1.05f + floor * kHospitalFloorHeightM;
                add("hospital north south connector glazing", x - 6.08f, z,
                    sill, 0.06f, 1.75f, 21.0f, StartFinish::Glass);
                add("hospital north south connector glazing", x + 6.08f, z,
                    sill, 0.06f, 1.75f, 21.0f, StartFinish::Glass);
            }
            add("hospital north south connector roof", x, z,
                0.25f + kHospitalHeightM, 12.5f, 0.30f, 25.5f,
                StartFinish::DarkRoof, true);
        }
    }

    // A clerestory and mechanical crown mark the centre without becoming a
    // fifth occupied floor.
    const Vec2 centre = block(1, 1);
    add("hospital central clerestory", centre.x, centre.z,
        0.55f + kHospitalHeightM, 32.0f, 2.0f, 26.0f,
        StartFinish::Glass);
    add("hospital central mechanical crown", centre.x, centre.z,
        2.55f + kHospitalHeightM, 34.0f, 0.55f, 28.0f,
        StartFinish::TealDoor, true);

    // Emergency receiving sits on the middle front block, facing the street
    // with a broad four-bay apron and a separate ambulance-only loop.
    const Vec2 emergency = block(1, 0);
    add("hospital emergency dropoff", emergency.x, emergency.z - 23.0f,
        0.10f, 76.0f, 0.10f, 10.5f, StartFinish::Concrete);
    add("hospital emergency canopy", emergency.x, emergency.z - 23.0f,
        5.7f, 64.0f, 0.30f, 10.5f, StartFinish::RedTrim, true);
    add("hospital emergency canopy column", emergency.x - 32.0f,
        emergency.z - 23.0f, 0.20f, 0.45f, 5.5f, 0.45f,
        StartFinish::Steel, true);
    add("hospital emergency canopy column", emergency.x + 32.0f,
        emergency.z - 23.0f, 0.20f, 0.45f, 5.5f, 0.45f,
        StartFinish::Steel, true);
    add("hospital emergency bay-two marker backing", emergency.x - 10.0f,
        emergency.z - 18.02f, 7.0f, 1.30f, 1.30f, 0.10f,
        StartFinish::Steel, true);
    add("hospital emergency bay-two marker face", emergency.x - 10.0f,
        emergency.z - 18.09f, 7.05f, 1.20f, 1.20f, 0.06f,
        StartFinish::RedTrim);
    for (float x : {-23.5f, -9.5f, 4.5f, 18.5f}) {
        add("hospital ambulance lane stripe", emergency.x + x,
            emergency.z - 23.0f, 0.215f, 0.16f, 0.018f, 9.5f,
            StartFinish::White);
    }

    // A small helipad gives the upper east wing a strong hospital silhouette.
    const Vec2 helipad = block(2, 0);
    const float helipad_y = 0.62f + kHospitalHeightM;
    add("hospital helipad deck", helipad.x, helipad.z, helipad_y,
        35.0f, 0.22f, 24.0f, StartFinish::Concrete, true);
    add("hospital helipad H cross", helipad.x, helipad.z, helipad_y + 0.24f,
        10.0f, 0.025f, 1.0f, StartFinish::White);
    add("hospital helipad H stem", helipad.x, helipad.z, helipad_y + 0.25f,
        1.0f, 0.025f, 8.0f, StartFinish::White);
    add("hospital helipad edge light", helipad.x - 15.0f,
        helipad.z - 10.0f, helipad_y + 0.25f, 0.35f, 0.18f, 0.35f,
        StartFinish::Yellow);
    add("hospital helipad edge light", helipad.x + 15.0f,
        helipad.z + 10.0f, helipad_y + 0.25f, 0.35f, 0.18f, 0.35f,
        StartFinish::Yellow);

    // The final row is the requested parking garage: two block widths, three
    // parking levels, open sides, perimeter columns, decks, and a clear street
    // entry so it reads as parking rather than another hospital wing.
    const float garage_x = kHospitalGarageCentre.x;
    const float garage_z = kHospitalGarageCentre.z;
    add("hospital garage lot", garage_x, garage_z, 0.0f,
        kHospitalGarageWidthM, 0.10f, kHospitalBlockDepthM,
        StartFinish::Asphalt);
    add("hospital garage ground deck", garage_x, garage_z, 0.10f,
        kHospitalGarageWidthM, 0.20f, 37.0f, StartFinish::Concrete, true);
    for (int level = 1; level <= 2; ++level) {
        const float deck = 0.10f + level * 3.7f;
        // Three slabs leave a real 9.6 x 12.5 m stair opening against the
        // north facade while preserving the driving deck around it.
        add("hospital garage upper deck", -22.9f, garage_z, deck,
            8.2f, 0.28f, 37.0f, StartFinish::Concrete, true);
        add("hospital garage upper deck", 54.9f, garage_z, deck,
            128.2f, 0.28f, 37.0f, StartFinish::Concrete, true);
        add("hospital garage upper deck", -14.0f, 192.25f, deck,
            9.6f, 0.28f, 24.5f, StartFinish::Concrete, true);
        add("hospital garage front fascia", garage_x, garage_z - 18.35f,
            deck + 0.28f, kHospitalGarageWidthM, 0.65f, 0.35f,
            StartFinish::Concrete, true);
        add("hospital garage rear fascia", garage_x, garage_z + 18.35f,
            deck + 0.28f, kHospitalGarageWidthM, 0.65f, 0.35f,
            StartFinish::Concrete, true);
    }
    for (int level = 0; level < 3; ++level) {
        const float bottom = 0.22f + level * 3.7f;
        for (float x : {-68.0f, -34.0f, 0.0f, 34.0f, 68.0f}) {
            if (x != 0.0f) {
                add("hospital garage column", garage_x + x,
                    garage_z - 15.0f, bottom, 0.60f, 3.35f, 0.60f,
                    StartFinish::Steel, true);
            }
            if (x != 34.0f) {
                add("hospital garage column", garage_x + x,
                    garage_z + 15.0f, bottom, 0.60f, 3.35f, 0.60f,
                    StartFinish::Steel, true);
            }
        }
        for (float x = -64.0f; x <= 64.0f; x += 8.0f) {
            const float stripe_x = garage_x + x;
            if ((stripe_x >= -19.0f && stripe_x <= -9.0f) ||
                (stripe_x >= 40.0f && stripe_x <= 52.0f))
                continue;
            add("hospital garage parking stripe", stripe_x,
                garage_z - 7.0f, bottom + 3.40f, 0.10f, 0.018f, 5.0f,
                StartFinish::White);
        }
    }
    add("hospital garage parapet front", garage_x, garage_z - 18.5f,
        7.95f, kHospitalGarageWidthM, 1.05f, 0.35f,
        StartFinish::TealDoor, true);
    add("hospital garage parapet rear", garage_x, garage_z + 18.5f,
        7.95f, kHospitalGarageWidthM, 1.05f, 0.35f,
        StartFinish::TealDoor, true);
    add("hospital garage entry walk", garage_x, garage_z - 22.0f, 0.10f,
        12.0f, 0.10f, 6.0f, StartFinish::Concrete);
    add("hospital garage sign", garage_x, garage_z - 18.72f, 5.3f,
        16.0f, 1.6f, 0.08f, StartFinish::RedTrim);
    // Project-bound parking prop. Its west-facing control plane owns one
    // fitted 2:3 image; the painted body keeps using the normal teal finish.
    add("hospital garage entry pay station body", 0.0f, 195.0f, 0.20f,
        0.74f, 1.25f, 0.68f, StartFinish::TealDoor, true);
    add("hospital garage entry pay station control face", -0.39f, 195.0f,
        0.47f, 0.04f, 0.90f, 0.60f, StartFinish::TealDoor);
    add("hospital garage skybridge floor", garage_x, garage_z - 31.0f,
        3.93f, 11.0f, 0.18f, 24.0f, StartFinish::Concrete, true);
    for (float x : {garage_x - 5.35f, garage_x + 5.35f}) {
        add("hospital garage skybridge lower rail", x, garage_z - 31.0f,
            4.11f, 0.22f, 0.72f, 24.0f, StartFinish::TealDoor, true);
        add("hospital garage skybridge glazing", x, garage_z - 31.0f,
            4.83f, 0.08f, 1.78f, 24.0f, StartFinish::Glass);
        add("hospital garage skybridge upper rail", x, garage_z - 31.0f,
            6.61f, 0.22f, 0.59f, 24.0f, StartFinish::TealDoor, true);
    }
    add("hospital garage skybridge roof", garage_x, garage_z - 31.0f, 7.2f,
        11.5f, 0.25f, 24.5f, StartFinish::TealDoor, true);
    add("hospital garage skybridge hospital landing", garage_x, 136.5f,
        3.93f, 11.0f, 0.18f, 13.0f, StartFinish::Concrete, true);
    for (float x : {garage_x - 5.35f, garage_x + 5.35f}) {
        add("hospital garage skybridge hospital landing wall", x, 136.5f,
            4.11f, 0.22f, 3.09f, 13.0f, StartFinish::WarmWall, true);
    }
    add("hospital garage skybridge hospital landing roof", garage_x,
        136.5f, 7.2f, 11.5f, 0.25f, 13.5f,
        StartFinish::TealDoor, true);

    return out;
}

}  // namespace apricot::city
