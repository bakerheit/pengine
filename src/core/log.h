#pragma once

#include <cstdarg>
#include <cstdio>

namespace apricot::log {

// Printf-style logging. Deliberately tiny and dependency-free: it is included
// by sim code, so it may never grow a dependency on anything that owns a
// device or a window.

enum class Level { Trace, Debug, Info, Warn, Error };

inline Level& min_level() {
    static Level level = Level::Info;
    return level;
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
    if (static_cast<int>(l) < static_cast<int>(min_level())) return;
    std::FILE* sinks[2] = {(l >= Level::Warn) ? stderr : stdout, log_file()};
    for (std::FILE* out : sinks) {
        if (!out) continue;
        std::fprintf(out, "[%s] %s:%d  ", level_name(l), basename(file), line);
        std::va_list args;
        va_start(args, fmt);
        std::vfprintf(out, fmt, args);
        va_end(args);
        std::fputc('\n', out);
        std::fflush(out);
    }
}

}  // namespace apricot::log

#define AP_LOG(level, ...) ::apricot::log::emit(level, __FILE__, __LINE__, __VA_ARGS__)
#define AP_TRACE(...) AP_LOG(::apricot::log::Level::Trace, __VA_ARGS__)
#define AP_DEBUG(...) AP_LOG(::apricot::log::Level::Debug, __VA_ARGS__)
#define AP_INFO(...)  AP_LOG(::apricot::log::Level::Info,  __VA_ARGS__)
#define AP_WARN(...)  AP_LOG(::apricot::log::Level::Warn,  __VA_ARGS__)
#define AP_ERROR(...) AP_LOG(::apricot::log::Level::Error, __VA_ARGS__)
