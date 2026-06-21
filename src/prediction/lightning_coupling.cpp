// src/prediction/lightning_coupling.cpp
#include <emc/prediction/lightning_coupling.hpp>

#include <cmath>   // std::log

#include <emc/core/constants.hpp>   // emc::constants::mu0, ::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

std::expected<void, emc::Error> validate(const LightningCouplingInput& in) {
    // r must be > 0: it is the denominator inside ln((r+d)/r) AND the ln base.
    // Pull the numeric value out in metres and range-check it as a plain double.
    if (auto ok = emc::require_positive(in.radius.numerical_value_in(m), "radius"); !ok)
        return ok;
    // r + d must stay positive: a negative d that cancels r would put a
    // non-positive value into ln() (a math-domain error), so guard it explicitly.
    const double r = in.radius.numerical_value_in(m);
    const double d = in.distance.numerical_value_in(m);
    if (!((r + d) > 0.0))
        return std::unexpected(emc::domain_error("ln argument (r + d) must be > 0", "distance"));
    return {};
}

emc::Result<LightningCouplingResult> calculate(const LightningCouplingInput& in) {
    // and_then runs the math only if validate() succeeded, and forwards the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).and_then([&]() -> emc::Result<LightningCouplingResult> {
        // ln() is a pure ratio, so evaluate it on the dimensionless (r+d)/r.
        const double r = in.radius.numerical_value_in(m);
        const double d = in.distance.numerical_value_in(m);
        const double ln_term = std::log((r + d) / r);

        // V_ind = (mu0 * h / (2*pi)) * ln((r+d)/r) * dI/dt
        // mu0 is [H/m], h is a length, dI/dt is [A/s]:
        // (H/m * m) * (A/s) = H*A/s = Wb/s = V. mp-units yields a voltage by
        // dimensional analysis; a wrong factor would not compile.
        const auto v_ind =
            (emc::constants::mu0 * in.loop_height / (2.0 * emc::constants::pi)) * ln_term * in.di_dt;

        // The product has a derived quantity kind, so store it into the named
        // Voltage alias with DIRECT init {..} — that runs the explicit "this volt
        // value IS a voltage" relabel (copy-init = would not, on purpose).
        return LightningCouplingResult{ .induced_voltage = emc::units::Voltage{ v_ind.in(si::volt) } };
    });
}

} // namespace emc::prediction
