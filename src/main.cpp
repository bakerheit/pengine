// apricot: entry point.
//
// Deliberately thin. Everything is in App (src/app/app.h); this file exists to
// parse the handful of process-level arguments and to make sure the version is
// the first thing in every log, so a bug report's first line identifies the
// build it came from.

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "app/app.h"
#include "core/log.h"

namespace {

void print_usage() {
    std::printf(
        "apricot %s\n"
        "\n"
        "  --verbose       log at debug level\n"
        "  --log FILE      also append the log to FILE\n"
        "  --frames N      render N frames, print a summary, then exit\n"
        "  --no-instancing start on the naive per-node draw path\n"
        "  --warp-every N  teleport across the island every N frames\n"
        "  --version       print the version and exit\n"
        "  --help          this text\n"
        "\n"
        "Controls: WASD drive, Space handbrake. F7 toggles instancing,\n"
        "F8 warps across the island. Esc or Ctrl+Q quits.\n"
        "\n"
        "--frames exists so the renderer can be exercised without a human at\n"
        "the keyboard: it runs a fixed number of frames, reports the draw and\n"
        "instance counts and the GL error state, and returns non-zero if any\n"
        "GL call failed.\n"
        "\n"
        "--warp-every is the same idea applied to STREAMING. A teleport evicts\n"
        "the entire resident world and refills it somewhere else, which is the\n"
        "hardest thing the streamer does and the one path a human would have to\n"
        "remember to test. Repeating it on a timer means a run either survives\n"
        "a dozen of them with a clean GL queue and a flat mesh count, or it\n"
        "does not.\n",
        APRICOT_VERSION);
}

}  // namespace

int main(int argc, char** argv) {
    const char* log_path = nullptr;
    int frame_limit = 0;
    int warp_every = 0;
    bool instancing = true;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(a, "--version") == 0) {
            std::printf("%s\n", APRICOT_VERSION);
            return 0;
        }
        if (std::strcmp(a, "--verbose") == 0) {
            apricot::log::min_level() = apricot::log::Level::Debug;
            continue;
        }
        if (std::strcmp(a, "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
            continue;
        }
        if (std::strcmp(a, "--frames") == 0 && i + 1 < argc) {
            frame_limit = std::atoi(argv[++i]);
            if (frame_limit <= 0) {
                std::fprintf(stderr, "--frames needs a positive count\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a, "--no-instancing") == 0) {
            instancing = false;
            continue;
        }
        if (std::strcmp(a, "--warp-every") == 0 && i + 1 < argc) {
            warp_every = std::atoi(argv[++i]);
            if (warp_every <= 0) {
                std::fprintf(stderr, "--warp-every needs a positive count\n");
                return 2;
            }
            continue;
        }
        std::fprintf(stderr, "unknown argument: %s\n", a);
        print_usage();
        return 2;
    }

    if (log_path && !apricot::log::open_log_file(log_path)) {
        // Not fatal: console logging still works, and refusing to start over a
        // log file would be a poor trade.
        AP_WARN("could not open log file '%s'; console only", log_path);
    }

    apricot::App app;
    // Set before init(): both affect what the first frame does.
    app.set_frame_limit(frame_limit);
    app.set_instancing(instancing);
    app.set_warp_interval(warp_every);
    if (!app.init()) {
        AP_ERROR("startup failed");
        app.shutdown();
        apricot::log::close_log_file();
        return 1;
    }

    const int rc = app.run();
    app.shutdown();
    apricot::log::close_log_file();
    return rc;
}
