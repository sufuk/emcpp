// include/emc/component/embedded_microstrip_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Impedance, ::CapacitancePerLength, ::TimePerLength

namespace emc::component {

// ===========================================================================
//  Embedded Microstrip (buried trace under a thin dielectric cover)
//
//      ln  = ln( 5.98*H / (0.8*W + T) )
//      Z0  = 87 * ln * (1 - (h1 - H - T)/0.1) / sqrt(eps_r + 1.41)             [ohm]
//      Tpd = 84.75 * sqrt( 0.475*eps_r*(1 + exp(-1.55*h1/H)) + 0.67 )          [ps/cm]
//      C0  = Tpd / Z0                                                          [pF/cm]
//
//  FORWARD-ONLY. Unlike the other three board-impedance calculators, the
//  (h1 - H - T)/0.1 term carries an ABSOLUTE length scale, so Z0 is NOT
//  scale-invariant and the embedded geometry has no validated closed-form
//  inverse — we therefore expose `calculate` (Z0, C0, Tpd) but no solve_*.
// ===========================================================================

// Inputs for the FORWARD solve (Z0). Geometry is typed Length, so the call site
// supplies `0.042 * mm` or `1.65 * mil` and the unit choice is decided once, in the
// type. eps_r is a plain dimensionless double per the canonical API.
struct EmbeddedMicrostripInput {
    emc::units::Length cover_height {};   // h1 : top of the cover dielectric
    emc::units::Length height       {};   // H  : trace -> plane dielectric height
    emc::units::Length thickness    {};   // T  : copper thickness
    emc::units::Length width        {};   // W  : trace width
    double             relative_permittivity = 4.7;   // eps_r  [-]
};

struct EmbeddedMicrostripResult {
    emc::units::Impedance            z0  {};   // ohm
    emc::units::CapacitancePerLength c0  {};   // F/m (display as pF/cm)
    emc::units::TimePerLength        tpd {};   // s/m (display as ps/cm)
};

// FORWARD only — the embedded geometry has no validated closed-form inverse, so we
// expose no solve_* here. (Deriving one would be unverified physics.)
// [[nodiscard]]: ignoring the Result drops the error path, which is a bug.
[[nodiscard]] emc::Result<EmbeddedMicrostripResult> calculate(const EmbeddedMicrostripInput& in);

// Shared validation (eps_r in [1,15]; positivity; 0.1 <= W/H <= 3; cover above
// trace h1 > H + T) reported as typed errors.
[[nodiscard]] std::expected<void, emc::Error> validate(const EmbeddedMicrostripInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic code
// and the static_assert below can check the Calculator contract at compile time.
struct EmbeddedMicrostripTrace {
    using Input  = EmbeddedMicrostripInput;
    using Result = EmbeddedMicrostripResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<EmbeddedMicrostripTrace>);

}  // namespace emc::component
