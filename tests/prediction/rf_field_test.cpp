// tests/prediction/rf_field_test.cpp
//
// Catch2 v3 tests for the EIRP far-field RF model
// (include/emc/prediction/rf_field.hpp).
//
//   E   = sqrt(30 * P_t_W * G_t_linear) / d   [V/m]
//   H   = E / (120*pi)                         [A/m]
//   P_D = E * H                                [W/m^2]
//
// All expected values are hand-computed / textbook (no CSV, no golden files).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>     // std::sqrt
#include <numbers>   // std::numbers::pi

#include <mp-units/systems/si.h>

#include <emc/prediction/rf_field.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // V, A, W, m, km
using emc::ErrorCode;
namespace pred = emc::prediction;

// ---------------------------------------------------------------------------
// (a) Known-value test, hand-computed for a clean 1 W / 0 dBi / 1 km scenario.
//
//   P_t = 30 dBm  -> 10^((30-30)/10) W = 1 W
//   G_t = 0 dBi   -> 10^(0/10)         = 1  (linear)
//   d   = 1 km    = 1000 m
//   E   = sqrt(30 * 1 * 1) / 1000 = sqrt(30)/1000 ~= 5.4772e-3 V/m
//   H   = E / (120*pi)
//   P_D = E * H
// ---------------------------------------------------------------------------
TEST_CASE("RF far-field known value (1 W / 0 dBi / 1 km)", "[prediction][rffield]") {
    const double E  = std::sqrt(30.0) / 1000.0;
    const double Z  = 120.0 * std::numbers::pi;   // free-space wave impedance ~376.99 ohm
    const double H  = E / Z;
    const double PD = E * H;

    const pred::RfFieldInput in{
        .transmit_power = emc::units::Dbm{30.0},
        .gain           = emc::units::Decibel{0.0},
        .distance       = 1.0 * km,
    };

    auto r = pred::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->electric_field, E  * (V / m),       1e-9));
    REQUIRE(emc::test::approx(r->magnetic_field, H  * (A / m),       1e-9));
    REQUIRE(emc::test::approx(r->power_density,  PD * (W / (m * m)), 1e-9));
}

// ---------------------------------------------------------------------------
// (a2) Second known-value point exercising non-trivial dBm/dBi/distance.
//
//   P_t = 43 dBm -> 10^((43-30)/10) W = 10^1.3 W ~= 19.95262 W
//   G_t = 12 dBi -> 10^1.2           ~= 15.84893 (linear)
//   d   = 100 m
//   E   = sqrt(30 * P_t_W * G_t_lin) / 100
// ---------------------------------------------------------------------------
TEST_CASE("RF far-field known value (43 dBm / 12 dBi / 100 m)", "[prediction][rffield]") {
    const double Pt_W   = std::pow(10.0, (43.0 - 30.0) / 10.0);   // dBm -> W
    const double Gt_lin = std::pow(10.0, 12.0 / 10.0);            // dBi -> linear
    const double d_m    = 100.0;
    const double E  = std::sqrt(30.0 * Pt_W * Gt_lin) / d_m;
    const double H  = E / (120.0 * std::numbers::pi);
    const double PD = E * H;

    const pred::RfFieldInput in{
        .transmit_power = emc::units::Dbm{43.0},
        .gain           = emc::units::Decibel{12.0},
        .distance       = 100.0 * m,
    };

    auto r = pred::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->electric_field, E  * (V / m),       1e-9));
    REQUIRE(emc::test::approx(r->magnetic_field, H  * (A / m),       1e-9));
    REQUIRE(emc::test::approx(r->power_density,  PD * (W / (m * m)), 1e-9));
}

// ---------------------------------------------------------------------------
// (b) Property tests, valid for ANY input:
//   - cross-field relations: H == E/(120*pi) and P_D == E*H (internal consistency)
//   - inverse-square: doubling the distance quarters the power density (E ~ 1/d).
// ---------------------------------------------------------------------------
TEST_CASE("RF far-field internal consistency", "[prediction][rffield][property]") {
    const pred::RfFieldInput in{
        .transmit_power = emc::units::Dbm{20.0},
        .gain           = emc::units::Decibel{6.0},
        .distance       = 0.5 * km,
    };

    auto r = pred::calculate(in);
    REQUIRE(r.has_value());

    const double E  = r->electric_field.numerical_value_in(V / m);
    const double H  = r->magnetic_field.numerical_value_in(A / m);
    const double PD = r->power_density.numerical_value_in(W / (m * m));

    REQUIRE(H  == Catch::Approx(E / (120.0 * std::numbers::pi)).epsilon(1e-12));
    REQUIRE(PD == Catch::Approx(E * H).epsilon(1e-12));
}

TEST_CASE("RF far-field obeys inverse-square law in power density",
          "[prediction][rffield][property]") {
    pred::RfFieldInput near{
        .transmit_power = emc::units::Dbm{33.0},
        .gain           = emc::units::Decibel{3.0},
        .distance       = 100.0 * m,
    };
    pred::RfFieldInput far = near;
    far.distance = 200.0 * m;   // 2x the distance

    auto rn = pred::calculate(near);
    auto rf = pred::calculate(far);
    REQUIRE(rn.has_value());
    REQUIRE(rf.has_value());

    const double E_near = rn->electric_field.numerical_value_in(V / m);
    const double E_far  = rf->electric_field.numerical_value_in(V / m);
    const double PD_near = rn->power_density.numerical_value_in(W / (m * m));
    const double PD_far  = rf->power_density.numerical_value_in(W / (m * m));

    // E ~ 1/d  -> halves; P_D ~ 1/d^2 -> quarters.
    REQUIRE(E_far  == Catch::Approx(E_near / 2.0).epsilon(1e-12));
    REQUIRE(PD_far == Catch::Approx(PD_near / 4.0).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// (c) Validation / edge tests. The only divide is by distance, so d <= 0 is
//     rejected with OutOfRange (require_positive in validate()).
// ---------------------------------------------------------------------------
TEST_CASE("RF far-field rejects zero distance", "[prediction][rffield][error]") {
    pred::RfFieldInput in{};
    in.distance = 0.0 * km;

    auto r = pred::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
}

TEST_CASE("RF far-field rejects negative distance", "[prediction][rffield][error]") {
    pred::RfFieldInput in{};
    in.distance = -10.0 * m;

    auto r = pred::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "distance");
}
