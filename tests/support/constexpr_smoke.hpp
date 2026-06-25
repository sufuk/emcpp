// tests/support/constexpr_smoke.hpp   (test-only)
#pragma once
//
// EMC_CONSTEXPR_SMOKE(expr, msg): assert a known headline value of a pure
// closed-form forward core. Where the toolchain implements C++ constexpr
// transcendental <cmath> (std::log/std::sqrt in a constant expression — gcc's
// libstdc++ via P1383, __cpp_lib_constexpr_cmath >= 202306L) this is a
// COMPILE-TIME guard; libc++ has NOT implemented it yet, so there the same
// expression is checked at RUNTIME with Catch2 instead.
//
// `expr` must be the fully-invoked expression, wrapped in parentheses so its
// internal commas survive macro expansion, e.g.
//     EMC_CONSTEXPR_SMOKE(([]{ ... return ...; }()), "msg");
#include <version>

#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202306L
#  define EMC_CONSTEXPR_SMOKE(expr, msg) static_assert((expr), msg)
#else
#  include <catch2/catch_test_macros.hpp>
// The trailing static_assert(true, ...) cleanly absorbs the call site's ';' so it
// is not a stray empty-declaration after the TEST_CASE body (which -Wpedantic flags).
#  define EMC_CONSTEXPR_SMOKE(expr, msg) \
       TEST_CASE(msg) { CHECK(expr); }   \
       static_assert(true, "EMC_CONSTEXPR_SMOKE expects a trailing semicolon")
#endif
