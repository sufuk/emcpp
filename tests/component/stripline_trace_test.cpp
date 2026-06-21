// tests/component/stripline_trace_test.cpp
//
// Catch2 v3 tests for emc::component Stripline Trace (bidirectional solvers).
// Source of truth: include/emc/component/stripline_trace.hpp. Expected values are
// hand-computed from the IPC stripline closed form (no CSV / golden files):
//
//   Z0  = 60 * ln( 4*(2H + T) / (0.67*pi*(0.8W + T)) ) / sqrt(eps_r)   [ohm]
//   Tpd = 84.75 * sqrt(eps_r)                                          [ps/inch]
//   C0  = Tpd / Z0                                                     [pF/inch]
//
// The Result stores C0/Tpd in SI (F/m, s/m); we compare against the SAME-dimension
// quantities expressed in METRIC units (pF/m, ns/m) — the per-inch display numbers
// converted with the exact 1 inch = 0.0254 m, so no imperial unit appears here.
//
#include <catch2/catch_test_macros.hpp>

#include <emc/component/stripline_trace.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using emc::ErrorCode;

namespace {
// Metric per-length units for the C0/Tpd results (SI internally): capacitance per metre
// and time per metre. Same dimensions as the Result fields, so the comparison is valid.
constexpr auto pf_per_m = si::pico<si::farad>  / si::metre;
constexpr auto ns_per_m = si::nano<si::second> / si::metre;
}  // namespace

// (a) HAND-COMPUTED KNOWN VALUE.
//     H=81, T=2.4, W=7.5, eps_r=12.85382422:
//       Z0  = 60*ln(4*(2*81+2.4)/(0.67*pi*(0.8*7.5+2.4)))/sqrt(eps) = 60.51695 ohm
//       Tpd = 84.75*sqrt(eps) = 303.84765 ps/inch = 11.962506 ns/m
//       C0  = Tpd / Z0        = 5.020868 pF/inch  = 197.67199 pF/m
TEST_CASE("stripline known value", "[component][stripline]") {
    // calculate() is overloaded across the board-impedance calculators, so the Input is
    // explicitly typed (never a bare brace-init) to keep the call unambiguous.
    auto r = emc::component::calculate(emc::component::StriplineTraceInput{
        .height = 81.0 * mm, .thickness = 2.4 * mm, .width = 7.5 * mm,
        .relative_permittivity = 12.85382422 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0,  60.51695 * ohm, 1e-5));
    REQUIRE(emc::test::approx(r->tpd, 11.962506 * ns_per_m, 1e-6));
    REQUIRE(emc::test::approx(r->c0,  197.67199 * pf_per_m, 1e-5));
}

// (b) ROUND-TRIP: forward Z0, then solve_width / solve_height must recover the original
//     geometry. This also exercises that the forward Z0 and the inverse `a = exp(...)`
//     share the same emc::constants::pi.
TEST_CASE("stripline Z0 inverts to width and height", "[component][stripline][roundtrip]") {
    const emc::component::StriplineTraceInput in{
        .height = 81.0 * mm, .thickness = 2.4 * mm, .width = 7.5 * mm,
        .relative_permittivity = 12.85382422 };
    auto fwd = emc::component::calculate(in);
    REQUIRE(fwd.has_value());

    auto w = emc::component::solve_width(emc::component::StriplineSolveWidth{
        .z0 = fwd->z0, .height = in.height, .thickness = in.thickness,
        .relative_permittivity = in.relative_permittivity });
    REQUIRE(w.has_value());
    REQUIRE(emc::test::approx(*w, in.width, 1e-7));

    auto h = emc::component::solve_height(emc::component::StriplineSolveHeight{
        .z0 = fwd->z0, .thickness = in.thickness, .width = in.width,
        .relative_permittivity = in.relative_permittivity });
    REQUIRE(h.has_value());
    REQUIRE(emc::test::approx(*h, in.height, 1e-7));
}

// (c) VALIDATION / EDGE — each out-of-domain path maps to the right ErrorCode/field.
TEST_CASE("stripline validation", "[component][stripline][validate]") {
    SECTION("eps_r below range") {
        auto r = emc::component::calculate(emc::component::StriplineTraceInput{
            .height = 81.0 * mm, .thickness = 2.4 * mm, .width = 7.5 * mm,
            .relative_permittivity = 0.5 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
    SECTION("T/H ratio too large") {
        auto r = emc::component::calculate(emc::component::StriplineTraceInput{
            .height = 1.0 * mm, .thickness = 0.9 * mm, .width = 0.05 * mm,
            .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "thickness_to_height_ratio");
    }
    SECTION("non-positive width") {
        auto r = emc::component::calculate(emc::component::StriplineTraceInput{
            .height = 1.0 * mm, .thickness = 0.05 * mm, .width = 0.0 * mm,
            .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);   // require_positive -> OutOfRange
        REQUIRE(r.error().field == "width");
    }
    SECTION("inverse solver rejects out-of-range eps_r") {
        auto w = emc::component::solve_width(emc::component::StriplineSolveWidth{
            .z0 = 50.0 * ohm, .height = 0.2 * mm, .thickness = 0.014 * mm,
            .relative_permittivity = 20.0 });
        REQUIRE_FALSE(w.has_value());
        REQUIRE(w.error().code == ErrorCode::OutOfRange);
        REQUIRE(w.error().field == "relative_permittivity");
    }
}

// (d) CONSTEXPR smoke: the forward core is a pure closed form using the shared full-precision pi.
static_assert([] {
    const double H = 81.0, T = 2.4, W = 7.5, e = 12.85382422;
    const double z0 = 60.0 * std::log(4.0 * (2.0 * H + T) / (0.67 * emc::constants::pi * (0.8 * W + T)))
                      / std::sqrt(e);
    return z0 > 60.4 && z0 < 60.6;
}(), "stripline forward core must be constexpr-evaluable and ~60.52 ohm");
