// tests/basic/far_field_criteria_test.cpp
//
// Tests for emc::basic far-field / near-field boundary criteria.
//   lambda = c / f
//   D > lambda/10  -> reactive = 0.62*sqrt(D^3/lambda), radiating = 2*D^2/lambda
//   otherwise      -> reactive = lambda/50,             radiating = lambda
#include <catch2/catch_test_macros.hpp>

#include <emc/basic/far_field_criteria.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

// (a) Hand-computed KNOWN VALUE — electrically-large branch.
//     f = 300 MHz -> lambda = c / 3e8 = 299792458 / 3e8 = 0.99930819... m.
//     D = 1 m, lambda/10 = 0.0999308 m, so D > lambda/10 -> large branch.
//       reactive  = 0.62 * sqrt(1^3 / lambda) = 0.62 / sqrt(lambda)
//       radiating = 2 * 1^2 / lambda          = 2 / lambda
TEST_CASE("far-field: large-antenna branch known value", "[basic][farfield][known]") {
    const emc::basic::FarFieldCriteriaInput in{ .frequency = 300.0 * MHz,
                                                .max_dimension = 1.0 * m };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(r->electrically_large);

    // lambda derived from the exact CODATA c so the expectation stays full precision.
    const double lambda = emc::constants::c.numerical_value_in(m / s) / 300.0e6;

    REQUIRE(emc::test::approx(r->wavelength,           lambda * m, 1e-12));
    REQUIRE(emc::test::approx(r->reactive_near_field,  (0.62 * std::sqrt(1.0 / lambda)) * m, 1e-9));
    REQUIRE(emc::test::approx(r->radiating_near_field, (2.0 / lambda) * m, 1e-9));

    // Sanity on the magnitudes: lambda ~ 0.99931 m, radiating ~ 2.0014 m.
    REQUIRE(emc::test::approx(r->wavelength,           0.99930819 * m, 1e-6));
    REQUIRE(emc::test::approx(r->radiating_near_field, 2.00138462 * m, 1e-6));
}

// (b) PROPERTY / re-derivation — both branches must match an independent closed
//     form computed with the true c. Covers a large-antenna case and a
//     small-antenna case so both code paths are exercised.
TEST_CASE("far-field criteria matches an independent re-derivation", "[basic][farfield][property]") {
    const double cc = emc::constants::c.numerical_value_in(m / s);

    struct Case { double f_mhz; double D_m; };
    for (const Case& tc : { Case{89.1, 10.0}, Case{300.0, 5.7},
                            Case{1000.0, 0.1}, Case{100.0, 0.05} }) {
        const emc::basic::FarFieldCriteriaInput in{ .frequency = tc.f_mhz * MHz,
                                                    .max_dimension = tc.D_m * m };
        const auto got = emc::basic::calculate(in);
        REQUIRE(got.has_value());

        const double lambda = cc / (tc.f_mhz * 1e6);
        const bool large = (tc.D_m > lambda / 10.0);
        double reactive, radiating;
        if (large) {
            reactive  = 0.62 * std::sqrt(std::pow(tc.D_m, 3.0) / lambda);
            radiating = 2.0 * std::pow(tc.D_m, 2.0) / lambda;
        } else {
            reactive  = lambda / 50.0;
            radiating = lambda;
        }

        REQUIRE(got->electrically_large == large);
        REQUIRE(emc::test::approx(got->wavelength,           lambda    * m, 1e-9));
        REQUIRE(emc::test::approx(got->reactive_near_field,  reactive  * m, 1e-9));
        REQUIRE(emc::test::approx(got->radiating_near_field, radiating * m, 1e-9));
    }
}

// (b2) BRANCH — force the small-antenna path (D <= lambda/10) and check the
//      lambda/50 and lambda limits relative to the returned wavelength.
//      100 MHz -> lambda ~ 3 m, lambda/10 = 0.3 m; D = 0.1 m < 0.3 m.
TEST_CASE("far-field: small-antenna branch uses lambda/50 and lambda", "[basic][farfield][branch]") {
    const emc::basic::FarFieldCriteriaInput in{ .frequency = 100.0 * MHz,
                                                .max_dimension = 0.1 * m };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->electrically_large);
    REQUIRE(emc::test::approx(r->reactive_near_field,  r->wavelength / 50.0, 1e-12));
    REQUIRE(emc::test::approx(r->radiating_near_field, r->wavelength,        1e-12));
}

// (c) VALIDATION / EDGE — zero frequency (lambda = c/0) is rejected before any math.
TEST_CASE("far-field rejects zero frequency", "[basic][farfield][validate]") {
    const auto r = emc::basic::calculate({ .frequency = 0.0 * Hz, .max_dimension = 1.0 * m });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// (c2) VALIDATION / EDGE — non-positive dimension is rejected.
TEST_CASE("far-field rejects non-positive max_dimension", "[basic][farfield][validate]") {
    const auto r = emc::basic::calculate({ .frequency = 300.0 * MHz, .max_dimension = 0.0 * m });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "max_dimension");
}

// (d) constexpr-friendliness: lambda = c/f folds at compile time for literal inputs,
//     the strongest guard against a c regression.
namespace {
constexpr double lambda_at_300mhz =
    emc::constants::c.numerical_value_in(m / s) / 300e6;
static_assert(lambda_at_300mhz > 0.999 && lambda_at_300mhz < 1.000,
              "lambda at 300 MHz must be ~0.9993 m with the true c");
}
