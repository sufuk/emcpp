// tests/shielding/cavity_resonance_test.cpp
//
// Catch2 v3 tests for emc::shielding cylindrical-cavity dominant-mode resonance.
//
// The closed form (from cavity_resonance.hpp):
//
//     f = (c / (2*pi*sqrt(eps_r))) * sqrt( (chi/r)^2 + (p*pi/l)^2 )
//
// where chi is a Bessel root (TE: J'_m root; TM: J_m root) and the axial term is
// p*pi/l. All expected values are hand-computed inline from this form — no CSV /
// golden vectors. The independent reference re-implements the kernel in plain
// doubles so the test is a true second opinion, not an echo of the library.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>   // std::ranges::find
#include <cmath>
#include <numbers>
#include <string_view>

#include <emc/shielding/cavity_resonance.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz
using emc::ErrorCode;
namespace sh = emc::shielding;

// Independent re-implementation of the cylinder kernel (the "second opinion").
static double ref_cyl(double eps_r, double chi, double r, double p, double l) {
    const double c  = 299'792'458.0;          // emc::constants::c
    const double pi = std::numbers::pi;
    const double kr = chi / r;                 // radial wavenumber  [1/m]
    const double kz = (p * pi) / l;            // axial wavenumber    [1/m]  (p=0 -> 0)
    return (c / (2.0 * pi * std::sqrt(eps_r))) * std::sqrt(kr * kr + kz * kz);
}

