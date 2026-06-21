// tests/cabling/crosstalk_test.cpp
//
// Catch2 v3 tests for emc::cabling::crosstalk (NEXT/FEXT lumped-element model).
// Expected values are hand-computed from the closed form documented in
// plan/implementation/13-cabling.md and verified against the implemented header
// include/emc/cabling/crosstalk.hpp (field names V_NE/V_FE/far_end_clamped, the
// kFarEndFloor constant, and the OutOfRange/DivisionByZero error codes).
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <emc/cabling/crosstalk.hpp>
#include <emc/core/constants.hpp>

#include <mp-units/systems/si.h>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // MHz, uH, pF, ohm
using emc::ErrorCode;
using mp_units::one;

namespace {

// Independent closed-form oracle for one input (mirrors the model incl. clamp).
// Used by the property/oracle test below; kept separate from the implementation
// so a transcription error in either side cannot agree with itself.
struct Oracle {
    double vne;
    double vfe;
    bool   clamped;
};

Oracle oracle(const emc::cabling::CrosstalkInput& in) {
    const double f   = in.f.numerical_value_in(si::hertz);
    const double RL  = in.R_L.numerical_value_in(si::ohm);
    const double RS  = in.R_S.numerical_value_in(si::ohm);
    const double RNE = in.R_NE.numerical_value_in(si::ohm);
    const double RFE = in.R_FE.numerical_value_in(si::ohm);
    const double Lm  = in.L_m.numerical_value_in(si::henry);
    const double Cm  = in.C_m.numerical_value_in(si::farad);

    const double w   = 2.0 * emc::constants::pi * f;
    const double ind = Lm / (RS + RL);
    const double cap = (RNE * RFE / (RNE + RFE)) * (RL * Cm / (RS + RL));
    const double A_NE = w * ((RNE / (RNE + RFE)) * ind + cap);
    const double A_FE = w * (-1.0 * (RFE / (RNE + RFE)) * ind + cap);

    Oracle o{20.0 * std::log10(A_NE), 0.0, A_FE <= 0.0};
    o.vfe = o.clamped ? -200.0 : 20.0 * std::log10(A_FE);
    return o;
}

}  // namespace

// (a) KNOWN-VALUE TESTS -- hand-computed from the closed form (20*log10).
TEST_CASE("crosstalk known values", "[cabling][crosstalk][known]") {
    SECTION("f=1MHz, all 50ohm, Lm=0.5uH, Cm=94683.6pF") {
        const emc::cabling::CrosstalkInput in{
            .f = 1.0 * MHz, .R_L = 50.0 * ohm, .R_S = 50.0 * ohm,
            .R_NE = 50.0 * ohm, .R_FE = 50.0 * ohm,
            .L_m = 0.5 * uH, .C_m = 94683.6 * pF};
        const auto r = emc::cabling::calculate(in);
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->V_NE.value * one, 17.44562 * one, 1e-4));
        REQUIRE(emc::test::approx(r->V_FE.value * one, 17.40893 * one, 1e-4));
        REQUIRE_FALSE(r->far_end_clamped);
    }
    SECTION("default preset: V_NE ~= 14.80376 dB") {
        const auto r = emc::cabling::calculate({});   // all defaults
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->V_NE.value * one, 14.80376 * one, 1e-4));
        REQUIRE(emc::test::approx(r->V_FE.value * one, 7.44422 * one, 1e-4));
        REQUIRE_FALSE(r->far_end_clamped);
    }
}

// (b) ORACLE / PROPERTY TEST -- an independent closed-form recomputation agrees
//     with the implementation on V_NE and V_FE to tight tolerance and on the
//     clamp flag. This exercises the asymmetric (RL!=RS, RNE!=RFE) regime where
//     the near-end ADD and far-end SUBTRACT sign convention matters.
TEST_CASE("crosstalk matches an independent closed-form oracle",
          "[cabling][crosstalk][oracle]") {
    const emc::cabling::CrosstalkInput in{
        .f = 2.5 * MHz, .R_L = 75.0 * ohm, .R_S = 50.0 * ohm,
        .R_NE = 60.0 * ohm, .R_FE = 90.0 * ohm,
        .L_m = 1.2 * uH, .C_m = 33000.0 * pF};
    const auto out = emc::cabling::calculate(in);
    REQUIRE(out.has_value());

    const Oracle ref = oracle(in);
    REQUIRE(emc::test::approx(out->V_NE.value * one, ref.vne * one, 1e-9));
    REQUIRE(emc::test::approx(out->V_FE.value * one, ref.vfe * one, 1e-9));
    REQUIRE(out->far_end_clamped == ref.clamped);
}

// (c) CLAMP TEST -- the documented V_FE floor. This input drives A_FE < 0 (the
//     inductive cancellation dominates), so V_FE must clamp to EXACTLY -200 dB,
//     set far_end_clamped, and match kFarEndFloor -- while V_NE stays finite.
TEST_CASE("crosstalk clamps V_FE to the -200 dB floor",
          "[cabling][crosstalk][clamp]") {
    const emc::cabling::CrosstalkInput in{
        .f = 10.42 * MHz, .R_L = 0.399 * ohm, .R_S = 5.428 * ohm,
        .R_NE = 22.92 * ohm, .R_FE = 79.86 * ohm,
        .L_m = 0.8 * uH, .C_m = 39628.4 * pF};
    const auto r = emc::cabling::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(r->far_end_clamped);
    REQUIRE(r->V_FE.value == -200.0);                       // exact floor, no log10
    REQUIRE(r->V_FE.value == emc::cabling::kFarEndFloor.value);
    REQUIRE(emc::test::approx(r->V_NE.value * one, 14.26700 * one, 1e-4));
}

// (d) VALIDATION / EDGE TESTS -- the right ErrorCode and field for bad inputs.
TEST_CASE("crosstalk rejects bad inputs", "[cabling][crosstalk][validation]") {
    SECTION("zero frequency is OutOfRange (log10 collapses)") {
        const auto r = emc::cabling::calculate(
            {.f = 0.0 * MHz, .L_m = 50.0 * uH, .C_m = 50000.0 * pF});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "f");
    }
    SECTION("R_NE + R_FE == 0 is DivisionByZero") {
        // Both split-ratio denominators are (R_NE + R_FE).
        const auto r = emc::cabling::calculate(
            {.R_NE = 0.0 * ohm, .R_FE = 0.0 * ohm, .L_m = 50.0 * uH, .C_m = 50000.0 * pF});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "R_NE+R_FE");
    }
    SECTION("R_S + R_L == 0 is DivisionByZero") {
        const auto r = emc::cabling::calculate(
            {.R_L = 0.0 * ohm, .R_S = 0.0 * ohm, .L_m = 50.0 * uH, .C_m = 50000.0 * pF});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "R_S+R_L");
    }
}
