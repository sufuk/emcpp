// include/emc/shielding/aperture.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Decibel

namespace emc::shielding {

// ===========================================================================
//  Aperture absorption (cutoff) loss
//    slot  (rectangular):  AL = 27.3 * depth / width      // d, w in inches
//    round (circular):     AL = 32   * depth / diameter   // d, D in inches
//  An aperture of depth d behaves like a below-cutoff waveguide; the 27.3 / 32
//  constants presuppose INCHES, so the impl evaluates every length in inches.
// ===========================================================================

// Which aperture geometry drives the absorption-loss constant.
// enum class (not a bool flag): the slot-vs-round choice is a typed, exhaustively
// switchable value the caller states explicitly, so no ambiguous branch reaches the math.
enum class ApertureShape {
    Slot,   // rectangular slot  -> AL = 27.3 * depth / width
    Round,  // circular hole     -> AL = 32   * depth / diameter
};

// ---------------------------------------------------------------------------
//  ApertureInput — depth plus ONE transverse dimension chosen by `shape`.
//  Lengths are real mp-units quantities (any length unit is accepted; the impl
//  evaluates in inches to reproduce the 27.3 / 32 constants). The unused field
//  (width for Round, diameter for Slot) may stay at its default.
//  Geometry is mp-units typed: the unit lives in the type, so wrong-unit math
//  will not compile and the inch conversion at the boundary cannot be wrong.
// ---------------------------------------------------------------------------
struct ApertureInput {
    emc::units::Length depth{};                 // aperture depth d
    emc::units::Length width{};                 // slot width  w   (used when shape == Slot)
    emc::units::Length diameter{};              // hole diam.  D   (used when shape == Round)
    ApertureShape      shape = ApertureShape::Slot;
};

// One scalar output: the absorption (cutoff) loss in dB.
// Decibel (not a linear mp-units unit): AL is a logarithmic dB value, so the
// wrapper keeps it out of the linear unit system — it cannot be added to a power in watts.
struct ApertureResult {
    emc::units::Decibel absorption_loss{};      // AL [dB]
};

// Reject non-positive geometry / division-by-zero (w = 0 or D = 0 would yield inf).
// [[nodiscard]]: a dropped validation result is a bug, so the compiler warns.
[[nodiscard]] std::expected<void, emc::Error> validate(const ApertureInput& in);

// Forward calculation: ApertureInput -> AL [dB]. [[nodiscard]]: the Result must not be ignored.
[[nodiscard]] emc::Result<ApertureResult> calculate(const ApertureInput& in);

// Tag type: names the (Input, Result, calculate, validate) triple so generic code
// and the static_assert below can check the calculator contract at compile time.
struct Aperture {
    using Input  = ApertureInput;
    using Result = ApertureResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Aperture>);

} // namespace emc::shielding
