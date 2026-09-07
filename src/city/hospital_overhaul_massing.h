#pragma once

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

struct HospitalOverhaulRect {
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;

    constexpr float width() const { return max_x - min_x; }
    constexpr float depth() const { return max_z - min_z; }
    constexpr float centre_x() const { return (min_x + max_x) * 0.5f; }
    constexpr float centre_z() const { return (min_z + max_z) * 0.5f; }
};

// These rectangles are the shared master-plan contract in kHospitalSite-local
// coordinates. Keeping the voids explicit makes it possible for integration
// tests to prove that the courtyards did not become another giant floor slab.
inline constexpr HospitalOverhaulRect kHospitalOverhaulEnvelope{
    -27.0f, 211.0f, -19.0f, 143.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulNorthBar{
    -15.0f, 204.0f, -8.0f, 34.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulWestBar{
    -15.0f, 45.0f, 34.0f, 138.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulEastBar{
    144.0f, 204.0f, 34.0f, 138.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulSouthBar{
    45.0f, 144.0f, 100.0f, 138.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulClinicalSpine{
    45.0f, 144.0f, 60.0f, 78.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulNorthCourt{
    45.0f, 144.0f, 34.0f, 60.0f};
inline constexpr HospitalOverhaulRect kHospitalOverhaulSouthCourt{
    45.0f, 144.0f, 78.0f, 100.0f};

inline constexpr int kHospitalOverhaulFloorCount = 4;
inline constexpr float kHospitalOverhaulStoreyM = kHospitalFloorHeightM;
inline constexpr float kHospitalOverhaulSlabBottomM = 0.10f;
inline constexpr float kHospitalOverhaulSlabThicknessM = 0.20f;
inline constexpr float kHospitalOverhaulOccupiedBaseM =
    kHospitalOverhaulSlabBottomM + kHospitalOverhaulSlabThicknessM;
inline constexpr float kHospitalOverhaulWallTopM =
    kHospitalOverhaulOccupiedBaseM +
    kHospitalOverhaulFloorCount * kHospitalOverhaulStoreyM;
inline constexpr float kHospitalOverhaulRoofTopM =
    kHospitalOverhaulWallTopM + 0.28f;
inline constexpr float kHospitalOverhaulMainEntryX = 0.0f;
inline constexpr float kHospitalOverhaulEdTraumaZ = 79.0f;
inline constexpr float kHospitalOverhaulGarageBridgeX = 46.0f;
inline constexpr Vec2 kHospitalOverhaulHelipadCentre{174.0f, 52.0f};

static_assert(kHospitalOverhaulFloorCount == 4,
              "the overhaul must remain a four-story hospital");
static_assert(kHospitalOverhaulNorthBar.min_x >=
                  kHospitalOverhaulEnvelope.min_x &&
              kHospitalOverhaulEastBar.max_x <=
                  kHospitalOverhaulEnvelope.max_x &&
              kHospitalOverhaulNorthBar.min_z >=
                  kHospitalOverhaulEnvelope.min_z &&
              kHospitalOverhaulSouthBar.max_z <=
                  kHospitalOverhaulEnvelope.max_z,
              "clinical bars must stay inside the master campus envelope");
static_assert(kHospitalOverhaulNorthCourt.width() == 99.0f &&
                  kHospitalOverhaulNorthCourt.depth() == 26.0f &&
                  kHospitalOverhaulSouthCourt.width() == 99.0f &&
                  kHospitalOverhaulSouthCourt.depth() == 22.0f,
              "both daylight courts must remain genuine open volumes");
static_assert(kHospitalOverhaulClinicalSpine.min_z ==
                  kHospitalOverhaulNorthCourt.max_z &&
              kHospitalOverhaulClinicalSpine.max_z ==
                  kHospitalOverhaulSouthCourt.min_z,
              "the clinical spine must separate, not occupy, the courts");
static_assert(kHospitalOverhaulGarageBridgeX >=
                  kHospitalOverhaulSouthBar.min_x &&
              kHospitalOverhaulGarageBridgeX <=
                  kHospitalOverhaulSouthBar.max_x,
              "the existing garage bridge axis must land on the south bar");

// Complete replacement shell for the clinical campus. The garage, ambulance
// apron, public drives, fitted art receivers, furniture and planting remain in
// their separately-owned streams. Every value below is kHospitalSite-local.
inline std::vector<StartPart> bake_hospital_overhaul_massing() {
    std::vector<StartPart> out;
    out.reserve(1800);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    const auto add_rect_floor = [&](const char* name,
                                    const HospitalOverhaulRect& rect,
                                    int level) {
        add(name, rect.centre_x(), rect.centre_z(),
            kHospitalOverhaulSlabBottomM +
                static_cast<float>(level) * kHospitalOverhaulStoreyM,
            rect.width(), kHospitalOverhaulSlabThicknessM, rect.depth(),
            StartFinish::Concrete, true);
    };

    // Five deliberately different bars make up each supported occupied level.
    // No slab crosses either court rectangle.
    for (int level = 0; level < kHospitalOverhaulFloorCount; ++level) {
        add_rect_floor("hospital overhaul north public diagnostic floor",
                       kHospitalOverhaulNorthBar, level);
        add_rect_floor("hospital overhaul west inpatient floor",
                       kHospitalOverhaulWestBar, level);
        add_rect_floor("hospital overhaul east surgery ED floor",
                       kHospitalOverhaulEastBar, level);
        add_rect_floor("hospital overhaul south support floor",
                       kHospitalOverhaulSouthBar, level);
        add_rect_floor("hospital overhaul central clinical spine floor",
                       kHospitalOverhaulClinicalSpine, level);
    }

    struct DoorCut {
        const char* name = nullptr;
        float centre_m = 0.0f;
        float width_m = 1.0f;
        float sill_m = 0.0f;
        float height_m = 2.4f;
        StartFinish frame_finish = StartFinish::TealDoor;
    };

    // One wall call produces its collision shell and inset glazing from the
    // same BuildingWall description. Door leaves are intentionally omitted:
    // the wall gap is real and the arrivals/public-realm streams can dress it
    // without putting a solid facade box back over the route.
    const auto append_wall = [&](const char* wall_name,
                                 const char* glazing_name, Vec2 a, Vec2 b,
                                 StartFinish wall_finish,
                                 StartFinish band_finish, float face_side,
                                 float bay_m,
                                 float glazing_width_m,
                                 float glazing_height_m,
                                 std::initializer_list<DoorCut> door_cuts) {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length <= 0.01f) return;

        std::vector<DoorCut> doors(door_cuts.begin(), door_cuts.end());
        std::vector<BuildingOpening> openings;
        const int bay_count = std::max(1, static_cast<int>(length / bay_m));
        openings.reserve(doors.size());

        for (const DoorCut& door : doors) {
            openings.push_back(
                {door.name, OpeningKind::Door, door.centre_m, door.width_m,
                 door.sill_m, door.height_m, StartFinish::Glass, 0, 0,
                 door.frame_finish, false});
        }

        const BuildingWall wall{wall_name,
                                a,
                                b,
                                kHospitalOverhaulOccupiedBaseM,
                                kHospitalOverhaulWallTopM -
                                    kHospitalOverhaulOccupiedBaseM,
                                0.36f,
                                wall_finish,
                                openings.data(),
                                openings.size()};
        const BuildingPlan plan{wall_name, &wall, 1};
        auto pieces = bake_building(plan);
        out.insert(out.end(), pieces.begin(), pieces.end());

        // Massing-level inset panes establish a department-specific rhythm.
        // The public-realm layer owns secondary mullions, reveals and fins;
        // keeping those out here avoids exploding each long wall into thousands
        // of draw pieces. Circulation doors above are still real wall cutouts.
        const Vec2 dir{dx / length, dz / length};
        const Vec2 face_normal{dir.z * face_side, -dir.x * face_side};
        const float yaw_deg =
            std::atan2(-dir.z, dir.x) * 57.2957795131f;
        const float actual_bay = length / static_cast<float>(bay_count);
        const float window_width =
            std::min(glazing_width_m, actual_bay - 1.0f);
        for (int level = 0; level < kHospitalOverhaulFloorCount; ++level) {
            const float sill =
                0.72f + static_cast<float>(level) *
                            kHospitalOverhaulStoreyM;
            for (int bay = 0; bay < bay_count; ++bay) {
                const float centre =
                    (static_cast<float>(bay) + 0.5f) * actual_bay;
                const float window_min = centre - window_width * 0.5f;
                const float window_max = centre + window_width * 0.5f;
                const float window_top = sill + glazing_height_m;
                bool meets_door = false;
                for (const DoorCut& door : doors) {
                    const float door_min =
                        door.centre_m - door.width_m * 0.5f;
                    const float door_max =
                        door.centre_m + door.width_m * 0.5f;
                    const float door_top = door.sill_m + door.height_m;
                    const bool horizontal_overlap =
                        window_max > door_min && window_min < door_max;
                    const bool vertical_overlap =
                        window_top > door.sill_m && sill < door_top;
                    if (horizontal_overlap && vertical_overlap) {
                        meets_door = true;
                        break;
                    }
                }
                if (meets_door) continue;
                const float x = a.x + dir.x * centre + face_normal.x * 0.22f;
                const float z = a.z + dir.z * centre + face_normal.z * 0.22f;
                add(glazing_name, x, z,
                    kHospitalOverhaulOccupiedBaseM + sill, window_width,
                    glazing_height_m, 0.07f, StartFinish::Glass, false,
                    yaw_deg);
            }
        }

        // Deep horizontal bands make the four occupied levels readable at
        // city speed. A band is split anywhere a tall portal crosses it.
        for (int level = 1; level <= kHospitalOverhaulFloorCount; ++level) {
            const float band_bottom =
                kHospitalOverhaulOccupiedBaseM +
                static_cast<float>(level) * kHospitalOverhaulStoreyM -
                0.18f;
            constexpr float kBandHeight = 0.34f;
            std::vector<std::pair<float, float>> blocked;
            for (const DoorCut& door : doors) {
                const float door_bottom =
                    kHospitalOverhaulOccupiedBaseM + door.sill_m;
                const float door_top = door_bottom + door.height_m;
                if (door_top > band_bottom &&
                    door_bottom < band_bottom + kBandHeight) {
                    blocked.emplace_back(
                        std::max(0.0f,
                                 door.centre_m - door.width_m * 0.5f),
                        std::min(length,
                                 door.centre_m + door.width_m * 0.5f));
                }
            }
            std::sort(blocked.begin(), blocked.end());
            float cursor = 0.0f;
            const auto emit_band = [&](float u0, float u1) {
                if (u1 - u0 <= 0.05f) return;
                const float mid = (u0 + u1) * 0.5f;
                add("hospital overhaul four-story floor band",
                    a.x + dir.x * mid, a.z + dir.z * mid, band_bottom,
                    u1 - u0, kBandHeight, 0.64f, band_finish, true,
                    yaw_deg);
            };
            for (const auto& cut : blocked) {
                emit_band(cursor, cut.first);
                cursor = std::max(cursor, cut.second);
            }
            emit_band(cursor, length);
        }
    };

    // Public north face: the 12 m main portal lands exactly on the north-lot
    // pedestrian spine at x=0. The smaller diagnostic and ED walk-in portals
    // keep public arrivals distinct from the Juniper trauma doors.
    append_wall(
        "hospital overhaul north public diagnostic wall",
        "hospital overhaul north public grouped glazing", {-15.0f, -8.0f},
        {204.0f, -8.0f}, StartFinish::WarmWall, StartFinish::Steel, 1.0f,
        9.0f, 6.7f, 1.92f,
        {{"hospital overhaul main lobby open portal", 15.0f, 12.0f, 0.0f,
          4.80f, StartFinish::TealDoor},
         {"hospital overhaul diagnostic open portal", 75.0f, 5.0f, 0.0f,
          3.45f, StartFinish::TealDoor},
         {"hospital overhaul ED walk-in open portal", 195.0f, 5.5f, 0.0f,
          3.60f, StartFinish::RedTrim}});

    // West inpatient frontage uses a tighter bedroom cadence and gives
    // Bellweather transit/walk-up traffic a real secondary entrance.
    append_wall(
        "hospital overhaul west inpatient wall",
        "hospital overhaul west inpatient room glazing", {-15.0f, -8.0f},
        {-15.0f, 138.0f}, StartFinish::WarmWall, StartFinish::TealDoor,
        -1.0f, 7.5f, 4.9f, 1.64f,
        {{"hospital overhaul Bellweather public open portal", 66.0f, 5.2f,
          0.0f, 3.50f, StartFinish::TealDoor}});

    // Juniper face: the eight-metre trauma portal and the service receiving
    // portal are physically separate, with surgery glazing between them.
    append_wall(
        "hospital overhaul east surgery ED wall",
        "hospital overhaul east surgery grouped glazing", {204.0f, -8.0f},
        {204.0f, 138.0f}, StartFinish::Concrete, StartFinish::Steel, 1.0f,
        10.5f, 5.1f, 1.48f,
        {{"hospital overhaul ED trauma open portal", 87.0f, 8.0f, 0.0f,
          4.80f, StartFinish::RedTrim},
         {"hospital overhaul east service receiving open portal", 126.0f,
          6.0f, 0.0f, 4.40f, StartFinish::Steel}});

    // The south edge is three different departments, not one copied facade.
    append_wall("hospital overhaul southwest inpatient wall",
                "hospital overhaul southwest patient glazing",
                {-15.0f, 138.0f}, {45.0f, 138.0f},
                StartFinish::WarmWall, StartFinish::TealDoor, -1.0f, 7.5f,
                4.8f, 1.60f,
                {{"hospital overhaul garage ground-walk west open portal",
                  59.2f, 3.2f, 0.0f, 3.20f, StartFinish::TealDoor},
                 {"hospital overhaul garage skybridge west open portal",
                  57.75f, 5.5f, 3.81f, 3.09f,
                  StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul south support wall",
        "hospital overhaul south support punched glazing", {45.0f, 138.0f},
        {144.0f, 138.0f}, StartFinish::Concrete, StartFinish::Steel, -1.0f,
        12.0f, 4.2f, 1.30f,
        {{"hospital overhaul garage ground-walk open portal", 1.7f, 3.2f,
          0.0f, 3.20f, StartFinish::TealDoor},
         {"hospital overhaul garage skybridge open portal", 3.5f, 7.0f,
          3.81f, 3.09f, StartFinish::TealDoor},
         {"hospital overhaul staff receiving open portal", 90.0f, 4.0f,
          0.0f, 3.60f, StartFinish::Steel}});
    append_wall("hospital overhaul southeast clinical wall",
                "hospital overhaul southeast clinical glazing",
                {144.0f, 138.0f}, {204.0f, 138.0f},
                StartFinish::Concrete, StartFinish::Steel, -1.0f, 10.0f,
                4.8f, 1.42f,
                {{"hospital overhaul service loading open portal", 14.0f,
                  5.6f, 0.0f, 4.80f, StartFinish::Steel},
                 {"hospital overhaul service loading open portal", 30.0f,
                  5.6f, 0.0f, 4.80f, StartFinish::Steel},
                 {"hospital overhaul service loading open portal", 46.0f,
                  5.6f, 0.0f, 4.80f, StartFinish::Steel}});

    // North healing court. Four doorway groups create a loop through the
    // public bar, both clinical bars and the central spine.
    append_wall(
        "hospital overhaul north court public wall",
        "hospital overhaul north court public glazing", {45.0f, 34.0f},
        {144.0f, 34.0f}, StartFinish::WarmWall, StartFinish::TealDoor, -1.0f,
        9.0f, 6.6f, 1.86f,
        {{"hospital overhaul north court northwest open portal", 27.0f,
          3.6f, 0.0f, 3.20f, StartFinish::TealDoor},
         {"hospital overhaul north court northeast open portal", 73.0f,
          3.6f, 0.0f, 3.20f, StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul north court west inpatient wall",
        "hospital overhaul north court west glazing", {45.0f, 34.0f},
        {45.0f, 60.0f}, StartFinish::WarmWall, StartFinish::TealDoor, 1.0f,
        8.5f, 5.8f, 1.82f,
        {{"hospital overhaul north court west open portal", 13.0f, 3.6f,
          0.0f, 3.20f, StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul north court east surgery wall",
        "hospital overhaul north court east glazing", {144.0f, 60.0f},
        {144.0f, 34.0f}, StartFinish::Concrete, StartFinish::TealDoor, 1.0f,
        8.5f, 5.8f, 1.74f,
        {{"hospital overhaul north court east open portal", 13.0f, 3.6f,
          0.0f, 3.20f, StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul north court clinical spine wall",
        "hospital overhaul north court spine glazing", {45.0f, 60.0f},
        {144.0f, 60.0f}, StartFinish::WarmWall, StartFinish::TealDoor, 1.0f,
        9.0f, 6.7f, 1.88f,
        {{"hospital overhaul north court spine west open portal", 27.0f,
          3.8f, 0.0f, 3.20f, StartFinish::TealDoor},
         {"hospital overhaul north court spine east open portal", 73.0f,
          3.8f, 0.0f, 3.20f, StartFinish::TealDoor}});

    // South healing court repeats the circulation logic but changes its
    // glazing proportions and materials to read as rehabilitation/support.
    append_wall(
        "hospital overhaul south court clinical spine wall",
        "hospital overhaul south court spine glazing", {45.0f, 78.0f},
        {144.0f, 78.0f}, StartFinish::WarmWall, StartFinish::TealDoor, -1.0f,
        9.0f, 6.2f, 1.76f,
        {{"hospital overhaul south court spine west open portal", 27.0f,
          3.8f, 0.0f, 3.20f, StartFinish::TealDoor},
         {"hospital overhaul south court spine east open portal", 73.0f,
          3.8f, 0.0f, 3.20f, StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul south court west inpatient wall",
        "hospital overhaul south court west glazing", {45.0f, 78.0f},
        {45.0f, 100.0f}, StartFinish::WarmWall, StartFinish::TealDoor, 1.0f,
        7.5f, 4.9f, 1.62f,
        {{"hospital overhaul south court west open portal", 11.0f, 3.6f,
          0.0f, 3.20f, StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul south court east surgery wall",
        "hospital overhaul south court east glazing", {144.0f, 100.0f},
        {144.0f, 78.0f}, StartFinish::Concrete, StartFinish::TealDoor, 1.0f,
        7.5f, 4.7f, 1.54f,
        {{"hospital overhaul south court east open portal", 11.0f, 3.6f,
          0.0f, 3.20f, StartFinish::TealDoor}});
    append_wall(
        "hospital overhaul south court support wall",
        "hospital overhaul south court support glazing", {45.0f, 100.0f},
        {144.0f, 100.0f}, StartFinish::Concrete, StartFinish::TealDoor, 1.0f,
        11.0f, 6.0f, 1.58f,
        {{"hospital overhaul south court support west open portal", 27.0f,
          3.8f, 0.0f, 3.20f, StartFinish::TealDoor},
         {"hospital overhaul south court support east open portal", 73.0f,
          3.8f, 0.0f, 3.20f, StartFinish::TealDoor}});

    const auto add_roof = [&](const char* name,
                              const HospitalOverhaulRect& rect) {
        add(name, rect.centre_x(), rect.centre_z(),
            kHospitalOverhaulWallTopM, rect.width(), 0.28f, rect.depth(),
            StartFinish::DarkRoof, true);
    };
    add_roof("hospital overhaul north public diagnostic roof slab",
             kHospitalOverhaulNorthBar);
    add_roof("hospital overhaul west inpatient roof slab",
             kHospitalOverhaulWestBar);
    add_roof("hospital overhaul east surgery ED roof slab",
             kHospitalOverhaulEastBar);
    add_roof("hospital overhaul south support roof slab",
             kHospitalOverhaulSouthBar);
    add_roof("hospital overhaul central clinical spine roof slab",
             kHospitalOverhaulClinicalSpine);

    constexpr float kParapetBottom = kHospitalOverhaulRoofTopM;
    constexpr float kParapetHeight = 0.92f;
    const auto parapet_x = [&](const char* name, float x0, float x1, float z) {
        add(name, (x0 + x1) * 0.5f, z, kParapetBottom, x1 - x0,
            kParapetHeight, 0.34f, StartFinish::WarmWall, true);
    };
    const auto parapet_z = [&](const char* name, float x, float z0, float z1) {
        add(name, x, (z0 + z1) * 0.5f, kParapetBottom, 0.34f,
            kParapetHeight, z1 - z0, StartFinish::Concrete, true);
    };

    // Outer silhouette plus both court rims. Bar-to-bar joins deliberately
    // have no parapet, preserving connected roof and clinical circulation.
    parapet_x("hospital overhaul north public parapet", -15.0f, 204.0f,
              -8.0f);
    parapet_z("hospital overhaul west inpatient parapet", -15.0f, -8.0f,
              138.0f);
    parapet_z("hospital overhaul east surgery parapet", 204.0f, -8.0f,
              138.0f);
    parapet_x("hospital overhaul south perimeter parapet", -15.0f, 204.0f,
              138.0f);
    parapet_x("hospital overhaul north court north parapet", 45.0f, 144.0f,
              34.0f);
    parapet_z("hospital overhaul north court west parapet", 45.0f, 34.0f,
              60.0f);
    parapet_z("hospital overhaul north court east parapet", 144.0f, 34.0f,
              60.0f);
    parapet_x("hospital overhaul north court spine parapet", 45.0f, 144.0f,
              60.0f);
    parapet_x("hospital overhaul south court spine parapet", 45.0f, 144.0f,
              78.0f);
    parapet_z("hospital overhaul south court west parapet", 45.0f, 78.0f,
              100.0f);
    parapet_z("hospital overhaul south court east parapet", 144.0f, 78.0f,
              100.0f);
    parapet_x("hospital overhaul south court support parapet", 45.0f,
              144.0f, 100.0f);

    // The atrium is the middle of the clinical spine, so daylight marks the
    // east-west transfer route without consuming either open court. It is a
    // non-occupied clerestory above the four-story roof datum.
    constexpr float kClerestoryBottom = kHospitalOverhaulRoofTopM + 0.14f;
    add("hospital overhaul central atrium clerestory north glass", 94.5f,
        64.0f, kClerestoryBottom, 36.0f, 2.25f, 0.08f,
        StartFinish::Glass);
    add("hospital overhaul central atrium clerestory south glass", 94.5f,
        74.0f, kClerestoryBottom, 36.0f, 2.25f, 0.08f,
        StartFinish::Glass);
    add("hospital overhaul central atrium clerestory west glass", 76.5f,
        69.0f, kClerestoryBottom, 0.08f, 2.25f, 10.0f,
        StartFinish::Glass);
    add("hospital overhaul central atrium clerestory east glass", 112.5f,
        69.0f, kClerestoryBottom, 0.08f, 2.25f, 10.0f,
        StartFinish::Glass);
    for (float x : {76.5f, 85.5f, 94.5f, 103.5f, 112.5f}) {
        add("hospital overhaul central atrium clerestory mullion", x,
            64.0f, kClerestoryBottom, 0.16f, 2.25f, 0.18f,
            StartFinish::Steel, true);
        add("hospital overhaul central atrium clerestory mullion", x,
            74.0f, kClerestoryBottom, 0.16f, 2.25f, 0.18f,
            StartFinish::Steel, true);
    }
    add("hospital overhaul central atrium clerestory cap", 94.5f, 69.0f,
        kClerestoryBottom + 2.25f, 36.5f, 0.24f, 10.5f,
        StartFinish::TealDoor, true);

    // Major plant is grouped behind screens on the support and southern east
    // bars. The helipad's northern approach sector stays completely clear.
    constexpr float kScreenBottom = kHospitalOverhaulRoofTopM + 0.12f;
    add("hospital overhaul south support roof screen north", 104.0f, 113.0f,
        kScreenBottom, 34.0f, 2.45f, 0.28f, StartFinish::TealDoor, true);
    add("hospital overhaul south support roof screen south", 104.0f, 129.0f,
        kScreenBottom, 34.0f, 2.45f, 0.28f, StartFinish::TealDoor, true);
    add("hospital overhaul south support roof screen east", 121.0f, 121.0f,
        kScreenBottom, 0.28f, 2.45f, 16.0f, StartFinish::TealDoor, true);
    add("hospital overhaul east clinical roof screen west", 160.0f, 112.0f,
        kScreenBottom, 0.28f, 2.45f, 16.0f, StartFinish::TealDoor, true);
    add("hospital overhaul east clinical roof screen south", 172.0f, 120.0f,
        kScreenBottom, 24.0f, 2.45f, 0.28f, StartFinish::TealDoor, true);
    add("hospital overhaul east clinical roof screen east", 184.0f, 112.0f,
        kScreenBottom, 0.28f, 2.45f, 16.0f, StartFinish::TealDoor, true);

    // Raised roof deck, H and access vestibule tie emergency aviation directly
    // to the east surgery/ED bar. These are roof structures, not a fifth floor.
    constexpr float kHelipadDeckBottom = kHospitalOverhaulRoofTopM + 0.32f;
    add("hospital overhaul east ED helipad support plinth",
        kHospitalOverhaulHelipadCentre.x,
        kHospitalOverhaulHelipadCentre.z, kHospitalOverhaulRoofTopM, 36.0f,
        0.42f, 24.0f, StartFinish::Steel, true);
    add("hospital overhaul east ED helipad deck",
        kHospitalOverhaulHelipadCentre.x,
        kHospitalOverhaulHelipadCentre.z, kHelipadDeckBottom, 42.0f, 0.24f,
        30.0f, StartFinish::Concrete, true);
    add("hospital overhaul east ED helipad H cross",
        kHospitalOverhaulHelipadCentre.x,
        kHospitalOverhaulHelipadCentre.z, kHelipadDeckBottom + 0.245f, 13.0f,
        0.025f, 1.20f, StartFinish::White);
    add("hospital overhaul east ED helipad H stem",
        kHospitalOverhaulHelipadCentre.x,
        kHospitalOverhaulHelipadCentre.z, kHelipadDeckBottom + 0.25f, 1.20f,
        0.025f, 10.0f, StartFinish::White);
    add("hospital overhaul east ED helipad access vestibule", 150.5f, 70.0f,
        kHospitalOverhaulRoofTopM + 0.10f, 8.0f, 2.55f, 6.0f,
        StartFinish::WarmWall, true);
    add("hospital overhaul east ED helipad access door", 150.5f, 66.96f,
        kHospitalOverhaulRoofTopM + 0.28f, 3.0f, 2.18f, 0.05f,
        StartFinish::Glass);
    add("hospital overhaul east ED helipad access cap", 150.5f, 70.0f,
        kHospitalOverhaulRoofTopM + 2.65f, 8.5f, 0.22f, 6.5f,
        StartFinish::TealDoor, true);

    return out;
}

}  // namespace apricot::city
