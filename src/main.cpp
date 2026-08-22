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
        "  --verbose      log at debug level\n"
        "  --log FILE     also append the log to FILE\n"
        "  --version      print the version and exit\n"
        "  --help         this text\n"
        "\n"
        "Controls: WASD drive, Space handbrake. Esc or Ctrl+Q quits.\n",
        APRICOT_VERSION);
}

}  // namespace

int main(int argc, char** argv) {
    const char* log_path = nullptr;

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
