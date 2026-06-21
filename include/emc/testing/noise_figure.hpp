// include/emc/testing/noise_figure.hpp
#pragma once

#include <expected>
#include <span>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, emc::invalid_input
#include <emc/core/units.hpp>        // emc::units::Decibel (typed dB wrapper)

namespace emc::testing {

// ---------------------------------------------------------------------------
//  Stage — one element of the receiver cascade. Both fields are logarithmic
//  (dB) values, modeled with the pinned emc::units::Decibel wrapper (NOT a
//  linear mp-units unit; dB never converts implicitly to/from linear power).
//
//  A whole cascade is just a std::span<const Stage>, so 2/3/N stages cost the
//  same at the call site and in the API.
// ---------------------------------------------------------------------------
struct Stage {
    emc::units::Decibel nf{};     // per-stage noise figure  NF_i [dB]
    emc::units::Decibel gain{};   // per-stage gain          G_i  [dB]
};

// ---------------------------------------------------------------------------
//  Input — the whole cascade as a non-owning span of stages. The span makes
//  the calculator agnostic to how the stages are stored (array, vector, ...);
//  the caller owns the storage.
// ---------------------------------------------------------------------------
struct NoiseFigureInput {
    std::span<const Stage> stages{};   // ordered: source/antenna .. last stage
};

// ---------------------------------------------------------------------------
//  Result — both outputs, each a dB value.
// ---------------------------------------------------------------------------
struct NoiseFigureResult {
    emc::units::Decibel noise_figure{};   // cascade NF = 10*log10(F)   [dB]
    emc::units::Decibel total_gain{};     // G_total = sum(G_i)         [dB]
};

// ---------------------------------------------------------------------------
//  validate() — structural checks the math relies on: keep std::log10 in its
//  domain (F > 0, which holds for >= 1 stage) and reject the empty cascade.
//  [[nodiscard]]: dropping the validation result (and a possible error) is a bug.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<void, emc::Error> validate(const NoiseFigureInput& in);

// ---------------------------------------------------------------------------
//  calculate() — Friis cascade. Forward-only.
// ---------------------------------------------------------------------------
[[nodiscard]] emc::Result<NoiseFigureResult> calculate(const NoiseFigureInput& in);

// ---------------------------------------------------------------------------
//  Tag type: names the (Input, Result, calculate, validate) triple so generic
//  code and the static_assert below can check the contract at compile time.
// ---------------------------------------------------------------------------
struct NoiseFigure {
    using Input  = NoiseFigureInput;
    using Result = NoiseFigureResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::testing::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::testing::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<NoiseFigure>);

} // namespace emc::testing
