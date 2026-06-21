// src/converter/antenna_factor.cpp
#include <emc/converter/antenna_factor.hpp>

#include <cmath>       // std::log10, std::pow

#include <mp-units/systems/si.h>

#include <emc/core/constants.hpp>

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;

std::expected<void, emc::Error> validate(const AntennaFactorInput& in) {
    // lambda = c/f requires f > 0; pull a plain double in Hz for the validator.
    const double f_hz = in.frequency.numerical_value_in(Hz);
    return emc::require_positive(f_hz, "frequency");
}

emc::Result<AntennaFactorResult> calculate(const AntennaFactorInput& in) {
    // and_then runs the math only when validate() succeeds and forwards the Error
    // otherwise, so the formula is unreachable on bad input (f <= 0).
    return validate(in).and_then([&]() -> emc::Result<AntennaFactorResult> {
        // lambda = c / f. c/f has a derived quantity kind, so direct-init {..} into
        // the named Length alias to relabel it explicitly (= would not compile).
        const emc::units::Length lambda{ (emc::constants::c / in.frequency).in(m) };
        const double lambda_m = lambda.numerical_value_in(m);

        // gain = 10*log10( (9.73 / (lambda * 10^(AF/20)))^2 )
        // 9.73 is the numeric form of the AF = 9.73 / (lambda*sqrt(G)) relation.
        const double af   = in.antenna_factor.value;                  // dB/m as a plain double
        const double term = 9.73 / (lambda_m * std::pow(10.0, af / 20.0));
        const double gain = 10.0 * std::log10(term * term);

        // Designated initializers name each field, so gain and wavelength can't swap.
        return AntennaFactorResult{
            .gain       = emc::units::Decibel{gain},   // dBi log wrapper, not a linear unit
            .wavelength = lambda,
        };
    });
}

} // namespace emc::converter
