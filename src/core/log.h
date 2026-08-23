#pragma once

#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace apricot::log {

// Printf-style logging. Deliberately tiny and dependency-free: it is included
// by sim code, so it may never grow a dependency on anything that owns a
// device or a window.
//
// A line looks like:
//
//     [    3.517] INFO  [core    ] asset_root.cpp:88  assets at 'assets'
//      ^uptime    ^level ^module    ^file:line        ^message
//
// Three properties are load-bearing, in descending order of how much they cost
// to get wrong:
//
//   1. THE DISABLED PATH DOES NO WORK. The level test lives in the MACRO, not
//      in emit(), so a filtered-out AP_TRACE never evaluates its arguments at
//      all. That distinction is the whole point: C evaluates every argument
//      before the call, so a check inside emit() still pays for the
//      to_string(), the std::string concatenation and the allocation behind a
//      trace line nobody asked to see. Trace logging you cannot afford to
//      leave in is trace logging that gets deleted, and then the day you need
//      it you have nothing.
//
//   2. ONE LINE, ONE WRITE. The message is formatted into a fixed stack
//      buffer and handed to each sink in a single fputs. No allocation, ever
//      — and two threads logging at once interleave whole lines rather than
//      shredding each other mid-fprintf.
//
//   3. THE MODULE TAG IS DERIVED, NOT DECLARED. It is the parent directory of
//      __FILE__, so `core`, `terrain`, `platform` and friends tag themselves
//      with no per-call-site ceremony and no tag that can rot out of date when
//      a file moves.
//
// On the clock: uptime_seconds() reads a monotonic clock. That is a DIAGNOSTIC
// timestamp on a log line and nothing else — no sim state is derived from it,
// no seed is taken from it. The engine's no-time-seeding rule (core/rng.h) is
// intact.

// Maximum bytes in one formatted log line, including the newline. Longer lines
// are truncated with a visible marker rather than allocating: a logger that
// can allocate is a logger that can fail, or block, exactly when the thing
// you are trying to diagnose is memory pressure.
inline constexpr std::size_t kMaxLineBytes = 1024;

enum class Level { Trace, Debug, Info, Warn, Error };

// The runtime threshold. Messages below it are dropped before their arguments
// are even evaluated (see AP_LOG). Settable at any point in the run.
inline Level& min_level() {
    static Level level = Level::Info;
    return level;
}

// The gate. Cheap enough to inline into every call site, and the reason the
// macros can skip argument evaluation entirely.
inline bool enabled(Level l) {
    return static_cast<int>(l) >= static_cast<int>(min_level());
}

inline const char* level_name(Level l) {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

inline const char* basename(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

// A borrowed, NOT null-terminated slice of __FILE__. Printed with "%.*s".
// Returning a slice rather than a std::string is what keeps emit() free of
// allocation.
struct ModuleTag {
    const char* ptr = "?";
    int len = 1;
};

// The directory component immediately above the file: "src/core/log.h" tags as
// "core". Pure pointer arithmetic over a string literal the compiler already
// baked into the binary.
inline ModuleTag module_tag(const char* path) {
    const char* last = nullptr;  // last separator seen
    const char* prev = nullptr;  // the one before it
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            prev = last;
            last = p;
        }
    }
    if (!last) return ModuleTag{};                       // bare filename
    if (!prev) return ModuleTag{path, static_cast<int>(last - path)};
    return ModuleTag{prev + 1, static_cast<int>(last - prev) - 1};
}

// Seconds since the first time anything asked. Monotonic, so it cannot jump
// backwards when the wall clock is corrected mid-session and leave a log whose
// timestamps do not order.
inline double uptime_seconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Optional per-run log file, opened by the app. When set, every message goes
// to the console AND is appended here, flushed per line so a crash never eats
// the tail — which is the only part you ever want. nullptr = console only.
inline std::FILE*& log_file() {
    static std::FILE* f = nullptr;
    return f;
}

inline bool open_log_file(const char* path) {
    if (log_file()) std::fclose(log_file());
    log_file() = std::fopen(path, "w");
    return log_file() != nullptr;
}

inline void close_log_file() {
    if (log_file()) {
        std::fclose(log_file());
        log_file() = nullptr;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
inline void emit(Level l, const char* file, int line, const char* fmt, ...) {
    // Re-checked here as well as in the macro. The macro's check is the one
    // that saves the argument evaluation; this one keeps the threshold honest
    // for anything that calls emit() directly.
    if (!enabled(l)) return;

    char buf[kMaxLineBytes];
    const ModuleTag tag = module_tag(file);

    int head = std::snprintf(buf, sizeof(buf), "[%9.3f] %s [%-8.*s] %s:%d  ",
                             uptime_seconds(), level_name(l), tag.len, tag.ptr,
                             basename(file), line);
    if (head < 0) head = 0;
    std::size_t len = static_cast<std::size_t>(head);
    bool truncated = false;
    if (len > sizeof(buf) - 1) {
        len = sizeof(buf) - 1;
        truncated = true;
    }

    std::va_list args;
    va_start(args, fmt);
    const int wrote = std::vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
    va_end(args);

    if (wrote > 0) {
        const std::size_t room = sizeof(buf) - len - 1;
        const std::size_t got = static_cast<std::size_t>(wrote);
        if (got < room) {
            len += got;
        } else {
            len += room;
            truncated = true;
        }
    }

    // Leave room for the newline and the terminator, and MARK a cut line. A
    // silently truncated log reads as a bug in the thing being logged rather
    // than in the logging, and that misdirection costs hours.
    if (len > sizeof(buf) - 2) len = sizeof(buf) - 2;
    if (truncated && len >= 3) {
        for (std::size_t i = len - 3; i < len; ++i) buf[i] = '.';
    }
    buf[len++] = '\n';
    buf[len] = '\0';

    // Warnings and errors go to stderr so a piped stdout still surfaces them.
    std::FILE* sinks[2] = {(l >= Level::Warn) ? stderr : stdout, log_file()};
    for (std::FILE* out : sinks) {
        if (!out) continue;
        std::fputs(buf, out);
        std::fflush(out);
    }
}

}  // namespace apricot::log

// The level test is HERE, outside the call, so that a disabled level costs one
// integer comparison and evaluates none of its arguments. Wrapped in do/while
// so it behaves like a statement in an unbraced `if`/`else`.
#define AP_LOG(level, ...)                                                \
    do {                                                                  \
        if (::apricot::log::enabled(level))                               \
            ::apricot::log::emit((level), __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define AP_TRACE(...) AP_LOG(::apricot::log::Level::Trace, __VA_ARGS__)
#define AP_DEBUG(...) AP_LOG(::apricot::log::Level::Debug, __VA_ARGS__)
#define AP_INFO(...)  AP_LOG(::apricot::log::Level::Info,  __VA_ARGS__)
#define AP_WARN(...)  AP_LOG(::apricot::log::Level::Warn,  __VA_ARGS__)
#define AP_ERROR(...) AP_LOG(::apricot::log::Level::Error, __VA_ARGS__)
