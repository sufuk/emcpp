// include/emc/filtering/ferrite_toroid.hpp
#pragma once

#include <expected>

#include <mp-units/systems/si.h>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::pi, emc::constants::mu0
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Frequency / Length / Inductance / Impedance

namespace emc::filtering {

// Pull the aliases we use into the namespace so the field/signature spellings stay short.
using emc::units::Frequency;
using emc::units::Inductance;
using emc::units::Impedance;
using emc::units::Length;

// ---------------------------------------------------------------------------
//  Input — geometry arrives as typed mp-units Length/Frequency, so no unit
//  factor tables are needed at the boundary. The complex relative permeability
//  is carried as TWO dimensionless doubles (mu_r' real, mu_r'' imag), matching
//  the canonical "mu_r is a double" rule. Defaults give a sensible
//  representative toroid so a bare `FerriteToroidInput{}` computes a valid case.
// ---------------------------------------------------------------------------
struct FerriteToroidInput {
    double    turns          = 1000.0;                       ///< N      [-]   number of turns
    double    mu_r_real      = 100.0;                        ///< mu_r'  [-]   real part of complex mu_r
    double    mu_r_imag      = 50.0;                         ///< mu_r'' [-]   imag (loss) part of complex mu_r
    Length    height         = 0.05 * mp_units::si::metre;   ///< h      [m]   core height (> 0)
    Length    outer_radius   = 0.04 * mp_units::si::metre;   ///< b      [m]   outer radius (> 0; ln(b/a))
    Length    inner_radius   = 0.03 * mp_units::si::metre;   ///< a      [m]   inner radius (> 0; ln(b/a) denom)
    Frequency frequency      = 1.0 * mp_units::si::mega<mp_units::si::hertz>;  ///< f [Hz]
};

// ---------------------------------------------------------------------------
//  Result — L/X/R/Z, all carrying units in the type (H, ohm, ohm, ohm); a front
//  end picks a display unit with `.in(unit)`.
// ---------------------------------------------------------------------------
struct FerriteToroidResult {
    Inductance inductance;   ///< L    [H]    geometric inductance of the wound core
    Impedance  reactance;    ///< X    [ohm]  2 pi f mu_r' L  (energy-storing, from mu')
    Impedance  resistance;   ///< R    [ohm]  2 pi f mu_r'' L (lossy, from mu'' — the suppression term)
    Impedance  impedance;    ///< |Z|  [ohm]  sqrt(R^2 + X^2)
};

/// Validate ranges/positivity. b > a > 0 (so ln(b/a) is finite & defined), h > 0,
/// f > 0, N > 0, and the permeability parts non-negative.
[[nodiscard]] std::expected<void, emc::Error> validate(const FerriteToroidInput& in);

/// Compute L, X, R, |Z| from the complex permeability and toroid geometry.
///   L  = (mu0 N^2 h / 2 pi) * ln(b/a)
///   X  = 2 pi f mu_r' L,  R = 2 pi f mu_r'' L,  |Z| = sqrt(R^2 + X^2)
[[nodiscard]] emc::Result<FerriteToroidResult> calculate(const FerriteToroidInput& in);

// ---------------------------------------------------------------------------
//  Calculator-concept binding: the (Input, Result, calculate) triple checked at
//  compile time, so a signature drift is a hard error AT THIS HEADER.
// ---------------------------------------------------------------------------
struct FerriteToroid {
    using Input  = FerriteToroidInput;
    using Result = FerriteToroidResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::filtering::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::filtering::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<FerriteToroid>);

} // namespace emc::filtering
