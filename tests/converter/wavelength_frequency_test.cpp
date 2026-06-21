// tests/converter/wavelength_frequency_test.cpp
//
// Catch2 v3 tests for emc::converter Wavelength <-> Frequency (bidirectional):
//   f = c / lambda   (solve_frequency)   and   lambda = c / f   (solve_wavelength)
// Both directions share one detail:: core that divides the SAME exact c, so the
// forward/inverse round-trip is exact by construction. Expected values are
// hand-computed / textbook closed form (no CSV / golden files).
#include <catch2/catch_test_macros.hpp>

#include <mp-units/systems/si.h>

#include <emc/converter/wavelength_frequency.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
namespace cv = emc::converter;

// (a) Hand-computed known value: lambda = 1 m -> f = c = 299_792_458 Hz exactly.
//     c is the SI exact defining constant, so the result is c numerically (in Hz).
TEST_CASE("wavelength<->freq: 1 m -> f = c", "[converter][wavelength_freq]") {
    auto r = cv::solve_frequency({.wavelength = 1.0 * m});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->frequency, 299'792'458.0 * Hz, 1e-9));
}

// (a2) Second hand-computed value, the inverse direction:
//      f = 2.99792458 GHz -> lambda = c / f = 299792458 / 2.99792458e9 = 0.1 m exactly.
TEST_CASE("wavelength<->freq: 2.99792458 GHz -> 0.1 m", "[converter][wavelength_freq]") {
    auto r = cv::solve_wavelength({.frequency = 2.997'924'58 * (si::giga<Hz>)});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->wavelength, 0.1 * m, 1e-9));
}

// (b) Round-trip / involution property: f -> lambda -> f is the identity.
//     Sharing the same c in both directions makes this exact to floating tolerance.
TEST_CASE("wavelength<->freq: f -> lambda -> f round-trip", "[converter][wavelength_freq][property]") {
    const auto f0 = 2.4 * (si::giga<Hz>);
    auto lam = cv::solve_wavelength({.frequency = f0});
    REQUIRE(lam.has_value());
    auto f1 = cv::solve_frequency({.wavelength = lam->wavelength});
    REQUIRE(f1.has_value());
    REQUIRE(emc::test::approx(f1->frequency, f0, 1e-12));
}

// (b2) Round-trip the other way: lambda -> f -> lambda is the identity.
TEST_CASE("wavelength<->freq: lambda -> f -> lambda round-trip", "[converter][wavelength_freq][property]") {
    const auto lam0 = 0.125 * m;
    auto f = cv::solve_frequency({.wavelength = lam0});
    REQUIRE(f.has_value());
    auto lam1 = cv::solve_wavelength({.frequency = f->frequency});
    REQUIRE(lam1.has_value());
    REQUIRE(emc::test::approx(lam1->wavelength, lam0, 1e-12));
}

// (c) Validation/edge: lambda = 0 must be rejected (lambda = 0 would divide by zero
//     in f = c/lambda). require_positive reports OutOfRange, never inf.
TEST_CASE("wavelength<->freq: zero wavelength rejected", "[converter][wavelength_freq][validation]") {
    auto r = cv::solve_frequency({.wavelength = 0.0 * m});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}

// (c2) Validation/edge: f = 0 must be rejected on the inverse direction too.
TEST_CASE("wavelength<->freq: zero frequency rejected", "[converter][wavelength_freq][validation]") {
    auto r = cv::solve_wavelength({.frequency = 0.0 * Hz});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}
