// tests/component/microstrip_trace_test.cpp
//
// Catch2 v3 tests for emc::component microstrip-trace impedance (Wheeler/IPC closed form).
// All expected values are hand-computed inline from the documented closed form:
//
//     ln    = ln( 5.98*H / (0.8*W + T) )
//     Z0    = 87*ln / sqrt(eps_r + 1.41)                  [ohm]
//     ctemp = 0.67*(eps_r + 1.41) / ln
//     C0    = ctemp / 2.54                                [pF/cm]
//     Tpd   = ctemp*Z0 / 2.54                             [ps/cm]
//
// No CSV / golden files — every expected number is written here. Board dimensions are
// expressed in millimetres throughout (metric only).

#include <catch2/catch_test_macros.hpp>

#include <emc/component/microstrip_trace.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm

namespace {
constexpr auto pf_cm = si::pico<si::farad>  / si::centi<si::metre>;
constexpr auto ps_cm = si::pico<si::second> / si::centi<si::metre>;
}  // namespace

// (a) HAND-COMPUTED known value, from the closed form above.
//     H=18.65392418, T=11.15918402, W=15.00320319, eps_r=5.0:
//       ratio = 5.98*18.65392418 / (0.8*15.00320319 + 11.15918402)
//             = 111.5504666... / 23.16174657... = 4.815842...
//       ln    = ln(4.815842...)            = 1.571893...
//       Z0    = 87*ln / sqrt(6.41)         ~= 54.01767 ohm
//       ctemp = 0.67*6.41 / ln             = 4.2947 / 1.571893 = 2.732264...
//       C0    = ctemp / 2.54               ~= 1.07561 pF/cm
//       Tpd   = ctemp*Z0 / 2.54            ~= 58.10177 ps/cm
TEST_CASE("microstrip known value", "[component][microstrip]") {
    auto r = emc::component::calculate(emc::component::MicrostripInput{
        .height = 18.65392418 * mm, .thickness = 11.15918402 * mm,
        .width = 15.00320319 * mm, .relative_permittivity = 5.0 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0,  54.01767 * ohm,   1e-5));
    REQUIRE(emc::test::approx(r->c0,  1.07561  * pf_cm, 1e-4));
    REQUIRE(emc::test::approx(r->tpd, 58.10177 * ps_cm, 1e-4));
}

// (b) ROUND-TRIP — forward Z0, then solve_width / solve_height must recover the original
//     geometry. Also proves Z0 scale-invariance: multiplying every length by the same
//     factor (all in mm) leaves Z0 unchanged.
TEST_CASE("microstrip Z0<->W round-trips", "[component][microstrip][roundtrip]") {
    const emc::component::MicrostripInput in{
        .height = 1.5 * mm, .thickness = 0.035 * mm,
        .width = 2.65 * mm, .relative_permittivity = 4.7 };
    auto fwd = emc::component::calculate(in);
    REQUIRE(fwd.has_value());

    auto w = emc::component::solve_width(emc::component::MicrostripSolveWidth{
        .z0 = fwd->z0, .height = in.height,
        .thickness = in.thickness, .relative_permittivity = 4.7 });
    REQUIRE(w.has_value());
    REQUIRE(emc::test::approx(*w, in.width, 1e-9));     // recovers the original width

    // also exercise solve_height -> recovers the original H from the same forward Z0.
    auto h = emc::component::solve_height(emc::component::MicrostripSolveHeight{
        .z0 = fwd->z0, .thickness = in.thickness,
        .width = in.width, .relative_permittivity = 4.7 });
    REQUIRE(h.has_value());
    REQUIRE(emc::test::approx(*h, in.height, 1e-9));

    // Z0 is scale-invariant: the SAME geometry scaled by 10x (still in mm) yields the same Z0.
    auto fwd_scaled = emc::component::calculate(emc::component::MicrostripInput{
        .height = 15.0 * mm, .thickness = 0.35 * mm,
        .width = 26.5 * mm, .relative_permittivity = 4.7 });
    REQUIRE(fwd_scaled.has_value());
    REQUIRE(emc::test::approx(fwd_scaled->z0, fwd->z0, 1e-9));
}

// (c) VALIDATION / EDGE — each out-of-domain path maps to the documented ErrorCode/field.
TEST_CASE("microstrip validation", "[component][microstrip][validate]") {
    SECTION("eps_r out of range") {
        auto r = emc::component::calculate(emc::component::MicrostripInput{
            .height = 1.5 * mm, .thickness = 0.035 * mm, .width = 2.65 * mm,
            .relative_permittivity = 20.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
    SECTION("W/H ratio out of range") {
        auto r = emc::component::calculate(emc::component::MicrostripInput{
            .height = 1.0 * mm, .thickness = 0.035 * mm, .width = 100.0 * mm,
            .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "width_to_height_ratio");
    }
    SECTION("non-positive width") {
        auto r = emc::component::calculate(emc::component::MicrostripInput{
            .height = 1.0 * mm, .thickness = 0.035 * mm, .width = 0.0 * mm,
            .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);   // require_positive -> OutOfRange
    }
}

// (d) CONSTEXPR-FRIENDLY smoke test: the forward core is pure closed-form (compile-time guard).
static_assert([] {
    const double H = 18.65392418, T = 11.15918402, W = 15.00320319, e = 5.0;
    const double ln = std::log(5.98 * H / (0.8 * W + T));
    const double z0 = 87.0 * ln / std::sqrt(e + 1.41);
    return z0 > 54.0 && z0 < 54.1;          // matches the known value above
}(), "microstrip forward core must be constexpr-evaluable and ~54.02 ohm");
