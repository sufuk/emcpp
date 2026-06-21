// src/converter/efield_power_density.cpp
#include <emc/converter/efield_power_density.hpp>

namespace emc::converter {

using namespace mp_units;
// Unit symbols pulled in by name so the math below reads like the formula.
using mp_units::si::unit_symbols::ohm;
using mp_units::si::unit_symbols::V;
using mp_units::si::unit_symbols::m;
using mp_units::si::unit_symbols::W;

emc::units::Impedance EFieldPowerDensityInput::default_eta() {
    return 377.0 * ohm;     // free-space wave impedance default
}

std::expected<void, emc::Error> validate(const EFieldPowerDensityInput& in) {
    // eta == 0 would divide by zero in P_D = E^2 / eta.
    return emc::require_nonzero(in.wave_impedance.numerical_value_in(ohm), "wave_impedance");
}

emc::Result<EFieldPowerDensityResult> calculate(const EFieldPowerDensityInput& in) {
    // and_then runs the math only if validate() succeeded, forwarding the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).and_then([&]() -> emc::Result<EFieldPowerDensityResult> {
        // P_D = E^2 / eta. mp-units DERIVES (V/m)^2 / ohm = W/m^2 for us, so the
        // product carries a derived quantity kind. Direct-init {..} into the named
        // PowerDensity alias runs the explicit "this W/m^2 value IS a power density"
        // relabel (copy-init = would not — mp-units makes the relabel deliberate).
        const auto e = in.electric_field;
        const emc::units::PowerDensity pd{ (e * e / in.wave_impedance).in(W / (m * m)) };
        return EFieldPowerDensityResult{ .power_density = pd };
    });
}

} // namespace emc::converter
