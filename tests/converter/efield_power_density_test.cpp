// tests/converter/efield_power_density_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for emc::converter E-Field -> Power Density: P_D = E^2 / eta.
// Expected values are hand-computed / textbook closed form (no CSV / golden files).
#include <catch2/catch_test_macros.hpp>

#include <mp-units/systems/si.h>

#include <emc/converter/efield_power_density.hpp>
#include <emc/core/error.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::converter::EFieldPowerDensityInput;

// (a) Hand-computed known value.
//     E = 19.4 V/m into free space (eta defaults to 377 ohm):
//     P_D = E^2 / eta = 19.4^2 / 377 = 376.36 / 377 ~= 0.998302... W/m^2.
TEST_CASE("efield->power: known free-space value", "[converter][efield_pd]") {
    auto r = emc::converter::calculate({.electric_field = 19.4 * (V / m)});  // eta defaults to 377
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->power_density,
                              (19.4 * 19.4 / 377.0) * (W / (m * m)), 1e-9));
}

// (a2) Second hand-computed value with an explicit non-default impedance.
//      E = 100 V/m, eta = 50 ohm -> P_D = 10000 / 50 = 200 W/m^2 exactly.
TEST_CASE("efield->power: explicit impedance", "[converter][efield_pd]") {
    auto r = emc::converter::calculate({.electric_field = 100.0 * (V / m),
                                        .wave_impedance = 50.0 * ohm});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->power_density, 200.0 * (W / (m * m)), 1e-9));
}

// (b) Property: P_D is quadratic in E (double E => quadruple P_D).
TEST_CASE("efield->power: quadratic in E", "[converter][efield_pd][property]") {
    auto pd = [](double e) {
        return emc::converter::calculate({.electric_field = e * (V / m)})
            ->power_density.numerical_value_in(W / (m * m));
    };
    REQUIRE(pd(20.0) == Catch::Approx(4.0 * pd(10.0)).epsilon(1e-12));
}

// (b2) Property: P_D is inversely proportional to eta (halving eta doubles P_D).
TEST_CASE("efield->power: inverse in eta", "[converter][efield_pd][property]") {
    auto pd = [](double eta) {
        return emc::converter::calculate({.electric_field = 10.0 * (V / m),
                                          .wave_impedance = eta * ohm})
            ->power_density.numerical_value_in(W / (m * m));
    };
    REQUIRE(pd(200.0) == Catch::Approx(2.0 * pd(400.0)).epsilon(1e-12));
}

// (c) Validation/edge: eta = 0 must be rejected as DivisionByZero, never inf.
TEST_CASE("efield->power: zero impedance rejected", "[converter][efield_pd][validation]") {
    auto r = emc::converter::calculate({.electric_field = 10.0 * (V / m),
                                        .wave_impedance = 0.0 * ohm});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::DivisionByZero);
}
