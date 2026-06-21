// tests/core/constants_test.cpp
//
// Tests for emc::constants — the single source of truth for physical constants.
// Strategy:
//   (a) Compile-time KNOWN-VALUE checks: pi and c hold their EXACT defining values.
//   (b) Run-time EM identity tests (z0 == mu0*c, c == 1/sqrt(eps0*mu0), z0 == sqrt(mu0/eps0))
//       compared unit-aware via emc::test::approx.
//   (c) A property test: the constants are positive and mu0 is within 0.01% of 4*pi*1e-7.
//
// All expected numbers are textbook / SI-defining values written inline. No CSV/golden files.

#include <catch2/catch_test_macros.hpp>

#include <cmath>     // std::sqrt
#include <numbers>   // std::numbers::pi_v

#include <emc/core/constants.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

namespace cst = emc::constants;

// ---------------------------------------------------------------------------
// (a) Compile-time KNOWN-VALUE checks.
//     The SI 2019 redefinition makes pi (math) and c (m/s) EXACT, so we can
//     assert them at compile time — a regression to a wrong literal fails the build.
// ---------------------------------------------------------------------------
static_assert(cst::pi == std::numbers::pi_v<double>,
              "pi must be std::numbers::pi (full precision)");
static_assert(cst::pi > 3.14159 && cst::pi < 3.14160,
              "pi must lie in the expected interval");
static_assert(cst::c.numerical_value_in(m / s) == 299'792'458.0,
              "c must be the EXACT SI defining value 299 792 458 m/s");
static_assert(cst::h.numerical_value_in(J * s) == 6.626'070'15e-34,
              "h must be the EXACT SI defining value");

TEST_CASE("c holds its exact SI defining value", "[core][constants]") {
    // Same exact value, now exercised at run time via approx (unit-aware).
    REQUIRE(emc::test::approx(cst::c, 299'792'458.0 * (m / s), 1e-12));
}

// ---------------------------------------------------------------------------
// (b) EM identity / round-trip tests. These are the load-bearing relationships
//     the whole library trusts. Each is checked unit-aware so a dimension slip
//     would not even compile.
// ---------------------------------------------------------------------------
TEST_CASE("free-space impedance equals mu0 * c", "[core][constants]") {
    // z0 = mu0 * c. Textbook value 376.730313668 ohm.
    const auto z0_from_mu0_c = (cst::mu0 * cst::c).in(ohm);
    REQUIRE(emc::test::approx(cst::z0, z0_from_mu0_c, 1e-9));
    REQUIRE(emc::test::approx(cst::z0, 376.730'313'668 * ohm, 1e-9));
}

TEST_CASE("free-space impedance equals sqrt(mu0 / eps0)", "[core][constants]") {
    // z0 = sqrt(mu0/eps0). Evaluate the ratio in ohm^2, sqrt it, rebuild a quantity.
    const double z0_sq = (cst::mu0 / cst::eps0).numerical_value_in(ohm * ohm);
    const auto   z0_from_sqrt = std::sqrt(z0_sq) * ohm;
    REQUIRE(emc::test::approx(cst::z0, z0_from_sqrt, 1e-6));
}

TEST_CASE("speed of light equals 1 / sqrt(eps0 * mu0)", "[core][constants]") {
    // c = 1/sqrt(eps0*mu0). The product eps0*mu0 has units (F/m)*(H/m) = s^2/m^2,
    // so 1/sqrt(...) recovers m/s. We extract in F*H/m^2 to match the header's check.
    const double inv_c_sq =
        (cst::eps0 * cst::mu0).numerical_value_in(si::farad * si::henry
                                                  / (si::metre * si::metre));
    const auto c_recovered = (1.0 / std::sqrt(inv_c_sq)) * (m / s);
    REQUIRE(emc::test::approx(cst::c, c_recovered, 1e-6));
}

// ---------------------------------------------------------------------------
// (c) Property / sanity tests.
// ---------------------------------------------------------------------------
TEST_CASE("mu0 is within 0.01% of the classical 4*pi*1e-7", "[core][constants]") {
    // Vacuum permeability is no longer EXACTLY 4*pi*1e-7 after the 2019 SI
    // redefinition, but it stays extremely close to it.
    const auto mu0_classical = (4.0 * cst::pi * 1e-7) * (H / m);
    REQUIRE(emc::test::approx(cst::mu0, mu0_classical, 1e-4));
}

TEST_CASE("eps0 is consistent with 1/(mu0*c^2)", "[core][constants]") {
    // eps0 = 1/(mu0*c^2). Derive eps0 from the exact c and CODATA mu0, compare to
    // the stored literal. Extract the derived value in F/m and rebuild a quantity.
    const double mu0_val = cst::mu0.numerical_value_in(H / m);
    const double c_val   = cst::c.numerical_value_in(m / s);
    const auto eps0_derived = (1.0 / (mu0_val * c_val * c_val)) * (F / m);
    REQUIRE(emc::test::approx(cst::eps0, eps0_derived, 1e-6));
}

TEST_CASE("core constants are strictly positive", "[core][constants]") {
    REQUIRE(cst::c.numerical_value_in(m / s)        > 0.0);
    REQUIRE(cst::mu0.numerical_value_in(H / m)      > 0.0);
    REQUIRE(cst::eps0.numerical_value_in(F / m)     > 0.0);
    REQUIRE(cst::z0.numerical_value_in(ohm)         > 0.0);
    REQUIRE(cst::h.numerical_value_in(J * s)        > 0.0);
    REQUIRE(cst::elementary_charge.numerical_value_in(A * s) > 0.0);
}
