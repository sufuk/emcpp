// tests/cabling/braid_coverage_test.cpp
//
// Tests for emc::cabling::braid_optical_coverage / calculate(BraidCoverageInput).
// Expected values are hand-computed from the closed form:
//   theta = atan( 2*pi*(D + 2d)*P / C )
//   F     = (P * N * d) / sin(theta)
//   OC    = 2F - F^2          (stored as fraction; percent() applies *100)
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <mp-units/systems/si.h>

#include <emc/cabling/braid_coverage.hpp>
#include <emc/core/constants.hpp>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

// (a) KNOWN-VALUE TEST -- the header's worked-example default geometry
//     (a typical RG-style braid), hand-computed from the closed form:
//        theta = 0.2679566261 rad
//        F     = 0.5494151518
//        OC    = 79.6973294554 %  (fraction 0.796973294554)
TEST_CASE("braid coverage worked example is 79.697 %", "[cabling][braid][known]") {
    const emc::cabling::BraidCoverageInput in{
        .d = 0.000114 * m, .D = 0.002950 * m, .P = 220.0, .C = 16.0, .N = 5.8};

    const auto r = emc::cabling::calculate(in);
    REQUIRE(r.has_value());

    // optical_coverage is the stored fraction (NOT yet *100).
    REQUIRE(emc::test::approx(r->optical_coverage, 0.7969732946 * one, 1e-9));
    // percent() is the display-boundary *100 of that same fraction.
    REQUIRE(emc::test::approx(r->percent() * one, 79.6973294554 * one, 1e-9));
    REQUIRE(emc::test::approx(r->weave_angle, 0.2679566261 * rad, 1e-9));
    REQUIRE(emc::test::approx(r->fill_factor * one, 0.5494151518 * one, 1e-9));
}

// (b) ORACLE / ROUND-TRIP TEST -- recompute the full closed form independently
//     for a second geometry and confirm the implementation matches to ~machine
//     epsilon. This pins the exact arithmetic the calculator performs.
TEST_CASE("braid coverage matches an independent closed-form oracle",
          "[cabling][braid][oracle]") {
    const emc::cabling::BraidCoverageInput in{
        .d = 0.000090 * m, .D = 0.004000 * m, .P = 180.0, .C = 24.0, .N = 7.0};

    const auto out = emc::cabling::calculate(in);
    REQUIRE(out.has_value());

    const double d = in.d.numerical_value_in(m);
    const double D = in.D.numerical_value_in(m);
    const double theta = std::atan(2.0 * emc::constants::pi * (D + 2.0 * d) * in.P / in.C);
    const double F = (in.P * in.N * d) / std::sin(theta);
    const double oc_fraction = 2.0 * F - F * F;

    REQUIRE(emc::test::approx(out->weave_angle, theta * rad, 1e-12));
    REQUIRE(emc::test::approx(out->fill_factor * one, F * one, 1e-12));
    REQUIRE(emc::test::approx(out->optical_coverage, oc_fraction * one, 1e-12));
    REQUIRE(emc::test::approx(out->percent() * one, 100.0 * oc_fraction * one, 1e-9));

    // The descriptive alias forwards to the same body -> identical result.
    const auto via_alias = emc::cabling::braid_optical_coverage(in);
    REQUIRE(via_alias.has_value());
    REQUIRE(emc::test::approx(via_alias->optical_coverage, out->optical_coverage, 1e-15));
}

// (b2) PROPERTY TEST -- OC = 2F - F^2 = 1 - (1-F)^2 is monotone increasing in F
//      on F in [0, 1]. Increasing the strand diameter d raises F, so coverage
//      must not DECREASE in that physical regime.
TEST_CASE("braid coverage rises with strand diameter (physical regime)",
          "[cabling][braid][property]") {
    auto oc_for = [](double d_m) {
        const auto r = emc::cabling::calculate(
            {.d = d_m * m, .D = 0.00295 * m, .P = 220.0, .C = 16.0, .N = 5.8});
        REQUIRE(r.has_value());
        return r->percent();
    };
    // Keep F < 1 so we stay on the monotone-increasing branch.
    REQUIRE(oc_for(0.00009) < oc_for(0.000114));
    REQUIRE(oc_for(0.000114) < oc_for(0.00013));
}

// (c) VALIDATION / EDGE TEST -- every geometric quantity must be strictly
//     positive (require_positive -> OutOfRange). A non-positive strand diameter,
//     zero pick density, or zero carrier count is not a cable.
TEST_CASE("braid coverage rejects non-positive geometry", "[cabling][braid][edge]") {
    const emc::cabling::BraidCoverageInput base{
        .d = 0.000114 * m, .D = 0.002950 * m, .P = 220.0, .C = 16.0, .N = 5.8};

    SECTION("zero strand diameter d -> OutOfRange") {
        auto in = base;
        in.d = 0.0 * m;
        const auto r = emc::cabling::calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("negative braid OD D -> OutOfRange") {
        auto in = base;
        in.D = -0.001 * m;
        const auto r = emc::cabling::calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("zero pick density P -> OutOfRange") {
        auto in = base;
        in.P = 0.0;
        const auto r = emc::cabling::calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("zero carrier count C (atan denominator) -> OutOfRange") {
        auto in = base;
        in.C = 0.0;
        const auto r = emc::cabling::calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
