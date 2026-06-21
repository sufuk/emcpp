// src/converter/vswr.cpp
#include <emc/converter/vswr.hpp>

#include <cmath>       // std::log10, std::abs

namespace emc::converter {

using namespace mp_units;
using mp_units::one;   // the dimensionless unit; numerical_value_in(one) extracts the raw ratio

std::expected<void, emc::Error> validate(const VswrInput& in) {
    // Pull the raw number out of the dimensionless quantity for the closed-form checks.
    const double vswr = in.vswr.numerical_value_in(one);

    // VSWR >= 1 is the physical floor (a ratio of standing-wave maxima to minima).
    // !(vswr >= 1.0) also rejects NaN, since any comparison with NaN is false.
    if (!(vswr >= 1.0))
        return std::unexpected(emc::out_of_range(1.0, 1e9, "vswr"));

    // We require STRICTLY > 1: VSWR == 1 is a perfect match (Gamma = 0), and then
    // RL = -20*log10(0) = +inf. Report it as a domain error the caller must handle,
    // rather than returning a poisoned +inf.
    if (vswr == 1.0)
        return std::unexpected(emc::domain_error(
            "VSWR == 1 implies infinite return loss (perfect match)", "vswr"));

    return {};
}

emc::Result<VswrResult> calculate(const VswrInput& in) {
    // and_then runs the math only if validate() succeeded, forwarding the Error
    // otherwise — so the log() math is unreachable on a degenerate (Gamma = 0) input.
    return validate(in).and_then([&]() -> emc::Result<VswrResult> {
        const double vswr  = in.vswr.numerical_value_in(one);
        const double gamma = (vswr - 1.0) / (vswr + 1.0);            // Gamma in (0, 1) for VSWR > 1

        // RL uses the -20*log10(Gamma) form (== -10*log10(Gamma^2) for Gamma > 0).
        const double rl = -20.0 * std::log10(gamma);
        const double ml = -10.0 * std::log10(1.0 - gamma * gamma);
        const double t  = std::abs(1.0 + gamma);                    // |1 + Gamma|
        const double il = -10.0 * std::log10(t * t);

        // Designated initializers name every field, so the four figures can never be
        // swapped. gamma * one relabels the raw ratio back into the Dimensionless type;
        // the dB losses go through the typed Decibel wrapper so they cannot be mistaken
        // for the dimensionless Gamma or for a linear power.
        return VswrResult{
            .reflection_coefficient = gamma * one,
            .return_loss            = emc::units::Decibel{rl},
            .mismatch_loss          = emc::units::Decibel{ml},
            .insertion_loss         = emc::units::Decibel{il},
        };
    });
}

} // namespace emc::converter
