// tests/prediction/esd_coupling_test.cpp
//
// Tests for the ESD induced-coupling model:
//   V_ind = (mu0 * h) / (2*pi) * ln((r + d) / r) * (I_peak / t_r)
//
// All expected values are hand-computed inline against that closed form with the
// library's CODATA mu0 (1.25663706212e-6 H/m); no CSV / golden files.
#include <catch2/catch_test_macros.hpp>

#include <cmath>      // std::log
#include <numbers>    // std::numbers::pi

#include <mp-units/systems/si.h>

#include <emc/prediction/esd_coupling.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// (a) Hand-computed known value.
//   h = 8 m, r = 0.20 m, d = 0.79 m, I_peak = 68 A, t_r = 9 ns
//   V = (mu0*8/(2*pi)) * ln(0.99/0.20) * (68 / 9e-9)
//     evaluated with the same mu0 the library uses, so a tight 1e-9 tolerance holds.
TEST_CASE("ESD coupling known value", "[prediction][esd]") {
    constexpr double mu0 = 1.25663706212e-6;   // H/m, CODATA — matches emc::constants::mu0
    const double expected_V =
        (mu0 * 8.0 / (2.0 * std::numbers::pi)) * std::log(0.99 / 0.20) * (68.0 / 9.0e-9);

    const pred::EsdCouplingInput in{
        .loop_height  = 8.0 * m,
        .radius       = 0.20 * m,
        .distance     = 0.79 * m,
        .peak_current = 68.0 * A,
        .rise_time    = 9.0 * ns,
    };

    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->induced_voltage, expected_V * si::volt, 1e-9));
}

// (b) Property: V_ind is exactly proportional to dI/dt = I_peak / t_r.
//     Doubling I_peak doubles V; doubling t_r halves V. The geometry (mu0, h, ln term)
//     is held fixed, so only the linear current-ramp dependence is exercised.
TEST_CASE("ESD coupling scales with dI/dt", "[prediction][esd][property]") {
    const pred::EsdCouplingInput base{
        .loop_height  = 5.0 * m,
        .radius       = 0.5 * m,
        .distance     = 0.5 * m,
        .peak_current = 50.0 * A,
        .rise_time    = 2.0 * ns,
    };

    auto a = pred::calculate(base);
    auto b = pred::calculate(pred::EsdCouplingInput{   // 2x peak current
        .loop_height  = base.loop_height,
        .radius       = base.radius,
        .distance     = base.distance,
        .peak_current = 100.0 * A,
        .rise_time    = base.rise_time,
    });
    auto c = pred::calculate(pred::EsdCouplingInput{   // 2x rise time
        .loop_height  = base.loop_height,
        .radius       = base.radius,
        .distance     = base.distance,
        .peak_current = base.peak_current,
        .rise_time    = 4.0 * ns,
    });

    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(emc::test::approx(b->induced_voltage, 2.0 * a->induced_voltage, 1e-9));
    REQUIRE(emc::test::approx(c->induced_voltage, 0.5 * a->induced_voltage, 1e-9));
}

// (c) Validation / edge: the formula divides by t_r, so t_r = 0 must be rejected
//     as DivisionByZero rather than producing an infinite voltage.
TEST_CASE("ESD coupling rejects t_r = 0", "[prediction][esd][error]") {
    pred::EsdCouplingInput in{};
    in.rise_time = 0.0 * ns;

    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DivisionByZero);
}

// (c) Validation / edge: r is the ln denominator and base, so r <= 0 is OutOfRange.
TEST_CASE("ESD coupling rejects r <= 0", "[prediction][esd][error]") {
    pred::EsdCouplingInput in{};
    in.radius = 0.0 * m;

    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);
}
