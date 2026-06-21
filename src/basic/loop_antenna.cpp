// src/basic/loop_antenna.cpp
#include <emc/basic/loop_antenna.hpp>

#include <cmath>   // std::sin, std::cos, std::sqrt, std::pow

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // A, m, Hz, V, s, ...

std::expected<void, emc::Error> validate(const LoopAntennaInput& in) {
    // Pull each field's numeric value out in its SI unit, then range-check as a plain
    // double. All four must be > 0: A and R appear under divisions (R^2, 1/R) and f in
    // 1/(f R) terms, so a zero or negative would give inf/nan rather than a field value.
    if (auto r = emc::require_positive(in.current.numerical_value_in(A),           "current");   !r) return r;
    if (auto r = emc::require_positive(in.loop_area.numerical_value_in(square(m)), "loop_area"); !r) return r;
    if (auto r = emc::require_positive(in.distance.numerical_value_in(m),          "distance");  !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz),        "frequency"); !r) return r;
    return {};
}

emc::Result<LoopAntennaResult> calculate(const LoopAntennaInput& in) {
    // transform runs the math only when validate() succeeds and forwards the Error
    // otherwise — so the formula below is unreachable on bad input, no if/return noise.
    return validate(in).transform([&] {
        // Pull everything into base SI scalars; the closed-form coefficients (f/c, 120,
        // (pi f / c)^2) are stated for SI base units, so we evaluate in plain doubles.
        const double I0 = in.current.numerical_value_in(A);
        const double A_ = in.loop_area.numerical_value_in(square(m));
        const double R  = in.distance.numerical_value_in(m);
        const double f  = in.frequency.numerical_value_in(Hz);
        const double th = in.theta.numerical_value_in(si::radian);
        const double cc = emc::constants::c.numerical_value_in(m / s);   // speed of light
        const double pi = emc::constants::pi;

        const double cosT = std::cos(th);
        const double sinT = std::sin(th);

        // Shared radical sqrt(1 + (c/(2 pi f R))^2) — appears in both H_r and E_phi; computed once.
        const double rad_common = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));

        const double Hr = (f / cc) * ((I0 * A_) / (R * R)) * cosT * rad_common;

        const double Htheta = (f / (2.0 * cc)) * ((I0 * A_) / R) * sinT *
            std::sqrt(std::pow(1.0 / R, 2.0) +
                      std::pow((2.0 * pi * f) / cc - cc / (2.0 * pi * f * R * R), 2.0));

        const double Ephi = 120.0 * std::pow((pi * f) / cc, 2.0) * ((I0 * A_) / R) * sinT * rad_common;

        // Re-attach SI units at the boundary so the public surface stays fully typed.
        // (Hr * (A/m)) has a derived "kind" that does NOT implicitly convert to the named
        // MagneticField/ElectricField aliases, so we DIRECT-init {..} each typed result —
        // that runs the explicit relabel (copy-init via = would not compile here).
        const emc::units::MagneticField h_r{ (Hr * (A / m)).in(A / m) };
        const emc::units::MagneticField h_theta{ (Htheta * (A / m)).in(A / m) };
        const emc::units::ElectricField e_phi{ (Ephi * (V / m)).in(V / m) };

        // Designated initializers name each output, so the H/E fields cannot be swapped.
        return LoopAntennaResult{
            .h_r     = h_r,
            .h_theta = h_theta,
            .e_phi   = e_phi,
        };
    });
}

} // namespace emc::basic
