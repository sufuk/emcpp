// tests/converter/vswr_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for the VSWR family (Γ / RL / ML / IL) multi-output converter.
// Expected values are hand-computed / textbook closed forms (no CSV, no golden files).
#include <catch2/catch_test_macros.hpp>

#include <cmath>     // std::log10, std::abs, std::pow

#include <mp-units/systems/si.h>

#include <emc/converter/vswr.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::converter::VswrInput;

// Closed-form expecteds for a given VSWR (valid for Γ > 0, i.e. VSWR > 1).
namespace {
struct Expected { double gamma, rl, ml, il; };
[[nodiscard]] Expected expect(double vswr) {
    const double g = (vswr - 1.0) / (vswr + 1.0);
    return { g,
             -20.0 * std::log10(g),                                  // RL
             -10.0 * std::log10(1.0 - g * g),                        // ML
             -10.0 * std::log10(std::pow(std::abs(1.0 + g), 2.0)) }; // IL
}
} // namespace

// ---------------------------------------------------------------------------
// (a) Known value: VSWR = 2.
//     Γ  = (2-1)/(2+1) = 1/3
//     RL = -20*log10(1/3) = 20*log10(3)            ≈ 9.5424250944 dB
//     ML = -10*log10(1 - 1/9) = -10*log10(8/9)     ≈ 0.5115252245 dB
//     IL = -20*log10(4/3)                          ≈ 2.4987747105 dB
// ---------------------------------------------------------------------------
TEST_CASE("vswr: VSWR=2 known value", "[converter][vswr]") {
    auto r = emc::converter::calculate({ .vswr = 2.0 * one });
    REQUIRE(r.has_value());

    REQUIRE(emc::test::approx(r->reflection_coefficient, (1.0 / 3.0) * one, 1e-12));
    REQUIRE(r->return_loss.value    == Catch::Approx(9.5424250944).epsilon(1e-9));
    REQUIRE(r->mismatch_loss.value  == Catch::Approx(0.5115252245).epsilon(1e-7));
    REQUIRE(r->insertion_loss.value == Catch::Approx(-2.4987747105).epsilon(1e-7));
}

// ---------------------------------------------------------------------------
// (b) Property / cross-check: all four figures match the closed form for a
//     representative VSWR (textbook 5.83 example). This pins the relationship
//     between Γ and the three dB losses, not just one number.
// ---------------------------------------------------------------------------
TEST_CASE("vswr: four-figure closed form", "[converter][vswr][property]") {
    const double vswr = 5.83;
    auto r = emc::converter::calculate({ .vswr = vswr * one });
    REQUIRE(r.has_value());

    const auto e = expect(vswr);
    REQUIRE(emc::test::approx(r->reflection_coefficient, e.gamma * one, 1e-9));
    REQUIRE(r->return_loss.value    == Catch::Approx(e.rl).epsilon(1e-9));
    REQUIRE(r->mismatch_loss.value  == Catch::Approx(e.ml).epsilon(1e-9));
    REQUIRE(r->insertion_loss.value == Catch::Approx(e.il).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// (b2) Monotonicity property: a worse match (larger VSWR) gives less return
//      loss. RL is a strictly decreasing function of VSWR for VSWR > 1.
// ---------------------------------------------------------------------------
TEST_CASE("vswr: return loss decreases with VSWR", "[converter][vswr][property]") {
    auto rl = [](double v) {
        return emc::converter::calculate({ .vswr = v * one })->return_loss.value;
    };
    REQUIRE(rl(6.0) < rl(2.0));
    REQUIRE(rl(2.0) < rl(1.5));
}

// ---------------------------------------------------------------------------
// (c) Validation / edge: VSWR == 1 (perfect match) => Γ = 0 => RL = +inf, so it
//     is rejected as a DomainError rather than returning a poisoned value.
//     VSWR < 1 is unphysical => OutOfRange.
// ---------------------------------------------------------------------------
TEST_CASE("vswr: degenerate inputs rejected", "[converter][vswr][validation]") {
    auto perfect = emc::converter::calculate({ .vswr = 1.0 * one });
    REQUIRE_FALSE(perfect.has_value());
    REQUIRE(perfect.error().code == emc::ErrorCode::DomainError);

    auto below = emc::converter::calculate({ .vswr = 0.5 * one });
    REQUIRE_FALSE(below.has_value());
    REQUIRE(below.error().code == emc::ErrorCode::OutOfRange);
}
