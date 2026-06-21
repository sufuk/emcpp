// include/emc/basic/dipole_antenna.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept
#include <emc/core/constants.hpp>    // emc::constants::c, ::pi
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Current/Length/Frequency/Angle/ElectricField/MagneticField

namespace emc::basic {

/// Inputs for the short-dipole near-field model.
/// Geometry, current and frequency are mp-units typed, so the unit lives in the
/// type and a wrong-unit mix-up will not compile. theta is a typed Angle: build
/// it from degrees once at the call site (e.g. (80.0 * deg).in(si::radian)).
struct DipoleAntennaInput {
    emc::units::Current   current{};      ///< I0  > 0
    emc::units::Length    length{};       ///< l   > 0  (dipole length)
    emc::units::Length    distance{};     ///< R   > 0  (observation distance)
    emc::units::Frequency frequency{};    ///< f   > 0
    emc::units::Angle     theta{};        ///< theta (polar angle)
};

/// Three near-field components: two E-fields [V/m] and one H-field [A/m].
/// The outputs are distinguished by type, so an E result can never be written
/// into the H slot by accident.
struct DipoleAntennaResult {
    emc::units::ElectricField e_r{};      ///< E_r     [V/m]
    emc::units::ElectricField e_theta{};  ///< E_theta [V/m]
    emc::units::MagneticField h_phi{};    ///< H_phi   [A/m]
};

// [[nodiscard]]: dropping the validation/result is a bug, so make it a warning.
[[nodiscard]] std::expected<void, emc::Error> validate(const DipoleAntennaInput& in);
[[nodiscard]] emc::Result<DipoleAntennaResult> calculate(const DipoleAntennaInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so the
// static_assert below checks the calculator contract at compile time.
struct DipoleAntenna {
    using Input  = DipoleAntennaInput;
    using Result = DipoleAntennaResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<DipoleAntenna>);

} // namespace emc::basic
