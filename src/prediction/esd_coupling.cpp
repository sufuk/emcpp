// src/prediction/esd_coupling.cpp
#include <emc/prediction/esd_coupling.hpp>

#include <cmath>   // std::log for the ln() term

#include <emc/core/constants.hpp>   // emc::constants::mu0, ::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, A, s, volt symbols

std::expected<void, emc::Error> validate(const EsdCouplingInput& in) {
    // r must be > 0: it is the denominator inside ln((r+d)/r) AND ln's base.
    // Pull the plain number in metres so the validator can range-check it.
    if (auto ok = emc::require_positive(in.radius.numerical_value_in(m), "radius"); !ok)
        return ok;

    // r+d > 0: a negative d that cancels r would put a non-positive value in ln().
    const double r = in.radius.numerical_value_in(m);
    const double d = in.distance.numerical_value_in(m);
    if (!((r + d) > 0.0))
        return std::unexpected(emc::domain_error("ln argument (r + d) must be > 0", "distance"));

    // t_r != 0: the formula divides by t_r. require_nonzero reports DivisionByZero
    // instead of letting the math emit a silent inf.
    if (auto ok = emc::require_nonzero(in.rise_time.numerical_value_in(s), "rise_time"); !ok)
        return ok;

    return {};   // empty == success
}

emc::Result<EsdCouplingResult> calculate(const EsdCouplingInput& in) {
    // and_then runs the math only if validate() succeeded; the error short-circuits
    // with no nested if/return. The lambda itself returns a Result, hence and_then
    // (not transform).
    return validate(in).and_then([&]() -> emc::Result<EsdCouplingResult> {
        // dI/dt from the ESD ramp: I_peak / t_r. mp-units forms a real A/s quantity,
        // so the final product is *checked* by dimensional analysis to be a voltage.
        const auto di_dt = in.peak_current / in.rise_time;   // quantity of A/s

        // ln() is a pure ratio, so evaluate it on the dimensionless (r+d)/r as plain doubles.
        const double r = in.radius.numerical_value_in(m);
        const double d = in.distance.numerical_value_in(m);
        const double ln_term = std::log((r + d) / r);

        // V_ind = (mu0 * h / (2*pi)) * ln((r+d)/r) * (I_peak / t_r).
        // (H/m * m) * (A/s) = H*A/s = Wb/s = V -> mp-units yields a voltage quantity.
        const auto v_ind =
            (emc::constants::mu0 * in.loop_height / (2.0 * emc::constants::pi)) * ln_term * di_dt;

        // The product has a derived quantity kind, so direct-init {..} into the named
        // Voltage alias to run the explicit "this volt value IS a voltage" relabel.
        return EsdCouplingResult{ .induced_voltage = emc::units::Voltage{ v_ind.in(si::volt) } };
    });
}

} // namespace emc::prediction
