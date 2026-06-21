// tests/component/inductance_test.cpp
//
// Catch2 v3 tests for the seven emc::component inductance calculators:
//   circular_loop_inductance, connector_pin_inductance, rectangular_loop_inductance,
//   solenoid_inductance, square_loop_inductance, toroid_inductance, via_inductance.
//
// Each calculator has its own Input/Result struct and its own free function; the
// EMC_BIND_INDUCTANCE macro also exposes overloaded calculate()/validate() resolved
// by Input type. To stay unambiguous we always pass an EXPLICITLY-TYPED Input struct.
//
// One TEST_CASE per calculator: a hand-computed known value, a property/scaling
// check, and an edge/validation check (positivity -> OutOfRange, denominator -> DivisionByZero).
// Expected numbers are independent closed-form references written inline (no golden CSV).

#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

#include <cmath>     // std::log, std::sqrt
#include <numbers>   // std::numbers::pi

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, mm, nH, uH, H, ...
using emc::ErrorCode;
namespace ec = emc::component;

// ===========================================================================
//  Circular Loop:  L = N^2 * R * mu0 * mu_r * (ln(8R/a) - 2)
// ===========================================================================
TEST_CASE("circular loop inductance", "[component][inductance][circular_loop]") {
    // (a) Hand value: N=1, R=0.3 m, a=0.5 mm, mu_r=1.
    //   ln(8*0.3/0.0005)=ln(4800)=8.476371; L = 0.3*1.25663706212e-6*(8.476371-2) = 2441.6 nH.
    SECTION("hand-computed value") {
        auto r = ec::circular_loop_inductance(ec::CircularLoopInput{
            .turns = 1.0, .loop_radius = 0.3 * m, .wire_radius = 0.5 * mm, .mu_r = 1.0});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 2441.6 * nH, 1e-3));
    }

    // (b) Property: L scales as N^2 -> tripling the turns multiplies L by 9.
    SECTION("scales as N^2") {
        auto base = ec::circular_loop_inductance(ec::CircularLoopInput{
            .turns = 1.0, .loop_radius = 0.3 * m, .wire_radius = 0.5 * mm, .mu_r = 1.0});
        auto more = ec::circular_loop_inductance(ec::CircularLoopInput{
            .turns = 3.0, .loop_radius = 0.3 * m, .wire_radius = 0.5 * mm, .mu_r = 1.0});
        REQUIRE(base.has_value());
        REQUIRE(more.has_value());
        REQUIRE(emc::test::approx(more->inductance,
                                  9.0 * base->inductance.numerical_value_in(nH) * nH, 1e-9));
    }

    // (c) Validation: a = 0 -> require_positive -> OutOfRange on wire_radius.
    SECTION("rejects zero wire radius") {
        auto r = ec::circular_loop_inductance(ec::CircularLoopInput{
            .turns = 1.0, .loop_radius = 0.3 * m, .wire_radius = 0.0 * m, .mu_r = 1.0});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "wire_radius");
    }
}

// ===========================================================================
//  Connector Pin (multi-output):
//    L   = (mu0 l / 2pi) * (ln(2l/r) - 3/4)
//    M_p = (mu0 l / 2pi) * (ln(2l/s) - 1)
// ===========================================================================
TEST_CASE("connector pin inductance", "[component][inductance][connector_pin]") {
    // (a) Hand value: l=0.01 m, r=0.3 mm, s=2.54 mm.
    //   coeff = mu0*0.01/(2pi) = 2.0e-9.
    //   L  = 2.0e-9*(ln(0.02/0.0003)-0.75) = 6.8994 nH.
    //   Mp = 2.0e-9*(ln(0.02/0.00254)-1)   = 2.1274 nH.
    SECTION("hand-computed self and mutual") {
        auto r = ec::connector_pin_inductance(ec::ConnectorPinInput{
            .length = 0.01 * m, .radius = 0.3 * mm, .spacing = 2.54 * mm});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->self_inductance,   6.8994 * nH, 1e-3));
        REQUIRE(emc::test::approx(r->mutual_inductance, 2.1274 * nH, 1e-3));
    }

    // (b) Property: with s == r the formulas differ only by (3/4 vs 1), so L > M_p (by coeff/4).
    SECTION("self exceeds mutual when spacing equals radius") {
        auto r = ec::connector_pin_inductance(ec::ConnectorPinInput{
            .length = 0.01 * m, .radius = 1.0 * mm, .spacing = 1.0 * mm});
        REQUIRE(r.has_value());
        REQUIRE(r->self_inductance.numerical_value_in(nH)
                > r->mutual_inductance.numerical_value_in(nH));
    }

    // (c) Validation: spacing = 0 -> require_positive -> OutOfRange on spacing.
    SECTION("rejects zero spacing") {
        auto r = ec::connector_pin_inductance(ec::ConnectorPinInput{
            .length = 0.01 * m, .radius = 0.3 * mm, .spacing = 0.0 * m});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "spacing");
    }
}

