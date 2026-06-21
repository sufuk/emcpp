// tests/component/resistance_test.cpp
//
// Rewritten Catch2 v3 tests for the four resistance calculators in
// emc::component (Circuit Board Trace, Cylindrical Conductor, Rectangular
// Conductor, Standard Gauge Wire / AWG). API names come from
// include/emc/component/resistance.hpp; formulas/expected values from
// plan/implementation/05-component-resistance.md.
//
// Rules respected here:
//  * Each calculator has its own Input type and its own free function — every
//    call site uses an EXPLICITLY-TYPED Input (e.g. ec::TraceResistanceInput{...}).
//  * Metric units only (mm, um, m, MHz...). No inch/mil.
//  * Only PUBLIC functions are called; nothing from emc::component::detail.
//  * approx(actual, expected): per-length Result fields compare against a
//    quantity in (si::ohm / si::metre); totals against si::ohm; skin depth/
//    diameter against metres.

#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <emc/component/resistance.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using namespace mp_units::si;   // metre, milli, micro prefixes
using emc::ErrorCode;
namespace ec = emc::component;

// ===========================================================================
//  Circuit Board Trace
// ===========================================================================

// (a) KNOWN-VALUE — pure DC regime (1 Hz => delta enormous => DC branch).
//     R = rho * l / (w*t) = 1.72e-8 * 1 / (1e-3 * 1e-3) = 0.0172 Ohm.
//     R per length = rho / (w*t) = 0.0172 Ohm/m.
TEST_CASE("trace_resistance DC limit equals rho*l/(w*t)", "[component][resistance][trace]") {
    auto r = ec::trace_resistance(ec::TraceResistanceInput{
        .frequency = 1.0 * Hz,             // delta huge -> DC branch guaranteed
        .length    = 1.0 * m,
        .width     = 1.0 * (milli<metre>),
        .thickness = 1.0 * (milli<metre>),
        // .resistivity defaults to copper 1.72e-8 ohm*m
    });
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    REQUIRE(emc::test::approx(r->resistance_total, 0.0172 * ohm, 1e-9));
    REQUIRE(emc::test::approx(r->resistance_per_length, 0.0172 * ohm, 1e-9));
}

// (b) PROPERTY / MONOTONICITY — higher frequency never lowers AC resistance.
TEST_CASE("trace_resistance is monotone non-decreasing in frequency", "[component][resistance][trace]") {
    auto R = [](double f_hz) {
        return ec::trace_resistance(ec::TraceResistanceInput{
                   .frequency = f_hz * Hz,
                   .length    = 1.0 * m,
                   .width     = 5.0 * (milli<metre>),
                   .thickness = 35.0 * (micro<metre>),
               })
            ->resistance_total.numerical_value_in(ohm);
    };
    REQUIRE(R(1.0e9) >= R(1.0e6));
    REQUIRE(R(1.0e6) >= R(1.0e3));
}

// (c) VALIDATION — zero thickness => not positive => OutOfRange on "thickness".
TEST_CASE("trace_resistance rejects non-positive geometry", "[component][resistance][trace]") {
    auto r = ec::trace_resistance(ec::TraceResistanceInput{
        .frequency = 1.0 * MHz,
        .length    = 1.0 * m,
        .width     = 1.0 * (milli<metre>),
        .thickness = 0.0 * (milli<metre>),
    });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "thickness");
}

// ===========================================================================
//  Cylindrical Conductor
// ===========================================================================

// (a) KNOWN-VALUE — DC copper wire, hand-computed.
//     A = pi*(0.5e-3)^2 = 7.853982e-7 m^2 ; rho_cu = 1/5.96e7 = 1.67785e-8 ohm*m
//     R = rho*l/A = 1.67785e-8 / 7.853982e-7 = 0.021363 Ohm.
TEST_CASE("cylindrical_conductor DC copper", "[component][resistance][cylindrical]") {
    auto r = ec::cylindrical_conductor_resistance(ec::CylindricalConductorInput{
        .frequency = 1.0 * Hz,
        .length    = 1.0 * m,
        .diameter  = 1.0 * (milli<metre>),
        .material  = emc::materials::Material::Copper,
    });
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    REQUIRE(emc::test::approx(r->resistance_total, 0.021363 * ohm, 1e-3));
    REQUIRE(emc::test::approx(r->resistance_per_length, 0.021363 * ohm, 1e-3));
}

// (b) CONSISTENCY — the back-filled rho and sigma are reciprocals.
TEST_CASE("cylindrical_conductor back-fills consistent rho/sigma", "[component][resistance][cylindrical]") {
    auto r = ec::cylindrical_conductor_resistance(ec::CylindricalConductorInput{
        .frequency = 1.0 * MHz,
        .length    = 1.0 * m,
        .diameter  = 1.0 * (milli<metre>),
        .material  = emc::materials::Material::Silver,
    });
    REQUIRE(r.has_value());
    const double rho   = r->resistivity.numerical_value_in(ohm * m);
    const double sigma = r->conductivity.numerical_value_in(S / m);
    REQUIRE(rho * sigma == Catch::Approx(1.0).epsilon(1e-12));
}

// (c) VALIDATION — Custom with no overrides => InvalidInput.
TEST_CASE("cylindrical_conductor Custom without props errors", "[component][resistance][cylindrical]") {
    auto r = ec::cylindrical_conductor_resistance(ec::CylindricalConductorInput{
        .frequency = 1.0 * MHz,
        .length    = 1.0 * m,
        .diameter  = 1.0 * (milli<metre>),
        .material  = emc::materials::Material::Custom,   // no rho/sigma supplied
    });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidInput);
}

// ===========================================================================
//  Rectangular Conductor
// ===========================================================================

