// tests/component/transmission_line_test.cpp
//
// Catch2 v3 tests for the seven emc::component transmission-line calculators:
//   Coaxial Line, Microstrip Line, Stripline, Narrow Trace Over Plane,
//   Wide Trace Over Plane, Wire Over Plane, Wire Pair.
//
// calculate() is OVERLOADED across seven Input types, so every call site uses an
// EXPLICITLY-TYPED Input (CoaxialLineInput{...}, MicrostripLineInput{...}, ...);
// a bare brace-init would be ambiguous and would not compile.
//
// Each calculator gets:
//   (a) a hand-computed / textbook KNOWN-VALUE check,
//   (b) a property / monotonicity check where meaningful,
//   (c) a validation / edge check that asserts the typed error code.
//
// All expected values are written inline (hand-computed or independently
// recomputed from the closed-form formulas) — no CSV/golden files.

#include <cmath>     // std::log10, std::log, std::sqrt, std::acosh, std::pow

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <emc/component/transmission_line.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/materials.hpp>

#include <mp-units/systems/si.h>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // mm, cm, m, Hz, MHz, ohm, F, H, S, ...
namespace tl = emc::component;

// ===========================================================================
//  Coaxial Line
// ===========================================================================

// (a) Hand-computed known value: D=2 in (50.8 mm), d=1 in (25.4 mm), eps_r=1.
//     Z0 = 138*log10(D/d)/sqrt(eps_r) is RATIO-based, so D/d=2 gives 138*log10(2) = 41.541 ohm
//     whether the diameters are supplied in mm or inches.
TEST_CASE("coaxial known value", "[component][transmission_line][coaxial]") {
    auto r = tl::calculate(tl::CoaxialLineInput{ .outer_diameter        = 50.8 * mm,
                                                 .inner_diameter        = 25.4 * mm,
                                                 .relative_permittivity = 1.0 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->impedance, 41.541 * ohm, 1e-3));
}

// (b) Textbook reference: D=3.2 in, d=0.9 in, eps_r=2.3, supplied in mm and recomputed from
//     the closed form. f_cutoff's 11.8 constant assumes the diameters in INCHES, so the
//     reference recompute uses the inch values (D_in, d_in) while the inputs stay metric.
TEST_CASE("coaxial textbook reference", "[component][transmission_line][coaxial]") {
    const double D_in = 3.2, d_in = 0.9, e = 2.3;        // inches, for the reference recompute
    const double D_mm = D_in * 25.4, d_mm = d_in * 25.4; // metric inputs to the calculator
    auto r = tl::calculate(tl::CoaxialLineInput{ .outer_diameter        = D_mm * mm,
                                                 .inner_diameter        = d_mm * mm,
                                                 .relative_permittivity = e });
    REQUIRE(r.has_value());
    const double lg = std::log10(D_in / d_in), se = std::sqrt(e);
    CHECK(emc::test::approx(r->impedance, (138.0 * lg / se) * ohm, 1e-4));
    CHECK(emc::test::approx(r->cutoff_frequency,
          (11.8 / (se * emc::constants::pi * (D_in + d_in) / 2.0)) * si::giga<Hz>, 1e-4));
}

// (b2) Property — monotonicity: widening D (fixed d, eps_r) raises Z0 (Z0 ~ ln(D/d)).
TEST_CASE("coaxial Z0 grows with D/d", "[component][transmission_line][coaxial][property]") {
    auto a = tl::calculate(tl::CoaxialLineInput{ .outer_diameter = 3.0 * mm, .inner_diameter = 1.0 * mm,
                                                 .relative_permittivity = 2.0 });
    auto b = tl::calculate(tl::CoaxialLineInput{ .outer_diameter = 6.0 * mm, .inner_diameter = 1.0 * mm,
                                                 .relative_permittivity = 2.0 });
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(b->impedance.numerical_value_in(ohm) > a->impedance.numerical_value_in(ohm));
}

// (c) Validation: D <= d -> InvalidInput; eps_r < 1 -> OutOfRange.
TEST_CASE("coaxial rejects bad geometry", "[component][transmission_line][coaxial][validation]") {
    auto r1 = tl::calculate(tl::CoaxialLineInput{ .outer_diameter = 1.0 * mm, .inner_diameter = 2.0 * mm,
                                                  .relative_permittivity = 2.0 });
    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r1.error().code == emc::ErrorCode::InvalidInput);

    auto r2 = tl::calculate(tl::CoaxialLineInput{ .outer_diameter = 3.0 * mm, .inner_diameter = 1.0 * mm,
                                                  .relative_permittivity = 0.5 });
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r2.error().code == emc::ErrorCode::OutOfRange);
}

