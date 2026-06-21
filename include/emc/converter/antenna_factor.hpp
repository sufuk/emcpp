// include/emc/converter/antenna_factor.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::converter {

/// Inputs for the antenna-factor -> gain conversion.
/// `antenna_factor` is a dB/m log quantity, modeled with the canonical Decibel
/// wrapper — it is NOT a linear mp-units unit, so the log value never mixes with
/// the linear metres math by accident.
struct AntennaFactorInput {
    emc::units::Frequency frequency{};            ///< f  (e.g. 100.0 * MHz), must be > 0
    emc::units::Decibel   antenna_factor{};       ///< AF [dB/m]
};

/// Result of the conversion.
struct AntennaFactorResult {
    emc::units::Decibel gain{};                   ///< realized gain [dBi]
    emc::units::Length  wavelength{};             ///< lambda = c/f (handy intermediate, exposed)
};

/// Range / positivity checks. Frequency must be > 0 (lambda = c/f). AF is unbounded
/// in principle, so we only guard f here.
// [[nodiscard]]: a dropped validation result hides a real error, so warn on it.
[[nodiscard]] std::expected<void, emc::Error>
validate(const AntennaFactorInput& in);

/// Forward conversion AF (dB/m) @ f  ->  gain (dBi).
[[nodiscard]] emc::Result<AntennaFactorResult>
calculate(const AntennaFactorInput& in);

// --- bind to the Calculator concept (compile-time contract) ------------------
// Tag type names the (Input, Result, calculate, validate) quad so generic code and
// the static_assert below can check the contract at compile time.
struct AntennaFactorToGain {
    using Input  = AntennaFactorInput;
    using Result = AntennaFactorResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::converter::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::converter::validate(in); }
};
static_assert(emc::ValidatedCalculator<AntennaFactorToGain>);

} // namespace emc::converter
