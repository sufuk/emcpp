// src/basic/far_field_criteria.cpp
#include <emc/basic/far_field_criteria.hpp>

#include <cmath>   // std::sqrt, std::pow (both constexpr in C++23)

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, m, s

std::expected<void, emc::Error> validate(const FarFieldCriteriaInput& in) {
    // Pull the numeric value out in each field's SI unit, then range-check it as a
    // plain double (the foundation validators report the allowed bound in that unit).
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r)
        return r;   // lambda = c/f needs f > 0 (also guards the division)
    if (auto r = emc::require_positive(in.max_dimension.numerical_value_in(m), "max_dimension"); !r)
        return r;   // D^3 and the branch comparison need D > 0
    return {};
}

emc::Result<FarFieldCriteriaResult> calculate(const FarFieldCriteriaInput& in) {
    // transform runs the math only if validate() succeeded, and forwards the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        // Work in plain doubles (metres / hertz) for the closed-form branch, then
        // relabel each result back into a Length at the end.
        const double f  = in.frequency.numerical_value_in(Hz);
        const double D  = in.max_dimension.numerical_value_in(m);
        const double cc = emc::constants::c.numerical_value_in(m / s);  // exact CODATA c

        const double lambda = cc / f;                 // lambda = c/f

        // Strict comparison: at exact equality the electrically-small branch runs.
        const bool large = (D > lambda / 10.0);

        double reactive  = 0.0;
        double radiating = 0.0;
        if (large) {                                  // electrically large antenna
            reactive  = 0.62 * std::sqrt(std::pow(D, 3.0) / lambda);
            radiating = (2.0 * std::pow(D, 2.0)) / lambda;
        } else {                                      // electrically small antenna
            reactive  = lambda / 50.0;
            radiating = lambda;
        }

        // value * unit builds each Length from a metre value; designated initializers
        // name the four result fields so the regime flag travels with the numbers.
        return FarFieldCriteriaResult{
            .wavelength           = lambda    * m,
            .reactive_near_field  = reactive  * m,
            .radiating_near_field = radiating * m,
            .electrically_large   = large,
        };
    });
}

} // namespace emc::basic
