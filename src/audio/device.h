#pragma once

#include "audio/mixer.h"

namespace apricot {

// The audio output device — the ONLY part of the audio module that touches
// hardware, which is exactly why it is the only part that lives in
// apricot_host. Gain maths (mixer.h) and waveform generation (synth.h) sit in
// the sim library and are tested with no device present.
//
// The playback callback runs on an OS-owned audio thread. Anything it reads
// must be safe to read there: no allocation, no locks, no logging.
class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Open the default output device and begin playback.
    //
    // NOT IMPLEMENTED YET. Currently logs the backend version and returns
    // false — it deliberately does not half-open a device. A caller must treat
    // false as "no audio this session" and carry on; audio is never load-
    // bearing for the app starting.
    // TODO(audio device ticket): open an ma_device, run the mixer in the
    // callback, and handle device-lost by reopening.
    bool start();

    void stop();
    bool running() const { return running_; }

    Mixer& mixer() { return mixer_; }
    const Mixer& mixer() const { return mixer_; }

private:
    Mixer mixer_;
    bool running_ = false;
};

}  // namespace apricot
