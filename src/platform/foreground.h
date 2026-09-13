#pragma once

namespace apricot::platform {

// Take this process OUT of the desktop's foreground, permanently, for the life
// of the run. No Dock icon, no menu bar, and — the part that matters — it can
// never become the active application, so it cannot take the keyboard from
// whatever the person at this machine is actually doing.
//
// WHY THIS IS NOT AN SDL HINT, because two of them look like it and neither
// does the job on macOS:
//
//   * SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN is WINDOWS ONLY in SDL2. Grep
//     the SDL tree: the only reader is SDL_windowswindow.c. On macOS
//     Cocoa_ShowWindow calls -makeKeyAndOrderFront: unconditionally.
//   * SDL_HINT_MAC_BACKGROUND_APP does half of it. It stops SDL forcing
//     NSApplicationActivationPolicyRegular and stops its
//     -activateIgnoringOtherApps:, but it never sets Accessory — and an
//     unbundled binary is Regular by default, so "SDL did not activate me" is
//     not the same as "I am not a foreground app". Measured: with that hint
//     set and the window created hidden, `lsappinfo front` during a scripted
//     check still reported `apricot`.
//
// So the last step has to be an explicit activation-policy call, which is
// Cocoa, which is why this is a two-implementation header. Call it AFTER
// SDL_Init has brought NSApp into existence and BEFORE the first window.
//
// Non-Apple platforms get the no-op: nothing else this engine runs on has an
// application-level foreground to be taken out of.
void keep_out_of_foreground();

}  // namespace apricot::platform
