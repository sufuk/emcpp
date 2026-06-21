// tests/converter/energy_frequency_test.cpp
//
// Catch2 v3 tests for emc::converter Energy <-> Frequency (E = h * f).
// Expected values are hand-computed / textbook closed forms under the exact
// 2019-SI Planck constant h = 6.626 070 15e-34 J*s and the exact elementary
// charge boundary 1 eV = 1.602 176 634e-19 J. No CSV / golden files.

#include <catch2/catch_test_macros.hpp>

#include <emc/converter/energy_frequency.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
namespace cv = emc::converter;

// (a) KNOWN VALUE — forward direction E = h * f, hand-computed in joules.
//     Pick f = 1.0e15 Hz (1 PHz). Then
//        E = 6.626 070 15e-34 J*s * 1.0e15 1/s = 6.626 070 15e-19 J.
//     We compare the result quantity against that exact joule value.
TEST_CASE("energy<->freq: forward E = h*f known value", "[converter][energy_freq]") {
    const auto f = 1.0e15 * Hz;
    auto r = cv::solve_energy({ .frequency = f });
    REQUIRE(r.has_value());

    // h * f = 6.62607015e-34 * 1.0e15 = 6.62607015e-19 J (exact under defining h).
    const cv::Energy expected = 6.626'070'15e-19 * J;
    REQUIRE(emc::test::approx(r->energy, expected, 1e-12));
}

// (a') KNOWN VALUE — inverse direction, textbook photon number.
//      A 1 eV photon: f = E/h = 1.602176634e-19 / 6.62607015e-34 ~= 2.417989e14 Hz.
TEST_CASE("energy<->freq: 1 eV photon frequency", "[converter][energy_freq]") {
    auto r = cv::solve_frequency({ .energy = cv::ev_to_joule(1.0) * J });
    REQUIRE(r.has_value());
    // Textbook reference value; loose tol since the literal is rounded.
    REQUIRE(emc::test::approx(r->frequency, 2.417989e14 * Hz, 1e-4));
}

// (b) ROUND-TRIP / PROPERTY — solve forward then back is the identity.
//     f -> E -> f must recover the original frequency (shared detail:: core, exact h).
TEST_CASE("energy<->freq: round-trip f->E->f identity", "[converter][energy_freq][property]") {
    const auto f0 = 5.0e14 * Hz;

    auto e = cv::solve_energy({ .frequency = f0 });
    REQUIRE(e.has_value());

    auto f1 = cv::solve_frequency({ .energy = e->energy });
    REQUIRE(f1.has_value());

    REQUIRE(emc::test::approx(f1->frequency, f0, 1e-12));

    // And the reverse round-trip E -> f -> E for an independent energy value.
    const cv::Energy e0 = 3.0e-19 * J;
    auto fb = cv::solve_frequency({ .energy = e0 });
    REQUIRE(fb.has_value());
    auto e2 = cv::solve_energy({ .frequency = fb->frequency });
    REQUIRE(e2.has_value());
    REQUIRE(emc::test::approx(e2->energy, e0, 1e-12));
}

// (c) VALIDATION / EDGE — non-positive frequency is rejected (require_positive).
//     A zero frequency would yield E = 0 silently; we want a typed OutOfRange error.
TEST_CASE("energy<->freq: nonpositive frequency rejected", "[converter][energy_freq][validation]") {
    auto zero = cv::solve_energy({ .frequency = 0.0 * Hz });
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(zero.error().code == emc::ErrorCode::OutOfRange);

    auto neg = cv::solve_energy({ .frequency = -1.0e9 * Hz });
    REQUIRE_FALSE(neg.has_value());
    REQUIRE(neg.error().code == emc::ErrorCode::OutOfRange);
}

// (c') VALIDATION / EDGE — non-positive energy is rejected on the inverse path.
TEST_CASE("energy<->freq: nonpositive energy rejected", "[converter][energy_freq][validation]") {
    auto r = cv::solve_frequency({ .energy = 0.0 * J });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}

// (d) constexpr eV boundary folds at compile time with the exact charge.
static_assert(cv::ev_to_joule(1.0) == 1.602'176'634e-19, "eV->J boundary must be exact");
static_assert(cv::joule_to_ev(cv::ev_to_joule(2.5)) == 2.5, "eV round-trip must be exact");
