// src/shielding/slot.cpp
#include <emc/shielding/slot.hpp>

#include <cmath>   // std::log10 (closed-form dB math)

#include <emc/core/constants.hpp>   // emc::constants::c (exact speed of light)

#include <mp-units/systems/si.h>

namespace emc::shielding {

namespace {
using namespace mp_units;
using mp_units::si::unit_symbols::Hz;   // hertz symbol for numerical_value_in
using mp_units::si::unit_symbols::m;    // metre symbol for numerical_value_in
} // namespace

std::expected<void, emc::Error> validate(const SlotSeInput& in) {
    // Pull each field's numeric value in its SI unit, then range-check as a plain
    // double (the foundation validators operate on doubles).
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r)
        return r;                                   // f = 0 -> lambda = c/0 (inf)
    if (auto r = emc::require_positive(in.length.numerical_value_in(m), "length"); !r)
        return r;                                   // length = 0 -> log10(lambda/0)
    return {};
}

emc::Result<SlotSeResult> calculate(const SlotSeInput& in) {
    // Guard first: on bad input forward the typed Error so the math is unreachable.
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    // lambda = c / f. c is emc::constants::c (the exact SI 299 792 458 m/s); the
    // division is dimensioned, so the quotient is a real length. Direct-init {..}
    // into the Length alias relabels the derived-kind metre value as a Length
    // (copy-init = would not compile — mp-units makes the relabel deliberate).
    const emc::units::Length lambda{ (emc::constants::c / in.frequency).in(m) };

    // Drop to plain doubles (both in metres) for the closed-form dB expression.
    const double lambda_m = lambda.numerical_value_in(m);
    const double len_m    = in.length.numerical_value_in(m);

    // SE = 20 * log10( lambda / (2 * length) ). Legitimately negative once
    // length > lambda/2; std::log10 from <cmath>. We do NOT clamp it.
    const double se = 20.0 * std::log10(lambda_m / (2.0 * len_m));   // [dB]

    return SlotSeResult{
        .wavelength = lambda,
        .shielding  = emc::units::Decibel{ se },   // dB stays out of the linear unit system
    };
}

} // namespace emc::shielding
