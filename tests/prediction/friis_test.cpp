// tests/prediction/friis_test.cpp
//
// Catch2 v3 tests for the Friis link-budget calculator (emc::prediction::Friis).
// Expected values are hand-computed against the closed form
//   P_rx = 30 + 10*log10( P_tx * 10^(G_tx/10) * 10^(G_rx/10) * (c/(4*pi*R*f))^2 )
// using the exact speed of light c = 299 792 458 m/s.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <numbers>

#include <mp-units/systems/si.h>

#include <emc/prediction/friis.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// (a) Hand-computed known value with exact c.
//   P_tx = 1 W, G_tx = G_rx = 0 dBi, f = 1 GHz, R = 1 m.
//   bracket = 1 * 1 * 1 * (c/(4*pi*1*1e9))^2
//   P_rx    = 30 + 10*log10(bracket)
// received_power is a typed Dbm whose .value is the dBm number (a plain double),
// so we compare it with Catch::Approx rather than the unit-aware approx helper.
TEST_CASE("Friis known value (exact c)", "[prediction][friis]") {
    const double c = 299792458.0;
    const double term = std::pow(c / (4.0 * std::numbers::pi * 1.0 * 1.0e9), 2.0);
    const double expected = 30.0 + 10.0 * std::log10(1.0 * 1.0 * 1.0 * term);

    const pred::FriisInput in{
        .tx_power  = 1.0 * W,
        .tx_gain   = emc::units::Decibel{0.0},
        .rx_gain   = emc::units::Decibel{0.0},
        .frequency = 1.0e9 * Hz,
        .range     = 1.0 * m,
    };
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(out->received_power.value == Catch::Approx(expected).epsilon(1e-12));
}

// (a2) Known value with non-trivial gains, range and frequency, fully hand-computed.
//   P_tx = 2 W, G_tx = 3 dBi, G_rx = 6 dBi, f = 2.4 GHz, R = 100 m.
TEST_CASE("Friis known value with gains and 100 m range", "[prediction][friis]") {
    const double c    = 299792458.0;
    const double Ptx  = 2.0;
    const double Gtx  = 3.0;
    const double Grx  = 6.0;
    const double f    = 2.4e9;
    const double R     = 100.0;
    const double term  = std::pow(c / (4.0 * std::numbers::pi * R * f), 2.0);
    const double bracket =
        Ptx * std::pow(10.0, Gtx / 10.0) * std::pow(10.0, Grx / 10.0) * term;
    const double expected = 30.0 + 10.0 * std::log10(bracket);

    const pred::FriisInput in{
        .tx_power  = 2.0 * W,
        .tx_gain   = emc::units::Decibel{3.0},
        .rx_gain   = emc::units::Decibel{6.0},
        .frequency = 2.4e9 * Hz,
        .range     = 100.0 * m,
    };
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(out->received_power.value == Catch::Approx(expected).epsilon(1e-12));
}

// (b) Property: free-space path loss obeys the 1/R^2 and 1/f^2 laws.
//   Doubling R drops P_rx by 20*log10(2) ~ 6.0206 dB; doubling f drops it by the
//   same amount. This pins the (c/(4*pi*R*f))^2 term, independent of the absolute
//   level, and is checked on a margin (the levels are dB, so an additive offset).
TEST_CASE("Friis path-loss properties", "[prediction][friis][property]") {
    const pred::FriisInput base{
        .tx_power  = 1.0 * W,
        .tx_gain   = emc::units::Decibel{3.0},
        .rx_gain   = emc::units::Decibel{3.0},
        .frequency = 1.0e9 * Hz,
        .range     = 10.0 * m,
    };
    auto p0  = pred::calculate(base);
    auto p2R = pred::calculate(pred::FriisInput{
        .tx_power = base.tx_power, .tx_gain = base.tx_gain, .rx_gain = base.rx_gain,
        .frequency = base.frequency, .range = 20.0 * m});
    auto p2f = pred::calculate(pred::FriisInput{
        .tx_power = base.tx_power, .tx_gain = base.tx_gain, .rx_gain = base.rx_gain,
        .frequency = 2.0e9 * Hz, .range = base.range});
    REQUIRE(p0.has_value());
    REQUIRE(p2R.has_value());
    REQUIRE(p2f.has_value());

    const double drop = 20.0 * std::log10(2.0);   // ~6.0206 dB per octave
    REQUIRE(p2R->received_power.value ==
            Catch::Approx(p0->received_power.value - drop).margin(1e-9));
    REQUIRE(p2f->received_power.value ==
            Catch::Approx(p0->received_power.value - drop).margin(1e-9));
}

// (b2) Property: P_rx is linear-in-dB with transmit power. Scaling P_tx by 10x
//   raises P_rx by exactly 10 dB, and the gains add directly (1 dBi extra on each
//   side raises P_rx by 2 dB).
TEST_CASE("Friis is linear in dB with power and gain", "[prediction][friis][property]") {
    const pred::FriisInput base{
        .tx_power  = 1.0 * W,
        .tx_gain   = emc::units::Decibel{0.0},
        .rx_gain   = emc::units::Decibel{0.0},
        .frequency = 1.0e9 * Hz,
        .range     = 10.0 * m,
    };
    auto p0   = pred::calculate(base);
    auto p10x = pred::calculate(pred::FriisInput{
        .tx_power = 10.0 * W, .tx_gain = base.tx_gain, .rx_gain = base.rx_gain,
        .frequency = base.frequency, .range = base.range});
    auto pgain = pred::calculate(pred::FriisInput{
        .tx_power = base.tx_power, .tx_gain = emc::units::Decibel{1.0},
        .rx_gain = emc::units::Decibel{1.0},
        .frequency = base.frequency, .range = base.range});
    REQUIRE(p0.has_value());
    REQUIRE(p10x.has_value());
    REQUIRE(pgain.has_value());

    REQUIRE(p10x->received_power.value ==
            Catch::Approx(p0->received_power.value + 10.0).margin(1e-9));
    REQUIRE(pgain->received_power.value ==
            Catch::Approx(p0->received_power.value + 2.0).margin(1e-9));
}

// (c) Validation / edge cases — each input has a physical domain.

// R = 0 makes the 1/(4*pi*R*f) denominator vanish.
TEST_CASE("Friis rejects R = 0", "[prediction][friis][error]") {
    pred::FriisInput in{};
    in.range = 0.0 * m;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DivisionByZero);
}

// f = 0 makes the 1/(4*pi*R*f) denominator vanish.
TEST_CASE("Friis rejects f = 0", "[prediction][friis][error]") {
    pred::FriisInput in{};
    in.frequency = 0.0 * Hz;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DivisionByZero);
}

// P_tx <= 0 is a log-domain error (require_positive -> OutOfRange).
TEST_CASE("Friis rejects P_tx <= 0", "[prediction][friis][error]") {
    pred::FriisInput in{};
    in.tx_power = 0.0 * W;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);
}
