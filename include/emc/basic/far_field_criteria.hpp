// include/emc/basic/far_field_criteria.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept
#include <emc/core/constants.hpp>    // emc::constants::c (used by the impl)
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Frequency, ::Length

namespace emc::basic {

// ===========================================================================
//  Far-field criteria (branch on D vs lambda/10)
//
//    lambda = c / f
//    if D > lambda/10 (electrically large antenna):
//        reactive_near_field  = 0.62 * sqrt(D^3 / lambda)
//        radiating_near_field = 2 * D^2 / lambda
//    else (electrically small antenna):
//        reactive_near_field  = lambda / 50
//        radiating_near_field = lambda
// ===========================================================================

// Inputs for calculate(const FarFieldCriteriaInput&).
// Both fields are mp-units typed, so the unit lives in the type and wrong-unit
// math will not compile. Defaults give a clean designated-initializer call site.
struct FarFieldCriteriaInput {
    emc::units::Frequency frequency{1.0 * mp_units::si::hertz};   // f > 0
    emc::units::Length    max_dimension{1.0 * mp_units::si::metre};  // D > 0 (largest antenna dimension)
};

// Wavelength plus the two near-field region boundaries.
struct FarFieldCriteriaResult {
    emc::units::Length wavelength{};            // lambda = c/f              [m]
    emc::units::Length reactive_near_field{};   // reactive boundary         [m]
    emc::units::Length radiating_near_field{};  // radiating boundary        [m]
    bool               electrically_large{};    // true iff D > lambda/10 (which branch ran)
};

// [[nodiscard]]: do not ignore the error — a dropped validation result is a bug.
[[nodiscard]] std::expected<void, emc::Error> validate(const FarFieldCriteriaInput& in);
[[nodiscard]] emc::Result<FarFieldCriteriaResult> calculate(const FarFieldCriteriaInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic code
// and the static_assert below can check the contract at compile time.
struct FarFieldCriteria {
    using Input  = FarFieldCriteriaInput;
    using Result = FarFieldCriteriaResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::basic::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<FarFieldCriteria>);

} // namespace emc::basic
