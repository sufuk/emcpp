// tests/basic/dipole_antenna_test.cpp
#include <emc/core/constants.hpp>
//
// Catch2 v3 tests for the short-dipole near-field model (emc::basic::DipoleAntenna).
// Expected values are hand-computed / independently re-derived from the closed form
//
//   E_r  = 60 · (I0·l / R²) · cos θ · sqrt( 1 + (c/(2π f R))² )
//   E_θ  = 30 · (I0·l / R)  · sin θ · sqrt( (1/R)² + ( 2π f/c − c/(2π f R²) )² )
//   H_φ  = (f/(2c)) · (I0·l / R) · sin θ · sqrt( 1 + (c/(2π f R))² )
//
// No CSV / golden files: every expected quantity is built inline.

#include <catch2/catch_test_macros.hpp>

#include <emc/basic/dipole_antenna.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

namespace {

// Build a typed Angle from degrees once (no angular system header needed):
// θ[rad] = θ[deg] · π / 180, attached to si::radian.
emc::units::Angle as_angle(double d) {
    return (d * emc::constants::pi / 180.0) * si::radian;
}

} // namespace

// (a) KNOWN-VALUE — the full three-component result vs an independent re-derivation
//     using the same true CODATA c and a single π. This pins every term of every
//     formula, not just one field.
TEST_CASE("dipole: all three fields match an independent re-derivation", "[basic][dipole][known]") {
    const double I0 = 1.0, l = 6.6, R = 6.7, f_mhz = 6.3, theta_deg = 80.0;

    const emc::basic::DipoleAntennaInput in{
        .current   = I0    * A,
        .length    = l     * m,
        .distance  = R     * m,
        .frequency = f_mhz * MHz,
        .theta     = as_angle(theta_deg),
    };

    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());

    // Independent closed-form evaluation in plain SI doubles.
    const double f   = f_mhz * 1.0e6;
    const double th  = theta_deg * emc::constants::pi / 180.0;
    const double cc  = emc::constants::c.numerical_value_in(m / s);
    const double pi  = emc::constants::pi;
    const double cosT = std::cos(th);
    const double sinT = std::sin(th);

    const double rad_common = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));

    const double Er = 60.0 * ((I0 * l) / (R * R)) * cosT * rad_common;
    const double Etheta = 30.0 * ((I0 * l) / R) * sinT *
        std::sqrt(std::pow(1.0 / R, 2.0) +
                  std::pow((2.0 * pi * f) / cc - cc / (2.0 * pi * f * R * R), 2.0));
    const double Hphi = (f / (2.0 * cc)) * ((I0 * l) / R) * sinT * rad_common;

    REQUIRE(emc::test::approx(r->e_r,     Er     * (V / m), 1e-9));
    REQUIRE(emc::test::approx(r->e_theta, Etheta * (V / m), 1e-9));
    REQUIRE(emc::test::approx(r->h_phi,   Hphi   * (A / m), 1e-9));
}

// (a2) KNOWN-VALUE — at θ = 90°, cos θ = 0 so E_r is exactly zero, and the
//      sin-driven E_θ / H_φ reduce to their full magnitude. We hand-compute H_φ
//      at this angle (sin 90° = 1) so the radial/transverse coupling is checked
//      against a fully written-out textbook number.
TEST_CASE("dipole: E_r vanishes at theta = 90 degrees", "[basic][dipole][known]") {
    const double I0 = 1.0, l = 1.0, R = 10.0, f_mhz = 100.0;

    const emc::basic::DipoleAntennaInput in{
        .current   = I0    * A,
        .length    = l     * m,
        .distance  = R     * m,
        .frequency = f_mhz * MHz,
        .theta     = as_angle(90.0),
    };

    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());

    // cos 90° = 0  ⇒  E_r = 0 exactly.
    REQUIRE(emc::test::approx(r->e_r, 0.0 * (V / m), 1e-6));

    // sin 90° = 1  ⇒  H_φ = (f/2c)·(I0·l/R)·sqrt(1 + (c/(2π f R))²).
    const double f  = f_mhz * 1.0e6;
    const double cc = emc::constants::c.numerical_value_in(m / s);
    const double pi = emc::constants::pi;
    const double rad_common = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));
    const double Hphi = (f / (2.0 * cc)) * ((I0 * l) / R) * 1.0 * rad_common;

    REQUIRE(emc::test::approx(r->h_phi, Hphi * (A / m), 1e-9));
}

