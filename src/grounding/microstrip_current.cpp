// src/grounding/microstrip_current.cpp
#include <emc/grounding/microstrip_current.hpp>

#include <emc/core/constants.hpp>   // emc::constants::pi

#include <mp-units/systems/si.h>

namespace emc::grounding {

using namespace mp_units;
using mp_units::si::unit_symbols::A;     // ampere
using mp_units::si::unit_symbols::m;     // metre

// ---------------------------------------------------------------------------
//  validate() — preconditions the closed-form needs.
// ---------------------------------------------------------------------------
std::expected<void, emc::Error>
validate(const MicrostripCurrentInput& in) {
    // Evaluate ranges in the field's natural display unit so a reported [lo,hi]
    // is meaningful to the user (mm for lengths).
    const double w_mm = in.trace_width.numerical_value_in(si::milli<m>);
    const double h_mm = in.height.numerical_value_in(si::milli<m>);

    // pi*w must be a nonzero denominator AND a width is physically positive.
    if (auto r = emc::require_positive(w_mm, "trace_width"); !r)
        return r;
    // 1 + (x/h)^2 has h in the denominator: h must be nonzero (sign irrelevant).
    if (auto r = emc::require_nonzero(h_mm, "height"); !r)
        return r;

    return {};   // I0 and x are unconstrained (x may be negative: symmetric profile)
}

// ---------------------------------------------------------------------------
//  microstrip_current_distribution() — the Lorentzian, with all unit handling
//  done by mp-units rather than manual factor multiplications.
//      j = (i0 / (pi*w)) * 1/(1 + (x/h)^2)
// ---------------------------------------------------------------------------
emc::Result<MicrostripCurrentResult>
microstrip_current_distribution(const MicrostripCurrentInput& in) {
    // transform runs the math only on validate()'s success arm and forwards the
    // Error otherwise — so the div-by-zero guard and the math compose with no
    // if/return boilerplate.
    return validate(in).transform([&] {
        // x/h is a pure dimensionless ratio; mp-units yields quantity<one>, and
        // .numerical_value_in(one) extracts the bare number for the (x/h)^2 term.
        const double ratio = (in.position / in.height).numerical_value_in(one);
        const double shape = 1.0 / (1.0 + ratio * ratio);   // Lorentzian shape factor [-]

        // Peak density I0/(pi*w): Current / Length = Current per Length = A/m.
        // mp-units checks the dimension; the result is an A/m-kind quantity.
        const auto j_peak = in.source_current / (emc::constants::pi * in.trace_width);

        // J = peak * shape, relabelled into the named MagneticField (A/m) alias.
        // Direct-init {..} runs the explicit "this A/m value IS a MagneticField"
        // conversion that the derived-kind product would not get from copy-init.
        const emc::units::MagneticField j{ (j_peak * shape).in(A / m) };

        return MicrostripCurrentResult{ .current_density = j };
    });
}

} // namespace emc::grounding
