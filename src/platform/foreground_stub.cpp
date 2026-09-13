#include "platform/foreground.h"

namespace apricot::platform {

// Every non-Apple target. X11 and Windows both hand focus to a window rather
// than to an application, so creating the window hidden — which the caller
// already does — is the whole of the fix there and there is nothing left for
// this to do.
void keep_out_of_foreground() {}

}  // namespace apricot::platform
