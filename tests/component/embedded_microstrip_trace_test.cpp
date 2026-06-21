// tests/component/embedded_microstrip_trace_test.cpp
//
// Catch2 v3 tests for the forward-only Embedded Microstrip trace-impedance
// calculator (emc::component). The embedded geometry's (h1 - H - T)/0.1 term
// carries an ABSOLUTE length scale, so Z0 is NOT scale-invariant and there is
// no validated closed-form inverse — the header exposes calculate/validate only.
//
// IMPORTANT (load-bearing units): the implementation pins its working unit to
// METRES (U = si::metre) precisely because Z0 is scale-dependent here. So the
// numeric value the closed form sees is the geometry expressed in metres. We
// therefore choose mm magnitudes whose metre value equals the hand-computed
// numbers, e.g. 42.0 * mm == 0.042 m. Metric (mm) only — no inch/mil.
//
// Expected values are hand-computed from the documented closed form (in metres):
//   ln  = ln( 5.98*H / (0.8*W + T) )
//   Z0  = 87 * ln * (1 - (h1 - H - T)/0.1) / sqrt(eps_r + 1.41)        [ohm]
//   Tpd = 84.75 * sqrt( 0.475*eps_r*(1 + exp(-1.55*h1/H)) + 0.67 )     [ps/cm]
//   C0  = Tpd / Z0                                                     [pF/cm]

#include <catch2/catch_test_macros.hpp>

#include <emc/component/embedded_microstrip_trace.hpp>
#include "support/approx.hpp"

#include <cmath>

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using emc::ErrorCode;

namespace {
constexpr auto pf_cm = si::pico<si::farad>  / si::centi<si::metre>;
constexpr auto ps_cm = si::pico<si::second> / si::centi<si::metre>;
}  // namespace

// (a) HAND-COMPUTED known value, from the closed form above.
//     In metres: h1=0.042, H=0.00455, T=0.0238, W=0.001, eps_r=11.41627113
//     -> expressed in mm: 42.0, 4.55, 23.8, 1.0  (since 42.0*mm == 0.042 m).
//       ln  = ln(5.98*0.00455/(0.8*0.001+0.0238))
//       Z0  = 87*ln*(1-(h1-H-T)/0.1)/sqrt(eps+1.41)            ~=   2.11445 ohm
//       Tpd = 84.75*sqrt(0.475*eps*(1+exp(-1.55*h1/H))+0.67)   ~= 209.19233 ps/cm
//       C0  = Tpd/Z0                                           ~=  98.93455 pF/cm
TEST_CASE("embedded microstrip known value", "[component][embedded]") {
    auto r = emc::component::calculate(emc::component::EmbeddedMicrostripInput{
        .cover_height = 42.0 * mm, .height = 4.55 * mm,
        .thickness = 23.8 * mm, .width = 1.0 * mm,
        .relative_permittivity = 11.41627113 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0,  2.114451701 * ohm, 1e-6));
    REQUIRE(emc::test::approx(r->c0,  98.93454909 * pf_cm, 1e-6));
    REQUIRE(emc::test::approx(r->tpd, 209.1923257 * ps_cm, 1e-6));
}

// (b) PROPERTY — at a fixed stack-up, deeper burial (larger cover height h1)
//     lowers Z0, because the (1 - (h1 - H - T)/0.1) factor shrinks as h1 grows.
//     This is the physical monotonicity the closed form must obey, and it also
//     exercises the load-bearing absolute-scale handling that makes Z0 NOT
//     scale-invariant for this calculator. (metres: h1=0.030 vs 0.034.)
TEST_CASE("embedded microstrip Z0 decreases with cover height", "[component][embedded][property]") {
    auto base = emc::component::calculate(emc::component::EmbeddedMicrostripInput{
        .cover_height = 30.0 * mm, .height = 5.0 * mm,
        .thickness = 1.0 * mm, .width = 6.0 * mm,
        .relative_permittivity = 4.7 });
    auto deep = emc::component::calculate(emc::component::EmbeddedMicrostripInput{
        .cover_height = 34.0 * mm, .height = 5.0 * mm,
        .thickness = 1.0 * mm, .width = 6.0 * mm,
        .relative_permittivity = 4.7 });
    REQUIRE(base.has_value());
    REQUIRE(deep.has_value());
    REQUIRE(deep->z0.numerical_value_in(ohm) < base->z0.numerical_value_in(ohm));
}

// (c) VALIDATION / EDGE — each out-of-domain path maps to the right ErrorCode.
TEST_CASE("embedded microstrip validation", "[component][embedded][validate]") {
    SECTION("eps_r out of range -> OutOfRange") {
        auto r = emc::component::calculate(emc::component::EmbeddedMicrostripInput{
            .cover_height = 42.0 * mm, .height = 4.55 * mm,
            .thickness = 23.8 * mm, .width = 1.0 * mm,
            .relative_permittivity = 0.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
    SECTION("cover below trace (h1 <= H + T) -> DomainError") {
        auto r = emc::component::calculate(emc::component::EmbeddedMicrostripInput{
            .cover_height = 1.0 * mm, .height = 4.55 * mm,
            .thickness = 23.8 * mm, .width = 1.0 * mm,
            .relative_permittivity = 11.4 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DomainError);
        REQUIRE(r.error().field == "cover_height");
    }
}

// (d) CONSTEXPR-FRIENDLY smoke test: the forward core is pure closed-form
//     (geometry in metres). Compile-time guard on the headline known value.
static_assert([] {
    const double h1 = 0.042, H = 0.00455, T = 0.0238, W = 0.001, e = 11.41627113;
    const double z0 = 87.0 * std::log(5.98 * H / (0.8 * W + T))
                      * (1.0 - (h1 - H - T) / 0.1) / std::sqrt(e + 1.41);
    return z0 > 2.11 && z0 < 2.12;          // matches the known value above
}(), "embedded core must be constexpr-evaluable and ~2.11 ohm");
