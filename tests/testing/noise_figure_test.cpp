// tests/testing/noise_figure_test.cpp
//
// Tests for emc::testing — Noise Figure of an RF receiver (Friis cascade, N-stage).
// Strategy:
//   (a) HAND-COMPUTED KNOWN-VALUE: a two-stage textbook chain whose cascade NF is
//       worked out by hand below, plus the trivial dB-sum total gain.
//   (b) PROPERTY tests: single-stage identity, high first-stage-gain dominance, and
//       order-sensitivity of NF vs order-independence of total gain (the physics the
//       calculator exists to express), checked against an independent in-test Friis
//       reference implementation.
//   (c) VALIDATION / EDGE: an empty cascade is rejected with ErrorCode::InvalidInput.
//
// NOTE on units: this calculator's inputs/outputs are emc::units::Decibel, a plain
// { double value; } LOG wrapper (NOT an mp-units quantity), so emc::test::approx
// (which compares same-dimension mp-units quantities) does not apply to the dB
// results. We compare the .value doubles directly with a relative tolerance. All
// expected numbers are textbook / hand-computed and written inline. No CSV/golden files.

#include <array>
#include <cmath>      // std::pow, std::log10, std::abs
#include <span>

#include <catch2/catch_test_macros.hpp>

#include <emc/testing/noise_figure.hpp>
#include <emc/core/error.hpp>   // emc::ErrorCode

#include "support/approx.hpp"

// mp-units is part of the library's vocabulary; pulled in for parity with the suite
// even though this purely-logarithmic calculator carries no linear mp-units quantity.
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

using emc::units::Decibel;
using namespace emc::testing;

namespace {

// Independent, dependency-free reference for the Friis cascade in dB, written straight
// from the textbook formula so the property tests do not merely re-run production code:
//   F = F1 + (F2-1)/g1 + (F3-1)/(g1*g2) + ...   with F_i = 10^(NF_i/10), g_j = 10^(G_j/10)
//   NF = 10*log10(F)
[[nodiscard]] double friis_nf_dB(std::span<const Stage> stages) noexcept {
    double F = 0.0, prod = 1.0;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        const double f = std::pow(10.0, stages[i].nf.value / 10.0);
        F += (i == 0 ? f : f - 1.0) / prod;
        prod *= std::pow(10.0, stages[i].gain.value / 10.0);
    }
    return 10.0 * std::log10(F);
}

// Decibel is a plain double wrapper, so compare .value with a relative tolerance plus
// a tiny absolute floor (mirrors emc::test::approx but for the non-unit dB scalar).
[[nodiscard]] bool db_close(Decibel a, Decibel b, double rel = 1e-9) noexcept {
    const double diff = std::abs(a.value - b.value);
    return diff <= 1e-12 || diff <= rel * std::max(std::abs(a.value), std::abs(b.value));
}

} // namespace

// ---------------------------------------------------------------------------
// (a) HAND-COMPUTED KNOWN-VALUE — textbook two-stage example.
//     LNA: NF=1 dB, G=20 dB ; 2nd stage: NF=10 dB, G=10 dB.
//       F1 = 10^(1/10)  = 1.2589254...   g1 = 10^(20/10) = 100
//       F2 = 10^(10/10) = 10
//       F  = F1 + (F2-1)/g1 = 1.2589254 + 9/100 = 1.3489254
//       NF = 10*log10(1.3489254) = 1.2998794 dB
//       G_total = 20 + 10 = 30 dB
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure two-stage textbook value", "[testing][noise_figure]") {
    const std::array<Stage, 2> stages{{
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
    }};

    const auto r = calculate(NoiseFigureInput{.stages = stages});
    REQUIRE(r.has_value());

    REQUIRE(db_close(r->noise_figure, Decibel{1.2998794}, 1e-6));
    REQUIRE(db_close(r->total_gain,   Decibel{30.0},      1e-12));
}

// ---------------------------------------------------------------------------
// (a') KNOWN-VALUE vs an independent Friis reference over a 3-stage chain —
//      production output must match the textbook formula transcribed in-test.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure matches an independent Friis reference", "[testing][noise_figure]") {
    const std::array<Stage, 3> stages{{
        {.nf = Decibel{1.0}, .gain = Decibel{20.0}},   // LNA
        {.nf = Decibel{6.0}, .gain = Decibel{-3.0}},   // passive mixer (loss)
        {.nf = Decibel{8.0}, .gain = Decibel{15.0}},   // IF amplifier
    }};

    const auto r = calculate(NoiseFigureInput{.stages = stages});
    REQUIRE(r.has_value());

    const double expected_nf   = friis_nf_dB(stages);
    const double expected_gain = 20.0 + (-3.0) + 15.0;   // dB sum, order-independent

    REQUIRE(db_close(r->noise_figure, Decibel{expected_nf},   1e-9));
    REQUIRE(db_close(r->total_gain,   Decibel{expected_gain}, 1e-12));
}

// ---------------------------------------------------------------------------
// (b1) PROPERTY — single stage is the identity: the cascade NF equals that
//      stage's NF and the total gain equals that stage's gain.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure single stage is the identity", "[testing][noise_figure][property]") {
    const std::array<Stage, 1> one{{ {.nf = Decibel{3.5}, .gain = Decibel{12.0}} }};

    const auto r = calculate(NoiseFigureInput{.stages = one});
    REQUIRE(r.has_value());

    REQUIRE(db_close(r->noise_figure, Decibel{3.5}));    // NF == NF_1 exactly
    REQUIRE(db_close(r->total_gain,   Decibel{12.0}));
}

// ---------------------------------------------------------------------------
// (b2) PROPERTY — a high first-stage gain dominates: with 40 dB of LNA gain the
//      downstream excess noise (F2-1)/g1 is tiny, so the cascade NF stays within
//      ~0.01 dB of the 1 dB first-stage figure (the whole reason an LNA exists).
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure: high first-stage gain dominates", "[testing][noise_figure][property]") {
    const std::array<Stage, 2> good{{
        {.nf = Decibel{1.0},  .gain = Decibel{40.0}},   // big LNA gain
        {.nf = Decibel{15.0}, .gain = Decibel{5.0}},    // very noisy 2nd stage
    }};

    const auto r = calculate(NoiseFigureInput{.stages = good});
    REQUIRE(r.has_value());
    REQUIRE(r->noise_figure.value > 1.00);
    REQUIRE(r->noise_figure.value < 1.02);
}

// ---------------------------------------------------------------------------
// (b3) PROPERTY — stage order changes the cascade NF but not the total gain.
//      LNA-first must be the lower-noise arrangement; total gain (a plain dB sum)
//      is order-independent.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure: stage order changes the cascade NF", "[testing][noise_figure][property]") {
    const std::array<Stage, 2> lna_first{{
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
    }};
    const std::array<Stage, 2> lna_last{{
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
    }};

    const auto a = calculate(NoiseFigureInput{.stages = lna_first});
    const auto b = calculate(NoiseFigureInput{.stages = lna_last});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    REQUIRE(a->noise_figure.value < b->noise_figure.value);   // LNA-first is quieter
    REQUIRE(db_close(a->total_gain, b->total_gain, 1e-12));    // gain order-independent
}

// ---------------------------------------------------------------------------
// (c) VALIDATION / EDGE — an empty cascade has no defined NF (log10 of 0) and
//     must be rejected with the right ErrorCode and field.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure rejects an empty cascade", "[testing][noise_figure][error]") {
    const auto r = calculate(NoiseFigureInput{.stages = {}});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
    REQUIRE(r.error().field == "stages");
}
