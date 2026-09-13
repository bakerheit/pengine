#include "platform/foreground.h"

#import <AppKit/AppKit.h>

#include "core/log.h"

namespace apricot::platform {

void keep_out_of_foreground() {
    // NSApp is created by SDL's video init, so this is only ever called after
    // it. A null NSApp means the caller got the order wrong; say so rather
    // than silently doing nothing, because the symptom of doing nothing is a
    // window that takes somebody's keyboard and no clue why.
    if (NSApp == nil) {
        AP_WARN("foreground: no NSApplication yet; call after SDL video init");
        return;
    }
    // Accessory, not Prohibited. Prohibited forbids windows outright, which
    // would take the GL drawable with it; Accessory keeps the process able to
    // own a window and a GL context while never becoming the active app.
    if (![NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]) {
        AP_WARN("foreground: macOS refused the accessory activation policy");
    }
}

}  // namespace apricot::platform
