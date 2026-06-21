// include/emc/component/capacitance.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::eps0, ::pi
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Area, ::Length, ::Capacitance

namespace emc::component {

// ===========================================================================
//  Parallel-plate capacitor:  C = eps0 * eps_r * A / d
// ===========================================================================

// Inputs for calculate(const ParallelPlateInput&).
// Geometry is mp-units typed (the unit lives in the type, so wrong-unit math will
// not compile); eps_r is a plain double because it is a bare ratio in the formula.
// Defaults (air, 1 m^2, 1 m) make designated-initializer call sites read cleanly.
struct ParallelPlateInput {
    emc::units::Area   area{1.0 * mp_units::square(mp_units::si::metre)};  // A  (> 0)
    emc::units::Length distance{1.0 * mp_units::si::metre};                // d  (> 0)
    double relative_permittivity{1.0};                                     // eps_r (>= 1)
};

struct ParallelPlateResult {
    emc::units::Capacitance capacitance{};   // C [F]; show with .in(si::pico<si::farad>)
};

// [[nodiscard]]: do not ignore the error — a dropped validation result is a bug.
[[nodiscard]] std::expected<void, emc::Error> validate(const ParallelPlateInput& in);
[[nodiscard]] emc::Result<ParallelPlateResult> calculate(const ParallelPlateInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic code
// and the static_assert below can check the contract at compile time.
struct ParallelPlate {
    using Input  = ParallelPlateInput;
    using Result = ParallelPlateResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<ParallelPlate>);

// ===========================================================================
//  Isolated conducting sphere:  C = 4 * pi * eps0 * r
// ===========================================================================

struct SphereInput {
    emc::units::Length radius{1.0 * mp_units::si::metre};   // r (> 0)
};

struct SphereResult {
    emc::units::Capacitance capacitance{};   // C [F]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const SphereInput& in);
[[nodiscard]] emc::Result<SphereResult> calculate(const SphereInput& in);

struct Sphere {
    using Input  = SphereInput;
    using Result = SphereResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Sphere>);

} // namespace emc::component
