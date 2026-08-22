#pragma once

// Assertions that DO NOT compile out under -DNDEBUG.
//
// The default build type is RelWithDebInfo, which defines NDEBUG, under which
// <cassert>'s assert() is a no-op. A test using plain assert() therefore
// SILENTLY PASSES while its checks do nothing — it does not fail, it lies.
// Use these instead. There is no <cassert> in this test tree and there should
// never be one.
//
//   REQUIRE(cond)                 one-arg form. Prints the expression and
//                                 file:line, then exits non-zero.
//   REQUIRE_MSG(cond, msg, case)  adds a human message and a case tag, for
//                                 when the same check runs over many inputs
//                                 and you need to know which one broke.
//   REQUIRE_NEAR(a, b, eps)       floating-point comparison. Prints both
//                                 values and the actual delta on failure,
//                                 which is the number you actually want.
//
// Semantics are "fatal if !cond" throughout, matching assert().

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace apricot_test {

inline void require_fail(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "REQUIRE failed: %s\n  at %s:%d\n", expr, file, line);
    std::exit(1);
}

inline void require_msg(bool cond, const char* msg, const char* case_name,
                        const char* file, int line) {
    if (cond) return;
    std::fprintf(stderr, "REQUIRE failed: %s [case: %s]\n  at %s:%d\n", msg,
                 case_name ? case_name : "", file, line);
    std::exit(1);
}

inline void require_near(double a, double b, double eps, const char* expr,
                         const char* file, int line) {
    const double d = std::fabs(a - b);
    if (d <= eps) return;
    std::fprintf(stderr,
                 "REQUIRE_NEAR failed: %s\n  %.17g vs %.17g (delta %.17g > %.17g)\n"
                 "  at %s:%d\n",
                 expr, a, b, d, eps, file, line);
    std::exit(1);
}

// Print a one-line pass marker so a passing run still shows what it covered.
inline void pass(const char* name) { std::printf("  ok  %s\n", name); }

inline int done(const char* suite) {
    std::printf("PASS %s\n", suite);
    return 0;
}

}  // namespace apricot_test

#define REQUIRE(cond)                                            \
    do {                                                         \
        if (!(cond))                                             \
            ::apricot_test::require_fail(#cond, __FILE__, __LINE__); \
    } while (0)

#define REQUIRE_MSG(cond, msg, case_name) \
    ::apricot_test::require_msg((cond), (msg), (case_name), __FILE__, __LINE__)

#define REQUIRE_NEAR(a, b, eps)                                             \
    ::apricot_test::require_near((a), (b), (eps), #a " ~= " #b, __FILE__, \
                                 __LINE__)
