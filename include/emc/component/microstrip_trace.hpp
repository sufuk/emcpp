// include/emc/component/microstrip_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Impedance, ::CapacitancePerLength, ::TimePerLength

namespace emc::component {

// ===========================================================================
//  Microstrip Trace — characteristic impedance of a surface trace over one
//  reference plane (the standard IPC/Wheeler microstrip closed form).
//  BIDIRECTIONAL: a forward calculate() for Z0 (plus C0, Tpd) and three
//  distinct inverse solve_* free functions, all sharing one detail:: core.
//
//      ln    = ln( 5.98*H / (0.8*W + T) )
//      Z0    = 87*ln / sqrt(eps_r + 1.41)              [ohm]
//      ctemp = 0.67*(eps_r + 1.41) / ln
//      C0    = ctemp / 2.54                            [pF/cm]
//      Tpd   = ctemp*Z0 / 2.54                         [ps/cm]
//
//  Z0 is scale-invariant (H, T, W appear only inside the dimensionless ratio
//  5.98*H / (0.8*W + T)), so the working unit is mm regardless of the caller's
//  display unit.
// ===========================================================================

// Inputs for the FORWARD solve (Z0). Geometry is mp-units typed Length, so the
// call site supplies `1.5 * mm` or `62.0 * mil` and the unit choice is decided
// once, in the type — wrong-unit math will not compile. eps_r is a plain
// dimensionless double because it is a bare ratio in the formula.
struct MicrostripInput {
    emc::units::Length height    {};               // H : dielectric thickness, trace -> plane (> 0)
    emc::units::Length thickness {};               // T : copper thickness (> 0)
    emc::units::Length width     {};               // W : trace width (> 0)
    double             relative_permittivity = 4.7;  // eps_r [-]  (1 <= eps_r <= 15)
};

struct MicrostripResult {
    emc::units::Impedance            z0  {};   // ohm
    emc::units::CapacitancePerLength c0  {};   // F/m (display as pF/cm)
    emc::units::TimePerLength        tpd {};   // s/m (display as ps/cm)
};

// FORWARD: solve characteristic impedance (+ C0, Tpd).
// [[nodiscard]]: dropping the Result drops the error path — that is a bug, so the compiler warns.
[[nodiscard]] emc::Result<MicrostripResult> calculate(const MicrostripInput& in);

// Shared validation (eps_r in [1,15]; positivity; 0.1 <= W/H <= 3) reported as
// typed errors. std::expected<void, Error>: empty {} means "ok".
[[nodiscard]] std::expected<void, emc::Error> validate(const MicrostripInput& in);

// --- INVERSE solves. Each takes the three known geometry/material fields plus a
//     target Z0 and returns the missing dimension as a typed Length. Distinct
//     names per target keep each signature self-documenting; the closed forms
//     (with k = Z0*sqrt(1.41 + eps_r)/87) are:
//        H = exp(k)*(0.8*W + T) / 5.98
//        T = 5.98*H / exp(k) - 0.8*W
//        W = (5.98*H / exp(k) - T) / 0.8
// ---------------------------------------------------------------------------
struct MicrostripSolveHeight {                 // known: T, W, eps_r, Z0  ->  H
    emc::units::Impedance z0 {};
    emc::units::Length    thickness {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct MicrostripSolveThickness {              // known: H, W, eps_r, Z0  ->  T
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct MicrostripSolveWidth {                  // known: H, T, eps_r, Z0  ->  W
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    thickness {};
    double                relative_permittivity = 4.7;
};

[[nodiscard]] emc::Result<emc::units::Length> solve_height   (const MicrostripSolveHeight&);
[[nodiscard]] emc::Result<emc::units::Length> solve_thickness(const MicrostripSolveThickness&);
[[nodiscard]] emc::Result<emc::units::Length> solve_width    (const MicrostripSolveWidth&);

// Tag type binding the forward (Input, Result, calculate, validate) triple to the
// Calculator concept (foundation calculator.hpp). Zero data; just names the contract.
struct MicrostripTrace {
    using Input  = MicrostripInput;
    using Result = MicrostripResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<MicrostripTrace>);

}  // namespace emc::component
