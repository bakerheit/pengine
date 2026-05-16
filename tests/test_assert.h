#pragma once

// Test-side assertion helpers that DON'T compile out under -DNDEBUG.
//
// The project's default build type is RelWithDebInfo, which defines NDEBUG.
// Under NDEBUG, `<cassert>`'s `assert(cond)` is a no-op — meaning any test
// using plain `assert()` *silently passes* even when its check fails, lying
// about correctness. PBD-044 caught this when its `assert(save_ipl(...))`
// was elided and a downstream eq-comparison started comparing default-
// constructed values. PBD-045 consolidates a project-wide alternative.
//
// Use these instead of `<cassert>` in tests:
//   REQUIRE(cond)                — succinct one-arg form, like a one-line
//                                  assert(). Prints expression + file/line
//                                  on failure, then exit(1).
//   FATAL_IF(cond, msg, case)    — three-arg form with a human message and
//                                  a per-case tag. Useful when the same
//                                  condition is checked across many cases
//                                  and the tag identifies which one failed.
//
// Semantics are "fatal if !cond" for both forms — same as `assert`.

#include <cstdio>
#include <cstdlib>

namespace pengine_test {

inline void require_fail(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "REQUIRE failed: %s\n  at %s:%d\n", expr, file, line);
    std::exit(1);
}

inline void fatal_if_not(bool cond, const char* msg, const char* case_name) {
    if (!cond) {
        std::fprintf(stderr, "FATAL: %s [case: %s]\n",
                     msg, case_name ? case_name : "");
        std::exit(1);
    }
}

} // namespace pengine_test

#define REQUIRE(cond) do { \
    if (!(cond)) ::pengine_test::require_fail(#cond, __FILE__, __LINE__); \
} while (0)

#define FATAL_IF(cond, msg, case_name) \
    ::pengine_test::fatal_if_not((cond), (msg), (case_name))
