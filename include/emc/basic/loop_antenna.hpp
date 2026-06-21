// include/emc/basic/loop_antenna.hpp
#pragma once

#include <expected>   // std::expected — typed success-or-error return for validate()

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

/// Inputs for the small-loop near-field model (magnetic dual of the short dipole).
/// Every field is a typed mp-units quantity, so a wrong unit cannot be passed in.
struct LoopAntennaInput {
    emc::units::Current   current{};      ///< I0  > 0
    emc::units::Area      loop_area{};    ///< A   > 0  (loop area, typed as area)
    emc::units::Length    distance{};     ///< R   > 0  (observation distance)
    emc::units::Frequency frequency{};    ///< f   > 0
    emc::units::Angle     theta{};        ///< theta  (store as Angle; build from degrees at the call site)
};

/// Three near-field components — two H-fields and one E-field (dual of the dipole).
/// Naming each output by type means an H value cannot be stored into the E slot.
struct LoopAntennaResult {
    emc::units::MagneticField h_r{};       ///< H_r     [A/m]
    emc::units::MagneticField h_theta{};   ///< H_theta [A/m]
    emc::units::ElectricField e_phi{};     ///< E_phi   [V/m]
};

// [[nodiscard]]: the Result/expected carries the only success or error signal, so
// dropping it on the floor is almost always a bug worth a compiler warning.
[[nodiscard]] std::expected<void, emc::Error> validate(const LoopAntennaInput& in);
[[nodiscard]] emc::Result<LoopAntennaResult> calculate(const LoopAntennaInput& in);

// Tag type that adapts the free functions to the Calculator concept (Input/Result + statics).
struct LoopAntenna {
    using Input  = LoopAntennaInput;
    using Result = LoopAntennaResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
// Compile-time proof the tag really models a validated calculator (catches signature drift early).
static_assert(emc::ValidatedCalculator<LoopAntenna>);

} // namespace emc::basic
