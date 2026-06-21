// src/converter/wavelength_frequency.cpp
#include <emc/converter/wavelength_frequency.hpp>

#include <emc/core/constants.hpp>   // emc::constants::c (one exact 299 792 458 m/s)

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;   // hertz symbol for numerical_value_in / .in
using mp_units::si::unit_symbols::m;    // metre symbol

namespace detail {
// Shared core: c = lambda * f. Both directions divide the SAME exact c, which is
// what makes the forward/inverse pair exact round-trips instead of two bodies
// that could drift apart with a typo'd factor.

[[nodiscard]] inline emc::units::Frequency freq_from_wavelength(emc::units::Length lambda) {
    // [m/s] / [m] = [1/s]. The quotient has a derived quantity kind, so we relabel
    // it into the named Frequency alias with direct-init {..} via .in(Hz).
    return emc::units::Frequency{(emc::constants::c / lambda).in(Hz)};
}

[[nodiscard]] inline emc::units::Length wavelength_from_freq(emc::units::Frequency f) {
    // [m/s] / [1/s] = [m]. Direct-init relabel into the named Length alias.
    return emc::units::Length{(emc::constants::c / f).in(m)};
}
} // namespace detail

// ----- wavelength -> frequency ---------------------------------------------

std::expected<void, emc::Error> validate(const WavelengthToFrequencyInput& in) {
    // lambda must be > 0: this is also the divide-by-zero guard for c / lambda.
    return emc::require_positive(in.wavelength.numerical_value_in(m), "wavelength");
}

emc::Result<FreqResult> solve_frequency(const WavelengthToFrequencyInput& in) {
    // and_then runs the math only when validate() succeeded, and forwards the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).and_then([&]() -> emc::Result<FreqResult> {
        return FreqResult{ .frequency = detail::freq_from_wavelength(in.wavelength) };
    });
}

// ----- frequency -> wavelength ---------------------------------------------

std::expected<void, emc::Error> validate(const FrequencyToWavelengthInput& in) {
    // f must be > 0: also the divide-by-zero guard for c / f.
    return emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency");
}

emc::Result<WaveResult> solve_wavelength(const FrequencyToWavelengthInput& in) {
    return validate(in).and_then([&]() -> emc::Result<WaveResult> {
        return WaveResult{ .wavelength = detail::wavelength_from_freq(in.frequency) };
    });
}

} // namespace emc::converter
