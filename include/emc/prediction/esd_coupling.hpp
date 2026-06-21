// include/emc/prediction/esd_coupling.hpp
#pragma once

#include <expected>   // std::expected: validate() returns "ok, or the first Error"

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept (compile-time contract)
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::{Length, Current, Time, Voltage}

namespace emc::prediction {

// Pull in the SI unit symbols (m, A, ns, s, ...) so the member defaults below can be
// written as literals like `0.03 * m`. The unit lives in the type, so a wrong-unit
// default would not compile.
namespace si = mp_units::si;
using namespace mp_units::si::unit_symbols;

// ===========================================================================
//  ESD Coupling Level:  V_ind = (mu0*h)/(2*pi) * ln((r+d)/r) * (I_peak / t_r)
//
//  Voltage induced in a small rectangular ground loop by the fast-rising current
//  of an electrostatic-discharge event (the mutual-inductance / dI/dt model).
// ===========================================================================

// Inputs for the ESD induced-coupling model. Lengths are real lengths (any unit),
// I_peak is a current, t_r is a (rise) time. Each field is mp-units typed, so the
// caller passes `0.79 * m` or `790 * mm` with implicit exact conversion and no
// per-unit scale factor to get wrong. Defaults describe a valid, representative ESD
// event, so a default-constructed Input is immediately usable.
struct EsdCouplingInput {
    emc::units::Length  loop_height{0.03 * m};   // h      : loop height
    emc::units::Length  radius{1.0 * m};         // r      : near-conductor radius/offset
    emc::units::Length  distance{0.03 * m};      // d      : loop-to-conductor distance
    emc::units::Current peak_current{75.0 * A};  // I_peak : ESD peak current
    emc::units::Time    rise_time{1.0 * ns};     // t_r    : current rise time (entered in ns)
};

// Output of the ESD coupling model.
struct EsdCouplingResult {
    emc::units::Voltage induced_voltage;   // V_ind (stored in SI volt; display in mV)
};

// Range / positivity / non-zero checks. Rejects the t_r divide-by-zero and guards the
// ln() argument. [[nodiscard]]: a dropped validation result is a bug, so warn.
[[nodiscard]] std::expected<void, emc::Error> validate(const EsdCouplingInput& in);

// Forward solve: V_ind from the loop geometry and the ESD current ramp.
[[nodiscard]] emc::Result<EsdCouplingResult> calculate(const EsdCouplingInput& in);

// --- Calculator-concept binding (compile-time contract check) -----------------
// Zero-data tag struct that names the (Input, Result, calculate, validate) quad and
// forwards to the free functions, so generic code can treat them as one named thing.
struct EsdCoupling {
    using Input  = EsdCouplingInput;
    using Result = EsdCouplingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<EsdCoupling>);

} // namespace emc::prediction
