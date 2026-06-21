// tests/basic/skin_depth_test.cpp
//
// Catch2 v3 tests for emc::basic::SkinDepth.
//   (a) hand-computed known value (copper @ 1 MHz),
//   (b) material-database re-derivation across several materials,
//   (c) property: delta scales as 1/sqrt(f),
//   (d) validation / edge cases (typed ErrorCodes).
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <emc/basic/skin_depth.hpp>
#include "support/approx.hpp"

#include <mp-units/math.h>            // mp_units::sqrt for quantities
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, MHz, S, m, ...
using emc::ErrorCode;
using emc::materials::Material;

// (a) Hand-computed textbook value. Copper @ 1 MHz.
//     sigma = 5.96e7 S/m, mu_r ~ 1, mu0 = 1.25663706212e-6 H/m, f = 1e6 Hz.
//     delta = sqrt(1 / (pi * 1e6 * 1.25663706212e-6 * 0.999991 * 5.96e7))
//           ~ 6.521e-5 m = 65.21 um.
TEST_CASE("skin depth: copper @ 1 MHz ~ 65 um", "[basic][skin_depth][known]") {
    const emc::basic::SkinDepthInput in{ .frequency = 1.0 * MHz, .material = Material::Copper };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->skin_depth, 65.21 * si::micro<m>, 1e-3));
}

// (b) Material path: delta re-derived from the same material database, across
//     several materials and frequencies. Guards the formula end-to-end.
TEST_CASE("skin depth matches an independent re-derivation", "[basic][skin_depth][reference]") {
    const auto mat   = GENERATE(Material::Copper, Material::Silver, Material::Gold,
                                Material::Aluminium, Material::Nickel);
    const auto f_mhz = GENERATE(1.0, 27.0, 100.0, 433.0);

    const emc::basic::SkinDepthInput in{ .frequency = f_mhz * MHz, .material = mat };
    const auto got = emc::basic::calculate(in);
    REQUIRE(got.has_value());

    const auto props = emc::materials::properties(mat).value();
    const auto mu    = emc::constants::mu0 * props.relative_permeability;
    const emc::units::Length expected =
        sqrt(1.0 / (emc::constants::pi * (f_mhz * MHz) * mu * props.conductivity));

    REQUIRE(emc::test::approx(got->skin_depth, expected, 1e-9));
}

// (c) Property: delta scales as 1/sqrt(f). Quadrupling f halves delta.
TEST_CASE("skin depth halves when frequency quadruples", "[basic][skin_depth][property]") {
    const auto lo = emc::basic::calculate({ .frequency = 1.0 * MHz, .material = Material::Copper });
    const auto hi = emc::basic::calculate({ .frequency = 4.0 * MHz, .material = Material::Copper });
    REQUIRE(lo.has_value());
    REQUIRE(hi.has_value());
    REQUIRE(emc::test::approx(hi->skin_depth, lo->skin_depth / 2.0, 1e-9));
}

// (d) Validation / edge tests -> typed ErrorCodes.
TEST_CASE("skin depth rejects bad input with typed errors", "[basic][skin_depth][validate]") {
    SECTION("zero frequency -> not positive (OutOfRange)") {
        const auto r = emc::basic::calculate({ .frequency = 0.0 * Hz, .material = Material::Gold });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "frequency");
    }
    SECTION("Custom with non-positive sigma -> OutOfRange") {
        const auto r = emc::basic::calculate({
            .frequency = 1.0 * MHz, .material = Material::Custom,
            .conductivity = 0.0 * (S / m), .relative_permeability = 1.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
