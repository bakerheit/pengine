#include "core/asset_root.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "core/log.h"

// Locating our own binary. This is process introspection, not hardware: no
// device, no window, no host library. It is what lets an installed layout
// resolve its assets without the launcher having to chdir first.
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

// Compile-time fallback root: the dev checkout's assets tree. The last resort,
// and the only strategy that can name a directory which does not exist.
#ifndef APRICOT_ASSETS_DIR
#define APRICOT_ASSETS_DIR "assets"
#endif

namespace apricot {
namespace {

namespace fs = std::filesystem;

// How far up from the working directory to look for an `assets` sibling. Deep
// enough to cover build/tests/ and build/bin/, shallow enough that a stray
// `assets` directory in a home folder can never be mistaken for ours.
constexpr int kMaxParentWalk = 4;

struct Resolved {
    std::string root;
    const char* strategy = "none";
    bool found = false;
};

// Absolute path to the running executable, or empty when the platform will not
// say. Empty is handled: the exe-relative strategies are simply skipped.
std::string executable_path() {
#if defined(__APPLE__)
    uint32_t size = 0;
    // First call fails on purpose and reports the buffer size it wants.
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));  // drop the trailing NUL padding
    return buf;
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return fs::path(std::wstring(buf, n)).string();
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : p.string();
#endif
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

Resolved resolve() {
    // --- 1. explicit override ------------------------------------------------
    // Wins over everything so a packager or a tool can point the engine at a
    // tree without relying on where it was launched from.
    if (const char* env = std::getenv("APRICOT_ASSETS"); env && *env) {
        if (is_dir(env)) return Resolved{std::string(env), "env", true};
        AP_WARN("APRICOT_ASSETS='%s' is not a directory; ignoring it", env);
    }

    // --- 2. the anchored working directory -----------------------------------
    // RELATIVE on purpose — see the header. This is the in-game path.
    if (is_dir("assets")) return Resolved{std::string("assets"), "cwd", true};

    // --- 3-6. layouts relative to the binary ---------------------------------
    const std::string exe = executable_path();
    if (!exe.empty()) {
        const fs::path dir = fs::path(exe).parent_path();
        struct Candidate {
            fs::path path;
            const char* strategy;
        };
        const Candidate candidates[] = {
            {dir / "assets", "exe"},
            {dir.parent_path() / "assets", "exe-parent"},
            {dir.parent_path() / "share" / "apricot" / "assets", "share"},
            {dir.parent_path() / "Resources" / "assets", "bundle"},
        };
        for (const Candidate& c : candidates) {
            if (is_dir(c.path)) {
                return Resolved{c.path.lexically_normal().string(), c.strategy,
                                true};
            }
        }
    }

    // --- 7. walk up from the working directory -------------------------------
    // Covers everything launched without anchoring: a test binary in
    // build/tests, a debugger started in build/, a shell sitting in src/.
    std::error_code ec;
    fs::path here = fs::current_path(ec);
    if (!ec) {
        for (int up = 0; up < kMaxParentWalk && here.has_parent_path(); ++up) {
            here = here.parent_path();
            const fs::path candidate = here / "assets";
            if (is_dir(candidate)) {
                return Resolved{candidate.lexically_normal().string(),
                                "cwd-walk", true};
            }
        }
    }

    // --- 8. the path baked in at compile time --------------------------------
    if (is_dir(APRICOT_ASSETS_DIR)) {
        return Resolved{std::string(APRICOT_ASSETS_DIR), "compiled-in", true};
    }

    // Nothing exists. Hand back the compile-time path anyway so callers get a
    // sensible-looking name in their own "could not open" messages, and say so
    // ONCE, here, rather than letting every loader guess at why.
    return Resolved{std::string(APRICOT_ASSETS_DIR), "none", false};
}

const Resolved& resolved() {
    // Resolved and logged exactly once, on first use.
    static const Resolved r = [] {
        Resolved out = resolve();
        if (out.found) {
            AP_INFO("assets: '%s' (via %s)", out.root.c_str(), out.strategy);
        } else {
            AP_WARN("assets: no assets directory found; falling back to '%s'. "
                    "Shaders and overlay fonts will not load; the procedural "
                    "world still will.",
                    out.root.c_str());
        }
        return out;
    }();
    return r;
}

}  // namespace

const std::string& asset_root() { return resolved().root; }

const char* asset_root_strategy() { return resolved().strategy; }

bool assets_found() { return resolved().found; }

std::string asset_path(std::string_view rel) {
    std::string p = asset_root();
    p.reserve(p.size() + 1 + rel.size());
    p += '/';
    p.append(rel);
    return p;
}

}  // namespace apricot
