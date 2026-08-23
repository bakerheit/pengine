#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/replay.h"

namespace apricot {

// The best lap on this world, and the ghost that drove it.
//
// Kept as ONE record rather than a time here and a tape over there, because
// the two are only meaningful together: a ghost from a different lap than the
// time it is shown against is a lie the player cannot see through.
struct BestLap {
    bool valid = false;
    uint64_t seed = 0;

    double lap_time = 0.0;

    // splits[i] is the lap clock when gate i was crossed, so splits[0] is
    // always 0.0 (the start line) and splits.size() == the route's gate count.
    std::vector<double> splits;

    ReplayTape tape;
};

// On-disk format version. Independent of kReplayTapeVersion: the container can
// gain a field without the tape's meaning changing, and vice versa.
//
// 2: the saved car carries VehicleState::shift_timer and recovery_timer and
//    each wheel's angular_velocity, normal_force and slip. Version 1 wrote the
//    fields that existed before PENG-7 and silently zeroed the rest on load,
//    which restarted a ghost with four locked wheels. Version 1 files are
//    refused rather than reinterpreted — the lap they hold was recorded
//    against a start state this build cannot reconstruct.
inline constexpr uint32_t kBestLapFileVersion = 2;

// Filename for a world's best lap. The DIRECTORY is the caller's business —
// this is save data, not an asset, and core/asset_root.h is for things shipped
// with the build. The host layer joins this onto wherever it keeps user files.
std::string best_lap_filename(uint64_t seed);

// Write the record. Returns false if the file could not be written; the caller
// gets to decide whether that is worth a message, but it is never silently a
// success.
bool save_best_lap(const std::string& path, const BestLap& record);

// Read a record back and hand it over ONLY if it belongs to `seed`. A best lap
// from another world would load as a plausible time attached to a ghost that
// drives into the sea.
//
// Returns false — leaving `out` reset, not half-filled — for a missing file, a
// bad magic, a version we do not understand, a seed mismatch, or a truncated
// tape. Every one of those is a refusal, never a partial load.
bool load_best_lap(const std::string& path, uint64_t seed, BestLap& out);

}  // namespace apricot
