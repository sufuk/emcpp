// tests/shielding/slot_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for the slot (lambda/2 resonance) shielding-effectiveness
// calculator, emc::shielding::calculate(SlotSeInput).
//
//   wavelength  lambda = c / f
//   SE                 = 20 * log10( lambda / (2 * length) )   [dB]   (may be negative)
//
// All expected values are hand-computed inline from the closed form under the
// exact SI c = 299'792'458 m/s. No CSV / golden files.

#include <cmath>   // std::log10

#include <catch2/catch_test_macros.hpp>

#include <emc/shielding/slot.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, MHz, m, mm, ...
namespace si = mp_units::si;

using emc::shielding::SlotSeInput;

// ---------------------------------------------------------------------------
// (a) KNOWN-VALUE: hand-computed from the closed form.
//
//   f = 300 MHz  ->  lambda = c/f = 299'792'458 / 3e8 = 0.999308193... m
//   length = 10 mm = 0.01 m
//   SE = 20*log10( 0.999308193 / (2*0.01) )
//      = 20*log10( 0.999308193 / 0.02 )
//      = 20*log10( 49.96540966 )
//      = 33.9760... dB
// ---------------------------------------------------------------------------
TEST_CASE("slot SE known value", "[shielding][slot]") {
    auto r = emc::shielding::calculate(SlotSeInput{
        .frequency = 300.0 * MHz,
        .length    = 10.0 * mm,
    });
    REQUIRE(r.has_value());

    // wavelength lambda = c/f, dimensioned Length.
    REQUIRE(emc::test::approx(r->wavelength, 0.999308193 * m, 1e-7));

    // SE [dB] — Decibel.value is a plain double.
    const double lambda = 299'792'458.0 / 300.0e6;
    const double se     = 20.0 * std::log10(lambda / (2.0 * 0.010));
    REQUIRE(r->shielding.value == Catch::Approx(se).epsilon(1e-9));
    REQUIRE(r->shielding.value == Catch::Approx(33.97339).epsilon(1e-5));
}

// ---------------------------------------------------------------------------
// (b) PROPERTY: at the exact half-wave resonance length == lambda/2, the slot
//     leaks exactly as much as it blocks, so SE crosses 0 dB.
//
//     f = 299.792458 MHz  ->  lambda = c/f = 1.0 m  ->  lambda/2 = 0.5 m.
//     SE = 20*log10( 1.0 / (2*0.5) ) = 20*log10(1) = 0 dB.
// ---------------------------------------------------------------------------
TEST_CASE("slot SE is 0 dB at half-wave resonance", "[shielding][slot][property]") {
    auto r = emc::shielding::calculate(SlotSeInput{
        .frequency = 299.792458 * MHz,
        .length    = 0.5 * m,
    });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->wavelength, 1.0 * m, 1e-9));
    REQUIRE(r->shielding.value == Catch::Approx(0.0).margin(1e-9));
}

// ---------------------------------------------------------------------------
// (b2) PROPERTY: past resonance (length > lambda/2) SE goes NEGATIVE and is
//      monotone-decreasing in slot length at fixed frequency. Also a +6.0206 dB
//      step per length-halving falls straight out of the 20*log10 form
//      (20*log10(2) = 6.0206 dB), so two lengths differing by 2x differ by that.
// ---------------------------------------------------------------------------
TEST_CASE("slot SE decreases monotonically with slot length", "[shielding][slot][property]") {
    auto small = emc::shielding::calculate(SlotSeInput{.frequency = 300.0 * MHz, .length =  5.0 * mm});
    auto big   = emc::shielding::calculate(SlotSeInput{.frequency = 300.0 * MHz, .length = 10.0 * mm});
    REQUIRE(small.has_value());
    REQUIRE(big.has_value());

    // Monotone trend: a longer slot (closer to resonance) blocks less.
    REQUIRE(small->shielding.value > big->shielding.value);

    // Halving the length (10 mm -> 5 mm) raises SE by exactly 20*log10(2).
    const double step = small->shielding.value - big->shielding.value;
    REQUIRE(step == Catch::Approx(20.0 * std::log10(2.0)).epsilon(1e-9));   // ~6.0206 dB

    // Past resonance: 5 m slot at 300 MHz (lambda ~ 1 m) leaks -> SE < 0.
    auto leaky = emc::shielding::calculate(SlotSeInput{.frequency = 300.0 * MHz, .length = 5.0 * m});
    REQUIRE(leaky.has_value());
    REQUIRE(leaky->shielding.value < 0.0);
}

// ---------------------------------------------------------------------------
// (c) VALIDATION / EDGE: zero frequency -> lambda = c/0; rejected as OutOfRange
//     by require_positive(frequency).
// ---------------------------------------------------------------------------
TEST_CASE("slot SE rejects zero frequency", "[shielding][slot][validation]") {
    auto r = emc::shielding::calculate(SlotSeInput{
        .frequency = 0.0 * Hz,
        .length    = 1.0 * m,
    });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// ---------------------------------------------------------------------------
// (c2) VALIDATION / EDGE: zero length -> log10(lambda/0); rejected as OutOfRange
//      by require_positive(length).
// ---------------------------------------------------------------------------
TEST_CASE("slot SE rejects zero length", "[shielding][slot][validation]") {
    auto r = emc::shielding::calculate(SlotSeInput{
        .frequency = 1.0 * MHz,
        .length    = 0.0 * m,
    });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "length");
}
