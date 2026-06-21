// src/shielding/aperture.cpp
#include <emc/shielding/aperture.hpp>

#include <mp-units/systems/international.h>   // international::inch (the "in" symbol)
#include <mp-units/systems/si.h>

namespace emc::shielding {

namespace {
using namespace mp_units;
using mp_units::international::unit_symbols::in;   // inch

// Evaluate every length in inches so the 27.3 / 32 constants reproduce exactly.
// numerical_value_in(in) performs the exact unit conversion at the boundary, so a
// caller may pass 2.0 * mm or 0.1 * in and no hand-rolled factor can be wrong.
[[nodiscard]] double inches(emc::units::Length L) { return L.numerical_value_in(in); }
} // namespace

std::expected<void, emc::Error> validate(const ApertureInput& in) {
    // depth is the numerator; the chosen transverse dimension is the denominator,
    // so both must be > 0 (require_positive also excludes 0, guarding the division).
    if (auto r = emc::require_positive(inches(in.depth), "depth"); !r) return r;
    if (in.shape == ApertureShape::Slot)
        return emc::require_positive(inches(in.width), "width");      // w in the denominator
    return emc::require_positive(inches(in.diameter), "diameter");    // D in the denominator
}

emc::Result<ApertureResult> calculate(const ApertureInput& in) {
    // Run the formula only on valid input; forward the typed Error otherwise.
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    const double d = inches(in.depth);
    double al{};
    if (in.shape == ApertureShape::Slot)
        al = 27.3 * d / inches(in.width);       // AL = 27.3 * d / w   (rectangular slot)
    else
        al = 32.0 * d / inches(in.diameter);    // AL = 32   * d / D   (round hole)

    // Decibel{..}: direct-init the dB value into the log wrapper; AL is a pure-ratio dB result.
    return ApertureResult{ .absorption_loss = emc::units::Decibel{ al } };
}

} // namespace emc::shielding