// ===========================================================================
//  Microstrip Line
// ===========================================================================

// (a) Hand-computed value on the W/H>1 branch: eps_r=10, W=9, H=1 (ratio=9).
TEST_CASE("microstrip known value (W/H>1)", "[component][transmission_line][microstrip]") {
    auto r = tl::calculate(tl::MicrostripLineInput{ .relative_permittivity = 10.0,
                                                    .width = 9.0 * mm, .height = 1.0 * mm });
    REQUIRE(r.has_value());
    const double eps_eff = (10.0 + 1) / 2 + (10.0 - 1) / (2 * std::sqrt(1 + 12 * (1.0 / 9.0)));
    REQUIRE(r->effective_permittivity == Catch::Approx(eps_eff).epsilon(1e-9));
    const double z0 = (120 * emc::constants::pi)
                    / (std::sqrt(eps_eff) * (9.0 + 1.393 + (2.0 / 3.0) * std::log(9.0 + 1.444)));
    REQUIRE(emc::test::approx(r->impedance, z0 * ohm, 1e-9));
}

// (a2) Hand-computed value on the W/H<1 branch: eps_r=4, W=1, H=2.
TEST_CASE("microstrip known value (W/H<1)", "[component][transmission_line][microstrip]") {
    const double e = 4.0, w = 1.0, h = 2.0, ratio = w / h, inv = h / w;
    auto r = tl::calculate(tl::MicrostripLineInput{ .relative_permittivity = e,
                                                    .width = w * mm, .height = h * mm });
    REQUIRE(r.has_value());
    const double eps_eff =
        (e + 1) / 2 + (e - 1) / 2 * (1 / std::sqrt(1 + 12 * inv) + 0.04 * std::pow(1 - ratio, 2));
    CHECK(r->effective_permittivity == Catch::Approx(eps_eff).epsilon(1e-9));
    const double z0 = (60.0 / std::sqrt(eps_eff)) * std::log(8.0 * inv + 0.25 * ratio);
    CHECK(emc::test::approx(r->impedance, z0 * ohm, 1e-9));
}

// (b) Property: 1 < eps_eff < eps_r (physical bound on the effective permittivity).
TEST_CASE("microstrip eps_eff bounded by eps_r", "[component][transmission_line][microstrip][property]") {
    auto r = tl::calculate(tl::MicrostripLineInput{ .relative_permittivity = 4.4,
                                                    .width = 3.0 * mm, .height = 1.6 * mm });
    REQUIRE(r.has_value());
    REQUIRE(r->effective_permittivity > 1.0);
    REQUIRE(r->effective_permittivity < 4.4);
}

// (c) Validation: W == H sits on the Hammerstad branch boundary -> Unsupported.
TEST_CASE("microstrip W==H is Unsupported", "[component][transmission_line][microstrip][validation]") {
    auto r = tl::calculate(tl::MicrostripLineInput{ .relative_permittivity = 4.0,
                                                    .width = 2.0 * mm, .height = 2.0 * mm });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::Unsupported);
}

// ===========================================================================
//  Stripline
// ===========================================================================

// (a) Hand-computed value: eps_r=4, h=t=w=1 -> Z0 = (60/2)*ln(1.9*3/1.8) = 30*ln(3.1666...).
TEST_CASE("stripline known value", "[component][transmission_line][stripline]") {
    auto r = tl::calculate(tl::StriplineInput{ .relative_permittivity = 4.0,
                                               .width = 1.0 * m, .height = 1.0 * m, .thickness = 1.0 * m });
    REQUIRE(r.has_value());
    const double z0 = 30.0 * std::log(1.9 * 3.0 / 1.8);
    REQUIRE(emc::test::approx(r->impedance, z0 * ohm, 1e-9));
}

// (b) Textbook geometry, independently recomputed: w=0.2, h=0.4, t=0.035 mm, eps_r=4.2.
TEST_CASE("stripline textbook reference", "[component][transmission_line][stripline]") {
    const double w = 0.2e-3, h = 0.4e-3, t = 0.035e-3, e = 4.2;
    auto r = tl::calculate(tl::StriplineInput{ .relative_permittivity = e,
                                               .width = 0.2 * mm, .height = 0.4 * mm, .thickness = 0.035 * mm });
    REQUIRE(r.has_value());
    const double z0 = (60.0 / std::sqrt(e)) * std::log((1.9 * (2 * h + t)) / (0.8 * w + t));
    CHECK(emc::test::approx(r->impedance, z0 * ohm, 1e-6));
}