// (b) PROPERTY — linearity in the source current I0. Every field is directly
//     proportional to I0 (it enters only through the I0·l product), so doubling
//     I0 doubles each output. This guards the I0 wiring independently of the
//     geometric / frequency factors.
TEST_CASE("dipole: fields scale linearly with current", "[basic][dipole][property]") {
    auto make = [](double I0) {
        return emc::basic::DipoleAntennaInput{
            .current   = I0   * A,
            .length    = 1.0  * m,
            .distance  = 5.0  * m,
            .frequency = 50.0 * MHz,
            .theta     = as_angle(35.0),
        };
    };

    const auto base = emc::basic::calculate(make(1.0));
    const auto dbl  = emc::basic::calculate(make(2.0));
    REQUIRE(base.has_value());
    REQUIRE(dbl.has_value());

    // 2× the current ⇒ exactly 2× each field component.
    REQUIRE(emc::test::approx(dbl->e_r,     2.0 * base->e_r,     1e-9));
    REQUIRE(emc::test::approx(dbl->e_theta, 2.0 * base->e_theta, 1e-9));
    REQUIRE(emc::test::approx(dbl->h_phi,   2.0 * base->h_phi,   1e-9));
}

// (b2) PROPERTY — the sin-driven transverse fields vanish on axis (θ = 0), while
//      the cos-driven radial field does not. A sin/cos swap in the wiring would
//      break this.
TEST_CASE("dipole: sin-driven fields vanish on axis (theta = 0)", "[basic][dipole][property]") {
    const emc::basic::DipoleAntennaInput in{
        .current   = 2.0  * A,
        .length    = 1.0  * m,
        .distance  = 5.0  * m,
        .frequency = 50.0 * MHz,
        .theta     = as_angle(0.0),
    };

    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());

    // sin 0 = 0  ⇒  E_θ = H_φ = 0; cos 0 = 1  ⇒  E_r is strictly positive.
    REQUIRE(emc::test::approx(r->e_theta, 0.0 * (V / m), 1e-6));
    REQUIRE(emc::test::approx(r->h_phi,   0.0 * (A / m), 1e-6));
    REQUIRE(r->e_r.numerical_value_in(V / m) > 0.0);
}

// (c) VALIDATION / EDGE — R = 0 makes R² and 1/R blow up, so it must be rejected
//     as a typed OutOfRange error on the "distance" field, not emitted as inf/nan.
TEST_CASE("dipole rejects zero distance", "[basic][dipole][validate]") {
    const emc::basic::DipoleAntennaInput in{
        .current   = 1.0   * A,
        .length    = 1.0   * m,
        .distance  = 0.0   * m,
        .frequency = 100.0 * MHz,
        .theta     = as_angle(45.0),
    };

    const auto r = emc::basic::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "distance");
}

// (c2) VALIDATION / EDGE — a non-positive frequency appears in 1/(fR) terms, so
//      f = 0 must also be rejected (OutOfRange on "frequency").
TEST_CASE("dipole rejects zero frequency", "[basic][dipole][validate]") {
    const emc::basic::DipoleAntennaInput in{
        .current   = 1.0 * A,
        .length    = 1.0 * m,
        .distance  = 5.0 * m,
        .frequency = 0.0 * Hz,
        .theta     = as_angle(45.0),
    };

    const auto r = emc::basic::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}
