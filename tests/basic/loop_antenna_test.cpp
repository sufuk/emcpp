// tests/basic/loop_antenna_test.cpp
#include <emc/core/constants.hpp>
//
// Catch2 v3 tests for the small-loop (magnetic) near-field calculator, emc::basic::LoopAntenna.
// Field model (SI base units), from include/emc/basic/loop_antenna.hpp + the implementation guide:
//
//   H_r  = (f / c)            · (I0·A / R²) · cos θ · √(1 + (c/(2π f R))²)
//   H_θ  = (f / (2c))         · (I0·A / R)  · sin θ · √((1/R)² + ((2π f / c) − c/(2π f R²))²)
//   E_φ  = 120 · (π f / c)²   · (I0·A / R)  · sin θ · √(1 + (c/(2π f R))²)
//
// Tests: (a) hand/independent re-derivation of E_φ (the distinctive (πf/c)² term),
//        (b) known value — H_r ∝ cos θ vanishes at θ = 90°,
//        (c) property — linearity in I0 (and round-trip of the cos/sin wiring),
//        (d) validation/edge — zero area rejected with a typed OutOfRange error.

#include <catch2/catch_test_macros.hpp>

#include <emc/basic/loop_antenna.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

namespace {
// Build a typed Angle from degrees once, the way a call site would.
emc::units::Angle as_angle(double d) { return (d * emc::constants::pi / 180.0) * si::radian; }
} // namespace

// (a) Reference: E_φ vs an independent closed-form re-derivation with the true c and π.
//     This guards the distinctive 120·(πf/c)² coefficient and the shared radical.
TEST_CASE("loop antenna matches an independent re-derivation", "[basic][loop][reference]") {
    const double I0 = 41.8, A_ = 4.2, R = 36.2, f_mhz = 6.1, theta_deg = 157.0;

    const emc::basic::LoopAntennaInput in{
        .current   = I0 * A,
        .loop_area = A_ * square(m),
        .distance  = R  * m,
        .frequency = f_mhz * MHz,
        .theta     = as_angle(theta_deg),
    };
    const auto got = emc::basic::calculate(in);
    REQUIRE(got.has_value());

    const double f  = f_mhz * 1e6;
    const double th = theta_deg * emc::constants::pi / 180.0;
    const double cc = emc::constants::c.numerical_value_in(m / s);
    const double pi = emc::constants::pi;

    const double rad  = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));
    const double Ephi = 120.0 * std::pow(pi * f / cc, 2.0) * (I0 * A_ / R) * std::sin(th) * rad;

    REQUIRE(emc::test::approx(got->e_phi, Ephi * (V / m), 1e-9));
}

// (b) Hand-computed: H_r carries a cos θ factor, so at θ = 90° it is exactly zero.
//     A sin/cos swap on H_r would make this nonzero.
TEST_CASE("loop: H_r vanishes at theta = 90 deg", "[basic][loop][known]") {
    const emc::basic::LoopAntennaInput in{
        .current   = 10.0 * A,
        .loop_area = 1.0  * square(m),
        .distance  = 10.0 * m,
        .frequency = 1.0  * MHz,
        .theta     = as_angle(90.0),
    };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->h_r, 0.0 * (A / m), 1e-6));
}

// (c) Property: every field is linear in the I0·A product. Doubling the current doubles
//     all three outputs. A stray square on a field would break this.
TEST_CASE("loop: fields scale linearly with current", "[basic][loop][property]") {
    const emc::basic::LoopAntennaInput base{
        .current   = 5.0 * A,
        .loop_area = 2.0 * square(m),
        .distance  = 8.0 * m,
        .frequency = 3.0 * MHz,
        .theta     = as_angle(60.0),
    };
    auto dbl = base;
    dbl.current = 10.0 * A;

    const auto a = emc::basic::calculate(base);
    const auto b = emc::basic::calculate(dbl);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    REQUIRE(emc::test::approx(b->h_r,     a->h_r     * 2.0, 1e-9));
    REQUIRE(emc::test::approx(b->h_theta, a->h_theta * 2.0, 1e-9));
    REQUIRE(emc::test::approx(b->e_phi,   a->e_phi   * 2.0, 1e-9));
}

// (d) Validation/edge: zero loop area has no physical meaning (A appears in every field);
//     it must be rejected as a typed OutOfRange error, not emitted as a silent zero.
TEST_CASE("loop rejects zero area", "[basic][loop][validate]") {
    const emc::basic::LoopAntennaInput in{
        .current   = 1.0  * A,
        .loop_area = 0.0  * square(m),
        .distance  = 5.0  * m,
        .frequency = 10.0 * MHz,
        .theta     = as_angle(45.0),
    };
    const auto r = emc::basic::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "loop_area");
}
