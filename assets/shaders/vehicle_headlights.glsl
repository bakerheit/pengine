// Native-space cutouts preserve the source UVs and exactly the same deformed
// body triangles. No independently bent cards or bright bezel-shaped boxes.
bool headlight_region(vec3 p, vec4 xy, vec2 z, bool round_lens) {
    if (p.z < z.x || p.z > z.y || any(lessThan(p.xy, xy.xy)) ||
        any(greaterThan(p.xy, xy.zw))) return false;
    vec2 q = (2.0*p.xy-xy.xy-xy.zw)/(xy.zw-xy.xy);
    return !round_lens || dot(q,q) <= 1.0;
}

bool vehicle_headlight_contains(vec3 p, int profile) {
    p.x = abs(p.x);
#define HEADLIGHT_MODEL(name, number) if (profile == number) {
#define HEADLIGHT_RECT(a,b,c,d,e,f) if (headlight_region(p,vec4(a,b,c,d),vec2(e,f),false)) return true;
#define HEADLIGHT_ROUND(a,b,c,d,e,f) if (headlight_region(p,vec4(a,b,c,d),vec2(e,f),true)) return true;
#define HEADLIGHT_END return false; }
#include "vehicle_headlight_profiles.inc"
#undef HEADLIGHT_MODEL
#undef HEADLIGHT_RECT
#undef HEADLIGHT_ROUND
#undef HEADLIGHT_END
    return false;
}

bool vehicle_brakelight_contains(vec3 p, int profile) {
    p.x = abs(p.x);
#define BRAKELIGHT_MODEL(name, number) if (profile == number) {
#define BRAKELIGHT_RECT(a,b,c,d,e,f) if (headlight_region(p,vec4(a,b,c,d),vec2(e,f),false)) return true;
#define BRAKELIGHT_ROUND(a,b,c,d,e,f) if (headlight_region(p,vec4(a,b,c,d),vec2(e,f),true)) return true;
#define BRAKELIGHT_END return false; }
#include "vehicle_brakelight_profiles.inc"
#undef BRAKELIGHT_MODEL
#undef BRAKELIGHT_RECT
#undef BRAKELIGHT_ROUND
#undef BRAKELIGHT_END
    return false;
}
