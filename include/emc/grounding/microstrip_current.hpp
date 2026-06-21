// include/emc/grounding/microstrip_current.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Current, Length, ...

namespace emc::grounding {

// ---------------------------------------------------------------------------
//  Input — the four geometric / electrical quantities of the microstrip return-
//  current problem. All mp-units quantities (no bare doubles, no factor-based
//  unit conversion), with sensible defaults so a designated-initializer call
//  site reads cleanly and a default-constructed Input is valid.
//      i0 = 1 mA,  w = 2.8 mm,  h = 1.6 mm,  x = 2 mm
// ---------------------------------------------------------------------------
struct MicrostripCurrentInput {
    // Defaults use the SI base units scaled to the documented values; mp-units
    // carries the unit in the type, so any wrong-unit math is a compile error.
    emc::units::Current source_current{0.001 * mp_units::si::ampere};  // I0 : trace current
    emc::units::Length  trace_width{0.0028 * mp_units::si::metre};     // w  : conductor width (> 0)
    emc::units::Length  height{0.0016 * mp_units::si::metre};          // h  : height above ground (!= 0)
    emc::units::Length  position{0.002 * mp_units::si::metre};         // x  : lateral offset (any real)
};

// ---------------------------------------------------------------------------
//  Result — the single output: ground-plane surface current density at x.
// ---------------------------------------------------------------------------
struct MicrostripCurrentResult {
    // J : linear surface current density [A/m] = current per unit transverse width.
    // We reuse MagneticField (A/m): a ground-plane surface current density and the
    // tangential H-field it implies share the same dimension and unit, so this alias
    // is dimensionally exact.
    emc::units::MagneticField current_density{};
};

// ---------------------------------------------------------------------------
//  validate() — physical/numerical preconditions the closed form requires:
//    * w > 0  (denominator pi*w)            -> require_positive
//    * h != 0 (denominator 1+(x/h)^2)       -> require_nonzero
//    * I0, x  are free (any real; x may be negative -> symmetric profile)
// [[nodiscard]]: a dropped validation result is a bug.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<void, emc::Error>
validate(const MicrostripCurrentInput& in);

// ---------------------------------------------------------------------------
//  Forward-only calculator: J(x) = (I0 / (pi*w)) * 1/(1 + (x/h)^2).
// ---------------------------------------------------------------------------
[[nodiscard]] emc::Result<MicrostripCurrentResult>
microstrip_current_distribution(const MicrostripCurrentInput& in);

// ---------------------------------------------------------------------------
//  Concept binding: the (Input, Result, calculate, validate) triple as a tag
//  type, checked at compile time so a signature drift is a build error here.
// ---------------------------------------------------------------------------
struct MicrostripCurrentDistribution {
    using Input  = MicrostripCurrentInput;
    using Result = MicrostripCurrentResult;
    static emc::Result<Result> calculate(const Input& in) {
        return microstrip_current_distribution(in);
    }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::grounding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<MicrostripCurrentDistribution>);

} // namespace emc::grounding
