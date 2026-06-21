// src/testing/noise_figure.cpp
#include <emc/testing/noise_figure.hpp>

#include <cmath>   // std::log10 (constexpr in C++23)

#include <emc/core/units.hpp>   // emc::units::to_ratio(Decibel) -> linear power ratio

namespace emc::testing {

// ---------------------------------------------------------------------------
//  validate()
//  The only true precondition for the Friis math is a non-empty cascade: with
//  >= 1 stage, F >= F_1 = 10^(NF_1/10) > 0, so std::log10(F) is always
//  in-domain. An empty cascade has no defined noise figure (log10 of 0), so we
//  reject it explicitly as a typed, recoverable InvalidInput rather than
//  letting -inf flow downstream. No per-stage range limits: any real NF/gain in
//  dB (including negative gain for lossy stages) is physically admissible.
// ---------------------------------------------------------------------------
std::expected<void, emc::Error> validate(const NoiseFigureInput& in) {
    if (in.stages.empty())
        return std::unexpected(emc::invalid_input(
            "receiver cascade must contain at least one stage", "stages"));
    return {};   // empty success: std::expected<void, Error> default-OK
}

// ---------------------------------------------------------------------------
//  calculate()  — Friis cascade over a std::span of N stages.
//
//    F            = sum_i  numerator_i / denominator_i
//    numerator_0   = 10^(NF_1/10)            (full linear factor of stage 1)
//    numerator_i>0 = 10^(NF_i/10) - 1        (excess noise of later stages)
//    denominator_i = product_{j<i} 10^(G_j/10)   (preceding gains only)
//    NF            = 10 * log10(F)
//    G_total       = sum_i  G_i              (added in dB)
// ---------------------------------------------------------------------------
emc::Result<NoiseFigureResult> calculate(const NoiseFigureInput& in) {
    // Guard first; on bad input forward the typed Error and never touch the math.
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    double F = 0.0;
    double total_gain_dB = 0.0;

    // Running product of the linear gains of all PRECEDING stages. Starts at 1
    // (empty product) so stage 0 sees denominator == 1. Building it incrementally
    // keeps the cascade O(N) instead of the naive O(N^2).
    double preceding_gain_product = 1.0;

    for (std::size_t i = 0; i < in.stages.size(); ++i) {
        // const& keeps the accumulation readable without copying the stage.
        const Stage& s = in.stages[i];

        // dB -> linear power ratio via the explicit, type-safe foundation helper:
        // the 10^(x/10) conversion happens ONLY at this boundary.
        const double f_lin = emc::units::to_ratio(s.nf);   // 10^(NF_i/10)

        const double numerator = (i == 0) ? f_lin : (f_lin - 1.0);
        F += numerator / preceding_gain_product;

        // Fold THIS stage's gain into the running product for the NEXT stage, and
        // accumulate the dB sum for the total-gain output (order-independent).
        preceding_gain_product *= emc::units::to_ratio(s.gain);   // 10^(G_i/10)
        total_gain_dB += s.gain.value;
    }

    // F >= 10^(NF_1/10) > 0 for any non-empty cascade, so log10 is in-domain;
    // validate() guarantees non-empty, so no DomainError branch is reachable.
    const double nf_dB = 10.0 * std::log10(F);

    // Direct-init the typed dB wrappers from the plain doubles (Decibel is the
    // logarithmic vocabulary; no implicit linear conversion ever happens here).
    return NoiseFigureResult{
        .noise_figure = emc::units::Decibel{nf_dB},
        .total_gain   = emc::units::Decibel{total_gain_dB},
    };
}

} // namespace emc::testing
