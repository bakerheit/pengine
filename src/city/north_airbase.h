#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include "city/start_area.h"

// Halberd Field, the north shore air station.
//
// Pinatty's only other airfield is Camber Point, 4.5 km away at the far south
// end of the island. Halberd is the counterweight: a short military strip on
// the coastal shelf beyond the Kepler refinery, reached by one guarded gate off
// the Yard Road. It is the northernmost thing a player can drive to.
//
// Three things shape the layout, and all three are on purpose:
//
//   * ONE WAY IN. The wire is continuous and the only opening is the main gate
//     on the south fence. A gate is only a decision if there is no way round
//     the back, so the perimeter is authored as a closed loop and the tests
//     check that it closes.
//   * THE RUNWAY IS NOT A ROAD. It never appears in kRoads. Traffic that
//     routes down a runway is a bug with a very good disguise, so the strip is
//     paving with its own ground collision and nothing else.
//   * MILITARY, NOT CIVIL. Camber Point is glass and kerbside drop-off. This
//     is blast walls, revetments, earth-covered magazines and a hardened tower.
//     If the two read the same from the air, the second one was not worth
//     building.
//
// The plate under all of it is kHalberdFieldTerrainOp in terrain_ops.h, flat to
// 9 m at every level of detail.

namespace apricot::city {

inline constexpr StartSite kHalberdFieldSite{
    "Halberd Field", {-690.0f, -2100.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 1090.0f, 240.0f, 9.0f, 2200.0f};

// Paving lies on the plate, so every slab shares one top. Vehicles drive on the
// ground rects named by halberd_ground_piece(), not on the slab sides.
// Paving stands 13 cm over the plate, which is the airport's convention and is
// not arbitrary: a road ribbon is DRAPED and CROWNED, so it rides up to ~12 cm
// above the flat bed it was authored on. Paving any lower than the crown does
// not sit under the road, it fights it.
inline constexpr float kHalberdPaveTop = 0.13f;

// Paving is LAYERED, and the layer is not decoration.
//
// An airfield's surfaces genuinely lie on top of one another: a taxiway runs
// out across an apron, a revetment's hardstanding is poured on the apron it
// opens onto. Authored as one flat height they are coplanar quads fighting for
// the depth buffer, which is what "two road layers" looks like from a car.
//
// So every plane declares which layer it is on and gets 8 mm of clearance per
// layer - under the wheels, invisible; in the depth buffer, decisive. Planes
// that share a layer must not overlap, and north_airbase_tests checks that.
enum class HalberdLayer : uint8_t {
    Ground = 0,   // the big concrete areas: aprons
    Hard = 1,     // hardstandings poured on them: revetments, fuel bund, walks
    Sealed = 2,   // asphalt: runway, taxiway, station road, motor pool
    Paint = 3,    // markings
};
inline constexpr float halberd_layer_top(HalberdLayer layer) {
    return kHalberdPaveTop + static_cast<float>(layer) * 0.008f;
}

// Runway 09/27. 900 m by 45 m, which is a forward strip rather than an
// international one - it is what fits the shelf, and it is a long straight to
// take off from or to drive down at a speed the city never allows.
inline constexpr float kHalberdRunwayZ = -72.0f;
inline constexpr float kHalberdRunwayHalfLength = 450.0f;
inline constexpr float kHalberdRunwayHalfWidth = 22.5f;
inline constexpr float kHalberdTaxiwayZ = -14.0f;
inline constexpr float kHalberdTaxiwayHalfWidth = 10.0f;

// Perimeter. The fence is inside the plate edge by 15 m so the earthwork
// feather never runs out from under a post.
inline constexpr float kHalberdFenceHalfX = 530.0f;
inline constexpr float kHalberdFenceNorth = -108.0f;
inline constexpr float kHalberdFenceSouth = 112.0f;

// The gate sits where the Halberd Approach (road 244) arrives, which is world
// x = -500. Local x is that minus the site origin; stating it as a constant
// keeps the road table and the opening in the wire from drifting apart.
inline constexpr float kHalberdGateX = 190.0f;
inline constexpr float kHalberdGateHalfWidth = 7.0f;

struct HalberdBuilder {
    std::vector<StartPart>& out;
    using F = BuildingFinish;

    void add(const char* name, float x, float z, float y, float w, float h, float d,
             F finish = F::Concrete, bool solid = false, float yaw = 0, float roll = 0,
             float pitch = 0) {
        out.push_back({name, {x, z}, y, w, h, d, finish, solid, pitch, yaw, roll});
    }
    // Paving. A thin plane rather than a slab: a 16 cm box grows lit vertical
    // edges along every joint and turns an apron into a chequerboard of kerbs.
    void pave(const char* name, float x, float z, float w, float d, F finish = F::Concrete,
              HalberdLayer layer = HalberdLayer::Ground) {
        add(name, x, z, halberd_layer_top(layer), w, .004f, d, finish);
    }
    void paint(const char* name, float x, float z, float w, float d, F finish = F::White,
               float yaw = 0) {
        add(name, x, z, halberd_layer_top(HalberdLayer::Paint), w, .004f, d, finish, false, yaw);
    }
};

// A run of security fence: posts, two rails and the mesh between them. Emitted
// as one call per side so the perimeter is written as a closed loop and reads
// as one in the source.
inline void halberd_fence_run(HalberdBuilder& b, float x0, float z0, float x1, float z1,
                              float gap_centre = 0, float gap_half = 0) {
    using F = BuildingFinish;
    const float dx = x1 - x0, dz = z1 - z0;
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < 1.f) return;
    const bool along_x = std::fabs(dx) > std::fabs(dz);
    const float ux = dx / length, uz = dz / length;
    // 20 m bays. The wire runs 2.5 km round the field, so a bay is a part count
    // decision before it is a look: at 8 m this one fence outweighed every
    // building on the station put together.
    constexpr float kBay = 20.0f;
    const int bays = static_cast<int>(length / kBay);
    for (int i = 0; i <= bays; ++i) {
        const float t = static_cast<float>(i) * kBay;
        const float px = x0 + ux * t, pz = z0 + uz * t;
        // A gate leaves the posts either side standing: the opening is in the
        // mesh, not in the structure.
        const float along = along_x ? px : pz;
        if (gap_half > 0 && std::fabs(along - gap_centre) < gap_half) continue;
        b.add("halberd fence post", px, pz, 0, .18f, 3.3f, .18f, F::Steel, true);
        if (i == bays) break;
        const float mx = px + ux * kBay * .5f, mz = pz + uz * kBay * .5f;
        const float mid = along_x ? mx : mz;
        if (gap_half > 0 && std::fabs(mid - gap_centre) < gap_half + kBay * .5f) continue;
        const float w = along_x ? kBay : .06f, d = along_x ? .06f : kBay;
        b.add("halberd fence mesh", mx, mz, .12f, w, 2.6f, d, F::Steel, true);
        b.add("halberd fence rail", mx, mz, 2.72f, along_x ? kBay : .11f, .11f,
              along_x ? .11f : kBay, F::Steel);
        // Barbed wire above the rail, as one thin bar. It was a leaning
        // outrigger box: rolled about the wrong axis, a 20 m bar tips along
        // its LENGTH, so the perimeter grew a row of five-metre diagonal
        // spikes. A straight wire needs no rotation and cannot get it wrong.
        b.add("halberd fence wire", mx, mz, 2.94f, along_x ? kBay : .05f, .05f,
              along_x ? .05f : kBay, F::Steel);
    }
}

// An earth-banked aircraft shelter: three blast walls open to the taxiway, with
// a hardstanding inside. This is the silhouette that says "military" from a
// kilometre away, which a hangar does not.
inline void halberd_revetment(HalberdBuilder& b, float x, float z) {
    using F = BuildingFinish;
    b.pave("halberd revetment hardstanding", x, z, 34, 30, F::Concrete, HalberdLayer::Hard);
    for (float side : {-1.f, 1.f})
        b.add("halberd blast wall", x + side * 17.f, z + 2.f, 0, 2.4f, 5.f, 26.f, F::Concrete, true);
    b.add("halberd blast wall", x, z + 15.f, 0, 36.4f, 5.f, 2.4f, F::Concrete, true);
    // Earth bank against the outer faces, battered back the way a real one is.
    for (float side : {-1.f, 1.f})
        b.add("halberd revetment bank", x + side * 20.5f, z + 2.f, 0, 4.6f, 3.4f, 26.f,
              F::DarkRoof, true);
    b.add("halberd revetment bank", x, z + 18.2f, 0, 41.f, 3.4f, 4.6f, F::DarkRoof, true);
}

// A vertical cylinder tank inside its bund. Named "round" so the host layer
// gives it the shared cylinder mesh.
inline void halberd_fuel_tank(HalberdBuilder& b, float x, float z, float radius, float height) {
    using F = BuildingFinish;
    b.add("halberd round fuel tank", x, z, .2f, radius * 2.f, height, radius * 2.f, F::Steel, true);
    b.add("halberd round tank roof", x, z, .2f + height, radius * 2.f + .5f, .5f,
          radius * 2.f + .5f, F::Steel);
    for (int band = 0; band < 3; ++band)
        b.add("halberd round tank band", x, z,
              1.6f + static_cast<float>(band) * (height - 2.4f) * .5f,
              radius * 2.f + .16f, .22f, radius * 2.f + .16f, F::DarkRoof);
    b.add("halberd tank ladder", x + radius, z, .2f, .5f, height, .12f, F::Steel);
}

// An earth-covered magazine. Concrete headwall, steel door, grassed mound.
inline void halberd_magazine(HalberdBuilder& b, float x, float z) {
    using F = BuildingFinish;
    b.add("halberd magazine mound", x, z, 0, 26.f, 5.2f, 17.f, F::DarkRoof, true);
    b.add("halberd magazine mound", x, z, 5.2f, 20.f, 1.6f, 12.f, F::DarkRoof);
    b.add("halberd magazine headwall", x, z - 8.9f, 0, 14.f, 5.4f, 1.4f, F::Concrete, true);
    b.add("halberd magazine door", x, z - 9.7f, .1f, 5.2f, 4.2f, .3f, F::Steel, true);
    b.pave("halberd magazine apron", x, z - 13.f, 14.f, 8.f, F::Concrete, HalberdLayer::Hard);
}


// Runway designator numerals, drawn as bars on a seven-segment cell. Painting
// "09" and "27" is what tells a player which way the strip runs, and four
// digits of bars is cheaper than a texture nobody would look at twice.
//
// A runway number is read FROM THE APPROACH, not from directly above: the top
// of the glyph points away down the strip. `approach` is +1 for the numbers a
// pilot landing eastbound reads and -1 for the other end, and it rotates the
// whole cell rather than asking the caller to place mirrored bars by hand.
inline void halberd_numeral(HalberdBuilder& b, int digit, float x, float across,
                            float scale, float approach) {
    // Segments: top, upper-left, upper-right, middle, lower-left, lower-right,
    // bottom, in glyph space where "up" is toward the top of the character.
    static const unsigned char kSegments[10] = {
        0x77, 0x24, 0x5D, 0x6D, 0x2E, 0x6B, 0x7B, 0x25, 0x7F, 0x6F};
    const unsigned char mask = kSegments[digit % 10];
    const float w = scale * .62f, h = scale, bar = scale * .15f;
    const struct { unsigned char bit; float right, up, wide, tall; } kBars[7] = {
        {0x01, 0, h * .5f, w, bar},              // top
        {0x02, -w * .5f, h * .25f, bar, h * .5f},
        {0x04, w * .5f, h * .25f, bar, h * .5f},
        {0x08, 0, 0, w, bar},                    // middle
        {0x10, -w * .5f, -h * .25f, bar, h * .5f},
        {0x20, w * .5f, -h * .25f, bar, h * .5f},
        {0x40, 0, -h * .5f, w, bar},             // bottom
    };
    // Glyph up maps to world x * approach; glyph right maps to world z *
    // approach. Both flip together, which is what mirrors the far-end pair.
    for (const auto& seg : kBars)
        if (mask & seg.bit)
            b.paint("halberd runway numeral", x + seg.up * approach,
                    across + seg.right * approach, seg.tall, seg.wide);
}

inline std::vector<StartPart> bake_halberd_field() {
    using F = BuildingFinish;
    std::vector<StartPart> out;
    out.reserve(1400);
    HalberdBuilder b{out};

    // --- The field ---------------------------------------------------------
    // No slab under the whole field. The plate is already flat to 9 m and the
    // site clearance keeps wild scatter off it, so the terrain's own surface is
    // the mown grass - and a 1080 x 200 m quad of concrete would only hide it.
    b.pave("halberd runway", 0, kHalberdRunwayZ, kHalberdRunwayHalfLength * 2,
           kHalberdRunwayHalfWidth * 2, F::Asphalt, HalberdLayer::Sealed);
    b.pave("halberd taxiway", 0, kHalberdTaxiwayZ, 900, kHalberdTaxiwayHalfWidth * 2, F::Asphalt,
           HalberdLayer::Sealed);
    for (float side : {-1.f, 1.f})
        // The link spans the GAP between the two, not the whole distance
        // between their centres: run centre to centre and it lies on top of
        // both, in the same layer, fighting each.
        b.pave("halberd taxiway link", side * 430.f,
               (kHalberdRunwayZ + kHalberdRunwayHalfWidth + kHalberdTaxiwayZ -
                kHalberdTaxiwayHalfWidth) * .5f, 20,
               (kHalberdTaxiwayZ - kHalberdTaxiwayHalfWidth) -
                   (kHalberdRunwayZ + kHalberdRunwayHalfWidth), F::Asphalt,
               HalberdLayer::Sealed);
    b.pave("halberd apron", -210, 30, 560, 68, F::Concrete);
    // Abuts the taxiway rather than reaching under it.
    b.pave("halberd east apron", 370, 16, 300, 40, F::Concrete);
    // The apron stops exactly where road 244's ribbon ends, at local z 110. They
    // abut rather than overlap: a ribbon crowns above its authored bed, so any
    // overlap at all is a seam that swaps as the camera moves.
    b.pave("halberd gate apron", kHalberdGateX, 75, 46, 70, F::Concrete);
    // Between the main apron and the gate apron, touching both, overlapping
    // neither.
    b.pave("halberd support apron", 118.5f, 40, 97, 76, F::Concrete);
    // The station road. Without it the ops block, the barracks and the motor
    // pool stand on open grass with no way between them, which reads as a set
    // of sheds dropped on a field rather than as a station.
    b.pave("halberd station road", 15, 68, 970, 10, F::Asphalt, HalberdLayer::Sealed);
    // Edge lines. A bare 10 m strip of asphalt laid across grass reads as a
    // hole in the ground rather than as a road.
    for (float side : {-1.f, 1.f})
        b.paint("halberd station road edge", 15, 68.f + side * 4.4f, 970, .4f);

    // --- Runway markings ---------------------------------------------------
    for (float side : {-1.f, 1.f})
        b.paint("halberd runway edge line", 0, kHalberdRunwayZ + side * 21.f, 900, .9f);
    for (int i = 0; i < 14; ++i) {
        const float x = -420.f + static_cast<float>(i) * 65.f;
        b.paint("halberd runway centreline", x, kHalberdRunwayZ, 30, .9f);
    }
    // Threshold piano keys, aiming points and touchdown bars, at both ends.
    for (float end : {-1.f, 1.f}) {
        const float threshold = end * (kHalberdRunwayHalfLength - 6.f);
        for (int bar = 0; bar < 8; ++bar) {
            const float z = kHalberdRunwayZ + (static_cast<float>(bar) - 3.5f) * 4.6f;
            b.paint("halberd runway threshold bar", threshold - end * 12.f, z, 22, 1.8f);
        }
        for (float side : {-1.f, 1.f})
            b.paint("halberd runway aiming point", threshold - end * 160.f,
                    kHalberdRunwayZ + side * 11.f, 42, 5.4f);
        for (int pair = 1; pair <= 2; ++pair)
            for (float side : {-1.f, 1.f})
                b.paint("halberd runway touchdown bar",
                        threshold - end * (230.f + static_cast<float>(pair) * 70.f),
                        kHalberdRunwayZ + side * 11.f, 20, 2.6f);
    }
    // 09 is read landing eastbound, 27 landing westbound. The pair at each end
    // sits across the strip in the reader's own left-to-right order.
    halberd_numeral(b, 0, -394, kHalberdRunwayZ - 9.5f, 17, 1.f);
    halberd_numeral(b, 9, -394, kHalberdRunwayZ + 9.5f, 17, 1.f);
    halberd_numeral(b, 2, 394, kHalberdRunwayZ + 9.5f, 17, -1.f);
    halberd_numeral(b, 7, 394, kHalberdRunwayZ - 9.5f, 17, -1.f);
    for (int i = 0; i < 22; ++i)
        b.paint("halberd taxiway centreline", -430.f + static_cast<float>(i) * 41.f,
                kHalberdTaxiwayZ, 26, .6f, F::Yellow);

    // Runway edge lights, and the approach bar out over the shore.
    for (int i = 0; i <= 18; ++i) {
        const float x = -450.f + static_cast<float>(i) * 50.f;
        for (float side : {-1.f, 1.f}) {
            b.add("halberd runway light stem", x, kHalberdRunwayZ + side * 26.f, .16f,
                  .16f, .5f, .16f, F::Steel);
            b.add("halberd runway light lens", x, kHalberdRunwayZ + side * 26.f, .58f,
                  .34f, .18f, .34f, F::White);
        }
    }

    // Apron markings. 35,000 m2 of unmarked concrete reads as a car park with
    // no cars; lead-in lines and parking Ts are what make it an apron.
    b.paint("halberd apron lead-in", -210, -2.5f, 560, .5f, F::Yellow);
    for (int i = 0; i < 3; ++i) {
        const float x = -370.f + static_cast<float>(i) * 130.f;
        b.paint("halberd apron lead-in", x, 14, .5f, 34, F::Yellow);
        for (float side : {-1.f, 1.f}) {
            b.paint("halberd apron lead-in", x + side * 40.f, 22, .5f, 50, F::Yellow);
            b.paint("halberd apron lead-in", x + side * 40.f, 45, 15, .5f, F::Yellow);
        }
    }
    for (int i = 0; i < 3; ++i)
        b.paint("halberd apron lead-in", 280.f + static_cast<float>(i) * 100.f, 4, .5f, 36,
                F::Yellow);

    // --- Hangars -----------------------------------------------------------
    // Three, doors north onto the apron. A barrel roof so the row does not read
    // as three more concrete boxes at a station already full of them.
    for (int i = 0; i < 3; ++i) {
        const float x = -370.f + static_cast<float>(i) * 130.f;
        b.add("halberd hangar wall", x, 46, 0, 62, 11, 26, F::Concrete, true);
        for (int arch = 0; arch < 5; ++arch) {
            const float t = (static_cast<float>(arch) - 2.f) * .2f;
            b.add("halberd hangar roof", x, 46, 11.f + (1.f - t * t * 4.f) * 2.6f,
                  62.6f, .6f, 26.f - std::fabs(t) * 20.f, F::DarkRoof);
        }
        b.add("halberd hangar door", x, 33.2f, .2f, 54, 9.4f, .5f, F::Steel, true);
        for (int leaf = 0; leaf < 6; ++leaf)
            b.add("halberd hangar door rib", x - 22.5f + static_cast<float>(leaf) * 9.f,
                  32.9f, .2f, .4f, 9.4f, .3f, F::DarkRoof);
        b.add(i == 0 ? "halberd hangar number 0"
                     : (i == 1 ? "halberd hangar number 1" : "halberd hangar number 2"),
              x - 26.f, 32.8f, 6.2f, 3.f, 3.f, .06f, F::White);
        b.add("halberd apron light mast", x + 34.f, 20.f, 0, .4f, 13.f, .4f, F::Steel, true);
        b.add("halberd apron light head", x + 34.f, 20.f, 13.f, 2.4f, .5f, 1.f, F::DarkRoof);
        b.add("halberd apron light lens", x + 34.f, 20.f, 12.9f, 2.f, .18f, .8f, F::White);
    }

    // --- Control tower -----------------------------------------------------
    // Squat and hardened, with the cab set on a solid shaft. A glass stalk is a
    // civil airport's tower and would read as Camber Point moved north.
    b.add("halberd tower base", 60, 26, 0, 20, 5.5f, 16, F::Concrete, true);
    b.add("halberd tower shaft", 60, 26, 5.5f, 12, 14.5f, 11, F::Concrete, true);
    b.add("halberd tower gallery", 60, 26, 20.f, 17, .6f, 15.5f, F::Concrete);
    b.add("halberd tower cab", 60, 26, 20.6f, 14.4f, 4.2f, 13.f, F::Glass, true);
    b.add("halberd tower cab frame", 60, 26, 20.6f, 14.8f, .35f, 13.4f, F::DarkRoof);
    b.add("halberd tower cab frame", 60, 26, 24.4f, 14.8f, .4f, 13.4f, F::DarkRoof);
    b.add("halberd tower roof", 60, 26, 24.8f, 16.4f, .7f, 15.f, F::DarkRoof);
    b.add("halberd tower mast", 60, 21, 25.5f, .3f, 9, .3f, F::Steel);
    for (int i = 0; i < 3; ++i)
        b.add("halberd tower aerial", 60, 21, 27.5f + static_cast<float>(i) * 2.2f,
              2.6f - static_cast<float>(i) * .6f, .12f, .12f, F::Steel);
    b.add("halberd tower beacon lens", 60, 21, 34.4f, .55f, .55f, .55f, F::RedTrim);

    // --- Radar -------------------------------------------------------------
    // The one thing on the station visible from the city. It stands clear of
    // the apron so the dish is a silhouette against the sea, not against a
    // hangar roof.
    b.add("halberd radar plinth", -250, 94, 0, 12, 2.4f, 12, F::Concrete, true);
    b.add("halberd radar tower", -250, 94, 2.4f, 4.2f, 24, 4.2f, F::Steel, true);
    for (int i = 0; i < 5; ++i)
        b.add("halberd radar brace", -250, 94, 4.f + static_cast<float>(i) * 4.6f,
              5.6f, .3f, 5.6f, F::Steel);
    b.add("halberd radar head", -250, 94, 26.4f, 6.4f, 2.4f, 5.f, F::DarkRoof);
    b.add("halberd radar dish", -250, 94, 27.f, 13.5f, 8.2f, 1.2f, F::White, false, 34.f, 0, -18.f);
    b.add("halberd radar dish spine", -250, 94, 27.f, 13.8f, .4f, .4f, F::Steel, false, 34.f);

    // --- Support buildings -------------------------------------------------
    b.add("halberd ops block", 130, 40, 0, 54, 9.5f, 22, F::Concrete, true);
    b.add("halberd ops roof", 130, 40, 9.5f, 55.6f, .7f, 23.6f, F::DarkRoof);
    for (int i = 0; i < 7; ++i)
        b.add("halberd ops window", 105.f + static_cast<float>(i) * 8.f, 28.9f, 3.2f,
              4.4f, 2.2f, .3f, F::Glass);
    for (int block = 0; block < 2; ++block) {
        const float x = 300.f + static_cast<float>(block) * 130.f;
        b.add("halberd barracks", x, 92, 0, 76, 8, 17, F::WarmWall, true);
        b.add("halberd barracks roof", x, 92, 8.f, 77.6f, .7f, 18.6f, F::DarkRoof);
        for (int i = 0; i < 10; ++i)
            for (float side : {-1.f, 1.f})
                b.add("halberd barracks window", x - 34.f + static_cast<float>(i) * 7.6f,
                      92.f + side * 8.7f, 2.6f, 2.6f, 1.9f, .25f, F::Glass);
        b.add("halberd barracks door", x - 38.f, 83.3f, 0, 1.9f, 2.4f, .3f, F::TealDoor);
        b.pave("halberd barracks walk", x, 79, 76, 6, F::Concrete, HalberdLayer::Hard);
    }
    // Parade ground and flag, on the station road between the magazines and the
    // barracks. Somewhere to stand people, which is the one thing a base has
    // that a factory does not.
    b.pave("halberd parade ground", 15, 92, 130, 30, F::Asphalt, HalberdLayer::Sealed);
    b.add("halberd flag pole", 88, 92, 0, .35f, 15, .35f, F::White, true);
    b.add("halberd flag", 90.4f, 92, 12.2f, 4.4f, 2.6f, .1f, F::RedTrim);

    // Motor pool: hardstanding, two vehicle sheds and a fuel point.
    // South of the station road, not across it: both are sealed asphalt and
    // share a layer.
    b.pave("halberd motor pool", -400, 92, 190, 38, F::Asphalt, HalberdLayer::Sealed);
    for (int shed = 0; shed < 2; ++shed) {
        const float x = -455.f + static_cast<float>(shed) * 110.f;
        b.add("halberd vehicle shed", x, 96, 0, 78, 5.6f, 20, F::Steel, true);
        b.add("halberd vehicle shed roof", x, 96, 5.6f, 80, .5f, 21.6f, F::DarkRoof);
        for (int bay = 0; bay < 5; ++bay)
            b.add("halberd shed pillar", x - 31.f + static_cast<float>(bay) * 15.5f, 86.2f,
                  0, .5f, 5.6f, .5f, F::Steel, true);
    }
    for (int i = 0; i < 9; ++i)
        b.paint("halberd parking bay", -470.f + static_cast<float>(i) * 20.f, 81, .3f, 20, F::White);

    // Fuel farm, bunded, at the west end clear of everything else.
    b.pave("halberd fuel compound", -470, 30, 96, 62, F::Concrete, HalberdLayer::Hard);
    for (float side : {-1.f, 1.f}) {
        b.add("halberd bund wall", -470.f + side * 47.f, 30, 0, 2.f, 2.2f, 62, F::Concrete, true);
        b.add("halberd bund wall", -470, 30.f + side * 30.f, 0, 96, 2.2f, 2.f, F::Concrete, true);
    }
    for (int i = 0; i < 3; ++i)
        halberd_fuel_tank(b, -505.f + static_cast<float>(i) * 35.f, 30, 8.f, 12.f);
    b.add("halberd pump house", -470, 51, 0, 10, 4, 7, F::Concrete, true);

    // Magazines, dug into the south-west corner well away from the fuel.
    for (int i = 0; i < 3; ++i) halberd_magazine(b, -180.f + static_cast<float>(i) * 46.f, 92.f);

    // --- Revetments --------------------------------------------------------
    for (int i = 0; i < 3; ++i) halberd_revetment(b, 280.f + static_cast<float>(i) * 100.f, 26.f);

    // --- Main gate ---------------------------------------------------------
    // Guardhouse, barrier, chicane blocks and a sign. This is the only opening
    // in the wire, so it is also the only part of the station most players will
    // ever look at closely.
    b.add("halberd guardhouse", kHalberdGateX + 12.f, 104, 0, 8, 3.6f, 7, F::Concrete, true);
    b.add("halberd guardhouse roof", kHalberdGateX + 12.f, 104, 3.6f, 10.4f, .5f, 9.4f, F::DarkRoof);
    b.add("halberd guardhouse window", kHalberdGateX + 8.f, 104, 1.5f, .3f, 1.5f, 4.4f, F::Glass);
    b.add("halberd gate light lens", kHalberdGateX + 12.f, 104, 4.f, 1.f, .16f, 1.f, F::White);
    for (float side : {-1.f, 1.f}) {
        b.add("halberd gate pier", kHalberdGateX + side * 7.6f, 112, 0, 1.4f, 4.4f, 1.4f,
              F::Concrete, true);
        b.add("halberd gate lamp", kHalberdGateX + side * 7.6f, 112, 4.4f, .8f, .7f, .8f, F::Steel);
    }
    b.add("halberd gate beam", kHalberdGateX, 112, 4.4f, 16.6f, 1.1f, 1.f, F::Concrete);
    b.add("halberd gate sign", kHalberdGateX, 111.4f, 5.5f, 11, 2.4f, .3f, F::DarkRoof);
    // The barrier is up. The gate is a decision the player gets to make, not a
    // wall with a guardhouse next to it.
    b.add("halberd barrier post", kHalberdGateX - 8.6f, 106, 0, .5f, 1.4f, .5f, F::Steel, true);
    b.add("halberd barrier arm", kHalberdGateX - 8.6f, 106, 1.4f, .3f, 5.4f, .3f, F::RedTrim,
          false, 0, 0, 6.f);
    // Blocks in facing PAIRS, not staggered. A slalom is what a real entry
    // does and it is also a wall to anything that cannot thread it: the design
    // here is a gate you can drive through, so the blocks narrow the throat to
    // 7.4 m and leave the line straight.
    for (int i = 0; i < 3; ++i)
        for (float side : {-1.f, 1.f})
            b.add("halberd chicane block", kHalberdGateX + side * 5.f,
                  100.f - static_cast<float>(i) * 8.f, 0, 2.6f, 1.1f, 1.6f, F::Concrete, true);

    // --- The wire ----------------------------------------------------------
    halberd_fence_run(b, -kHalberdFenceHalfX, kHalberdFenceNorth,
                      kHalberdFenceHalfX, kHalberdFenceNorth);
    halberd_fence_run(b, kHalberdFenceHalfX, kHalberdFenceNorth,
                      kHalberdFenceHalfX, kHalberdFenceSouth);
    halberd_fence_run(b, kHalberdFenceHalfX, kHalberdFenceSouth,
                      -kHalberdFenceHalfX, kHalberdFenceSouth,
                      kHalberdGateX, kHalberdGateHalfWidth);
    halberd_fence_run(b, -kHalberdFenceHalfX, kHalberdFenceSouth,
                      -kHalberdFenceHalfX, kHalberdFenceNorth);
    return out;
}

// Surfaces a vehicle drives on. The runway is in here and in no road table:
// it carries ground collision so the player can use it, and no lane so traffic
// can never route down it.
//
// Exact names, not prefixes. "halberd runway" as a prefix also catches the
// centreline, the threshold bars and the edge lights, and painting a ground
// rect under every stripe puts 40 overlapping collision planes on the strip.
inline bool halberd_ground_piece(const StartPart& p) {
    if (!p.name) return false;
    static const char* kSurfaces[] = {
        "halberd runway", "halberd taxiway",
        "halberd taxiway link", "halberd apron", "halberd east apron",
        "halberd gate apron", "halberd motor pool", "halberd parade ground",
        "halberd fuel compound", "halberd revetment hardstanding",
        "halberd support apron", "halberd station road", "halberd barracks walk",
        "halberd magazine apron"};
    for (const char* name : kSurfaces)
        if (std::strcmp(p.name, name) == 0) return true;
    return false;
}
} // namespace apricot::city
