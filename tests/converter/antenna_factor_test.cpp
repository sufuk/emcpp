// tests/converter/antenna_factor_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for emc::converter::AntennaFactorToGain (AF [dB/m] @ f -> gain [dBi]).
//   (a) hand-computed known value (f = 300 MHz, AF = 0 dB/m) plus the exposed lambda = c/f,
//   (b) property: raising AF by 20 dB/m lowers gain by exactly 20 dB (the 10^(AF/20) term),
//   (c) validation / edge case: f = 0 is rejected as OutOfRange, never inf.
#include <catch2/catch_test_macros.hpp>

#include <cmath>            // std::log10, std::pow

#include <emc/converter/antenna_factor.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, MHz, m, s, ...
using emc::converter::AntennaFactorInput;
using emc::units::Decibel;

// (a) Hand-computed KNOWN value.
//     f = 300 MHz -> lambda = c/f ~ 0.999308 m. With AF = 0 dB/m the 10^(AF/20) term is 1, so
//         gain = 10 * log10( (9.73 / lambda)^2 )  [dBi].
//     We re-derive the closed-form number inline (no golden file) and also pin lambda = c/f.
TEST_CASE("antenna factor: known value at 300 MHz, AF = 0", "[converter][antenna_factor][known]") {
    const double f_hz     = 300.0e6;
    const double lambda_m = emc::constants::c.numerical_value_in(m / s) / f_hz;   // ~0.999308 m
    const double expected = 10.0 * std::log10(std::pow(9.73 / lambda_m, 2.0));    // AF = 0 -> ~19.78 dBi

    const AntennaFactorInput in{ .frequency = 300.0 * MHz, .antenna_factor = Decibel{0.0} };
    const auto r = emc::converter::calculate(in);

    REQUIRE(r.has_value());
    // gain is a log (dB) wrapper, so compare its plain .value against the hand-computed dBi number.
    REQUIRE(r->gain.value == Catch::Approx(expected).epsilon(1e-9));
    // The exposed lambda intermediate really is c/f.
    REQUIRE(emc::test::approx(r->wavelength, lambda_m * m, 1e-9));
}

// (b) PROPERTY: AF appears as 10^(AF/20) in the denominator of the (squared) log argument, so
//     adding 20 dB/m to AF multiplies that denominator by 10, i.e. drops the gain by exactly 20 dB.
//     Locks the dB-arithmetic invariant independent of frequency.
TEST_CASE("antenna factor: +20 dB/m AF drops gain by 20 dB", "[converter][antenna_factor][property]") {
    const auto gain_for = [](double af) {
        return emc::converter::calculate(
                   { .frequency = 100.0 * MHz, .antenna_factor = Decibel{af} })
            ->gain.value;
    };
    REQUIRE((gain_for(0.0) - gain_for(20.0)) == Catch::Approx(20.0).epsilon(1e-9));
}

// (c) VALIDATION / edge: lambda = c/f needs f > 0, so f = 0 must surface as a typed OutOfRange
//     error (require_positive), never a poisoned inf from dividing by zero.
TEST_CASE("antenna factor: zero frequency is rejected", "[converter][antenna_factor][validate]") {
    const auto r = emc::converter::calculate(
        { .frequency = 0.0 * Hz, .antenna_factor = Decibel{2.0} });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);   // require_positive -> OutOfRange
    REQUIRE(r.error().field == "frequency");
}
