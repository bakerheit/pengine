// Wind-blown snow under an open-sided roof (a fuel canopy, a bus shelter).
//
// THIS FILE IS COMPILED TWICE: lit.frag #includes it for the drawn snow, and
// physics/snow_shelter.h #includes it as C++ for tyre grip and snow depth. One
// source, so the snow the car drives on is the snow the player sees. Keep it in
// the common subset of GLSL 3.30 and C++17: float literals carry an `f`, and
// only clamp/max, which both sides provide.
//
// Snow blows in from the open edge and thins toward the middle. How far it
// reaches scales with how high the roof underside is above the point: a tall
// canopy lets drift well in, a low carport barely at all. Past that reach an
// open roof still lets a faint dusting settle. Enclosed roofs never call this;
// their interiors stay bare.
const float kSnowDriftReachPerMetre = 0.60f;  // metres of reach per metre of headroom
const float kSnowDriftMaxReachM = 4.00f;      // tallest canopies stop here
const float kSnowDriftDusting = 0.05f;        // deep under an open roof

// edge_distance_m: horizontal distance in from the nearest roof edge.
// clearance_m: roof underside height above the point.
// Returns the fraction of the open-ground snow cover that reaches the point.
// The C++ side defines SNOW_DRIFT_LINKAGE as `inline` (a header function).
#ifndef SNOW_DRIFT_LINKAGE
#define SNOW_DRIFT_LINKAGE
#endif
SNOW_DRIFT_LINKAGE float snow_drift_exposure(float edge_distance_m, float clearance_m) {
    float reach = clamp(clearance_m * kSnowDriftReachPerMetre, 0.0f,
                        kSnowDriftMaxReachM);
    float t = reach > 0.0f ? clamp(edge_distance_m / reach, 0.0f, 1.0f) : 1.0f;
    return max((1.0f - t) * (1.0f - t), kSnowDriftDusting);
}
