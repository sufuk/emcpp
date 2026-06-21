// include/emc/prediction/lightning_coupling.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::{Length, Voltage}

namespace emc::prediction {

namespace si = mp_units::si;
using namespace mp_units::si::unit_symbols;   // m, A, s for the field defaults below

// ===========================================================================
//  Lightning Coupling Level
//      V_ind = (mu0 * h) / (2*pi) * ln((r + d) / r) * (dI/dt)
//
//  Same loop-coupling geometry as the ESD model, but the lightning current is
//  given directly as a slew rate dI/dt [A/s] rather than I_peak / t_r, and the
//  result is reported in volts. There is no t_r divide, so the only failure mode
//  is the ln() argument.
// ===========================================================================

// dI/dt is a current slew rate: amperes per second. There is no named alias for
// it in the canonical units.hpp, so we name one locally from the ISQ derived
// quantity (current / time)[A/s]. Kept in this header because only the coupling
// calculators need it; modeling it as a real quantity makes (H/m * m) * (A/s)
// dimensionally *checked* to be volts at compile time.
using CurrentSlewRate =
    mp_units::quantity<(mp_units::isq::electric_current / mp_units::isq::time)
                       [mp_units::si::ampere / mp_units::si::second], double>;

// Inputs for the lightning induced-coupling model. Lengths are mp-units typed
// (the unit lives in the type, so wrong-unit math will not compile); dI/dt is the
// typed slew rate above. Defaults (all 1 m, dI/dt = 1 A/s) make a
// default-constructed Input immediately valid and usable.
struct LightningCouplingInput {
    emc::units::Length loop_height{1.0 * m};   ///< h : loop height
    emc::units::Length radius{1.0 * m};        ///< r : near-conductor radius/offset
    emc::units::Length distance{1.0 * m};      ///< d : loop-to-conductor distance
    CurrentSlewRate    di_dt{1.0 * (A / s)};   ///< dI/dt : current slew rate
};

// Output of the lightning coupling model.
struct LightningCouplingResult {
    emc::units::Voltage induced_voltage{};   ///< V_ind (stored SI volt)
};

// [[nodiscard]]: dropping a validation result is a bug, so the compiler warns.
[[nodiscard]] std::expected<void, emc::Error> validate(const LightningCouplingInput& in);
// Forward solve: V_ind from the loop geometry and the lightning current slew.
[[nodiscard]] emc::Result<LightningCouplingResult> calculate(const LightningCouplingInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic code
// and the static_assert below can check the contract at compile time.
struct LightningCoupling {
    using Input  = LightningCouplingInput;
    using Result = LightningCouplingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<LightningCoupling>);

} // namespace emc::prediction
