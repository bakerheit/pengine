#include "app/app.h"

#include "app/mistral_soft_top.h"
#include "core/log.h"

namespace apricot {

// `--convertible-check`: does J actually fold the Mistral's canvas, in the real
// renderer, on the real key path? The suite in tests/mistral_soft_top_tests.cpp
// pins the maths; this is the half a headless suite cannot answer — that the
// key reaches the canvas, and that the three poses are worth looking at.
//
// The captures are keyed off the canvas's own fraction rather than off frame
// numbers, so an image can never claim a pose the top was not in.
void App::capture_convertible_check() {
    if (!is_convertible(car_visual_.active_car())) {
        if (!convertible_check_failed_) {
            AP_ERROR("convertible check: the player is not in a convertible");
            convertible_check_failed_ = true;
        }
        return;
    }
    if (screenshot_path_.empty()) return;
    const float stowed = soft_top_.stowed;
    const unsigned bit =
        (stowed <= 0.f && soft_top_.target <= 0.f) ? 1u :
        (stowed > .35f && stowed < .65f) ? 2u :
        (stowed >= 1.f && soft_top_.target >= 1.f && frames_rendered_ > 460) ? 4u : 0u;
    if (!bit || (convertible_check_captures_ & bit)) return;
    if (save_screenshot(screenshot_path_ + (bit == 1u ? ".top-up.bmp" :
            bit == 2u ? ".folding.bmp" : ".top-down.bmp"))) {
        convertible_check_captures_ |= bit;
        AP_INFO("convertible check captured the canvas at %.2f stowed",
                static_cast<double>(stowed));
    }
}

}  // namespace apricot
