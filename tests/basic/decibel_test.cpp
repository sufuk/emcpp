// tests/basic/decibel_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for the bidirectional Decibel / dBm calculator
// (emc::basic, include/emc/basic/decibel.hpp). All expected values are
// hand-computed / textbook — no CSV golden files.
//
//   Gain panel:   dB = 20·log10(V_gain) = 10·log10(P_gain)
//                 V_gain = 10^(dB/20),  P_gain = 10^(dB/10)
//   Level panel:  P[W]   = 10^((dBm − 30)/10)
//                 dBm    = 10·log10(P / 1 mW)
//                 V_rms  = √(P·R),   Vp = V_rms·√2
//
#include <catch2/catch_test_macros.hpp>

#include <emc/basic/decibel.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // V, W, ohm, ...
using emc::ErrorCode;

// ---------------------------------------------------------------------------
// (a) KNOWN-VALUE (hand-computed / textbook)
// ---------------------------------------------------------------------------

// Gain panel: +20 dB ⇒ voltage gain 10, power gain 100 (textbook anchor).
//   V_gain = 10^(20/20) = 10^1  = 10
//   P_gain = 10^(20/10) = 10^2  = 100
TEST_CASE("decibel: +20 dB ⇒ voltage gain 10, power gain 100", "[basic][decibel][known]") {
    const auto g = emc::basic::gain_from_db(emc::units::Decibel{20.0});
    REQUIRE(g.has_value());
    REQUIRE(g->decibels.value == Catch::Approx(20.0));
    REQUIRE(g->voltage_gain == Catch::Approx(10.0));
    REQUIRE(g->power_gain   == Catch::Approx(100.0));
}

// Level panel: 10 dBm into 50 Ω (hand-computed).
//   P  = 10^((10 − 30)/10) = 10^(-2) = 0.01 W = 10 mW
//   V  = √(P·R) = √(0.01 · 50) = √0.5 ≈ 0.70710678 V
//   Vp = V·√2  = √0.5 · √2 = 1.0 V exactly
TEST_CASE("decibel: 10 dBm into 50 Ω ⇒ P=10 mW, Vp=1 V", "[basic][decibel][known]") {
    const auto l = emc::basic::level_from_dbm(emc::units::Dbm{10.0}, 50.0 * ohm);
    REQUIRE(l.has_value());

    REQUIRE(l->dbm.value == Catch::Approx(10.0));
    REQUIRE(emc::test::approx(l->power,   0.01 * W, 1e-9));
    REQUIRE(emc::test::approx(l->power,   10.0 * si::milli<W>, 1e-9));
    REQUIRE(emc::test::approx(l->voltage, std::sqrt(0.5) * V, 1e-9));
    REQUIRE(emc::test::approx(l->peak,    1.0 * V, 1e-9));
}

// ---------------------------------------------------------------------------
// (b) PROPERTY / ROUND-TRIP (every field is a driver → involutions)
// ---------------------------------------------------------------------------

TEST_CASE("decibel solvers round-trip", "[basic][decibel][roundtrip]") {
    SECTION("dB → voltage gain → dB is the identity") {
        const auto g1 = emc::basic::gain_from_db(emc::units::Decibel{6.0});
        REQUIRE(g1.has_value());
        const auto g2 = emc::basic::gain_from_voltage_gain(g1->voltage_gain);
        REQUIRE(g2.has_value());
        REQUIRE(g2->decibels.value == Catch::Approx(6.0));
    }

    SECTION("dB → power gain → dB is the identity") {
        const auto g1 = emc::basic::gain_from_db(emc::units::Decibel{3.0});
        REQUIRE(g1.has_value());
        const auto g2 = emc::basic::gain_from_power_gain(g1->power_gain);
        REQUIRE(g2.has_value());
        REQUIRE(g2->decibels.value == Catch::Approx(3.0));
    }

    SECTION("dBm → power → dBm into 50 Ω is the identity") {
        const auto a = emc::basic::level_from_dbm(emc::units::Dbm{13.0}, 50.0 * ohm);
        REQUIRE(a.has_value());
        const auto b = emc::basic::level_from_power(a->power, 50.0 * ohm);
        REQUIRE(b.has_value());
        REQUIRE(b->dbm.value == Catch::Approx(13.0));
    }

    SECTION("voltage → peak → voltage is the identity") {
        const auto a = emc::basic::level_from_voltage(2.0 * V, 50.0 * ohm);
        REQUIRE(a.has_value());
        const auto b = emc::basic::level_from_peak(a->peak, 50.0 * ohm);
        REQUIRE(b.has_value());
        REQUIRE(emc::test::approx(b->voltage, 2.0 * V, 1e-9));
    }
}

// Cross-check the level panel against an independent closed-form re-derivation
// over several (dBm, load) pairs — guards dBm↔W, V=√(PR), Vp=V√2 together.
TEST_CASE("decibel level panel matches an independent re-derivation",
          "[basic][decibel][property]") {
    struct Sample { double dbm; double load; };
    for (const auto [dbm, load] : { Sample{-30.0, 50.0}, Sample{0.0, 75.0},
                                    Sample{13.0, 50.0},  Sample{30.0, 600.0} }) {
        const auto l = emc::basic::level_from_dbm(emc::units::Dbm{dbm}, load * ohm);
        REQUIRE(l.has_value());

        const double p_w = std::pow(10.0, (dbm - 30.0) / 10.0);
        REQUIRE(l->dbm.value == Catch::Approx(dbm));
        REQUIRE(emc::test::approx(l->power,   p_w * W, 1e-9));
        REQUIRE(emc::test::approx(l->voltage, std::sqrt(p_w * load) * V, 1e-9));
        REQUIRE(emc::test::approx(l->peak,    std::sqrt(2.0) * std::sqrt(p_w * load) * V, 1e-9));
    }
}

// ---------------------------------------------------------------------------
// (c) VALIDATION / EDGE (typed ErrorCode)
// ---------------------------------------------------------------------------

TEST_CASE("decibel rejects bad input with typed errors", "[basic][decibel][validate]") {
    SECTION("voltage gain ≤ 0 → log10 domain (OutOfRange)") {
        const auto r = emc::basic::gain_from_voltage_gain(0.0);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("power gain ≤ 0 → log10 domain (OutOfRange)") {
        const auto r = emc::basic::gain_from_power_gain(-2.0);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("zero load on voltage solver → DivisionByZero") {
        const auto r = emc::basic::level_from_voltage(1.0 * V, 0.0 * ohm);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
    }

    SECTION("zero load on power solver → OutOfRange (load must be > 0)") {
        const auto r = emc::basic::level_from_power(1.0 * W, 0.0 * ohm);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
