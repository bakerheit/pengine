#include "city/miandi_presentation.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    city::BuildingPiece p{"hotel wall", {}, 0.f, .3f, 18.f, 50.f,
                          city::BuildingFinish::WarmWall, true};
    const auto hotel = city::miandi_presentation(p, true);
    const auto context = city::miandi_presentation(p, false);
    REQUIRE(hotel.surface == city::MiandiSurface::Stucco);
    REQUIRE(hotel.tint != context.tint);
    REQUIRE_NEAR(hotel.u_tiles, 50.f / 2.4f, 1e-5f);
    REQUIRE_NEAR(hotel.v_tiles, 18.f / 2.4f, 1e-5f);
    REQUIRE(!city::miandi_ground_piece(p));
    for (int f = 0; f <= static_cast<int>(city::BuildingFinish::PoolWater); ++f) {
        p.finish = static_cast<city::BuildingFinish>(f);
        for (bool ocean : {false, true}) {
            const auto style = city::miandi_presentation(p, ocean);
            REQUIRE(style.tint[3] == 1.f);
            for (float c : style.tint) REQUIRE(c >= 0.f && c <= 1.f);
        }
    }
    p = {"miandi resort terrace paving", {}, .12f, 12.f, .08f, 24.f,
         city::BuildingFinish::Concrete, false};
    REQUIRE(city::miandi_ground_piece(p));
    const auto paving = city::miandi_presentation(p, true);
    REQUIRE_NEAR(paving.u_tiles, 5.f, 1e-5f);
    REQUIRE_NEAR(paving.v_tiles, 10.f, 1e-5f);
    p.bottom_m = 3.f;
    REQUIRE(!city::miandi_ground_piece(p));
    p.bottom_m = .12f;
    p.finish = city::BuildingFinish::PoolWater;
    REQUIRE(!city::miandi_ground_piece(p));
    apricot_test::pass("Miandi physical-scale materials and safe paving support");
    const std::array<float, 4> neutral{.5f, .5f, .5f, 1.f};
    for (const auto& entry : {
             std::pair{"Bellmar wall", city::BuildingFinish::WarmWall},
             std::pair{"Maravelle wall", city::BuildingFinish::White},
             std::pair{"Palmera wall", city::BuildingFinish::WarmWall},
             std::pair{"Club Candela wall", city::BuildingFinish::WarmWall},
             std::pair{"Tropico Ballroom wall", city::BuildingFinish::Yellow},
             std::pair{"Mirage window", city::BuildingFinish::Glass}}) {
        p.name = entry.first;
        p.finish = entry.second;
        const auto tint = city::miandi_venue_tint(p, neutral);
        REQUIRE(tint != neutral && tint[3] == 1.f);
        for (float c : tint) REQUIRE(c >= 0.f && c <= 1.f);
    }
    REQUIRE(city::miandi_venue_tint(p, neutral)[2] < .2f); // smoked Mirage glass
    p.name = "unrelated hotel window";
    REQUIRE(city::miandi_venue_tint(p, neutral) == neutral);
    p.name = "miandi neon amber Candela lettering";
    p.finish = city::BuildingFinish::WarmWall;
    REQUIRE(city::miandi_venue_tint(p, neutral) == neutral);
    apricot_test::pass("venue palettes are distinct, non-emissive and do not affect unrelated sites or neon");
    return 0;
}