// (a) KNOWN-VALUE — square cross-section DC copper, hand-computed.
//     rho_cu = 1/5.96e7 = 1.67785e-8 ; A = 1e-6 m^2 ; R = 0.0167785 Ohm.
TEST_CASE("rectangular_conductor DC square copper", "[component][resistance][rectangular]") {
    auto r = ec::rectangular_conductor_resistance(ec::RectangularConductorInput{
        .frequency = 1.0 * Hz,
        .length    = 1.0 * m,
        .width     = 1.0 * (milli<metre>),
        .thickness = 1.0 * (milli<metre>),
        .material  = emc::materials::Material::Copper,
    });
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    REQUIRE(emc::test::approx(r->resistance_total, 0.0167785 * ohm, 1e-4));
    REQUIRE(emc::test::approx(r->resistance_per_length, 0.0167785 * ohm, 1e-4));
}

// (b) EQUIVALENCE — a rectangle equals the trace formula at the same resistivity,
//     proving the shared core is wired identically across both calculators.
TEST_CASE("rectangular reduces to trace at the same resistivity", "[component][resistance][rectangular]") {
    auto rect = ec::rectangular_conductor_resistance(ec::RectangularConductorInput{
        .frequency = 10.0 * MHz,
        .length    = 1.0 * m,
        .width     = 3.0 * (milli<metre>),
        .thickness = 1.0 * (milli<metre>),
        .material  = emc::materials::Material::Copper,
    });
    const double rho = emc::materials::copper.resistivity.numerical_value_in(ohm * m);
    auto trace = ec::trace_resistance(ec::TraceResistanceInput{
        .frequency   = 10.0 * MHz,
        .length      = 1.0 * m,
        .width       = 3.0 * (milli<metre>),
        .thickness   = 1.0 * (milli<metre>),
        .resistivity = rho * (ohm * m),
    });
    REQUIRE(rect.has_value());
    REQUIRE(trace.has_value());
    REQUIRE(emc::test::approx(rect->resistance_total, trace->resistance_total, 1e-9));
    REQUIRE(emc::test::approx(rect->resistance_per_length, trace->resistance_per_length, 1e-9));
}

// (c) VALIDATION — zero width => OutOfRange on "width".
TEST_CASE("rectangular_conductor rejects zero width", "[component][resistance][rectangular]") {
    auto r = ec::rectangular_conductor_resistance(ec::RectangularConductorInput{
        .frequency = 1.0 * MHz,
        .length    = 1.0 * m,
        .width     = 0.0 * (milli<metre>),
        .thickness = 1.0 * (milli<metre>),
    });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "width");
}

// ===========================================================================
//  Standard Gauge Wire (AWG)
// ===========================================================================

// (a) GAUGE PARSER — aught sizes and integers; trailing garbage rejected.
TEST_CASE("parse_awg_gauge handles aught and integer gauges", "[component][resistance][gauge]") {
    REQUIRE(ec::parse_awg_gauge("OOOO").value() == -3);
    REQUIRE(ec::parse_awg_gauge("OOO").value()  == -2);
    REQUIRE(ec::parse_awg_gauge("OO").value()   == -1);
    REQUIRE(ec::parse_awg_gauge("O").value()    ==  0);
    REQUIRE(ec::parse_awg_gauge("0").value()    ==  0);
    REQUIRE(ec::parse_awg_gauge("12").value()   == 12);
    REQUIRE(ec::parse_awg_gauge("-3").value()   == -3);
    REQUIRE_FALSE(ec::parse_awg_gauge("12x").has_value());   // trailing garbage rejected
    REQUIRE(ec::parse_awg_gauge("oops").error().code == ErrorCode::InvalidInput);
}

// (b) KNOWN DIAMETER — AWG 36 is the law's anchor: 92^0 = 1 => dm = 0.0254*0.005 = 127 um.
//     AWG 0 diameter ~ 8.2515 mm (handbook value).
TEST_CASE("awg_diameter at gauge 36 is the 5-mil anchor", "[component][resistance][gauge]") {
    REQUIRE(emc::test::approx(ec::awg_diameter(36), 127.0 * (micro<metre>), 1e-6));
    REQUIRE(emc::test::approx(ec::awg_diameter(0), 8.2515 * (milli<metre>), 1e-3));
}

// (c) KNOWN-VALUE — DC AWG-24 copper resistance (handbook ~ 0.0842 Ohm/m;
//     table copper sigma differs slightly, so use a loose tolerance).
TEST_CASE("standard_gauge_wire AWG-24 DC copper", "[component][resistance][gauge]") {
    auto r = ec::standard_gauge_wire_resistance(ec::StandardGaugeWireInput{
        .frequency = 1.0 * Hz,
        .length    = 1.0 * m,
        .gauge     = "24",
        .material  = emc::materials::Material::Copper,
    });
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    REQUIRE(r->resistance_total.numerical_value_in(ohm) == Catch::Approx(0.0842).epsilon(0.05));
    // For a 1 m length the per-length value equals the total numerically.
    REQUIRE(emc::test::approx(r->resistance_per_length,
                              r->resistance_total.numerical_value_in(ohm) * ohm, 1e-9));
}

// (d) VALIDATION — bad gauge string => InvalidInput on "gauge".
TEST_CASE("standard_gauge_wire rejects bad gauge", "[component][resistance][gauge]") {
    auto r = ec::standard_gauge_wire_resistance(ec::StandardGaugeWireInput{
        .frequency = 1.0 * MHz,
        .length    = 1.0 * m,
        .gauge     = "12x",
        .material  = emc::materials::Material::Copper,
    });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidInput);
    REQUIRE(r.error().field == "gauge");
}
