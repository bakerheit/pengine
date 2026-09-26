// Documentation benchmark: default CLASSIC GTA preset, flat dry ground,
// stationary full-throttle launch, automatic gearbox, no traffic or damage.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "app/vehicle_model_tuning.h"
#include "physics/terrain_collider.h"

int main() {
    using namespace apricot;
    constexpr float dt = 1.f / 120.f;
    constexpr float mph_to_mps = .44704f;
    constexpr float target = 60.f * mph_to_mps;
    constexpr int window_steps = 5 * 120;
    std::puts("brand,model,mesh,texture,top_speed_mph,zero_to_60_s,peak_speed_mph,run_seconds,settled");
    for (const auto& definition : kPlayerCars) {
        TerrainCollider ground(0xC4A5u);
        ground.add_static_ground_rect({0.f, 0.f}, 200.f, {60000.f, 60000.f},
                                      0.f, Surface::Rock);
        const auto tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta,
                                                definition.id);
        auto car = spawn_vehicle(tuning, ground, 0.f, 0.f, 0.f);
        car.position.y = 200.f + static_ride_height(tuning);
        InputFrame input;
        input.brake = 1.f;
        for (int i = 0; i < 2 * 120; ++i)
            car = step_vehicle(car, tuning, input, ground, dt);
        input.brake = 0.f;
        input.throttle = 1.f;
        float previous_speed = std::max(0.f, vehicle_forward_speed(car));
        float zero_to_60 = std::numeric_limits<float>::infinity();
        float peak = 0.f, sum = 0.f, previous_mean = 0.f, mean = 0.f;
        int stable_windows = 0, steps = 0;
        bool settled = false;
        for (int i = 0; i < 180 * 120; ++i) {
            car = step_vehicle(car, tuning, input, ground, dt);
            const float forward_speed = vehicle_forward_speed(car);
            if (!std::isfinite(forward_speed) || !std::isfinite(car.position.y)) {
                std::fprintf(stderr, "Non-finite simulation: %s %s\n",
                             definition.brand, definition.model);
                return 1;
            }
            const float speed = std::max(0.f, forward_speed);
            peak = std::max(peak, speed);
            if (!std::isfinite(zero_to_60) && speed >= target) {
                const float fraction = (target - previous_speed) /
                                       std::max(.000001f, speed - previous_speed);
                zero_to_60 = (static_cast<float>(i) + fraction) * dt;
            }
            previous_speed = speed;
            sum += speed;
            steps = i + 1;
            if (steps % window_steps != 0) continue;
            mean = sum / static_cast<float>(window_steps);
            sum = 0.f;
            stable_windows = steps >= 30 * 120 && mean > .1f &&
                             std::abs(mean - previous_mean) < .02f
                                 ? stable_windows + 1 : 0;
            previous_mean = mean;
            if (stable_windows >= 3) { settled = true; break; }
        }
        std::printf("%s,%s,%s,%s,", definition.brand, definition.model,
                    definition.mesh_path, definition.texture_path);
        if (settled) std::printf("%.3f", static_cast<double>(mean / mph_to_mps));
        std::printf(",");
        if (std::isfinite(zero_to_60)) std::printf("%.4f", static_cast<double>(zero_to_60));
        std::printf(",%.3f,%.1f,%s\n", static_cast<double>(peak / mph_to_mps),
                    static_cast<double>(static_cast<float>(steps) * dt),
                    settled ? "true" : "false");
    }
}
