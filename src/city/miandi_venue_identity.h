#pragma once

namespace apricot::city {

// Display names and sign copy are independent of the stable parcel/code IDs.
// Keep these original venue identities consistent across plans and signage.
struct MiandiVenueIdentity {
    const char* name;
    const char* sign;
    const char* direction;
};

inline constexpr MiandiVenueIdentity kBellmarIdentity{
    "The Bellmar", "THE BELLMAR", "Pink Deco; old money and faded glamour"};
inline constexpr MiandiVenueIdentity kMaravelleIdentity{
    "The Maravelle", "THE MARAVELLE", "Elegant beachfront hotel; pale walls and cyan neon"};
inline constexpr MiandiVenueIdentity kPalmeraIdentity{
    "The Palmera", "THE PALMERA", "Grand resort; buttercream, pool terrace and cabana bar"};
inline constexpr MiandiVenueIdentity kMirageIdentity{
    "Club Mirage", "CLUB MIRAGE", "Smoked windows, bright neon and a late-night warehouse club"};
inline constexpr MiandiVenueIdentity kCandelaIdentity{
    "Club Candela", "CLUB CANDELA", "Warm neighborhood Latin nightlife and a domino patio"};
inline constexpr MiandiVenueIdentity kTropicoIdentity{
    "Tropico Ballroom", "TROPICO BALLROOM", "Older dance hall; cream trim and a broad marquee"};

}  // namespace apricot::city