// ===========================================================================
//  Rectangular Loop (long bracket; symmetric in w<->h).
// ===========================================================================
TEST_CASE("rectangular loop inductance", "[component][inductance][rectangular_loop]") {
    // (a) Hand value: N=1, w=h=1 m, a=1 mm, mu_r=1.
    //   diag=sqrt(2)=1.4142136.
    //   bracket = -4 + 2*1.4142136 - 2*ln(2.4142136) + 2*ln(2000) = 12.267485.
    //   L = (mu0/pi)*bracket = (1.25663706212e-6/pi)*12.267485 = 4906.99 nH.
    SECTION("hand-computed value") {
        auto r = ec::rectangular_loop_inductance(ec::RectangularLoopInput{
            .turns = 1.0, .width = 1.0 * m, .height = 1.0 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 4906.99 * nH, 1e-3));
    }

    // (b) Property: the loop is geometrically symmetric, so swapping w<->h is invariant.
    SECTION("symmetric in width<->height") {
        auto a = ec::rectangular_loop_inductance(ec::RectangularLoopInput{
            .turns = 5.0, .width = 2.0 * m, .height = 0.5 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0});
        auto b = ec::rectangular_loop_inductance(ec::RectangularLoopInput{
            .turns = 5.0, .width = 0.5 * m, .height = 2.0 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0});
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(emc::test::approx(a->inductance, b->inductance, 1e-9));
    }

    // (c) Validation: a = 0 -> require_positive -> OutOfRange on wire_radius.
    SECTION("rejects zero wire radius") {
        auto r = ec::rectangular_loop_inductance(ec::RectangularLoopInput{
            .turns = 1.0, .width = 1.0 * m, .height = 1.0 * m, .wire_radius = 0.0 * m, .mu_r = 1.0});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "wire_radius");
    }
}

// ===========================================================================
//  Solenoid (Wheeler long-coil):  L = mu0 N^2 pi r^2 / l   (constexpr end-to-end).
// ===========================================================================
TEST_CASE("solenoid inductance", "[component][inductance][solenoid]") {
    // (a) Hand value: N=10, r=0.01 m, l=0.10 m.
    //   L = mu0*100*pi*1e-4/0.10 = 1.25663706212e-6*0.3141593 = 394.78 nH.
    SECTION("hand-computed value") {
        auto r = ec::solenoid_inductance(ec::SolenoidInput{
            .turns = 10.0, .radius = 0.01 * m, .length = 0.10 * m});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 394.78 * nH, 1e-3));
    }

    // (b) Property: L proportional to N^2 -> doubling the turns quadruples L.
    SECTION("scales as N^2") {
        auto l1 = ec::solenoid_inductance(ec::SolenoidInput{
            .turns = 10.0, .radius = 0.01 * m, .length = 0.10 * m});
        auto l2 = ec::solenoid_inductance(ec::SolenoidInput{
            .turns = 20.0, .radius = 0.01 * m, .length = 0.10 * m});
        REQUIRE(l1.has_value());
        REQUIRE(l2.has_value());
        REQUIRE(emc::test::approx(l2->inductance,
                                  4.0 * l1->inductance.numerical_value_in(nH) * nH, 1e-9));
    }

    // (c) Validation: length = 0 -> require_nonzero -> DivisionByZero on length.
    SECTION("rejects zero length") {
        auto r = ec::solenoid_inductance(ec::SolenoidInput{
            .turns = 10.0, .radius = 0.01 * m, .length = 0.0 * m});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "length");
    }

    // (d) The closed form is constexpr end-to-end.
    SECTION("evaluates at compile time") {
        constexpr auto r = ec::solenoid_inductance(ec::SolenoidInput{
            .turns = 10.0, .radius = 0.01 * m, .length = 0.10 * m});
        STATIC_REQUIRE(r.has_value());
        STATIC_REQUIRE(r->inductance.numerical_value_in(nH) > 0.0);
    }
}

