// src/prediction/rf_field.cpp
#include <emc/prediction/rf_field.hpp>

#include <cmath>   // std::sqrt

#include <mp-units/systems/si.h>

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // V, A, W, m

std::expected<void, emc::Error> validate(const RfFieldInput& in) {
    // d must be > 0: it is the denominator of E = sqrt(30*P*G)/d (and == 0 gives inf).
    return emc::require_positive(in.distance.numerical_value_in(m), "distance");
}

emc::Result<RfFieldResult> calculate(const RfFieldInput& in) {
    // transform runs the math only if validate() succeeded, forwarding the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        // dBm -> watt via the canonical typed wrapper. to_power(Dbm) yields
        // P = 10^(dBm/10) mW, which equals 10^((dBm-30)/10) W — exactly P_t_W.
        // Read it back as a plain double in watts for the raw-double math below.
        const double Pt_W = emc::units::to_power(in.transmit_power).numerical_value_in(W);

        // dBi -> linear gain: G_t_linear = 10^(dBi/10). to_ratio() centralizes this
        // power-ratio conversion for the Decibel wrapper.
        const double Gt_lin = emc::units::to_ratio(in.gain);

        const double d_m = in.distance.numerical_value_in(m);

        // Free-space wave impedance, the textbook 120*pi (~376.99 ohm). Full-precision
        // pi from the one shared library constant.
        const double Z = 120.0 * emc::constants::pi;

        const double E = std::sqrt(30.0 * Pt_W * Gt_lin) / d_m;   // V/m
        const double H = E / Z;                                    // A/m
        const double PD = E * H;                                   // W/m^2

        // Each product below has a DERIVED quantity kind, so store it into the named
        // emc::units alias with DIRECT init {..} — the explicit, deliberate relabel
        // that mp-units requires (copy-init = would not compile here on purpose).
        return RfFieldResult{
            .electric_field = emc::units::ElectricField{ (E  * (V / m)).in(V / m) },
            .magnetic_field = emc::units::MagneticField{ (H  * (A / m)).in(A / m) },
            .power_density  = emc::units::PowerDensity { (PD * (W / (m * m))).in(W / (m * m)) },
        };
    });
}

} // namespace emc::prediction