// (a) KNOWN-VALUE — hand-computed dominant mode of the default cylinder
//     (l = 1.0 m, r = 0.05 m, eps_r = 1.0). The dominant TE111 mode (chi = 1.841,
//     p = 1) has the smallest chi among the p=1 family:
//
//       f = c/(2*pi) * sqrt( (1.841/0.05)^2 + (pi/1.0)^2 )
//         = 47 713 451.6 * sqrt( 36.82^2 + 3.141593^2 )
//         ~= 1.763 19 GHz
TEST_CASE("cylindrical cavity dominant mode (default geometry) known value",
          "[shielding][cavity]") {
    sh::CylindricalCavityInput in{.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 1.0};
    auto r = sh::cylindrical_cavity_modes(in);
    REQUIRE(r.has_value());

    // The dominant resonance is the ef111 TE mode.
    auto it = std::ranges::find(r->modes, std::string_view{"ef111"},
                                &sh::ModeFrequency::label);
    REQUIRE(it != r->modes.end());

    const double f_expected = ref_cyl(1.0, 1.841, 0.05, 1.0, 1.0);   // ~1.76311e9 Hz
    REQUIRE(emc::test::approx(it->frequency, f_expected * Hz, 1e-9));

    // And dominant() must return exactly that mode's frequency (lowest of the 21).
    REQUIRE(emc::test::approx(r->dominant(), f_expected * Hz, 1e-9));

    // Hand-rounded absolute check (catches a gross prefactor / unit slip): 1.76319 GHz.
    REQUIRE(emc::test::approx(r->dominant(), 1.763'19 * si::giga<si::hertz>, 1e-4));
}

// (a') KNOWN-VALUE — a named TM mode, mf011 (chi = 2.405, p = 1):
//
//       f = c/(2*pi) * sqrt( (2.405/0.05)^2 + (pi/1.0)^2 ) ~= 2.302 GHz
TEST_CASE("cylindrical cavity mf011 known value", "[shielding][cavity]") {
    sh::CylindricalCavityInput in{.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 1.0};
    auto r = sh::cylindrical_cavity_modes(in);
    REQUIRE(r.has_value());

    auto it = std::ranges::find(r->modes, std::string_view{"mf011"},
                                &sh::ModeFrequency::label);
    REQUIRE(it != r->modes.end());

    const double f_expected = ref_cyl(1.0, 2.405, 0.05, 1.0, 1.0);
    REQUIRE(emc::test::approx(it->frequency, f_expected * Hz, 1e-9));
}

// (a'') KNOWN-VALUE — every one of the 21 modes reproduces the independent
//        reference across a spread of geometries (also a dielectric eps_r=4.3 case,
//        which exercises the 1/sqrt(eps_r) prefactor). Guards table ordering: each
//        (chi, p) row must map to the right radial/axial term.
TEST_CASE("cylindrical cavity matches independent reference (all modes)",
          "[shielding][cavity]") {
    const sh::CylindricalCavityInput cases[] = {
        {.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 1.0},
        {.length = 0.3 * m, .radius = 0.02 * m, .eps_r = 4.3},
        {.length = 2.0 * m, .radius = 0.10 * m, .eps_r = 2.2},
    };
    for (const auto& in : cases) {
        auto r = sh::cylindrical_cavity_modes(in);
        REQUIRE(r.has_value());
        for (std::size_t i = 0; i < sh::kCylindricalModeCount; ++i) {
            const auto& s = sh::kCylindricalModes[i];
            const double expected =
                ref_cyl(in.eps_r, s.chi, in.radius.numerical_value_in(m),
                        static_cast<double>(s.p), in.length.numerical_value_in(m));
            REQUIRE(emc::test::approx(r->modes[i].frequency, expected * Hz, 1e-9));
        }
    }
}

// (b) PROPERTY — geometric scaling: doubling radius AND length halves every mode
//     frequency. Both the radial (chi/r) and axial (p*pi/l) wavenumbers scale by
//     1/2, so the whole sqrt(...) and thus f scale by 1/2. Catches a stray unit
//     factor that would survive the per-mode known-value checks.
TEST_CASE("cylindrical cavity scales inversely with size",
          "[shielding][cavity][property]") {
    sh::CylindricalCavityInput a{.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 1.0};
    sh::CylindricalCavityInput b{.length = 2.0 * m, .radius = 0.10 * m, .eps_r = 1.0};
    auto ra = sh::cylindrical_cavity_modes(a);
    auto rb = sh::cylindrical_cavity_modes(b);
    REQUIRE(ra.has_value());
    REQUIRE(rb.has_value());
    for (std::size_t i = 0; i < sh::kCylindricalModeCount; ++i)
        REQUIRE(emc::test::approx(rb->modes[i].frequency,
                                  ra->modes[i].frequency / 2.0, 1e-9));
}

// (b') PROPERTY — dielectric scaling: f ~ 1/sqrt(eps_r). Filling the same cavity
//      with eps_r = 4 must divide every frequency by exactly 2.
TEST_CASE("cylindrical cavity scales as 1/sqrt(eps_r)",
          "[shielding][cavity][property]") {
    sh::CylindricalCavityInput air{.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 1.0};
    sh::CylindricalCavityInput die{.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 4.0};
    auto ra = sh::cylindrical_cavity_modes(air);
    auto rd = sh::cylindrical_cavity_modes(die);
    REQUIRE(ra.has_value());
    REQUIRE(rd.has_value());
    for (std::size_t i = 0; i < sh::kCylindricalModeCount; ++i)
        REQUIRE(emc::test::approx(rd->modes[i].frequency,
                                  ra->modes[i].frequency / 2.0, 1e-9));
}

// (c) VALIDATION / EDGE — out-of-domain inputs are typed errors, never inf/nan.
TEST_CASE("cylindrical cavity rejects bad input", "[shielding][cavity][validate]") {
    SECTION("zero radius -> OutOfRange on 'radius'") {
        auto r = sh::cylindrical_cavity_modes(
            {.length = 1.0 * m, .radius = 0.0 * m, .eps_r = 1.0});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "radius");
    }
    SECTION("zero length -> OutOfRange on 'length'") {
        auto r = sh::cylindrical_cavity_modes(
            {.length = 0.0 * m, .radius = 0.05 * m, .eps_r = 1.0});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "length");
    }
    SECTION("eps_r below 1 -> OutOfRange on 'eps_r'") {
        auto r = sh::cylindrical_cavity_modes(
            {.length = 1.0 * m, .radius = 0.05 * m, .eps_r = 0.5});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "eps_r");
    }
}

// (d) COMPILE-TIME — the Bessel-root spec table is a constexpr constant with the
//     right families and roots (the dominant ef111 is TE; mf011 is TM, J_0 first root).
static_assert(sh::kCylindricalModes.size() == sh::kCylindricalModeCount);
static_assert(sh::kCylindricalModes[0].family == sh::CylFamily::TE);   // ef111
static_assert(sh::kCylindricalModes[0].chi == 1.841);                  // J'_1 first root
static_assert(sh::kCylindricalModes[9].family == sh::CylFamily::TM);   // mf011
static_assert(sh::kCylindricalModes[9].chi == 2.405);                  // J_0 first root
