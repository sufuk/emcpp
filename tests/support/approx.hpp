// tests/support/approx.hpp
#pragma once

#include <algorithm>   // std::max
#include <cmath>       // std::abs
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <mp-units/systems/si.h>

namespace emc::test {

// Compares two quantities while respecting their units.
// We read both values in the EXPECTED quantity's unit. Because mp-units puts the
// unit in the type, this conversion only compiles when the two share a dimension,
// so comparing e.g. meters against ohms is a build error, not a silent bug.
// Then we use a relative tolerance, plus a tiny absolute floor so values near zero
// still compare cleanly.
//
//   REQUIRE(emc::test::approx(result.skin_depth, 559.0 * um));   // default rel_tol
//   REQUIRE(emc::test::approx(result.z0, 50.0 * ohm, 1e-4));     // looser tol
//
template <class Q>
// [[nodiscard]]: do not ignore the result; a dropped pass/fail check is a bug, so the compiler warns.
// noexcept: promises not to throw, which lets the compiler optimize and callers rely on it.
[[nodiscard]] bool approx(Q actual, Q expected, double rel_tol = 1e-6) noexcept {
    const auto unit = expected.unit;                       // the unit we will compare in
    const double a  = actual.numerical_value_in(unit);     // won't compile if dimensions differ
    const double e  = expected.numerical_value_in(unit);
    const double diff = std::abs(a - e);
    const double abs_floor = 1e-12;
    return diff <= abs_floor || diff <= rel_tol * std::max(std::abs(a), std::abs(e));
}

// Catch2 matcher version of the same check. Using a matcher makes a failing test
// print the actual and expected values nicely:
//   REQUIRE_THAT(result.skin_depth, emc::test::WithinUnits(559.0 * um, 1e-6));
template <class Q>
struct UnitMatcher : Catch::Matchers::MatcherGenericBase {
    Q expected;
    double rel_tol;
    UnitMatcher(Q e, double t) : expected{e}, rel_tol{t} {}

    bool match(const Q& actual) const { return approx(actual, expected, rel_tol); }

    std::string describe() const override {
        return "is within " + std::to_string(rel_tol) + " (relative) of the expected quantity";
    }
};

template <class Q>
// [[nodiscard]]: the returned matcher is meant to be passed to REQUIRE_THAT; ignoring it is a mistake.
[[nodiscard]] UnitMatcher<Q> WithinUnits(Q expected, double rel_tol = 1e-6) {
    return UnitMatcher<Q>{expected, rel_tol};
}

} // namespace emc::test
