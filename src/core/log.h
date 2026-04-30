#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace pengine::log {

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

inline void emit(Level l, const char* file, int line, const char* fmt, ...) {
    if (static_cast<int>(l) < static_cast<int>(min_level())) return;
    std::FILE* out = (l >= Level::Warn) ? stderr : stdout;
    std::fprintf(out, "[%s] %s:%d  ", level_name(l), basename(file), line);
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(out, fmt, args);
    va_end(args);
    std::fputc('\n', out);
    std::fflush(out);
}

} // namespace pengine::log

#define PE_LOG(level, ...) ::pengine::log::emit(level, __FILE__, __LINE__, __VA_ARGS__)
#define PE_TRACE(...) PE_LOG(::pengine::log::Level::Trace, __VA_ARGS__)
#define PE_DEBUG(...) PE_LOG(::pengine::log::Level::Debug, __VA_ARGS__)
#define PE_INFO(...)  PE_LOG(::pengine::log::Level::Info,  __VA_ARGS__)
#define PE_WARN(...)  PE_LOG(::pengine::log::Level::Warn,  __VA_ARGS__)
#define PE_ERROR(...) PE_LOG(::pengine::log::Level::Error, __VA_ARGS__)
