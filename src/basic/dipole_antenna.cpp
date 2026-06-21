// src/basic/dipole_antenna.cpp
#include <emc/basic/dipole_antenna.hpp>

#include <cmath>   // std::sin, std::cos, std::sqrt, std::pow

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // A, m, Hz, V, s, ...

std::expected<void, emc::Error> validate(const DipoleAntennaInput& in) {
    // Pull each field's numeric value in its SI unit, then range-check as a plain
    // double. All four must be > 0: R appears as R^2 and 1/R, and f appears in
    // 1/(fR) terms, so a zero would blow up to inf/nan.
    if (auto r = emc::require_positive(in.current.numerical_value_in(A),    "current");   !r) return r;
    if (auto r = emc::require_positive(in.length.numerical_value_in(m),     "length");    !r) return r;
    if (auto r = emc::require_positive(in.distance.numerical_value_in(m),   "distance");  !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    return {};
}

emc::Result<DipoleAntennaResult> calculate(const DipoleAntennaInput& in) {
    // transform runs the math only when validate() succeeds and forwards the Error
    // otherwise, so the formula is unreachable on bad input with no if/return noise.
    return validate(in).transform([&] {
        // Pull everything into base SI scalars; the 60/30 and f/(2c) coefficients
        // assume SI units (A, m, Hz). theta is converted to radians for the trig.
        const double I0   = in.current.numerical_value_in(A);
        const double l    = in.length.numerical_value_in(m);
        const double R    = in.distance.numerical_value_in(m);
        const double f    = in.frequency.numerical_value_in(Hz);
        const double th   = in.theta.numerical_value_in(si::radian);     // degrees were folded into the Angle already
        const double cc   = emc::constants::c.numerical_value_in(m / s); // speed of light
        const double pi   = emc::constants::pi;

        const double cosT = std::cos(th);
        const double sinT = std::sin(th);

        // common radical sqrt(1 + (c/(2*pi*f*R))^2) — shared by E_r and H_phi,
        // computed once to document that they share it.
        const double rad_common = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));

        const double Er = 60.0 * ((I0 * l) / (R * R)) * cosT * rad_common;

        const double Etheta = 30.0 * ((I0 * l) / R) * sinT *
            std::sqrt(std::pow(1.0 / R, 2.0) +
                      std::pow((2.0 * pi * f) / cc - cc / (2.0 * pi * f * R * R), 2.0));

        const double Hphi = (f / (2.0 * cc)) * ((I0 * l) / R) * sinT * rad_common;

        // Re-attach units at the boundary: direct-init the plain SI scalars back
        // into the named field types (V/m for E, A/m for H).
        return DipoleAntennaResult{
            .e_r     = Er     * (V / m),
            .e_theta = Etheta * (V / m),
            .h_phi   = Hphi   * (A / m),
        };
    });
}

} // namespace emc::basic
