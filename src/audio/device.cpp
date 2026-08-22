#include "audio/device.h"

#include "miniaudio.h"

#include "core/log.h"

namespace apricot {

AudioDevice::~AudioDevice() { stop(); }

bool AudioDevice::start() {
    // NOT IMPLEMENTED — see device.h. The ma_version_string() call is not
    // decoration: it forces a real symbol reference into the backend, so this
    // module's link against miniaudio_impl.c is proven by the build rather
    // than assumed. A header-only include would compile happily and then fail
    // at link time for whoever writes the real implementation.
    AP_WARN("audio device not implemented yet; running silent (backend %s)",
            ma_version_string());
    running_ = false;
    return false;
}

void AudioDevice::stop() {
    if (!running_) return;
    running_ = false;
}

}  // namespace apricot