// (b2) Property: higher eps_r -> lower Z0 (the 1/sqrt(eps_r) scaling).
TEST_CASE("stripline Z0 falls with eps_r", "[component][transmission_line][stripline][property]") {
    auto lo = tl::calculate(tl::StriplineInput{ .relative_permittivity = 2.0,
                                                .width = 0.2 * mm, .height = 0.4 * mm, .thickness = 0.035 * mm });
    auto hi = tl::calculate(tl::StriplineInput{ .relative_permittivity = 8.0,
                                                .width = 0.2 * mm, .height = 0.4 * mm, .thickness = 0.035 * mm });
    REQUIRE(lo.has_value());
    REQUIRE(hi.has_value());
    REQUIRE(hi->impedance.numerical_value_in(ohm) < lo->impedance.numerical_value_in(ohm));
}

// (c) Validation: w=0 -> require_positive -> OutOfRange.
TEST_CASE("stripline rejects bad input", "[component][transmission_line][stripline][validation]") {
    auto r = tl::calculate(tl::StriplineInput{ .relative_permittivity = 4.0,
                                               .width = 0.0 * m, .height = 1.0 * m, .thickness = 1.0 * m });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}

// ===========================================================================
//  Narrow Trace Over Plane  (h > w)
// ===========================================================================

// (a) Hand-computed L: h=10mm, w=1mm -> L = 0.2*acosh(40) ~ 0.87638 uH/m.
TEST_CASE("narrow trace known L", "[component][transmission_line][narrow]") {
    auto r = tl::calculate(tl::NarrowTraceInput{ .frequency = 1.0 * MHz,
                                                 .trace_height = 10.0 * mm, .trace_width = 1.0 * mm,
                                                 .trace_thickness = 0.035 * mm,
                                                 .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, (0.2 * std::acosh(40.0)) * (si::micro<H> / m), 1e-9));
}

// (b) Z0 from hand-computed L and C with an explicit (Custom) sigma, independent recompute.
TEST_CASE("narrow trace Z0 reference", "[component][transmission_line][narrow]") {
    const double h = 11e-3, w = 5e-3, e = 1.0;
    auto r = tl::calculate(tl::NarrowTraceInput{ .frequency = 22.0 * MHz,
                                                 .trace_height = 11.0 * mm, .trace_width = 5.0 * mm,
                                                 .trace_thickness = 2.0 * mm,
                                                 .conductor = emc::materials::Material::Custom,
                                                 .custom_sigma = 5.96e7 * (S / m),
                                                 .relative_permittivity = e });
    REQUIRE(r.has_value());
    const double geom = std::acosh(4 * h / w);
    const double L = 0.2 * geom;
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);
    const double C = (2 * emc::constants::pi * eps0 * e / geom) * 1e12;
    const double Z0 = std::sqrt(1e6 * L / C);
    CHECK(emc::test::approx(r->characteristic_impedance, Z0 * ohm, 1e-4));
}