// ===========================================================================
//  Square Loop:  L = N^2 * (2 mu0 mu_r w / pi) * (ln(w/a) - 0.774)
// ===========================================================================
TEST_CASE("square loop inductance", "[component][inductance][square_loop]") {
    // (a) Hand value: N=1, w=1 m, a=1 mm, mu_r=1.
    //   L = (2*mu0*1/pi)*(ln(1000)-0.774) = 8.0e-7*6.1337553 = 4907.0 nH.
    SECTION("hand-computed value") {
        auto r = ec::square_loop_inductance(ec::SquareLoopInput{
            .turns = 1.0, .side = 1.0 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 4907.0 * nH, 2e-3));
    }

    // (b) Property: monotonic in side -> larger w gives larger ln(w/a) and larger L.
    SECTION("grows with side length") {
        auto small = ec::square_loop_inductance(ec::SquareLoopInput{
            .turns = 1.0, .side = 0.5 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0});
        auto big = ec::square_loop_inductance(ec::SquareLoopInput{
            .turns = 1.0, .side = 2.0 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0});
        REQUIRE(small.has_value());
        REQUIRE(big.has_value());
        REQUIRE(big->inductance.numerical_value_in(nH) > small->inductance.numerical_value_in(nH));
    }

    // (c) Validation: a = 0 -> require_positive -> OutOfRange on wire_radius.
    SECTION("rejects zero wire radius") {
        auto r = ec::square_loop_inductance(ec::SquareLoopInput{
            .turns = 1.0, .side = 1.0 * m, .wire_radius = 0.0 * m, .mu_r = 1.0});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "wire_radius");
    }
}

// ===========================================================================
//  Toroid (rectangular core):  L = (mu0 N^2 h / 2pi) * ln(b/a)
// ===========================================================================
TEST_CASE("toroid inductance", "[component][inductance][toroid]") {
    // (a) Hand value: N=10, h=0.01 m, b=0.06 m, a=0.04 m.
    //   L = (mu0*100*0.01/(2pi))*ln(1.5) = 2.0e-7*0.4054651 = 0.081093 uH.
    SECTION("hand-computed value") {
        auto r = ec::toroid_inductance(ec::ToroidInput{
            .turns = 10.0, .height = 0.01 * m, .outer_radius = 0.06 * m, .inner_radius = 0.04 * m});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 0.081093 * uH, 1e-4));
    }

    // (b) Property: b == a -> ln(1) == 0 -> L == 0 (clean boundary).
    SECTION("zero when outer equals inner radius") {
        auto r = ec::toroid_inductance(ec::ToroidInput{
            .turns = 10.0, .height = 0.01 * m, .outer_radius = 0.05 * m, .inner_radius = 0.05 * m});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 0.0 * uH, 1e-9));
    }

    // (c) Validation: inner_radius = 0 -> require_positive -> OutOfRange on inner_radius.
    SECTION("rejects zero inner radius") {
        auto r = ec::toroid_inductance(ec::ToroidInput{
            .turns = 10.0, .height = 0.01 * m, .outer_radius = 0.06 * m, .inner_radius = 0.0 * m});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "inner_radius");
    }
}

// ===========================================================================
//  Via (PCB):  L = (mu0 h / 2pi) * (ln(4h/d) - 1)
// ===========================================================================
TEST_CASE("via inductance", "[component][inductance][via]") {
    // (a) Hand value: h=1.6 mm, d=0.75 mm.
    //   L = (mu0*0.0016/(2pi))*(ln(8.533333)-1) = 3.2e-10*1.144099 = 0.36611 nH.
    SECTION("hand-computed value") {
        auto r = ec::via_inductance(ec::ViaInput{.height = 1.6 * mm, .diameter = 0.75 * mm});
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->inductance, 0.36611 * nH, 2e-3));
    }

    // (b) Property: smaller diameter -> larger ln(4h/d) -> larger L.
    SECTION("grows as diameter shrinks") {
        auto thin = ec::via_inductance(ec::ViaInput{.height = 1.6 * mm, .diameter = 0.3 * mm});
        auto fat  = ec::via_inductance(ec::ViaInput{.height = 1.6 * mm, .diameter = 1.0 * mm});
        REQUIRE(thin.has_value());
        REQUIRE(fat.has_value());
        REQUIRE(thin->inductance.numerical_value_in(nH) > fat->inductance.numerical_value_in(nH));
    }

    // (c) Validation: diameter = 0 -> require_positive -> OutOfRange on diameter.
    SECTION("rejects zero diameter") {
        auto r = ec::via_inductance(ec::ViaInput{.height = 1.6 * mm, .diameter = 0.0 * mm});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "diameter");
    }
}
