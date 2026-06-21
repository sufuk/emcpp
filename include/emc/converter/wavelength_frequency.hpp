// include/emc/converter/wavelength_frequency.hpp
#pragma once

#include <expected>   // std::expected: the "value or Error" return shape for validate()

#include <emc/core/calculator.hpp>   // (kept for parity with sibling converters; concepts surface)
#include <emc/core/error.hpp>        // emc::Result, emc::Error, the require_* validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Frequency typed quantities

namespace emc::converter {

// ===========================================================================
//  Wavelength <-> Frequency (bidirectional):  f = c/lambda  and  lambda = c/f
//
//  Modeled as TWO distinctly named free functions (solve_frequency /
//  solve_wavelength) that share ONE detail:: core. Bidirectional => named
//  solvers, not a magic std::optional target. Sharing the core means both
//  directions divide the SAME exact c, so lambda->f->lambda is an exact
//  round-trip by construction.
// ===========================================================================

// ----- wavelength -> frequency ---------------------------------------------

// Aggregate Input: one mp-units Length field. The unit lives in the type, so a
// caller passes 500.0 * nm or 1.0 * m and wrong-unit math will not compile.
struct WavelengthToFrequencyInput {
    emc::units::Length wavelength{};   ///< lambda (> 0)
};

struct FreqResult {
    emc::units::Frequency frequency{};   ///< f = c / lambda  [Hz]
};

// [[nodiscard]]: dropping a validation result is a bug, so the compiler warns.
[[nodiscard]] std::expected<void, emc::Error> validate(const WavelengthToFrequencyInput& in);
[[nodiscard]] emc::Result<FreqResult>         solve_frequency(const WavelengthToFrequencyInput& in);

// ----- frequency -> wavelength ---------------------------------------------

struct FrequencyToWavelengthInput {
    emc::units::Frequency frequency{};   ///< f (> 0)
};

struct WaveResult {
    emc::units::Length wavelength{};      ///< lambda = c / f  [m]
};

// Same validate name, different overload: the Input type selects the right one.
[[nodiscard]] std::expected<void, emc::Error> validate(const FrequencyToWavelengthInput& in);
[[nodiscard]] emc::Result<WaveResult>         solve_wavelength(const FrequencyToWavelengthInput& in);

} // namespace emc::converter
