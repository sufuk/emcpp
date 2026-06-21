// include/emc/converter/vswr.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators, error factories
#include <emc/core/units.hpp>        // emc::units::Dimensionless, ::Decibel

namespace emc::converter {

// ===========================================================================
//  VSWR family (multi-output):
//      Gamma = (VSWR - 1) / (VSWR + 1)        reflection coefficient  [-]
//      RL    = -20 * log10(Gamma)             return loss             [dB]
//      ML    = -10 * log10(1 - Gamma^2)       mismatch loss           [dB]
//      IL    = -10 * log10(|1 + Gamma|^2)     insertion loss          [dB]
//  From one VSWR (>= 1) we compute four mismatch figures in one shot.
// ===========================================================================

// VSWR is a pure dimensionless ratio (>= 1). Stored as a Dimensionless quantity
// so the "ratio-ness" lives in the type; the four losses below are dB log values.
struct VswrInput {
    emc::units::Dimensionless vswr{};     // VSWR [-], must be >= 1 (and > 1 for finite RL)
};

// Multi-output aggregate: all four figures come back named, so a consumer can
// pull them with structured bindings: auto [g, rl, ml, il] = *r;
struct VswrResult {
    emc::units::Dimensionless reflection_coefficient{};  // Gamma [-]
    emc::units::Decibel       return_loss{};             // RL [dB]
    emc::units::Decibel       mismatch_loss{};           // ML [dB]
    emc::units::Decibel       insertion_loss{};          // IL [dB]
};

// Guards VSWR >= 1, and strictly > 1: VSWR == 1 gives Gamma = 0 -> log(0) = +inf.
// [[nodiscard]]: dropping a validation result is a bug, so the compiler warns.
[[nodiscard]] std::expected<void, emc::Error> validate(const VswrInput& in);

// Forward-only, multi-output conversion. [[nodiscard]]: the four-figure result
// must not be silently dropped.
[[nodiscard]] emc::Result<VswrResult> calculate(const VswrInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic code
// and the static_assert below can check the contract at compile time.
struct Vswr {
    using Input  = VswrInput;
    using Result = VswrResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::converter::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::converter::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Vswr>);

} // namespace emc::converter
