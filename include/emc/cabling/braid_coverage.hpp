// include/emc/cabling/braid_coverage.hpp
#pragma once

#include <expected>                  // std::expected<void, Error> validate() return

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept + static_assert tag
#include <emc/core/error.hpp>        // emc::Result, emc::Error
#include <emc/core/units.hpp>        // emc::units::Length, Dimensionless, Angle

namespace emc::cabling {

// ---------------------------------------------------------------------------
//  Input — braid geometry. Lengths are mp-units quantities (so the call site is
//  unit-explicit: 0.114 * mm, not a bare 0.000114 that *means* metres). Pick
//  density P, carrier count C and ends-per-carrier N are dimensionless counts,
//  so they stay plain doubles. Defaults give a realistic worked example
//  (a typical RG-style braid) so designated-initializer call sites read cleanly.
// ---------------------------------------------------------------------------
struct BraidCoverageInput {
    emc::units::Length d = 0.000114 * emc::units::si::metre;   // strand (wire) diameter
    emc::units::Length D = 0.002950 * emc::units::si::metre;   // braid outer diameter
    double             P = 220.0;   // picks per unit length [1/m]
    double             C = 16.0;    // number of carriers [-]
    double             N = 5.8;     // strands (ends) per carrier [-]
};

// ---------------------------------------------------------------------------
//  Result — the coverage plus the intermediate weave angle and fill factor,
//  which are useful diagnostics. optical_coverage is a generic Dimensionless
//  ratio carrying the fraction (0..1); percent() is a convenience for printing.
// ---------------------------------------------------------------------------
struct BraidCoverageResult {
    emc::units::Dimensionless optical_coverage;  // fraction in [0, 1] (NOT yet *100)
    emc::units::Angle         weave_angle;       // theta [rad]
    double                    fill_factor;       // F [-]

    // percent() applies the *100 only at the display boundary, so the stored
    // ratio stays unambiguous (0.79, not "is this 0.79 or 79?").
    [[nodiscard]] double percent() const {
        // mp-units spells the dimension-one unit as mp_units::one (top level, not
        // under si); extract the plain ratio, then scale to a percentage.
        return 100.0 * optical_coverage.numerical_value_in(mp_units::one);
    }
};

/// Validate braid geometry: all dimensions/counts must be strictly positive and
/// the carrier count C must be non-zero (it is a denominator inside atan()).
/// [[nodiscard]]: a dropped validation result hides a bad-input bug.
[[nodiscard]] std::expected<void, emc::Error> validate(const BraidCoverageInput& in);

/// Compute optical coverage from braid geometry (forward only).
/// theta = atan(2*pi*(D+2d)*P/C); F = P*N*d/sin(theta); OC = 2F - F^2.
[[nodiscard]] emc::Result<BraidCoverageResult> calculate(const BraidCoverageInput& in);

/// Descriptive alias requested by the calculator inventory (doc 07); same body.
[[nodiscard]] emc::Result<BraidCoverageResult> braid_optical_coverage(const BraidCoverageInput& in);

// ---- Calculator concept binding (compile-time contract, foundation §5) -------
// The tag names the (Input, Result, calculate, validate) quadruple so generic
// code and the static_assert below can verify the contract at compile time.
struct BraidOpticalCoverage {
    using Input  = BraidCoverageInput;
    using Result = BraidCoverageResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::cabling::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::cabling::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<BraidOpticalCoverage>);

} // namespace emc::cabling
