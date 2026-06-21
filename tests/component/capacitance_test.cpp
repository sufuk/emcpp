// tests/component/capacitance_test.cpp
// Catch2 v3 tests for emc::component::ParallelPlate and emc::component::Sphere
// capacitance calculators. Expected values are hand-computed / textbook (inline),
// no CSV / golden files.
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include <emc/component/capacitance.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // F, m, cm, mm, pF, ...
using mp_units::si::metre;
using emc::ErrorCode;

// eps0 used in the hand-computed references (matches emc::constants::eps0).
namespace {
constexpr double kEps0 = 8.854'187'8128e-12;   // F/m
}

// ===========================================================================
//  Parallel plate :  C = eps0 * eps_r * A / d
// ===========================================================================

// ---- (a) Hand-computed known value: C(1 m^2, 1 m, eps_r=1) == eps0 ----------
// A = 1 m^2, d = 1 m, eps_r = 1  =>  C = eps0 = 8.8541878128e-12 F.
TEST_CASE("ParallelPlate equals eps0 for unit geometry", "[component][capacitance]") {
    const emc::component::ParallelPlateInput in{ .area     = 1.0 * square(metre),
                                                 .distance = 1.0 * metre };
    auto r = emc::component::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->capacitance, kEps0 * F, 1e-9));
}

// ---- (b) Textbook reference value: 100 cm^2 plate, 1 mm gap, air ------------
// A = 100 cm^2 = 0.01 m^2, d = 1 mm = 1e-3 m, eps_r = 1.
// C = eps0 * A/d = 8.8541878128e-12 * 0.01 / 1e-3 = 8.8541878128e-11 F = 88.5419 pF.
TEST_CASE("ParallelPlate matches textbook 100cm^2/1mm air gap", "[component][capacitance]") {
    const emc::component::ParallelPlateInput in{
        .area     = 100.0 * square(si::centi<metre>),
        .distance = 1.0   * si::milli<metre>,
    };
    auto r = emc::component::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->capacitance.in(si::pico<F>), 88.541878128 * si::pico<F>, 1e-6));
}

// ---- (c) Property: C scales linearly with area, inversely with distance,
//          and linearly with eps_r. -----------------------------------------
TEST_CASE("ParallelPlate scaling laws", "[component][capacitance][property]") {
    const emc::component::ParallelPlateInput base{ .area     = 2.0 * square(metre),
                                                   .distance = 3.0 * metre };
    auto c0 = emc::component::calculate(base);
    REQUIRE(c0.has_value());

    // Double the area -> double the capacitance.
    auto c_2a = emc::component::calculate({ .area = 4.0 * square(metre), .distance = 3.0 * metre });
    REQUIRE(c_2a.has_value());
    REQUIRE(emc::test::approx(c_2a->capacitance, (2.0 * c0->capacitance).in(F), 1e-9));

    // Halve the distance -> double the capacitance.
    auto c_halfd = emc::component::calculate({ .area = 2.0 * square(metre), .distance = 1.5 * metre });
    REQUIRE(c_halfd.has_value());
    REQUIRE(emc::test::approx(c_halfd->capacitance, (2.0 * c0->capacitance).in(F), 1e-9));

    // eps_r = 2 -> double the capacitance.
    auto c_er = emc::component::calculate({ .area = 2.0 * square(metre), .distance = 3.0 * metre,
                                            .relative_permittivity = 2.0 });
    REQUIRE(c_er.has_value());
    REQUIRE(emc::test::approx(c_er->capacitance, (2.0 * c0->capacitance).in(F), 1e-9));
}

// ---- (d) Validation / edge cases -------------------------------------------
TEST_CASE("ParallelPlate rejects bad geometry", "[component][capacitance][validate]") {
    SECTION("distance == 0 -> OutOfRange (require_positive fires, not inf)") {
        auto r = emc::component::calculate({ .area = 1.0 * square(metre), .distance = 0.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "distance");
    }
    SECTION("negative area -> OutOfRange") {
        auto r = emc::component::calculate({ .area = -1.0 * square(metre), .distance = 1.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "area");
    }
    SECTION("eps_r < 1 -> OutOfRange") {
        auto r = emc::component::calculate({ .area = 1.0 * square(metre), .distance = 1.0 * metre,
                                             .relative_permittivity = 0.5 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
}

// ===========================================================================
//  Isolated conducting sphere :  C = 4 * pi * eps0 * r
// ===========================================================================

// ---- (a) Hand-computed known value: C(1 m) == 4*pi*eps0 --------------------
// r = 1 m  =>  C = 4*pi*eps0 = 4*pi*8.8541878128e-12 = 1.1126500560e-10 F.
TEST_CASE("Sphere equals 4*pi*eps0 for unit radius", "[component][capacitance]") {
    auto r = emc::component::calculate({ .radius = 1.0 * metre });
    REQUIRE(r.has_value());
    const double expected_F = 4.0 * std::numbers::pi * kEps0;   // ~1.11265e-10
    REQUIRE(emc::test::approx(r->capacitance, expected_F * F, 1e-9));
}

// ---- (b) Textbook reference value: 10 cm radius sphere ---------------------
// r = 0.1 m  =>  C = 4*pi*eps0*0.1 = 1.1126500560e-11 F = 11.1265 pF.
TEST_CASE("Sphere matches textbook 10cm radius", "[component][capacitance]") {
    auto r = emc::component::calculate({ .radius = 10.0 * si::centi<metre> });
    REQUIRE(r.has_value());
    const double expected_pF = 4.0 * std::numbers::pi * kEps0 * 0.1 / 1e-12;   // ~11.1265
    REQUIRE(emc::test::approx(r->capacitance.in(si::pico<F>), expected_pF * si::pico<F>, 1e-6));
}

// ---- (c) Property: capacitance is linear in radius -------------------------
TEST_CASE("Sphere capacitance is linear in radius", "[component][capacitance][property]") {
    auto c1 = emc::component::calculate({ .radius = 1.0 * metre });
    auto c3 = emc::component::calculate({ .radius = 3.0 * metre });
    REQUIRE(c1.has_value());
    REQUIRE(c3.has_value());
    REQUIRE(emc::test::approx(c3->capacitance, (3.0 * c1->capacitance).in(F), 1e-9));
}

// ---- (d) Validation / edge cases -------------------------------------------
TEST_CASE("Sphere rejects non-physical radius", "[component][capacitance][validate]") {
    SECTION("radius == 0 -> OutOfRange") {
        auto r = emc::component::calculate({ .radius = 0.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "radius");
    }
    SECTION("negative radius -> OutOfRange") {
        auto r = emc::component::calculate({ .radius = -2.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "radius");
    }
}
