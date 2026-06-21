// tests/component/dual_stripline_trace_test.cpp
//
// Catch2 v3 tests for the bidirectional Dual Stripline Trace calculator.
// API names come from include/emc/component/dual_stripline_trace.hpp:
//   - forward: emc::component::calculate(const DualStriplineInput&)
//   - inverse: solve_height / solve_gap / solve_thickness / solve_width,
//     each taking its own DualSolve* Input struct.
// Geometry is typed emc::units::Length; we express it in millimetres (METRIC only).
#include <catch2/catch_test_macros.hpp>

#include <emc/component/dual_stripline_trace.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using emc::ErrorCode;

// (a) HAND-COMPUTED known value, from the closed form in the guide.
//     H=122, C=8.5, T=8.1, W=25, eps_r=9.866046252 (all mm):
//       d  = 0.67*pi*(0.8*25 + 8.1)
//       Z0 = 0.5*( 60*ln(8*122/d)/sqrt(eps) + 60*ln(8*(122+8.5)/d)/sqrt(eps) ) ~= 54.19471 ohm
TEST_CASE("dual stripline known value", "[component][dual]") {
    auto r = emc::component::calculate(emc::component::DualStriplineInput{
        .height = 122.0 * mm, .gap = 8.5 * mm, .thickness = 8.1 * mm,
        .width = 25.0 * mm, .relative_permittivity = 9.866046252 });
    REQUIRE(r.has_value());
    // z0 is an Impedance, so the expected must share that dimension (value * ohm).
    REQUIRE(emc::test::approx(r->z0, 54.19471 * ohm, 1e-4));
}

// (b) ROUND-TRIP — forward Z0, then each inverse must recover its own dimension.
//     solve_gap recovers C, solve_height recovers H, solve_width recovers W.
TEST_CASE("dual stripline round-trips", "[component][dual][roundtrip]") {
    const emc::component::DualStriplineInput in{
        .height = 122.0 * mm, .gap = 8.5 * mm, .thickness = 8.1 * mm,
        .width = 25.0 * mm, .relative_permittivity = 9.866046252 };

    auto fwd = emc::component::calculate(in);
    REQUIRE(fwd.has_value());

    auto g = emc::component::solve_gap(emc::component::DualSolveGap{
        .z0 = fwd->z0, .height = in.height, .thickness = in.thickness,
        .width = in.width, .relative_permittivity = in.relative_permittivity });
    REQUIRE(g.has_value());
    REQUIRE(emc::test::approx(*g, in.gap, 1e-6));   // recovers C (a Length)

    auto h = emc::component::solve_height(emc::component::DualSolveHeight{
        .z0 = fwd->z0, .gap = in.gap, .thickness = in.thickness,
        .width = in.width, .relative_permittivity = in.relative_permittivity });
    REQUIRE(h.has_value());
    REQUIRE(emc::test::approx(*h, in.height, 1e-6));   // recovers H

    auto w = emc::component::solve_width(emc::component::DualSolveWidth{
        .z0 = fwd->z0, .height = in.height, .gap = in.gap,
        .thickness = in.thickness, .relative_permittivity = in.relative_permittivity });
    REQUIRE(w.has_value());
    REQUIRE(emc::test::approx(*w, in.width, 1e-6));   // recovers W
}

// (c) VALIDATION / EDGE — each out-of-domain path maps to the right ErrorCode.
TEST_CASE("dual stripline validation", "[component][dual][validate]") {
    SECTION("eps_r out of range") {
        auto r = emc::component::calculate(emc::component::DualStriplineInput{
            .height = 122.0 * mm, .gap = 8.5 * mm, .thickness = 8.1 * mm,
            .width = 25.0 * mm, .relative_permittivity = 16.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
    SECTION("non-positive gap") {
        auto r = emc::component::calculate(emc::component::DualStriplineInput{
            .height = 122.0 * mm, .gap = 0.0 * mm, .thickness = 8.1 * mm,
            .width = 25.0 * mm, .relative_permittivity = 9.8 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);   // require_positive(gap)
        REQUIRE(r.error().field == "gap");
    }
    SECTION("solve_gap guards H == 0 as division-by-zero") {
        auto g = emc::component::solve_gap(emc::component::DualSolveGap{
            .z0 = 50.0 * ohm, .height = 0.0 * mm, .thickness = 8.1 * mm,
            .width = 25.0 * mm, .relative_permittivity = 9.8 });
        REQUIRE_FALSE(g.has_value());
        REQUIRE(g.error().code == ErrorCode::DivisionByZero);
    }
}
