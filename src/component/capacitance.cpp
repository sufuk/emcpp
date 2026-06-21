// src/component/capacitance.cpp
#include <emc/component/capacitance.hpp>

#include <mp-units/systems/si.h>

namespace emc::component {

using namespace mp_units;
using mp_units::si::metre;

// Unit "metre squared" — area's storage unit. (mp_units::square gives the m^2 unit.)
inline constexpr auto m2 = square(metre);

// ---- Parallel plate --------------------------------------------------------

std::expected<void, emc::Error> validate(const ParallelPlateInput& in) {
    // Pull the numeric value out in each field's SI unit, then range-check as a plain
    // double (the foundation validators report the allowed [lo, hi] in that unit).
    const double a = in.area.numerical_value_in(m2);
    const double d = in.distance.numerical_value_in(metre);

    if (auto r = emc::require_positive(a, "area"); !r) return r;
    // distance must be > 0: also guards the division by d (d == 0 would give inf).
    if (auto r = emc::require_positive(d, "distance"); !r) return r;
    if (auto r = emc::require_nonzero(d, "distance"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1.0e6, "relative_permittivity"); !r)
        return r;
    return {};
}

emc::Result<ParallelPlateResult> calculate(const ParallelPlateInput& in) {
    // transform runs the math only if validate() succeeded, and forwards the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        // C = eps0 * eps_r * A / d. eps0 is [F/m]; (A/d) is a length; the product is
        // farads. The product's quantity "kind" is a derived expression, so we store
        // it into the named Capacitance type with DIRECT init {..} — that runs the
        // explicit "this farad value IS a capacitance" conversion (copy-init = would
        // not, on purpose: mp-units makes the relabel deliberate).
        const emc::units::Capacitance c{
            (emc::constants::eps0 * in.relative_permittivity * (in.area / in.distance))
                .in(si::farad)};
        return ParallelPlateResult{ .capacitance = c };
    });
}

// ---- Sphere ----------------------------------------------------------------

std::expected<void, emc::Error> validate(const SphereInput& in) {
    const double r = in.radius.numerical_value_in(metre);
    if (auto v = emc::require_positive(r, "radius"); !v) return v;   // r > 0 (also excludes 0)
    return {};
}

emc::Result<SphereResult> calculate(const SphereInput& in) {
    return validate(in).transform([&] {
        // C = 4 * pi * eps0 * r. eps0 is [F/m], r is a length -> farads. Direct init {..}
        // relabels the derived-kind farad value as a Capacitance (explicit on purpose).
        const emc::units::Capacitance c{
            (4.0 * emc::constants::pi * emc::constants::eps0 * in.radius).in(si::farad)};
        return SphereResult{ .capacitance = c };
    });
}

} // namespace emc::component
