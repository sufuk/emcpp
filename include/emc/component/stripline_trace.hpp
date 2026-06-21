// include/emc/component/stripline_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, ErrorCode, validators
#include <emc/core/units.hpp>        // emc::units::Length / Impedance / CapacitancePerLength / TimePerLength

namespace emc::component {

// ===========================================================================
//  Stripline Trace  (bidirectional solvers over one shared physics core)
//
//  Characteristic impedance of a trace centered between two reference planes —
//  the standard IPC stripline closed form:
//
//     Z0  = 60 * ln( 4*(2H + T) / (0.67*pi*(0.8W + T)) ) / sqrt(eps_r)   [ohm]
//     Tpd = 84.75 * sqrt(eps_r)                                          [ps/inch]
//     C0  = Tpd / Z0                                                     [pF/inch]
//
//  Bidirectional: a forward solve (Z0, +C0, +Tpd) plus three inverse solves
//  (height / thickness / width), each a distinct named free function sharing the
//  detail:: core. Z0 is SCALE-INVARIANT (H, T, W appear only inside dimensionless
//  ratios), so the math runs in mm regardless of the caller's display unit; the
//  caller reads any unit back with .in(mm) / .in(mil).
// ===========================================================================

// Inputs for the FORWARD solve (Z0). Geometry is typed Length, so the call site
// supplies `0.2 * mm` or `8.0 * mil` and the unit decision is made once, in the
// type — never by branching inside the math. eps_r is a plain dimensionless double
// because it is a bare ratio in the formula.
struct StriplineTraceInput {
    emc::units::Length height    {};   // H : plane-to-plane / 2 spacing (dielectric thickness)
    emc::units::Length thickness {};   // T : copper thickness
    emc::units::Length width     {};   // W : trace width
    double             relative_permittivity = 4.7;   // eps_r  [-]
};

struct StriplineTraceResult {
    emc::units::Impedance            z0  {};   // ohm
    emc::units::CapacitancePerLength c0  {};   // F/m (display as pF/inch)
    emc::units::TimePerLength        tpd {};   // s/m (display as ps/inch)
};

// FORWARD: solve characteristic impedance (+ C0, Tpd).
// [[nodiscard]]: ignoring the Result drops the error path — that is a bug, so warn.
[[nodiscard]] emc::Result<StriplineTraceResult>    calculate(const StriplineTraceInput&);

// Shared validation (eps_r in [1,15]; positivity; T/H < 0.25; H > T; W/(H-T) < 0.35)
// reported as typed std::expected<void, Error>.
[[nodiscard]] std::expected<void, emc::Error> validate (const StriplineTraceInput&);

// --- INVERSE solves. Each takes the three known geometry/material fields + a target
//     Z0 and returns the missing dimension as a typed Length. Distinct names per
//     target keep each signature self-documenting and the round-trip tests direct. ---
struct StriplineSolveHeight {                  // known: T, W, eps_r, Z0  ->  H
    emc::units::Impedance z0 {};
    emc::units::Length    thickness {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct StriplineSolveThickness {               // known: H, W, eps_r, Z0  ->  T
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct StriplineSolveWidth {                   // known: H, T, eps_r, Z0  ->  W
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    thickness {};
    double                relative_permittivity = 4.7;
};

[[nodiscard]] emc::Result<emc::units::Length> solve_height   (const StriplineSolveHeight&);
[[nodiscard]] emc::Result<emc::units::Length> solve_thickness(const StriplineSolveThickness&);
[[nodiscard]] emc::Result<emc::units::Length> solve_width    (const StriplineSolveWidth&);

// Tag binding the forward triple to the Calculator concept (foundation §5). The
// static_assert checks the (Input, Result, calculate, validate) contract at compile time.
struct StriplineTrace {
    using Input  = StriplineTraceInput;
    using Result = StriplineTraceResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<StriplineTrace>);

}  // namespace emc::component
