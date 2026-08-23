#include "audio/device.h"

#include "miniaudio.h"

#include <atomic>
#include <cstddef>

#include "core/log.h"

namespace apricot {

// THE AUDIO THREAD STARTS HERE AND THE RULES CHANGE.
//
// Everything below data_callback() runs on a thread the OS owns, on a deadline
// measured in a few milliseconds, with no way to report a problem. On that
// thread there is no malloc, no free, no mutex, no logging, no file IO, no
// exception, and no call into anything that might do one of those on your
// behalf. A callback that misses its deadline does not slow down — it drops
// out, and a dropout is louder than the sound it interrupted.
//
// The entire discipline is discharged in one place: VoiceMixer::render() drains
// a lock-free ring and then touches only its own fixed arrays. This file's job
// is to hand it a buffer and stay out of the way.

// The backend gives a device exactly ONE user-data pointer, and two different
// callbacks need to reach two different things through it. So the pointer is
// this struct, and the mixer hangs off it — one extra dereference on the audio
// thread, which is free, versus a global, which is not.
struct AudioDevice::Impl {
    ma_device device{};
    VoiceMixer* mixer = nullptr;
    bool device_ok = false;

    // Set from the backend's notification callback, which may run on the audio
    // thread. Read from the sim thread. Atomic because it genuinely is shared,
    // and relaxed because nothing is ordered against it.
    std::atomic<bool> stopped_unexpectedly{false};
};

namespace {

// Pulled by the backend whenever it wants more audio.
void data_callback(ma_device* device, void* output, const void* input,
                   ma_uint32 frame_count) {
    (void)input;  // playback only; there is no capture path and never will be
    if (!device || !output) return;
    auto* impl = static_cast<AudioDevice::Impl*>(device->pUserData);
    if (!impl || !impl->mixer) return;
    impl->mixer->render(static_cast<float*>(output),
                        static_cast<std::size_t>(frame_count));
}

// Device lifecycle events. Also potentially on the audio thread, so it does
// nothing but set a flag — AP_WARN takes a FILE* and can block on a full pipe,
// which is exactly the kind of thing that turns a warning into a dropout.
void notification_callback(const ma_device_notification* notification) {
    if (!notification || !notification->pDevice) return;
    if (notification->type != ma_device_notification_type_stopped) return;
    auto* impl =
        static_cast<AudioDevice::Impl*>(notification->pDevice->pUserData);
    if (impl) impl->stopped_unexpectedly.store(true, std::memory_order_relaxed);
}

}  // namespace

AudioDevice::~AudioDevice() { stop(); }

bool AudioDevice::start() {
    if (running_) return true;

    Impl* impl = new Impl{};
    impl->mixer = &mixer_;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    // f32 because the mixer works in float and a format conversion in the
    // callback would be work done on the worst possible thread. Stereo because
    // the pan law in mixer.h is a stereo pan law.
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    // 0 means "whatever the hardware natively runs at". Asking for 48000 on a
    // device running at 44100 makes the backend insert a resampler in the
    // callback path; we already resample per voice, so we take the device's
    // rate and retune ourselves instead of paying for it twice.
    config.sampleRate = 0;
    config.dataCallback = data_callback;
    config.notificationCallback = notification_callback;
    config.pUserData = impl;

    if (ma_device_init(nullptr, &config, &impl->device) != MA_SUCCESS) {
        // The expected outcome on a CI runner, in a container, or on a machine
        // with nothing plugged in. Not an error, and deliberately not fatal.
        AP_WARN("audio: no output device available (backend %s); "
                "running silent", ma_version_string());
        delete impl;
        mixer_.set_silent(true);
        running_ = false;
        return false;
    }
    impl->device_ok = true;

    sample_rate_ = impl->device.sampleRate;
    AP_INFO("audio: device open, %u Hz, %u channels (backend %s)",
            sample_rate_, impl->device.playback.channels, ma_version_string());

    // ORDER MATTERS FOR THE NEXT THREE STEPS and getting it wrong is a race
    // that only shows up on someone else's machine:
    //   1. prepare the mixer for the device's rate,
    //   2. synthesise the bank,
    //   3. only then let the device start pulling.
    // ma_device_start() can fire the callback before it returns.
    mixer_.prepare(sample_rate_);
    mixer_.set_silent(false);
    mixer_.stop_all();

    bank_ = synth_bank(sample_rate_);
    AP_INFO("audio: bank synthesised — %zu engine layers, %zu surfaces, "
            "%.2f s of PCM, zero files read",
            kEngineLayerCount, kSurfaceCount,
            static_cast<double>(bank_total_seconds_()));

    if (ma_device_start(&impl->device) != MA_SUCCESS) {
        AP_WARN("audio: device opened but would not start; running silent");
        ma_device_uninit(&impl->device);
        delete impl;
        mixer_.set_silent(true);
        bank_ = SfxBank{};
        sample_rate_ = 0;
        running_ = false;
        return false;
    }

    impl_ = impl;
    running_ = true;
    return true;
}

void AudioDevice::stop() {
    if (!impl_) {
        running_ = false;
        mixer_.set_silent(true);
        return;
    }

    if (impl_->stopped_unexpectedly.load(std::memory_order_relaxed)) {
        // Logged here rather than in the notification callback, because this is
        // the sim thread and logging is allowed again.
        //
        // NOT IMPLEMENTED: reopening after a device loss (headphones unplugged,
        // interface removed). Doing it properly needs a per-frame poll from the
        // app layer, which nothing calls yet. Until then the session goes
        // silent and says so, which is at least not a lie.
        // TODO(audio device reopen): add AudioDevice::poll() and call it from
        // the frame loop.
        AP_WARN("audio: device stopped unexpectedly; the session is silent "
                "from here (reopen is not implemented)");
    }

    // uninit stops the device and JOINS the audio thread. After it returns
    // nothing is reading the bank's clips, which is the only reason it is safe
    // for bank_ to be destroyed after this.
    if (impl_->device_ok) ma_device_uninit(&impl_->device);
    delete impl_;
    impl_ = nullptr;

    mixer_.stop_all();
    mixer_.set_silent(true);
    running_ = false;
    sample_rate_ = 0;
}

// Total PCM generated, for the one startup log line that proves the bank is
// real. Cheap, runs once, and it is the number you actually want to see when
// somebody asks how much memory the "no audio assets" engine spends on audio.
float AudioDevice::bank_total_seconds_() const {
    float total = 0.0f;
    for (std::size_t i = 0; i < kEngineLayerCount; ++i) {
        total += bank_.engine_power[i].duration_seconds();
        total += bank_.engine_overrun[i].duration_seconds();
    }
    total += bank_.tyre_scrub.duration_seconds();
    for (const PcmClip& c : bank_.surface_roll) total += c.duration_seconds();
    for (const PcmClip& c : bank_.suspension_thump) total += c.duration_seconds();
    total += bank_.rain.duration_seconds();
    total += bank_.wind.duration_seconds();
    total += bank_.checkpoint.duration_seconds();
    total += bank_.lap_record.duration_seconds();
    return total;
}

}  // namespace apricot
