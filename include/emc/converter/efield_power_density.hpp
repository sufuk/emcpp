// include/emc/converter/efield_power_density.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::ElectricField, ::Impedance, ::PowerDensity

namespace emc::converter {

// ===========================================================================
//  E-Field -> Power Density:  P_D = E^2 / eta
//
//  A plane-wave electric field strength E and a wave impedance eta convert to
//  the far-field power density. Forward-only.
// ===========================================================================

// E-field -> power-density inputs. wave_impedance defaults to free-space 377 ohm.
// E is mp-units typed (V/m), so a wrong-dimension field will not compile.
struct EFieldPowerDensityInput {
    emc::units::ElectricField electric_field{};                 ///< E [V/m]
    // Member default via default_eta(): a call site can omit wave_impedance and
    // still get free-space behavior, with no magic literal at the boundary.
    emc::units::Impedance     wave_impedance = default_eta();   ///< eta [ohm], default 377

    static emc::units::Impedance default_eta();                 ///< 377 ohm (declared, defined in .cpp)
};

struct EFieldPowerDensityResult {
    emc::units::PowerDensity power_density{};                    ///< P_D [W/m^2]
};

// [[nodiscard]]: a dropped validation/result is a bug, so the compiler warns.
[[nodiscard]] std::expected<void, emc::Error>
validate(const EFieldPowerDensityInput& in);

[[nodiscard]] emc::Result<EFieldPowerDensityResult>
calculate(const EFieldPowerDensityInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic
// code and the static_assert below can check the contract at compile time.
struct EFieldToPowerDensity {
    using Input  = EFieldPowerDensityInput;
    using Result = EFieldPowerDensityResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::converter::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::converter::validate(in); }
};
static_assert(emc::ValidatedCalculator<EFieldToPowerDensity>);

} // namespace emc::converter
