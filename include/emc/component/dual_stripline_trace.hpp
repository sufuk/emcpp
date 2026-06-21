// include/emc/component/dual_stripline_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Impedance, ::CapacitancePerLength, ::TimePerLength

namespace emc::component {

// ===========================================================================
//  Dual Stripline Trace  (bidirectional)
//
//  Two offset traces between two reference planes. Z0 is the AVERAGE of two
//  single-stripline-like terms — one using the trace-to-plane spacing H, the
//  other using H + C (C is the inter-trace gap):
//
//    Z0  = ½·[ 60·ln( 8·H       / (0.67·pi·(0.8·W + T)) ) / sqrt(eps_r)
//            + 60·ln( 8·(H + C) / (0.67·pi·(0.8·W + T)) ) / sqrt(eps_r) ]   [ohm]
//    Tpd = 84.75·sqrt(eps_r)                                               [ps/inch]
//    C0  = Tpd / Z0                                                        [pF/inch]
//
//  Geometry arrives as typed emc::units::Length, so mm-vs-mils is a single
//  call-site type choice, never a branch in the math. eps_r is a plain double
//  (a bare ratio in the formula). Z0 is scale-invariant (H, C, T, W all appear
//  only inside the dimensionless log ratios), so the implementation works in mm
//  for any caller display unit.
// ===========================================================================

// Inputs for the FORWARD solve (Z0, C0, Tpd).
struct DualStriplineInput {
    emc::units::Length height    {};                 // H : trace -> nearer plane spacing
    emc::units::Length gap       {};                 // C : spacing between the two traces
    emc::units::Length thickness {};                 // T : copper thickness
    emc::units::Length width     {};                 // W : trace width
    double             relative_permittivity = 4.7;  // eps_r  [-]
};

struct DualStriplineResult {
    emc::units::Impedance            z0  {};   // ohm
    emc::units::CapacitancePerLength c0  {};   // F/m (display as pF/inch)
    emc::units::TimePerLength        tpd {};   // s/m (display as ps/inch)
};

// FORWARD: characteristic impedance (+ C0, Tpd). [[nodiscard]] — dropping the
// Result drops the error path, which is a bug.
[[nodiscard]] emc::Result<DualStriplineResult> calculate(const DualStriplineInput& in);

// Shared validation (eps_r in [1,15]; positivity of H/C/T/W; H>T; W/(H-T)<0.35;
// T/H<0.25) reported as typed errors via std::expected.
[[nodiscard]] std::expected<void, emc::Error> validate(const DualStriplineInput& in);

// --- INVERSE solves. Five named targets (the extra `solve_gap` is just another
//     named solver sharing the detail:: core — no runtime optional dispatch).
//     Each carries the four KNOWN fields + the target Z0 and returns the missing
//     dimension as a typed Length, so the return type is self-documenting. ------

struct DualSolveHeight {                 // known: C, T, W, eps_r, Z0  ->  H
    emc::units::Impedance z0 {};
    emc::units::Length    gap {};
    emc::units::Length    thickness {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct DualSolveGap {                     // known: H, T, W, eps_r, Z0  ->  C
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    thickness {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct DualSolveThickness {               // known: H, C, W, eps_r, Z0  ->  T
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    gap {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct DualSolveWidth {                   // known: H, C, T, eps_r, Z0  ->  W
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    gap {};
    emc::units::Length    thickness {};
    double                relative_permittivity = 4.7;
};

[[nodiscard]] emc::Result<emc::units::Length> solve_height   (const DualSolveHeight&);
[[nodiscard]] emc::Result<emc::units::Length> solve_gap      (const DualSolveGap&);
[[nodiscard]] emc::Result<emc::units::Length> solve_thickness(const DualSolveThickness&);
[[nodiscard]] emc::Result<emc::units::Length> solve_width    (const DualSolveWidth&);

// Tag binding the forward (Input, Result, calculate, validate) triple to the
// Calculator concept (foundation calculator.hpp). The static_assert checks the
// contract at compile time.
struct DualStriplineTrace {
    using Input  = DualStriplineInput;
    using Result = DualStriplineResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<DualStriplineTrace>);

}  // namespace emc::component
