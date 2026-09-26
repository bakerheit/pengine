#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace apricot {

// The snow a vehicle carries on its own panels, separate from the ground.
//
// The world shader used to shade vehicles from the ground field at each pixel,
// so a snowy car went bare the instant it rolled under a fuel canopy, and lost
// its roof snow driving along a freshly plowed street. Snow on a car is the
// car's: it settles while the roof is open to the sky and it is snowing, stays
// put under cover, blows partly off at speed, and melts.
//
// Pure and deterministic: no clock, no statics. The owner steps it at the
// fixed sim step with the roof's exposure (SnowShelterField::exposure) and the
// weather. Presentation reads a load in [0, 1], absolute visible cover on the
// same scale as Conditions::snow_cover.

// A roof is tested against the shelter field this far above the ground point
// a vehicle stands on: a car's roof, comfortably below any drive-under canopy.
inline constexpr float kVehicleSnowRoofAboveGroundM = 1.4f;

struct VehicleSnowWeather {
    float cover = 0.0f;     // [0, 1] visible cover on open ground now
    float snowfall = 0.0f;  // [0, 1] falling right now
    float heat = 0.0f;      // [0, 1] heatwave severity
};

struct VehicleSnowLoadTuning {
    // Full snowfall on a fully exposed roof: bare to fully covered in four
    // minutes. Settling never takes a vehicle past the open ground's cover
    // scaled by its exposure, so a car half under a canopy fills only halfway.
    float settle_per_s = 1.0f / 240.0f;
    // Dry and cold: a blanket lasts most of an hour. Snowfall pauses it.
    float melt_per_s = 1.0f / 2400.0f;
    // A full heatwave clears a car in a couple of minutes.
    float heat_melt_per_s = 1.0f / 150.0f;
    // A running engine warms the hood and slowly eats the load from below.
    float engine_melt_per_s = 1.0f / 1800.0f;
    // Airflow sheds loose snow above town speed, fastest at highway speed,
    // but only down to a packed floor: real cars keep a cap on the roof.
    float shed_start_mps = 9.0f;
    float shed_full_mps = 25.0f;
    float shed_per_s = 1.0f / 12.0f;
    float shed_floor = 0.35f;
};

namespace vehicle_snow_detail {
inline float unit(float value) {
    if (!(value > 0.0f)) return 0.0f;  // NaN and negatives
    return value < 1.0f ? value : 1.0f;
}
}  // namespace vehicle_snow_detail

// What a vehicle that has stood here through the current conditions carries:
// the ambient cover, less whatever its roof is sheltered from. Used to seed
// the player car at start and each traffic car as it appears, and as the
// whole state of a car that never moves.
inline float seed_vehicle_snow_load(float roof_exposure,
                                    const VehicleSnowWeather& weather) {
    using vehicle_snow_detail::unit;
    return unit(weather.cover) * unit(roof_exposure);
}

inline float step_vehicle_snow_load(float load, float roof_exposure,
                                    float speed_mps, bool engine_running,
                                    const VehicleSnowWeather& weather, float dt,
                                    const VehicleSnowLoadTuning& tuning = {}) {
    using vehicle_snow_detail::unit;
    load = unit(load);
    if (!(dt > 0.0f) || !std::isfinite(dt)) return load;
    const float exposure = unit(roof_exposure);
    const float snowfall = unit(weather.snowfall);
    const float cap = unit(weather.cover) * exposure;
    if (snowfall > 0.0f && load < cap)
        load = std::min(cap, load + tuning.settle_per_s * snowfall * exposure * dt);
    const float melt = (snowfall > 0.0f ? 0.0f : tuning.melt_per_s) +
                       unit(weather.heat) * tuning.heat_melt_per_s +
                       (engine_running ? tuning.engine_melt_per_s : 0.0f);
    load = std::max(0.0f, load - melt * dt);
    const float speed = std::isfinite(speed_mps) ? std::abs(speed_mps) : 0.0f;
    if (load > tuning.shed_floor && speed > tuning.shed_start_mps) {
        const float airflow = unit((speed - tuning.shed_start_mps) /
            std::max(tuning.shed_full_mps - tuning.shed_start_mps, 0.001f));
        const float fraction = std::min(1.0f, tuning.shed_per_s * airflow * dt);
        load -= (load - tuning.shed_floor) * fraction;
    }
    return unit(load);
}

// One moving vehicle this step, keyed by the crowd's stable identity.
struct VehicleSnowSample {
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    int64_t generation = 0;
    float roof_exposure = 1.0f;
    float speed_mps = 0.0f;
    bool engine_running = true;
};

// Loads for a population that appears and retires: the ambient traffic. A new
// identity is seeded; an identity that leaves is forgotten. Stepping visits
// each sample once, so the cost is one shelter query per car per sim step.
class VehicleSnowLoads {
public:
    void step(const std::vector<VehicleSnowSample>& samples,
              const VehicleSnowWeather& weather, float dt,
              const VehicleSnowLoadTuning& tuning = {}) {
        next_.clear();
        next_.reserve(samples.size());
        for (const VehicleSnowSample& sample : samples) {
            const Entry* old = find(sample.lane_key, sample.slot);
            Entry entry{sample.lane_key, sample.slot, sample.generation, 0.0f};
            if (old && old->generation == sample.generation) {
                entry.load = step_vehicle_snow_load(
                    old->load, sample.roof_exposure, sample.speed_mps,
                    sample.engine_running, weather, dt, tuning);
            } else {
                entry.load = seed_vehicle_snow_load(sample.roof_exposure, weather);
            }
            next_.push_back(entry);
        }
        // The crowd hands vehicles over sorted on identity; do not rely on it.
        if (!std::is_sorted(next_.begin(), next_.end(), less))
            std::sort(next_.begin(), next_.end(), less);
        entries_.swap(next_);
    }

    // Negative when this departure is unknown (not yet stepped).
    float load(uint64_t lane_key, uint32_t slot, int64_t generation) const {
        const Entry* entry = find(lane_key, slot);
        return entry && entry->generation == generation ? entry->load : -1.0f;
    }

    std::size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); next_.clear(); }

private:
    struct Entry {
        uint64_t lane_key;
        uint32_t slot;
        int64_t generation;
        float load;
    };
    static bool less(const Entry& a, const Entry& b) {
        return a.lane_key != b.lane_key ? a.lane_key < b.lane_key : a.slot < b.slot;
    }
    const Entry* find(uint64_t lane_key, uint32_t slot) const {
        const Entry probe{lane_key, slot, 0, 0.0f};
        const auto it = std::lower_bound(entries_.begin(), entries_.end(), probe, less);
        return it != entries_.end() && it->lane_key == lane_key && it->slot == slot
            ? &*it : nullptr;
    }

    std::vector<Entry> entries_;
    std::vector<Entry> next_;
};

}  // namespace apricot
