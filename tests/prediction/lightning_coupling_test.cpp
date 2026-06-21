// tests/prediction/lightning_coupling_test.cpp
//
// Real Catch2 v3 tests for emc::prediction::LightningCoupling.
//
//   V_ind = (mu0 * h) / (2*pi) * ln((r + d) / r) * (dI/dt)
//
// Expected values are hand-computed from the closed-form expression with the
// library's CODATA mu0; one cross-consistency invariant pins lightning and the
// ESD kernel to the same coupling core; one edge case covers the ln-argument
// (r > 0) domain guard.
#include <catch2/catch_test_macros.hpp>

#include <cmath>      // std::log
#include <numbers>    // std::numbers::pi

#include <mp-units/systems/si.h>

#include <emc/prediction/lightning_coupling.hpp>
#include <emc/prediction/esd_coupling.hpp>   // for the cross-consistency property
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// The same CODATA value the library's emc::constants::mu0 carries, so the
// hand-computed reference and the implementation agree to a tight tolerance.
static constexpr double kMu0 = 1.256'637'062'12e-6;   // H/m

// (a) KNOWN VALUE — hand-computed against the closed form.
//   h = 8 m, r = 0.20 m, d = 0.79 m, dI/dt = 1.13e9 A/s
//   V = (mu0*8/(2*pi)) * ln(0.99/0.20) * 1.13e9
TEST_CASE("Lightning coupling known value", "[prediction][lightning]") {
    const double V =
        (kMu0 * 8.0 / (2.0 * std::numbers::pi)) * std::log(0.99 / 0.20) * 1.13e9;

    const pred::LightningCouplingInput in{
        .loop_height = 8.0 * m,
        .radius      = 0.20 * m,
        .distance    = 0.79 * m,
        .di_dt       = 1.13e9 * (A / s),
    };

    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->induced_voltage, V * si::volt, 1e-9));
}

// (b) PROPERTY — V_ind is exactly linear in dI/dt for fixed geometry.
//   Doubling the slew rate doubles the induced voltage; scaling by 3 triples it.
TEST_CASE("Lightning coupling scales linearly with dI/dt", "[prediction][lightning][property]") {
    const pred::LightningCouplingInput base{
        .loop_height = 5.0 * m,
        .radius      = 0.50 * m,
        .distance    = 0.50 * m,
        .di_dt       = 2.0e8 * (A / s),
    };
    auto a = pred::calculate(base);
    auto b = pred::calculate(pred::LightningCouplingInput{
        .loop_height = base.loop_height, .radius = base.radius, .distance = base.distance,
        .di_dt = 4.0e8 * (A / s)});   // 2x slew
    auto c = pred::calculate(pred::LightningCouplingInput{
        .loop_height = base.loop_height, .radius = base.radius, .distance = base.distance,
        .di_dt = 6.0e8 * (A / s)});   // 3x slew

    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(emc::test::approx(b->induced_voltage, 2.0 * a->induced_voltage, 1e-9));
    REQUIRE(emc::test::approx(c->induced_voltage, 3.0 * a->induced_voltage, 1e-9));
}

// (b2) CROSS-CONSISTENCY — the key invariant tying lightning and ESD to one
//      shared coupling kernel: with dI/dt == I_peak / t_r and identical geometry,
//      the two calculators must return the same induced voltage.
TEST_CASE("Lightning == ESD when dI/dt = I_peak / t_r", "[prediction][lightning][property]") {
    const double Ipeak = 68.0;        // A
    const double tr_ns = 9.0;         // ns
    const double di_dt = Ipeak / (tr_ns * 1e-9);   // A/s

    auto lc = pred::calculate(pred::LightningCouplingInput{
        .loop_height = 8.0 * m, .radius = 0.20 * m, .distance = 0.79 * m,
        .di_dt = di_dt * (A / s)});
    auto esd = pred::calculate(pred::EsdCouplingInput{
        .loop_height = 8.0 * m, .radius = 0.20 * m, .distance = 0.79 * m,
        .peak_current = Ipeak * A, .rise_time = tr_ns * ns});

    REQUIRE(lc.has_value());
    REQUIRE(esd.has_value());
    REQUIRE(emc::test::approx(lc->induced_voltage, esd->induced_voltage, 1e-9));
}

// (c) VALIDATION / EDGE — r <= 0 puts a non-positive base in ln((r+d)/r);
//     require_positive rejects it with OutOfRange.
TEST_CASE("Lightning coupling rejects r <= 0", "[prediction][lightning][error]") {
    pred::LightningCouplingInput in{};
    in.radius = 0.0 * m;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);
}

// (c2) VALIDATION / EDGE — a negative distance that cancels the radius drives the
//      ln argument (r + d) to <= 0, which is a DomainError.
TEST_CASE("Lightning coupling rejects r + d <= 0", "[prediction][lightning][error]") {
    pred::LightningCouplingInput in{};
    in.radius   = 1.0 * m;
    in.distance = -1.0 * m;   // r + d = 0 -> ln domain violation
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DomainError);
}
