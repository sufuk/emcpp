// tests/filtering/ferrite_toroid_test.cpp
//
// Catch2 v3 tests for emc::filtering::ferrite_toroid. Expected numbers are
// hand-computed against the closed form
//     L  = (mu0 N^2 h / 2 pi) * ln(b/a)
//     X  = 2 pi f mu_r' L,   R = 2 pi f mu_r'' L,   |Z| = sqrt(R^2 + X^2)
// using emc::constants::mu0 = 1.25663706212e-6 H/m (the same value the
// implementation uses), so the oracle matches the code to full precision.

#include <cmath>      // std::log, std::sqrt

#include <catch2/catch_test_macros.hpp>

#include <mp-units/systems/si.h>

#include <emc/filtering/ferrite_toroid.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz, H, ohm
using mp_units::si::mega;
using mp_units::si::hertz;

using emc::filtering::FerriteToroidInput;
using emc::filtering::calculate;

// ---------------------------------------------------------------------------
// (a) KNOWN VALUE — pin L, X, R, |Z| with a paper calculation.
//     N=1000, mu_r'=100, mu_r''=50, h=0.05 m, b=0.04 m, a=0.03 m, f=1 MHz.
//       ln(b/a) = ln(0.04/0.03) = ln(4/3) = 0.28768207245
//       L = (1e6 * 1.25663706212e-6 * 0.05 / (2 pi)) * 0.28768207245
//         = 2.876820726e-3 H            (~ 2876.8 uH)
//       omega = 2 pi * 1e6 = 6.283185307e6
//       X = omega * 100 * L = 1.807559772e6 ohm
//       R = omega *  50 * L = 9.037798859e5 ohm
//       |Z| = sqrt(R^2 + X^2) = X * sqrt(1.25) = 2.020913262e6 ohm
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid hand value (representative toroid)", "[filtering][ferrite]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.04 * m,
                        .inner_radius = 0.03 * m, .frequency = 1.0 * si::mega<hertz>});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 2.876820726e-3 * H,   1e-6));
    REQUIRE(emc::test::approx(r->reactance,  1.807559772e6  * ohm, 1e-6));
    REQUIRE(emc::test::approx(r->resistance, 9.037798859e5  * ohm, 1e-6));
    REQUIRE(emc::test::approx(r->impedance,  2.020913262e6  * ohm, 1e-6));
}

// ---------------------------------------------------------------------------
// (b) PROPERTY: |Z| = sqrt(R^2 + X^2) — the magnitude is the hypotenuse of the
//     (R, X) right triangle, dominates each leg, and R/X tracks mu''/mu' exactly
//     (same L, same omega): here 120/80 = 1.5.
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid |Z| is the R-X hypotenuse", "[filtering][ferrite][property]") {
    auto r = calculate({.turns = 250.0, .mu_r_real = 80.0, .mu_r_imag = 120.0,
                        .height = 0.012 * m, .outer_radius = 0.018 * m,
                        .inner_radius = 0.010 * m, .frequency = 10.0 * si::mega<hertz>});
    REQUIRE(r.has_value());
    const double R = r->resistance.numerical_value_in(ohm);
    const double X = r->reactance.numerical_value_in(ohm);
    const double Z = r->impedance.numerical_value_in(ohm);
    REQUIRE(emc::test::approx(r->impedance, std::sqrt(R * R + X * X) * ohm, 1e-9));
    REQUIRE(Z >= R);                       // hypotenuse >= each leg
    REQUIRE(Z >= X);
    REQUIRE(emc::test::approx((R / X) * one, 1.5 * one, 1e-9));   // mu''/mu' = 120/80
}

// ---------------------------------------------------------------------------
// (c) PROPERTY: |Z| scales as N^2 (R, X, L all linear in L, and L proportional
//     to N^2). Doubling the turns quadruples |Z|.
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid impedance scales as N^2", "[filtering][ferrite][property]") {
    const FerriteToroidInput base{.turns = 100.0, .mu_r_real = 60.0, .mu_r_imag = 40.0,
                                  .height = 0.01 * m, .outer_radius = 0.02 * m,
                                  .inner_radius = 0.01 * m, .frequency = 1.0 * si::mega<hertz>};
    auto r1 = calculate(base);
    FerriteToroidInput dbl = base;
    dbl.turns = 200.0;
    auto r2 = calculate(dbl);
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE(emc::test::approx(r2->impedance,
                              4.0 * r1->impedance.numerical_value_in(ohm) * ohm, 1e-9));
}

// ---------------------------------------------------------------------------
// (d) VALIDATION — a = 0 is the ln(b/0) blow-up; must be a typed OutOfRange.
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid rejects zero inner radius", "[filtering][ferrite][error]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.04 * m,
                        .inner_radius = 0.0 * m, .frequency = 1.0 * si::mega<hertz>});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "inner_radius");
}

// ---------------------------------------------------------------------------
// (e) VALIDATION — b < a would give a negative inductance -> DomainError.
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid rejects outer < inner radius", "[filtering][ferrite][error]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.02 * m,
                        .inner_radius = 0.03 * m, .frequency = 1.0 * si::mega<hertz>});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::DomainError);
    REQUIRE(r.error().field == "outer_radius");
}

// ---------------------------------------------------------------------------
// (f) VALIDATION — non-positive frequency -> OutOfRange.
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid rejects non-positive frequency", "[filtering][ferrite][error]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.04 * m,
                        .inner_radius = 0.03 * m, .frequency = 0.0 * Hz});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// ---------------------------------------------------------------------------
// (g) EDGE: b == a -> ln(1) == 0 -> L = X = R = |Z| = 0 (an allowed boundary,
//     "no core"): the calc succeeds and every output is exactly zero.
// ---------------------------------------------------------------------------
TEST_CASE("ferrite toroid b==a gives zero impedance", "[filtering][ferrite][property]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.03 * m,
                        .inner_radius = 0.03 * m, .frequency = 1.0 * si::mega<hertz>});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 0.0 * H,   1e-12));
    REQUIRE(emc::test::approx(r->reactance,  0.0 * ohm, 1e-12));
    REQUIRE(emc::test::approx(r->resistance, 0.0 * ohm, 1e-12));
    REQUIRE(emc::test::approx(r->impedance,  0.0 * ohm, 1e-12));
}
