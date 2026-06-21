// include/emc/shielding/slot.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Frequency, ::Length, ::Decibel

namespace emc::shielding {

// ===========================================================================
//  Slot (lambda/2 resonance):
//      wavelength  lambda = c / f
//      SE                 = 20 * log10( lambda / (2 * length) )   [dB]
//
//  A long slot in a shield radiates most efficiently near its half-wavelength
//  resonance; SE measures how well the shield still blocks a slot of physical
//  `length`. SE goes NEGATIVE once length > lambda/2 (the slot leaks more than
//  it blocks) — the Decibel wrapper models that correctly and we never clamp it.
// ===========================================================================

// ---------------------------------------------------------------------------
//  SlotSeInput — frequency and the physical slot length.
//  Both fields are mp-units typed (the unit lives in the type, so wrong-unit
//  math will not compile); a caller may pass any frequency/length literal.
// ---------------------------------------------------------------------------
struct SlotSeInput {
    emc::units::Frequency frequency{};   // f       [Hz]
    emc::units::Length    length{};      // slot l  [m]
};

// Two outputs returned as one value: the wavelength and the shielding effectiveness.
struct SlotSeResult {
    emc::units::Length  wavelength{};    // lambda = c/f  [m]
    emc::units::Decibel shielding{};     // SE            [dB]  (may be negative)
};

// Reject f <= 0 (div-by-zero for lambda = c/f) and length <= 0
// (div-by-zero / log10 domain). [[nodiscard]]: a dropped validation is a bug.
[[nodiscard]] std::expected<void, emc::Error> validate(const SlotSeInput& in);

// Forward calculation: SlotSeInput -> {lambda, SE}.
[[nodiscard]] emc::Result<SlotSeResult> calculate(const SlotSeInput& in);

// Tag type: names the (Input, Result, calculate, validate) quadruple so generic
// code and the static_assert below can check the contract at compile time.
struct SlotSe {
    using Input  = SlotSeInput;
    using Result = SlotSeResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<SlotSe>);

} // namespace emc::shielding
