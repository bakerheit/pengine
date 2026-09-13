#pragma once

#include <cstring>
#include "city/start_area.h"

namespace apricot::city {

// Rendering and ground support must agree about which airport pieces are
// paving. These are authored slab tops, not the terrain underneath them.
inline bool is_airport_paving(const city::StartPart& part) {
    const auto part_name_has=[](const city::StartPart& p,const char* text) { return std::strstr(p.name,text)!=nullptr; };
    const auto part_name_is=[](const city::StartPart& p,const char* text) { return std::strcmp(p.name,text)==0; };
    const bool runway_surface =
        part_name_has(part, "runway ") &&
        (part_name_has(part, "09-27") || part_name_has(part, "08-26") ||
         part_name_has(part, "shoulder"));
    return runway_surface ||
           part_name_has(part, "airport apron") ||
           part_name_has(part, "passenger apron") ||
           part_name_has(part, "maintenance apron") ||
           part_name_has(part, "taxiway alpha") ||
           part_name_has(part, "taxiway bravo") ||
           part_name_has(part, "dropoff road") ||
           part_name_has(part, "public parking") ||
           part_name_has(part, "airport access road") ||
           (part_name_has(part,"terminal parking") && part.finish==city::StartFinish::Asphalt) ||
           part_name_has(part, "hangar apron") ||
           part_name_is(part, "airport fire response apron") ||
           part_name_is(part, "airport hotel lot") ||
           part_name_is(part, "rental car lot") ||
           part_name_is(part, "air cargo yard");
}

inline bool is_airport_landside_paving(const city::StartPart& part) {
    const auto part_name_has=[](const city::StartPart& p,const char* text) { return std::strstr(p.name,text)!=nullptr; };
    const auto part_name_is=[](const city::StartPart& p,const char* text) { return std::strcmp(p.name,text)==0; };
    return part_name_is(part,"airport frontage pedestrian connector") ||
           part_name_has(part,"terminal parking walk") ||
           part_name_has(part,"airport taxi pedestrian") ||
           part_name_has(part,"airport taxi shelter approach") ||
           (part_name_has(part,"airport hotel ") && (part_name_has(part,"walk") || part_name_has(part,"approach"))) ||
           part_name_is(part,"rental entrance walk") ||
           part_name_has(part, "terminal pedestrian plaza") ||
           part_name_has(part, "terminal splitter island") ||
           part_name_has(part, "terminal parking island") ||
           part_name_has(part, "terminal pedestrian connector") ||
           part_name_is(part, "airport hotel courtyard") ||
           part_name_is(part, "airport hotel service court") ||
           part_name_is(part, "rental forecourt") ||
           part_name_is(part, "airport security lane island");
}

inline bool airport_ground_piece(const StartPart& part) {
    return !part.solid && part.pitch_deg==0.f && part.roll_deg==0.f &&
        (part.finish==StartFinish::Concrete || part.finish==StartFinish::Asphalt) &&
        (is_airport_paving(part) || is_airport_landside_paving(part));
}

} // namespace apricot::city