// (c) Validation: h <= w -> InvalidInput.
TEST_CASE("narrow trace requires h>w", "[component][transmission_line][narrow][validation]") {
    auto r = tl::calculate(tl::NarrowTraceInput{ .frequency = 1.0 * MHz,
                                                 .trace_height = 1.0 * mm, .trace_width = 5.0 * mm,
                                                 .trace_thickness = 0.035 * mm,
                                                 .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}

// ===========================================================================
//  Wide Trace Over Plane  (w > 5h)
// ===========================================================================

// (a) Hand-computed L: h=1mm, w=10mm -> L = 0.4*pi*0.1 = 0.1256637 uH/m.
TEST_CASE("wide trace known L", "[component][transmission_line][wide]") {
    auto r = tl::calculate(tl::WideTraceInput{ .frequency = 1.0 * MHz,
                                               .trace_height = 1.0 * mm, .trace_width = 10.0 * mm,
                                               .trace_thickness = 0.035 * mm,
                                               .conductor = emc::materials::Material::Copper,
                                               .relative_permittivity = 1.0 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance,
            (0.4 * emc::constants::pi * 0.1) * (si::micro<H> / m), 1e-9));
}

// (b) Z0 from hand-computed L and C with explicit sigma, independent recompute.
TEST_CASE("wide trace Z0 reference", "[component][transmission_line][wide]") {
    const double h = 0.2e-3, w = 5e-3, e = 4.4;
    auto r = tl::calculate(tl::WideTraceInput{ .frequency = 100.0 * MHz,
                                               .trace_height = 0.2 * mm, .trace_width = 5.0 * mm,
                                               .trace_thickness = 0.035 * mm,
                                               .conductor = emc::materials::Material::Custom,
                                               .custom_sigma = 5.96e7 * (S / m),
                                               .relative_permittivity = e });
    REQUIRE(r.has_value());
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);
    const double L = 0.4 * emc::constants::pi * h / w;
    const double C = (eps0 * e * w / h) * 1e12;
    CHECK(emc::test::approx(r->characteristic_impedance, std::sqrt(1e6 * L / C) * ohm, 1e-4));
}

// (c) Validation: w <= 5h -> InvalidInput.
TEST_CASE("wide trace requires w>5h", "[component][transmission_line][wide][validation]") {
    auto r = tl::calculate(tl::WideTraceInput{ .frequency = 1.0 * MHz,
                                               .trace_height = 1.0 * mm, .trace_width = 4.0 * mm,
                                               .trace_thickness = 0.035 * mm,
                                               .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}

// ===========================================================================
//  Wire Over Plane  (h > a)
// ===========================================================================

// (a) Hand-computed L: h=22cm, a=20cm -> L = 0.2*acosh(1.1).
TEST_CASE("wire over plane known L", "[component][transmission_line][wireplane]") {
    auto r = tl::calculate(tl::WireOverPlaneInput{ .frequency = 5.0 * Hz,
                                                   .wire_height = 22.0 * cm, .wire_radius = 20.0 * cm,
                                                   .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, (0.2 * std::acosh(1.1)) * (si::micro<H> / m), 1e-9));
}

// (b) L at a wider separation (h=10cm, a=1cm) -> 0.2*acosh(10), independent recompute.
TEST_CASE("wire over plane L reference", "[component][transmission_line][wireplane]") {
    auto r = tl::calculate(tl::WireOverPlaneInput{ .frequency = 5.0 * Hz,
                                                   .wire_height = 10.0 * cm, .wire_radius = 1.0 * cm,
                                                   .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    CHECK(emc::test::approx(r->inductance, (0.2 * std::acosh(10.0)) * (si::micro<H> / m), 1e-9));
}

// (c) Validation: h <= a -> InvalidInput.
TEST_CASE("wire over plane requires h>a", "[component][transmission_line][wireplane][validation]") {
    auto r = tl::calculate(tl::WireOverPlaneInput{ .frequency = 5.0 * Hz,
                                                   .wire_height = 10.0 * cm, .wire_radius = 20.0 * cm,
                                                   .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}

// ===========================================================================
//  Wire Pair  (s > d)
// ===========================================================================

// (a) Hand-computed L: s=10cm, d=1cm -> L = 0.4*acosh(10).
TEST_CASE("wire pair known L", "[component][transmission_line][wirepair]") {
    auto r = tl::calculate(tl::WirePairInput{ .frequency = 1.0 * MHz,
                                              .spacing = 10.0 * cm, .diameter = 1.0 * cm,
                                              .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, (0.4 * std::acosh(10.0)) * (si::micro<H> / m), 1e-9));
}

// (b) Property: at identical geometry, the wire-pair R is exactly twice the single
//     wire-over-plane R (the 2000 vs 1000 R numerator, same effective area).
TEST_CASE("wire pair R is double the single-wire R", "[component][transmission_line][wirepair][property]") {
    const auto f = 1.0 * MHz;
    const auto a = 0.5 * cm;   // radius 0.5cm -> diameter 1cm
    auto pair = tl::calculate(tl::WirePairInput{ .frequency = f, .spacing = 10.0 * cm, .diameter = 1.0 * cm,
                                                 .conductor = emc::materials::Material::Copper });
    auto wire = tl::calculate(tl::WireOverPlaneInput{ .frequency = f, .wire_height = 10.0 * cm, .wire_radius = a,
                                                      .conductor = emc::materials::Material::Copper });
    REQUIRE(pair.has_value());
    REQUIRE(wire.has_value());
    const double rp = pair->resistance.numerical_value_in(si::milli<ohm> / m);
    const double rw = wire->resistance.numerical_value_in(si::milli<ohm> / m);
    REQUIRE(rp == Catch::Approx(2.0 * rw).epsilon(1e-9));
}

// (c) Validation: s <= d -> InvalidInput.
TEST_CASE("wire pair requires s>d", "[component][transmission_line][wirepair][validation]") {
    auto r = tl::calculate(tl::WirePairInput{ .frequency = 1.0 * MHz,
                                              .spacing = 1.0 * cm, .diameter = 2.0 * cm,
                                              .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}
